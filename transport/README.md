# fikore-transport

`fikore-transport` is a discrete-time transport layer for FikoRE offline
co-simulation. Applications submit bytes or finite objects; the model handles
segmentation, congestion control, acknowledgements, retransmission and
cancellation accounting; FikoRE handles radio capacity, scheduling, queues,
loss, delay budgets, ECN and mobility.

No real TCP packet or kernel socket crosses this boundary. Segments are
simulation objects carried through a `Link`.

## Current status

Implemented:

- deterministic TTI clock and fixed-step lockstep runner;
- TCP sender/receiver with SACK, RTO, pacing and receive window;
- Reno, CUBIC and externally bound Prague controllers;
- ideal fixed-window diagnostic transport;
- Runner-level open-loop UDP;
- deterministic `LoopbackLink`;
- `FikoreLink` over retained `fikore-control-1` events;
- generic `TransportBackend` request, cancellation and `NetworkStep` interface;
- persistent TCP connection pools with an explicit fresh-connection baseline;
- numeric and textual FikoRE UE addressing;
- `fikore-iperf3` TCP/UDP offline sessions with interval text/JSON reporting;
- validated integration with the external SFV v0.7.2 example.

This is a research model, not a full Linux TCP stack. Read
[`docs/LIMITATIONS.md`](docs/LIMITATIONS.md) before interpreting results.

## Architecture

```text
application / SFV player
        │ NetworkBackend
        ▼
 TransportBackend
        │
  Runner + Flow
        │ Link
  ┌─────┴──────┐
  │            │
LoopbackLink  FikoreLink ── fikore-control-1 ── FikoRE
```

The NetworkBackend boundary contains request IDs and generic progress. The Link
boundary contains transport segments and terminal arrivals. FikoRE wire messages
do not enter the player.

## Quick start

From the emulator repository root:

```bash
python3 -m venv transport/.venv
transport/.venv/bin/pip install -e transport

make test-transport
make test-transport-integration
```

The first target uses deterministic links. The second drives the built emulator;
if `bin/fikore` is absent, its Python test reports a skip and exits successfully.
Check test output rather than inferring integration coverage from status zero.

The editable install exposes `fikore-iperf3`, an offline session validator:

```bash
# TCP/CUBIC, one stream on one member of the controlDemo UE group
fikore-iperf3 -c config/control_demo.ini --ue controlDemo_0 -t 10 -i 1

# UDP at 20 Mbit/s, reverse direction, two streams per selected UE
fikore-iperf3 -c config/control_demo.ini --ue controlDemo \
  -u -b 20M -l 1200 -R -P 2 -t 10 -J

# Validate and print the temporary offline configuration without starting FikoRE
fikore-iperf3 --config transport/examples/iperf-tcp.json --dry-run
```

`--ue` accepts a configured UE block (`controlDemo`, expanded to
`controlDemo_0`, `controlDemo_1`) or one concrete textual target. The source INI
is never changed. Selected live UE blocks are converted to simulated injection;
unselected background blocks retain their original traffic.

Prague additionally needs its external source/binding:

```bash
make -C transport/prague
```

`PRAGUE_DIR` can point to the L4STeam `udp_prague` checkout. The external source
is not modified or vendored.

## Feature and evidence matrix

| Feature | Implemented | Tested | Evidence | Current limitation |
| :-- | :--: | :--: | :-- | :-- |
| Fixed-step TTI lockstep | yes | yes | `test_loopback_transfer.py`, `test_fikore_link.py` | no idle skipping or event-stopping grant |
| TCP sender/receiver, SACK, RTO, pacing | yes | yes | `test_loopback_transfer.py` | not a complete Linux stack |
| Reno | yes | yes, external | `validate_tcp_references.py`, `tcp-reference.json` | controller validation only |
| CUBIC | yes | yes, external | `validate_tcp_references.py`, `tcp-reference.json` | controller validation only |
| Prague controller binding | yes | yes, external vectors | `validate_tcp_references.py`, `tcp-reference.json` | external binding required; not Linux-stack equivalence |
| Prague + FikoRE DualPI2 | yes | yes, integration scenario | `validate_scale.py`, `scale-prague.json` | checked scenario, not coexistence campaign |
| Ideal diagnostic transport | yes | yes | loopback transport tests | sees terminal outcomes directly |
| UDP offered load | Runner only | yes | Runner tests/scenarios | not in `TransportBackend` |
| FikoRE event Link | yes | yes | `test_fikore_link.py`, C++ control tests | fixed polling boundaries |
| Object requests/cancellation | yes | yes | `test_backend.py`, `test_fikore_link.py` | HTTP/1.1-style pool; no HTTP/2 multiplexing |
| Multi-UE NetworkBackend | yes | yes | scale and SFV runs | validated subset, not all harness scenarios |
| SFV v0.7.2 bridge | yes | yes, external | `validate_sfv.py`, `sfv-pilot.json` | one external version/example |
| iperf-like CLI | yes | yes | `test_iperf.py`, `test_iperf_fikore.py`, `iperf-smoke.json` | offline model, not a kernel iperf3 endpoint |

## Reproducible runners

Run from the repository root with `PYTHONPATH=transport`.

```bash
# Control-protocol cost
python3 transport/benchmarks/bench_lockstep.py

# Small end-to-end examples
python3 transport/benchmarks/e2e_fikore.py
python3 transport/benchmarks/e2e_backend.py
python3 transport/benchmarks/e2e_prague.py

# Versioned 300 s campaigns
python3 transport/benchmarks/validate_scale.py --help

# External TCP controller comparison
python3 transport/benchmarks/validate_tcp_references.py --help

# External SFV v0.7.2 integration
python3 transport/benchmarks/validate_sfv.py --help

# Offline interactive/session validation
fikore-iperf3 --help
```

Checked summaries live in `benchmarks/results/`. Generated files record their
source runner, regeneration command and tested FikoRE revision.
`sfv-pilot.json` is explicitly a curated aggregate of several run manifests.
Compare each `fikore_revision` with the checkout before citing its metrics.

## Package layout

```text
fikore_transport/
  clock.py         deterministic clock and scheduled actions
  link.py          Link records and deterministic LoopbackLink
  tcp.py           sender and receiver state
  cc.py            Reno and CUBIC
  cc_prague.py     Prague binding adapter
  ideal.py         fixed-window diagnostic transport
  udp.py           Runner-level open-loop datagrams
  runner.py        lockstep transport runner
  scenario.py      section-aware rendering of repeated-UE INI files
  emulator.py      FikoRE process/configuration wrapper
  fikore_link.py   fikore-control-1 Link
  backend.py       generic object-oriented NetworkBackend
  iperf_*.py       offline session configuration, execution and reporting
prague/            C ABI shim for the external Prague controller
tests/             deterministic and emulator-backed tests
benchmarks/        examples, campaigns and checked results
docs/              as-built documentation, limits and roadmap
```

## Documentation

The numbered documents describe only implemented behaviour:

1. [Scope and boundary](docs/01-scope-and-boundary.md)
2. [Clock and lockstep](docs/02-clock-and-lockstep.md)
3. [Link and control protocol](docs/03-link-and-control-protocol.md)
4. [Transport model](docs/04-transport-model.md)
5. [Congestion control](docs/05-congestion-control.md)
6. [Application patterns](docs/06-application-patterns.md)
7. [NetworkBackend and SFV](docs/07-network-backend-and-sfv.md)
8. [Validation evidence](docs/08-validation-evidence.md)
9. [External TCP validation](docs/09-external-tcp-validation.md)

Keep these two roles separate:

- [Current limitations and non-guarantees](docs/LIMITATIONS.md)
- [Unimplemented roadmap with acceptance criteria](docs/FUTURE-ROADMAP.md)
