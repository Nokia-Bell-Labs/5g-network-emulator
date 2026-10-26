# Application Patterns — As Built

**Status:** implemented application-facing operations
**Code:** `fikore_transport/tcp.py`, `fikore_transport/udp.py`,
`fikore_transport/backend.py`
**Tests:** `test_loopback_transfer.py`, `test_transports.py`,
`test_backend.py`, `test_fikore_link.py`

This document names only application patterns that exist in the current code.

## Stream writes

`TcpSender.app_write(nbytes)` adds a finite number of application bytes:

```python
sender = TcpSender(
    flow=1,
    cc=Cubic(mss=1500),
    clock=runner.clock,
    sched=runner.sched,
    mss=1500,
)
runner.add_flow(Flow(sender, TcpReceiver(flow=1, mss=1500)))
sender.app_write(8 * 1024 * 1024)
runner.run_until(10_000)
```

The sender applies congestion control, receiver-window limits, pacing,
retransmission and acknowledgement processing. This does not open a real socket
or read a host file.

For a saturated bulk stream, `TcpSender.set_unlimited()` keeps application data
available until the caller stops the run. `finish_writes()` stops adding new
bytes while preserving ACK, RTO and retransmission processing during drain.

## Offline iperf-like sessions

The installed `fikore-iperf3` command creates TCP or UDP flows directly on one
`Runner`; it does not route sessions through the object-oriented
`TransportBackend`.

```bash
fikore-iperf3 -c config/control_demo.ini --ue controlDemo_0 \
  -t 10 -P 2 -C cubic -i 1

fikore-iperf3 -c config/control_demo.ini --ue controlDemo_1 \
  -u -b 20M -l 1200 -R -t 10 -J
```

Supported iperf-shaped options include duration (`-t`), finite TCP bytes (`-n`),
parallel streams (`-P`), reverse/UL (`-R`), UDP (`-u`), offered bitrate (`-b`),
datagram size (`-l`), interval (`-i`), MSS (`-M`), receive window (`-w`) and
controller (`-C`). Server, socket binding, authentication, zerocopy and SCTP
options have no offline-model equivalent and are rejected.

Duration runs stop offering data at `-t`, then emit a final drain interval so
late delivery, retransmission and UDP loss are not hidden. Finite TCP `-n` runs
ignore the duration default and use `--timeout` only as an abort guard.

Runs can instead be described by a schema-versioned JSON file passed with
`--config`. Command-line values override JSON values. `--set
SECTION.KEY=VALUE` and `--set-ue UE_ID.KEY=VALUE` apply reviewed scenario
overrides before the mandatory barrier/offline overlay.

Selected `ue_type: 0` blocks are converted only in the generated effective INI;
the source scenario is not modified. A selected block with `n_ues > 1` expands
to its textual targets (`name_0`, `name_1`, ...). Unselected background UE
blocks keep their configured traffic.

## UDP offered load

`udp_flow()` creates a `Flow`, `UdpSource` and `UdpSink` for Runner-level
open-loop traffic:

```python
flow, source, sink = udp_flow(
    flow=1,
    clock=runner.clock,
    sched=runner.sched,
    rate_mbps=10.0,
    duration_ttis=1_000,
)
runner.add_flow(flow)
runner.run_until(1_100)
report = sink.report()
```

This UDP primitive is not exposed through `TransportBackend`. It has no
congestion control or delivery recovery.

## Ideal transport

`BackendConfig(transport="ideal")` selects the diagnostic fixed-window
transport. Its `IdealSender` sends against a `SharedWindow` and can recover from
reported terminal Link outcomes immediately.

It is used to separate network capacity from TCP controller dynamics; it is not
a TCP variant or deployable application protocol.

## NetworkBackend objects

The harness-facing interface is `TransportBackend`:

```python
backend = TransportBackend(link, BackendConfig(
    window_ttis=10,
    horizon_ttis=5_000,
    tcp_connection_mode="persistent",
))
backend.submit_request(ue_id=0, request_id="video-3-segment-7",
                       bytes_total=512_000)

step = backend.advance()  # initial step at t=0
step = backend.advance()  # advances one configured window
backend.cancel_request(ue_id=0, request_id="video-3-segment-7")
```

Direction, ECN, receive window, controller and TCP connection mode are backend
configuration fields. Persistent mode is the default: an object reuses an idle
connection for its UE, or opens another when concurrent objects occupy every
pooled connection. The idle pool retains at most
`max_idle_tcp_connections_per_ue` flows. `tcp_connection_mode="fresh"` gives
every object new TCP state. The backend maps connection byte ranges to:

- `DownloadProgress`;
- `DownloadCompleted`;
- `DownloadCancelled`;
- `NetworkTelemetryReceived`.

The request ID belongs to the application; segment tags belong to the Link.
Neither is inferred from the other.

## Object cancellation

Cancellation stops new application data. Segments already submitted to the Link
remain attributed to the request and form its cancellation tail. The backend
emits `DownloadCancelled` after `flow.in_network` reaches zero, then retires the
cancelled connection after any Link-carried ACK tail also drains. It is not
reused, so cancellation cannot corrupt a later object.

This distinction is required for SFV wastage accounting: cancellation does not
erase bytes that already consumed network resources.

## Multi-UE operation

Multiple request flows can share one Link and emulator cell. Every
`TransportBackend.advance()` ticks all active flows on one Runner clock.
`FikoreLink` can map logical backend UE identifiers to sparse physical emulator
indices or textual configured targets.

## Available entry points

| Need | Current entry point |
| :-- | :-- |
| Saturated TCP flow | `TcpSender.set_unlimited()` |
| Finite TCP stream write | `TcpSender.app_write()` |
| Open-loop UDP | `udp_flow()` |
| Offline TCP/UDP session | `fikore-iperf3` |
| Ideal object baseline | `BackendConfig(transport="ideal")` |
| Fresh TCP object baseline | `BackendConfig(tcp_connection_mode="fresh")` |
| Finite downloadable object | `TransportBackend.submit_request()` |
| Cancel downloadable object | `TransportBackend.cancel_request()` |
| SFV integration | `benchmarks/validate_sfv.py` |

Current behavioural constraints belong in [`LIMITATIONS.md`](LIMITATIONS.md).
