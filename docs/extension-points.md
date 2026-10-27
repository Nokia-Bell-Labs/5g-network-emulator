# Extension Points

FikoRE has three registries for research code that needs to add a scheduler metric, a runtime parameter, or per-TTI logic without editing the emulator sources. A fourth extension point, packet annotation, lets research code read captured packets at the NFQUEUE boundary. A fifth, packet fates, lets it observe what finally happens to each packet. Each one adds nothing by default: the controller, parameter, annotation and fate registries start empty, the metric registry holds only the seven built-in metrics, and a run with nothing registered behaves as it did before they existed.

A registration is a file-scope object in any translation unit that is linked into the binary. Link the object files directly, or force the linker to keep the unit: a static library drops a translation unit that nothing references, and its registrar never runs.

| Registry | Registers | Header |
|---|---|---|
| Scheduler metric | A score per UE and resource block group | `mac_layer/metric_handler.h` |
| Runtime parameter | A knob for the `set`, `get` and `describe` operations of the control plane | `utils/control/param_registry.h` |
| Controller | An object called once per TTI | `simulator/controller_registry.h` |

Every registry keeps its list in a function-local static, so registrars in different translation units can run in any order. A duplicate name or identifier, or an empty callable, throws `std::invalid_argument`. From a registrar that happens during static initialisation, so it ends the process before the simulation starts.

## Scheduler metrics

A metric is an entry with an identifier, a name, a score callable and a throughput recipe. The seven built-in metrics (identifiers 0 to 6) are registered first, through the same function, so their identifiers are taken. `METRIC_EXTERNAL_FIRST` (1000) is where identifiers for added metrics are expected to start.

```cpp
#include <mac_layer/metric_handler.h>

namespace
{
metric_entry make_entry()
{
    metric_entry e;
    e.id = 1000;
    e.name = "rate_squared_over_history";
    e.score = [](const metric_info &m, double /*t*/, int /*f*/) {
        return m.current_tp * m.current_tp / std::max(m.avrg_tp, 1e-6f);
    };
    e.recipe = {2.0f, 1.0f, true, true};
    e.throughput_based = true;
    return e;
}

const metric_registrar registration(make_entry());
}
```

A configuration selects it with `metric_type: 1000`. A `metric_type` that is not registered is a start-up error that lists the registered identifiers. Earlier versions mapped it to round robin.

- `score` can be called several times for the same UE and resource block group within one TTI, and from different threads: the UE threads and the grid worker threads, concurrently for different UEs. It must be a function of its arguments and of state that it only reads, and it must be safe to call from several threads at once. `metric_info` carries the rate, the average throughput, the delay terms, and the UE id and direction (`TX_DL` or `TX_UL`) of the UE being scored.
- `recipe` tells `phy_layer` how to feed the metric. `uses_history` makes it supply `avrg_tp` the way PF does; `supports_provisional_history` allows reranking within a TTI. See [throughput-schedulers.md](throughput-schedulers.md) for the meaning of the exponents.
- `throughput_based` must be set for a metric that ranks by rate without history. Otherwise `phy_layer` reuses the value computed at the last CQI refresh.
- `bind` is optional. It builds the callable and the recipe for one handler from the configuration, which is how PF takes `pf_alpha`.
- The UE `priority` knob multiplies the result of every metric.

## Runtime parameters

`param_registry::add` appends a knob after the built-in catalogue. The built-in knobs keep their order, and the registry is immutable once it has been built, so `add` after that point throws `std::logic_error`.

```cpp
#include <utils/control/param_registry.h>

namespace
{
double g_gain_db = 0.0;

const bool registered = [] {
    param_entry e;
    e.name = "ext.gain_db";
    e.type = param_type::number;
    e.min = -10.0;
    e.max = 10.0;
    e.description = "Example knob, in dB.";
    e.apply = [](ue &, const param_value &v, std::string &reason) {
        g_gain_db = std::get<double>(v);
        return true;
    };
    e.read = [](ue &) { return g_gain_db; };
    param_registry::add(e);
    return true;
}();
}
```

- Names use lower-case letters, digits, `_` and `.`, start with a letter, and have no empty segment. Follow the naming rules at the top of `param_registry.h` for units and for the `dl.` and `ul.` prefixes.
- `param_type::text` carries a UTF-8 string of at most `max_length` bytes (4096 by default). The control plane rejects a longer or non-UTF-8 value. A text knob reports its value to `get` through `read_text`; without one it is absent from `get`.
- `apply` runs at the quiescent point of the TTI, like every built-in knob, so it may write state that the controllers and the grids read without a lock. `set` validates type and range first, and the command goes to the journal, so a replay applies it again.
- `describe` lists the added knobs with their type, range, unit and description. A client that understands only `number` and `boolean` has to ignore or render `text`; the bundled dashboard renders a text field.

## Controllers

A controller observes the cell once per TTI. The simulator builds the registered controllers at the end of its constructor, after the UE list is final, and calls them from `simulator::step` after the control plane tick and before the MAC step. They run whether or not the control plane is enabled.

```cpp
#include <simulator/controller_registry.h>

namespace
{
struct tick_counter : public controller
{
    void on_tick(const tick_context &ctx) override
    {
        ticks_++;
        // ctx.sim_t, ctx.tti and ctx.ues (read only) describe the TTI that is about to run.
    }

    std::uint64_t ticks_ = 0;
};

struct registrar
{
    registrar()
    {
        register_controller("tick_counter",
            [](const std::string &, const cell_info &, const std::vector<ue> &) {
                return std::make_unique<tick_counter>();
            });
    }
};
const registrar tick_counter_registration;
}
```

- The call happens on the simulator thread while both worker pools are parked. The UE list shows the complete result of the previous TTI and needs no lock.
- A controller observes: it keeps its own state and may publish telemetry. Changes to the UEs go through the control plane.
- `ctx.ues` is the vector the simulator owns, and it can reallocate when UEs are added. Keep state keyed by UE id and look the UE up again at each tick.
- The factory receives the configuration file path, so that a controller can read a section of its own. The loader ignores unknown sections and unknown keys, although it still logs "added" for the keys it did not recognise.
- A factory may return `nullptr` to stay out of a run.
- An exception from `on_tick` ends the run. The message carries the controller name and the TTI.

## Packet annotation

Packet annotation reads captured packets. An annotator receives the bytes of one packet at the NFQUEUE boundary and fills a small fixed-size record, an `ext_blob`: 32 bytes of data and a 16-bit `kind`. An ingest observer receives that record together with the packet uid when the capture record becomes an `ip_pkt`. The emulator never interprets the record. `ip_pkt` and `harq_pkt` do not carry it, so everything downstream of `ingest` is unchanged; an observer that needs the annotation later keeps its own table keyed by uid.

```cpp
#include <atomic>
#include <cstdint>

#include <netfilter/capture_annotation.h>

namespace
{
std::atomic<std::uint64_t> seen_ipv4_headers{0};

// Reader thread: recognise a packet whose first byte is 0x45 (IPv4, 20-byte header) and keep that byte.
bool annotate_first_byte(const nfq_packet_metadata &meta, ext_blob &blob)
{
    if (meta.payload.empty() || meta.payload[0] != 0x45)
        return false;
    blob.data[0] = meta.payload[0];
    blob.kind = 1;
    return true;
}

// UE phase thread, once per uid.
void count_annotated(std::uint32_t /*uid*/, int /*tx_dir*/, int /*queue_num*/, const ext_blob &blob)
{
    if (blob.kind == 1)
        seen_ipv4_headers.fetch_add(1, std::memory_order_relaxed);
}

struct registrar
{
    registrar()
    {
        register_annotator(&annotate_first_byte);
        register_ingest_observer(&count_annotated);
    }
};
const registrar registration;
}
```

- **Annotators** are plain function pointers, called in registration order from `pkt_capture::handle_capture`, before the packet is enqueued. The first one that returns `true` wins and the rest are skipped. It should set a non-zero `kind`. When an annotator returns `false`, what it wrote to the blob is discarded, so a packet that nobody recognises keeps `kind == 0` and zeroed data. An annotator that throws is treated as not having recognised the packet; the exception is swallowed and counted in `annotator_failures()`.
- **Observers** are plain function pointers with the signature `void(std::uint32_t uid, int tx_dir, int queue_num, const ext_blob &)`. They are called once per captured packet in `captured_packet_handler::ingest`, including packets with `kind == 0` and packets that the handler drops at once because its completion queue is full. An observer that throws is swallowed and counted in `ingest_observer_failures()`, and the observers after it still run.
- **Freeze rule.** Register at start-up, typically from a file-scope object. The annotator list freezes at the first captured packet and the observer list at the first call of `captured_packet_handler::ingest`; a registration after that throws `std::logic_error`. A null function throws `std::invalid_argument` and a 17th entry in one list throws `std::length_error`. The reader threads read an immutable list with no lock, and there is no way to clear a registry in a running process.
- **Annotators run on the NFQUEUE reader thread.** There is one such thread per queue, so one annotator can run on several threads at once. It must not allocate, must not wait, and must synchronise any state it shares. With nothing registered the capture path adds one atomic load per packet.
- **Observers run on the thread that calls `ingest`**, which is the phase thread of the UE that owns the queue. They must not block. Keep their state per UE, or synchronise it.
- **IPv4 only.** The capture is bound to `AF_INET`, and the payload is a whole IPv4 packet. IPv6 traffic is not captured.
- **Read only.** An annotator sees the packet and cannot change it. The only write to a captured packet remains the CE codepoint of an `ACCEPT_CE` verdict. Verdicts, ECN handling and counters are the same with and without annotators registered.
- `uid` is the NFQUEUE packet id, the same number that `ip_pkt::uid` carries. A capture record is popped once, so an observer sees a uid once, whatever fragments the packet is later split into.
- `tx_dir` is the direction that the caller gave to `ingest`. During shutdown the handler drains the last packets with `TX_DL` whatever its direction, so an observer that needs the true direction should also use `queue_num`.

## Packet fates

A fate observer is told, once per packet, how the packet ended: delivered, expired, queue dropped or radio dropped. It receives a `pkt_fate_event` at the point where the handler resolves that fate, in the simulated source and in the live (captured) source alike.

```cpp
#include <atomic>
#include <cstdint>

#include <pdcp_layer/pkt_fate.h>

namespace
{
std::atomic<std::uint64_t> delivered_ce_bits{0};
std::atomic<std::uint64_t> lost_bits{0};

// UE phase thread, once per uid.
void count_fates(const pkt_fate_event &event)
{
    if (event.fate == bit_fate::delivered)
    {
        if (event.ce)
            delivered_ce_bits.fetch_add(event.bits, std::memory_order_relaxed);
    }
    else
    {
        lost_bits.fetch_add(event.bits, std::memory_order_relaxed);
    }
}

struct registrar
{
    registrar() { register_fate_observer(&count_fates); }
};
const registrar registration;
}
```

The event carries `uid`, `fate`, `bits`, `delay_s`, `ce`, `tx_dir`, `queue_num` and `ue_id`.

- **`uid`** is the NFQUEUE packet id in live mode. In simulated mode it counts the packets of one handler, so it repeats across UEs and directions; use `ue_id` and `tx_dir` with it. In live mode it is the number that the ingest observers of packet annotation receive, so a fate observer can look up what it stored at ingest.
- **`bits`** is the original size of the packet, the whole of it, whatever its fragments met on the way. Several fragments of one packet, released in different TTIs or in different HARQ blocks, produce one event, when the last of them is accounted.
- **`fate`** is the final fate. A packet whose fragments met different fates reports the first failure that the handler saw. The per-UE counters work per fragment, so for such a packet they split the bits between two fates while the event carries one. For every packet whose fragments share a fate, the sum of `bits` over the events of a fate equals the matching counter (`delivered_bits_total()`, `expired_bits_total()`, `queue_dropped_bits_total()`, `radio_dropped_bits_total()`) at the end of a run.
- **`delay_s`** is the time from the arrival of the packet (`ip_pkt::ip_t`) to the moment its fate is resolved, in seconds and never negative. For a delivered packet that is the release; for a drop it is the drop. In live mode a packet is released in uid order when `order_packets` is set, so the release can wait for an earlier packet.
- **`ce`** is true only for a packet that was delivered with a CE mark (an `ACCEPT_CE` verdict in live mode). A marked packet that is then lost reports `ce == false`.
- **`queue_num`** is the NFQUEUE number in live mode and -1 in simulated mode. `tx_dir` and `ue_id` are -1 when a handler was built outside a `pdcp_layer` and nobody told it.
- **Every fate is reported.** That includes a tail drop on a full buffer, an AQM drop at dequeue, a packet dropped while still in the HARQ buffer, the packets that a detach empties from the queues, and the packets that a live handler drops at once because its completion queue is full.
- **Read only.** An observer sees the event and changes nothing: no counter, verdict, queue or order, so the bit-conservation residual keeps its meaning and a run gives the same counters with and without observers. With nothing registered, each fate costs one byte load.
- **Freeze rule.** Register at start-up, typically from a file-scope object. The registry freezes at the first `packet_handler::step` of any handler (or at the first resolved fate of a handler that is used without a step); a registration after that throws `std::logic_error`. A null function throws `std::invalid_argument` and a 17th observer throws `std::length_error`. Observers run in registration order. One that throws is swallowed and counted in `fate_observer_failures()`, and the observers after it still run. There is no way to clear the registry in a running process.
- **Threads.** Observers run on the phase thread of the UE that owns the handler, one thread per UE, and may run at the same time for different UEs. They must not block, and must keep their state per UE or synchronise it.

## Example

`examples/observability/` is a small module that uses all five. It counts packets, bits, CE marks and delay per UE, direction and DSCP class, publishes the change of those counters as telemetry, scores like proportional fair while counting how often each UE is scored, and adds two runtime parameters that choose what is reported for a UE. It reads and counts; it does not change a decision of the emulator.

It is not part of the default build. `make example-observability` builds `bin/fikore-observability`, and `make test-examples` builds and runs its test. See `examples/observability/README.md` for what each file shows and how to run it.

## Order within a TTI

Parameters from the control plane are applied first, then the controllers run, then the MAC step schedules using the metric. A weight that a controller computes in TTI `n` from the state at the end of TTI `n-1` is therefore the one the metric reads in TTI `n`.
