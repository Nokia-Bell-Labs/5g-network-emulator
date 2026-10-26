# Congestion Control — As Built

**Status:** Reno, CUBIC, Prague and ideal implemented
**Code:** `fikore_transport/cc.py`, `fikore_transport/cc_prague.py`,
`transport/prague/`

## Common controller interface

Controllers receive transport observations rather than emulator internals:

- newly acknowledged bytes;
- RTT samples;
- duplicate/SACK loss evidence;
- retransmission timeout;
- delivered CE bytes and ECN feedback.

They expose a congestion window and pacing decision to the sender. Queue-drop,
radio-drop and expiry counters remain measurement data and are not direct input
to TCP controllers.

## Reno

Reno implements:

- initial congestion window;
- slow start;
- additive increase in congestion avoidance;
- multiplicative decrease on detected congestion;
- fast-recovery state;
- timeout collapse and slow-start-threshold update.

It provides the conventional loss-based baseline and is checked against RFC
vectors and the independent ns.py implementation.

## CUBIC

CUBIC implements the RFC/Linux-form cubic window trajectory:

```text
W_cubic(t) = C * (t - K)^3 + W_max
```

with:

- `C = 0.4`;
- `beta_cubic = 0.7`, retaining 70% of the prior window after congestion;
- epoch and `W_max` tracking;
- Reno-friendly region;
- slow-start and timeout transitions.

The implementation is compared ACK-by-ACK with ns.py v0.4.5. The checked
1000-ACK reference run records zero-byte maximum congestion-window error.

## Prague

Prague uses the external `L4STeam/udp_prague` controller through
`cc_prague.py` and the C ABI shim in `transport/prague/`. The binding translates
model events and timing into the external controller's API and returns its
window/pacing decision.

The path supports:

- AccECN-style CE accounting used by the model;
- scalable CE response;
- pacing;
- FikoRE DualPI2/L4S scenarios.

Prague requires the external source/binding to be available for that
configuration. There is no separate pure-Python Prague controller.

The external validation compiles and exercises the pinned
`prague_cc.cpp`; it verifies that the repository binding loads the independent
controller and passes deterministic vectors. It does not prove equivalence to a
complete Linux TCP Prague stack.

## Ideal

`ideal` is a fixed-window diagnostic transport. It can consume terminal Link
outcomes immediately and does not model TCP congestion inference. Use it to
separate transport-controller effects from the capacity/loss path, not as a
production protocol claim.

## ECN path

For ECN-capable transport:

1. the sender marks submissions ECT as configured;
2. FikoRE may deliver bytes with CE under DualPI2;
3. the Link preserves CE bytes as a subset of delivered bytes;
4. receiver feedback reports CE;
5. the controller updates its congestion estimate/window.

Dropped and expired bytes remain terminal outcomes; CE-marked bytes remain
delivered.

## Selection

The controller is selected when constructing a `TcpSender` or configuring
`TransportBackend`, for example:

```python
backend = TransportBackend(link, BackendConfig(cc_factory=Cubic))
```

All object requests handled by a backend use its configured transport defaults
unless the caller explicitly constructs separate models/backends.

## Validation scope

`transport/benchmarks/validate_tcp_references.py` checks:

- RFC 5681 Reno vectors;
- RFC 6298 RTT/RTO vectors;
- RFC 8312 CUBIC vectors;
- RFC 9438 CUBIC vectors;
- Reno and CUBIC ACK trajectories against ns.py v0.4.5;
- loading and deterministic operation of external Prague source.

Detailed reproduction and interpretation are in
[`09-external-tcp-validation.md`](09-external-tcp-validation.md).

A simultaneous Prague+CUBIC coexistence campaign is not part of the current
evidence. It remains in [`FUTURE-ROADMAP.md`](FUTURE-ROADMAP.md).
