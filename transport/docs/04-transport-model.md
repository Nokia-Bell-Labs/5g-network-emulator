# Transport Model — As Built

**Status:** implemented packet/segment-level model
**Code:** `fikore_transport/tcp.py`, `fikore_transport/runner.py`,
`fikore_transport/clock.py`

## Flow state

Each transport flow owns:

- application bytes queued and delivered;
- next sequence number and cumulative ACK point;
- outstanding segments with send time and retransmission state;
- receiver cumulative sequence and SACK blocks;
- congestion window and slow-start threshold;
- receiver-advertised window;
- pacing credit;
- RTT estimator and retransmission timer;
- selected congestion controller;
- ECN negotiation/state where applicable.

The model uses byte sequence space. Segments are normally at most the configured
MSS; the final segment of a finite write may be shorter.

## Sending

A sender may transmit when all applicable limits allow it:

```text
sendable
  = application data available
  ∩ congestion window
  ∩ receiver window
  ∩ pacing credit
```

Retransmissions have priority over new data. Every Link submission receives a
unique segment tag so terminal network accounting can be mapped back to the
flow and sequence range.

`app_write()` queues finite application bytes. `set_unlimited()` keeps a
saturated sender supplied.

## Connection reuse

`TransportBackend` uses an HTTP/1.1-style persistent TCP pool by default. Each
UE reuses an idle flow for a later object, preserving sequence space, congestion
window, RTT estimates and controller state. If all of a UE's flows are busy, a
concurrent object opens another flow. Once a peak of concurrent work subsides,
at most `max_idle_tcp_connections_per_ue` connections are retained (six by
default); excess idle flows close administratively.

Objects remain independently attributable through absolute byte ranges in the
shared connection sequence space. `BackendConfig.tcp_connection_mode="fresh"`
selects the former one-flow-per-object baseline. The ideal diagnostic transport
always uses one flow per object.

## Receiver and acknowledgements

The receiver tracks cumulative in-order delivery and out-of-order SACK blocks.
`Flow.ack_over_link` / `BackendConfig.ack_over_link` selects one of two paths:

- `False` (default): acknowledgement timing is modelled locally;
- `True`: ACK segments traverse the Link in the reverse direction.

ACK processing:

- advances the cumulative ACK;
- removes newly acknowledged outstanding ranges;
- updates RTT only from eligible samples;
- updates SACK state;
- informs the congestion controller of newly acknowledged and CE-marked bytes;
- re-arms or cancels the retransmission timer.

## SACK and loss recovery

Out-of-order arrivals create merged SACK blocks. The sender uses duplicate ACK
and SACK evidence to identify missing sequence ranges and schedule
retransmission. Partial cumulative acknowledgements can advance the left edge
while retaining later SACK information.

This is a research-model recovery path, not a complete implementation of every
Linux recovery algorithm.

## RTT and RTO

The RTT estimator follows the RFC 6298 form:

```text
first sample:
  SRTT   = R
  RTTVAR = R / 2

later samples:
  RTTVAR = (1 - beta) * RTTVAR + beta * |SRTT - R|
  SRTT   = (1 - alpha) * SRTT + alpha * R

RTO = SRTT + max(G, K * RTTVAR)
```

with the configured lower and upper bounds. Karn's rule excludes ambiguous RTT
samples from retransmitted data. Expiry retransmits the oldest eligible
outstanding range and exponentially backs off the timer.

The timer belongs to the flow rather than to each packet.

## Pacing

Pacing converts the controller's current rate/window decision into byte credit
over simulated time. Unused credit is bounded so a long idle period cannot
produce an unbounded burst. Pacing never permits sending beyond congestion or
receiver windows.

## Network outcomes

For TCP controllers:

- delivered data reaches the receiver, possibly CE-marked;
- terminal network loss is not disclosed directly to the sender;
- loss is inferred from ACK/SACK or RTO evidence.

The model retains queue-drop, radio-drop and expiry attribution for metrics,
but those counters are not an oracle for Reno, CUBIC or Prague.

`ideal` is the explicit exception: it may use direct outcome knowledge as a
diagnostic baseline.

## Cancellation and close

Application cancellation prevents new object data from entering the sender.
Already submitted or transport-buffered bytes retain object attribution while
they drain. A cancelled connection is not returned to the pool; it closes after
its accounting and ACK tails reach a terminal state. This keeps cancellation
isolated from concurrent objects on other pooled connections.

There is no modelled SYN/SYN-ACK handshake or FIN exchange. Connections begin
with initialized transport state and close administratively.

## Conservation

Tests and scenarios check that each submitted segment reaches exactly one
terminal fate:

```text
submitted = delivered + queue_drop + radio_drop + expired
```

CE is a delivered-byte property, not another terminal fate. Retransmissions are
new network transmissions of an existing sequence range and are counted as
such in network-byte totals.

## Evidence

- ACK, SACK, loss, RTO, pacing and receiver-window tests:
  `transport/tests/test_loopback_transfer.py`
- FikoRE terminal outcome mapping:
  `transport/tests/test_fikore_link.py`
- Object cancellation:
  `transport/tests/test_backend.py`
- External controller checks:
  `transport/benchmarks/validate_tcp_references.py`

Protocol features intentionally absent from this model are itemized in
[`LIMITATIONS.md`](LIMITATIONS.md).
