# FikoRE: 5g-network-emulator

Real-time applications and workloads increasingly depend on predictable connectivity, low latency, sufficient throughput and controlled packet loss. This is especially relevant for interactive media, industrial control, robotics, edge offloading, cloud-assisted applications and other services where the network becomes part of the application behavior. Testing these workloads only over best-effort or production networks makes it difficult to isolate network effects, reproduce conditions and understand which radio configurations satisfy the application requirements.

5G technologies should theoretically be able to fulfill many of these requirements. However, application developers and researchers often need a practical way to evaluate how real applications behave under different 5G and beyond RAN conditions before having access to a specific deployment or while exploring alternative configurations. Consequently, we implemented FikoRE, a real-time 5G and beyond Radio Access Network (RAN) emulator, to test and optimize workloads and applications under controlled, reproducible network conditions.

##  Goals

There are several emulators publicly available online, such as NS-3, SimuLTE or Simul5G. However, their complexity is extremely high, especially for non-telecommunications-specialists. We present FikoRE, our real-time 5G Radio Access Network (RAN) emulator carefully designed for application-level experimentation and prototyping. Its modularity and straightforward implementation allow multidisciplinary user to rapidly use or even modify it to test their own applications. Contrarily to the mentioned simulators/emulators, the goal of FikoRE is not mainly to understand and test the network, but study how the network and its different possible configurations behave for particular applications, use-cases and verticals.  

As we want to test our solutions in with actual networked applications we designed FikoRE to:  

* Work in Real-Time.
* Handle actual IP traffic efficiently. 
* Handle multiple emulated users with real or simulated traffic. 
* Model the real behavior of the network with sufficient accuracy. 

Besides, we need the emulator to be simple to use and, more importantly, easy to modify for possible particular needs. Consequently, FikoRE:  

* Has a high level of modularity to allow easy modifications.  
* Follows a straightforward implementation.  
* Is simple to use both as an emulator (real traffic) and simulator (simulated traffic).  

Furthermore, as the goal is to test actual applications on different possible scenarios and understand the most optimal network configurations, we understand is crucial to allow to easily modify the resource allocation algorithms and procedures. We believe it’s a crucial step in which an optimal algorithm design can produce an optimal behavior of the network for very specific applications. For this reason, we have designed the algorithm to allow the users to easily implement and test their own resource allocation algorithms.  

## How to Compile and Run

To build the emulator, use:

```bash
make
```
This produces the executable at `bin/fikore`.

For a quick local check without privileged IP traffic, use:

```bash
make test
make smoke
```

To run FikoRE with a specific configuration:

For offline simulated traffic:
```bash
bash run_scripts/run_offline.sh
bash run_scripts/run_offline.sh config/offline_umi_n40_npn.ini
bash run_scripts/run_offline.sh -a config/offline_umi_n40_npn.ini
```

For an offline iperf-shaped session on a scenario (the selected UEs are simulated; the `.ini` is not modified):
```bash
bash run_scripts/run_iperf.sh -c config/offline_uma_n78_pedestrian.ini --ue studyPedestrian -t 10 -i 1
```

For real-traffic emulation with Linux namespaces and `NFQUEUE`, use the lab workflow documented in [run_scripts/README.md](run_scripts/README.md), for example:

```bash
sudo BACKEND=host UE_COUNT=2 run_scripts/run_fikore_nfqueue_lab.sh up
bash run_scripts/run_api.sh      # dashboard and control API on http://localhost:8100/
run_scripts/run_emulated.sh config/emulated_rural_n78_single_with_background.ini
```

For emulation mode, `sudo` is required because namespaces, iptables rules, and packet queue access require elevated privileges.

### Changing parameters while it runs

FikoRE can be steered at runtime: UE priority, rate cap, SINR offset, position, speed,
traffic target, and logical attach or detach. It works both in fast mode and in real
time, and the full description is in the
[Runtime Control](https://github.com/nokia/5g-network-emulator/wiki/Runtime-Control) wiki page.

The quickest way to see it is the demo config, which opens a control socket and feeds
telemetry to the API:

```bash
./bin/fikore config/control_demo.ini &
./run_scripts/run_api.sh &

# A dashboard with the telemetry panels, the UE trajectory and a control panel:
#   http://localhost:8100/        (over SSH: ssh -L 8100:localhost:8100 <host>)

curl localhost:8100/schema                       # the knob catalogue
curl -X POST localhost:8100/control/ue/0 \
     -H 'content-type: application/json' \
     -d '{"priority": 8, "dl.rmax_mbps": 25}'
curl localhost:8100/ue/0/state                   # asked to the emulator, not cached
```

Without the API, the same over the raw socket:

```bash
nc -U /tmp/fikore-control.sock
{"proto":"fikore-control-1"}
{"id":1,"cmds":[{"target":"ue/0","set":{"priority":8.0}}]}
```

UEs can also be addressed by the textual `ue_id` from the configuration. A
single `ue_id: car` is `ue/car`; when the section creates several UEs they are
`ue/car_0`, `ue/car_1`, and so on. Numeric targets remain supported. State and
event replies keep the numeric `target` and add the stable textual `ue_id`.

A transport model should use the incremental `events` operation instead of polling
the full UE state every TTI:

```json
{"id":2,"cmds":[{"op":"events","after":0,"include_state":true}]}
```

The reply carries only per-tag counter movements newer than `after`, plus an
optional compact state snapshot. Advance `after` only after consuming the complete
reply: unacknowledged events are retained and the same cursor safely replays them.
`max_object_events` bounds that replay log (65536 by default). Overflow aborts a
barrier run; an asynchronous client must recover atomically with
`{"op":"events","after":0,"resync":true}`.
See [`docs/runtime-control-events.md`](docs/runtime-control-events.md) for the
wire contract and cursor rules.

The Python transport models that consume this interface live in
[`transport/`](transport/README.md): TCP with Reno, CUBIC or Prague, the
fixed-window injection baseline, UDP, and the `NetworkBackend` facade used by
external experiment harnesses. The numbered transport documents describe only
implemented behaviour; current non-guarantees are in
[`transport/docs/LIMITATIONS.md`](transport/docs/LIMITATIONS.md), and
unimplemented work is isolated in
[`transport/docs/FUTURE-ROADMAP.md`](transport/docs/FUTURE-ROADMAP.md).

A scenario can also be scripted with no network at all, through `timeline_file` in the
`[Control]` section, and any session can be journaled and replayed exactly.

> Beware: under Round Robin (`metric_type: 5`) the scheduler ignores priority, so that
> particular knob will appear to do nothing. Use MT (`4`), BET (`1`), or PF (`6`) for
> weighted-priority experiments.

### Exit status

`fikore` returns zero only after a normal completed run. Aborted and invalid
runs use stable Unix-style statuses: 64 usage, 66 missing input, 70 fatal
runtime/software, 74 lost control peer, 75 credit timeout, 76 control-protocol
integrity failure (including event backlog), and 78 invalid configuration.
SIGINT and SIGTERM retain their native signal status.

MT, BET, PF, and RR configuration, shared metric semantics, and migration from
the legacy per-UE beta parameter are documented in
[docs/throughput-schedulers.md](docs/throughput-schedulers.md). The active
table-driven packet-error and retry model, its historical provenance, exact
accounting contract, and calibration limitations are documented in
[docs/harq-legacy-bler.md](docs/harq-legacy-bler.md).

Macroscopic-map provenance, schema evolution, and deterministic legacy
generation are documented in
[docs/map-provenance.md](docs/map-provenance.md) and
[docs/map-schema-v2.md](docs/map-schema-v2.md).

Explicit UE environment type, building penetration, and vehicle penetration are
documented in [docs/o2i-v2.md](docs/o2i-v2.md).

The complete PHY Model V2 equations, controlled evidence, limitations, and
open Phase 3 options are in the
[external technical-review manuscript](docs/phy-model-evolution-external-review.md).
Its reproducibility matrix is
[docs/baselines/phy-v2-validation-matrix.md](docs/baselines/phy-v2-validation-matrix.md).

## Execution Warnings and Troubleshooting
When running FikoRE in emulator mode, you must press Ctrl+C twice:

  -First Ctrl+C stops the emulator.

  -Second Ctrl+C stops the iperf3 process and triggers the results plotting.

If you encounter Python errors such as:
```bash
ModuleNotFoundError: No module named 'numpy'
```
it means that your Python environment is missing required libraries. The run scripts now prefer the repository virtual environment at `.venv`.

Recommended setup:
```bash
python3 -m venv .venv
. .venv/bin/activate
pip install numpy matplotlib
```

If you use the wrappers under `run_scripts/`, they will automatically pick `.venv` when it exists.

## Citing

If you want to use FikoRE in your research, don't forget to cite us!

```
@article{gonzalezmorin2023fikore,
  author  = {González Morín, Diego and López-Morales, Manuel J. and Pérez, Pablo and García Armada, Ana and Villegas, Álvaro},
  title   = {FikoRE: 5G and Beyond RAN Emulator for Application Level Experimentation and Prototyping},
  journal = {IEEE Network},
  year    = {2023},
  volume  = {37},
  number  = {4},
  pages   = {48--55},
  doi     = {10.1109/MNET.002.2200595},
  url     = {https://doi.org/10.1109/MNET.002.2200595}
}
```

## Description and Usage

For more info and usage check the [wiki](https://github.com/nokia/5g-network-emulator/wiki)
