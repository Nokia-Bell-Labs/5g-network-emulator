# Observability example

A small module that uses every extension point of FikoRE (see `docs/extension-points.md`). It measures the DSCP field of IPv4 packets and reports, per UE, direction and DSCP class, what happened to those packets. It reads and counts; it never changes a decision of the emulator.

It is built on demand and is not part of the default binary, the tools or `make test`.

```
make example-observability     # builds bin/fikore-observability
make test-examples             # builds and runs the example's test
```

The example's object files are linked straight into the binary, next to those of the emulator. Do not put them in a static library: the linker would drop the files that nothing references, and with them the registrations.

## What it does

| Part | Extension point | File |
|---|---|---|
| Reads the DSCP and the protocol of each captured packet into the 32-byte record | Packet annotation (annotator) | `observability.cpp`, `annotate_ipv4` |
| Remembers the DSCP of a packet from ingest until its fate is known | Packet annotation (ingest observer) | `observability.cpp`, `on_ingest` |
| Adds the packet to the counters of its UE, direction and class | Packet fate observer | `observability.cpp`, `on_fate` |
| Publishes the change of the counters every N TTIs | Controller | `observability.cpp`, `dscp_reporter` |
| Scores like proportional fair and counts the calls per UE | Scheduler metric `pf_counted` (id 1000) | `counted_pf_metric.cpp` |
| Switch and filter of what is reported for a UE | Runtime parameters | `observability.cpp`, `register_all` |

Packets of a simulated source do not pass the capture, so they carry no annotation and are counted in the class `none`. In live mode, a packet takes the class of its DSCP.

### Telemetry

Every `period_tti` TTIs the controller publishes the change since the previous report, through the monitoring manager. Nothing is sent when monitoring is disabled.

- `ex_dscp`, tags `ue`, `dir` (`dl` or `ul`) and `dscp` (0 to 63 or `none`). Fields `packets`, `delivered_bits`, `dropped_bits` and `ce_packets` are sums; `delay_ms` is the mean delay per packet, from arrival to the moment its fate is resolved.
- `ex_sched`, tags `ue` and `dir`. Field `score_calls`, a sum: how many times the metric scored the UE.

`dropped_bits` counts the bits of every packet that did not end as delivered: expired, dropped by the queue, or lost on the radio.

### Parameters

| Name | Type | Meaning |
|---|---|---|
| `example.dscp_report` | boolean | Report this UE or not. Default true. |
| `example.dscp_filter` | text | Classes reported for this UE: a comma-separated list of DSCP values and the word `none`, for example `46,10,none`. Empty reports every class. |

A UE whose report is off is skipped, and what happened in that interval is not carried over when it is turned on again.

### The instrumented metric

`pf_counted` asks the registry for the built-in PF, binds it with the same parameters, and wraps the score it gets. The wrapper counts the call and returns the result unchanged, so `metric_type: 1000` schedules exactly like `metric_type: 6`. The test checks that, bit for bit, on random inputs and on a whole simulated run. `score` is called many times per TTI from several threads, even for the same UE, so the wrapper does nothing but one relaxed atomic increment.

## Run it

`config.ini` is a two-UE scenario with the metric and the controller switched on. It runs in real time, for three seconds, and sends the telemetry as InfluxDB line protocol to UDP port 8097 of the local host. To see the lines, listen in one terminal and run in another:

```
python3 -c "import socket; s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('127.0.0.1', 8097)); [print(s.recvfrom(4096)[0].decode().strip()) for _ in range(20)]"
bin/fikore-observability examples/observability/config.ini
```

The controller reads its own section of the configuration file:

```ini
[Observability]
enabled: true
period_tti: 100
```

Without that section the controller is not built, and the example only counts. To change a parameter while the emulator runs, use the control plane like for any other parameter. In a timeline file, for example:

```json
{"id":1,"at_tti":500,"cmds":[{"target":"ue/0","set":{"example.dscp_filter":"46,none"}}]}
```

## Limits of the example

- It accounts for UE ids below 256 (`max_ues`). Events of a higher id are counted in `out_of_range()` and not reported.
- A packet between ingest and fate waits in a table of at most 65536 entries. A packet that finds it full is not classified.
- The table of pending packets has one lock. A design that cannot afford even that keeps one table per queue, since the observers of a queue run on one thread.
- The annotator reads IPv4 only. The ECN bits are not touched: the L4S queue classifies by them.
