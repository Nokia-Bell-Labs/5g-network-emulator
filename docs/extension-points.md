# Extension Points

FikoRE has three registries for research code that needs to add a scheduler metric, a runtime parameter, or per-TTI logic without editing the emulator sources. Each one is empty by default, and a run with nothing registered behaves as it did before the registries existed.

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

- `score` is called once per UE and resource block group, on the grid threads, and must only read. `metric_info` carries the rate, the average throughput, the delay terms, and the UE id and direction (`TX_DL` or `TX_UL`) of the UE being scored.
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

## Order within a TTI

Parameters from the control plane are applied first, then the controllers run, then the MAC step schedules using the metric. A weight that a controller computes in TTI `n` from the state at the end of TTI `n-1` is therefore the one the metric reads in TTI `n`.
