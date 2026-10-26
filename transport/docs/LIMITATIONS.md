# Current Limitations and Non-Guarantees

This file describes constraints of the implementation that exists now. It is
not a backlog. Proposed changes and their acceptance criteria are kept in
[`FUTURE-ROADMAP.md`](FUTURE-ROADMAP.md).

## TCP protocol scope

The transport is a discrete segment-level research model, not a complete host
TCP stack. It does not model:

- SYN/SYN-ACK connection establishment;
- FIN/RST close exchange or TIME_WAIT;
- Nagle's algorithm;
- delayed ACK policy;
- receiver-window scaling negotiation;
- Linux socket-buffer autotuning;
- PRR;
- RACK/TLP;
- HyStart++;
- kernel qdisc, NIC offload or host scheduling effects.

Flows begin with initialized sender/receiver state and close administratively.
Results that depend materially on connection setup, short-flow kernel policy or
host implementation details need a real-stack validation path.

## External controller validation

Reno and CUBIC are compared with RFC vectors and ns.py. Prague loads the
external reference controller. This validates controller equations and selected
state transitions, not packet-for-packet Linux-stack equivalence.

The independent repositories are not vendored and the external comparison is
not part of default CI.

## Prague dependency

Prague requires the external L4STeam source/binding at build or run setup,
according to the selected path. There is no independent pure-Python fallback.
An environment without that dependency can run Reno, CUBIC and ideal scenarios
but cannot claim Prague coverage.

## ACK path choice

`ack_over_link=False` models ACK timing locally and does not consume
reverse-path Link capacity. It is cheaper and deterministic, but cannot show ACK
compression, reverse congestion or ACK loss.

`ack_over_link=True` sends ACK segments through the Link and can represent
those effects, at a higher simulation cost. Results must state which path was
used.

## Ideal transport is an oracle

The ideal controller can inspect terminal network outcomes directly. TCP
controllers cannot. Ideal therefore gives a diagnostic capacity/recovery
baseline and must not be described as a deployable transport or TCP variant.

## UDP scope

Open-loop UDP exists only as a Runner-level scenario primitive. It is not
available through `TransportBackend`, has no NetworkBackend request lifecycle
and has no transport recovery or congestion controller.

It is available to `fikore-iperf3`, which reports terminal loss, one-way delay
and RFC 3550 jitter after draining network outcomes. UDP does not produce an RTT
measurement.

## iperf3 compatibility scope

`fikore-iperf3` follows familiar bulk-session options and text/JSON shapes; it
is not the upstream iperf3 program and does not open client/server sockets.
Server mode, real ports/interfaces, authentication, SCTP, zerocopy, kernel
socket options and host scheduling are not modelled.

TCP RTT, congestion window and retransmissions are values from the research
transport model. UDP loss is a FikoRE terminal outcome. These results must not
be described as measurements of a host kernel stack. The JSON carries a
FikoRE-specific metadata block and is not guaranteed to be byte-for-byte
compatible with every third-party iperf3 parser.

## Persistent connection scope

The default `TransportBackend` TCP mode is an HTTP/1.1-style pool: sequential
objects reuse an idle connection and concurrent objects open separate
connections. It does not multiplex multiple active objects over one connection
as HTTP/2 or HTTP/3 would.

Cancelling an object retires its connection after the network tail drains rather
than reusing it. This isolates object accounting but does not model an HTTP/2
stream reset that leaves the underlying connection healthy.

## Fixed-step execution

Transport advancement currently visits every TTI. `Scheduler.next_tti()` can
identify a future scheduled action but is not wired into the model to skip idle
intervals.

This affects wall-clock cost, not intended simulated-time semantics.

## Grant completion

The control protocol supports absolute grants. A multi-TTI grant cannot
currently stop early when a requested application/network event occurs.
Lockstep callers use fixed boundaries and then inspect retained events.

## Runtime MSS consistency

The transport model has a configured MSS and FikoRE has a runtime packet-size
configuration. `FikoreLink` does not currently query and reject a mismatch.
Scenario configuration and review must keep them consistent.

## SFV validation scope

The checked integration covers the external SFV example at v0.7.2, including
two UEs, B1/B2, concurrent requests, swipes and cancellation accounting. It does
not validate:

- every scenario in the external repository;
- browser and simulator equivalence;
- future external revisions;
- the complete VQEG experiment matrix.

## Evidence and performance

Checked wall-clock timings depend on CPU, build flags, container/runtime load
and host scheduling. They are observations, not portable deadlines or
performance guarantees.

`sfv-pilot.json` is a curated run summary rather than direct native output of
the external SFV process. The runner, external version and tested FikoRE
revision must remain recorded with it.

## Emulator overload behaviour

Object feedback is retained up to a configured safety limit. Barrier-mode
overflow aborts the run rather than silently losing accounting; async mode
requires explicit resynchronisation after a gap.

Pathological overfeed can still create substantial emulator queue/backlog and
expiry-scanning work before the safety mechanisms terminate or drain it.
Scenarios should bound offered load and inspect conservation/error status.

## Event observation lag

Network outcomes generated in one emulator TTI are collected at the next
quiescent point. Transport reacts at observation time, not occurrence time.
This one-step protocol timing is part of the current integration semantics.

## Test-depth differences

The principal paths have unit and integration coverage, but not every internal
branch has an independent external oracle. In particular, partial-ACK/SACK
recovery details and uncommon multi-loss sequences are validated by repository
tests rather than by complete Linux trace equivalence.

The Python emulator integration test exits successfully after reporting a skip
when `bin/fikore` is absent. Prague tests can likewise skip when the binding is
unavailable. `validate_scale.py --mode prague` writes a `skipped` result and
returns zero in that case. Prague evidence therefore requires an actual result
with `bytes_conserved: true`, not merely a zero command status.
