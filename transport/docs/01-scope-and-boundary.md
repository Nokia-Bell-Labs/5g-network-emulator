# Scope and Boundary — As Built

**Status:** implemented
**Package:** `transport/fikore_transport`

## Purpose

The transport package turns application demand into transport segments and
carries those segments over a replaceable Link. It exists between a generic
NetworkBackend application interface and either a deterministic test link or
the FikoRE emulator.

```text
application / SFV harness
          │ NetworkBackend
          ▼
   TransportBackend
          │
      Runner + Flow
  ┌───────┴────────┐
  │ TCP/ideal/UDP  │
  └───────┬────────┘
          │ Link
    ┌─────┴─────┐
    │           │
LoopbackLink  FikoreLink ── fikore-control-1 ── FikoRE
```

## Package responsibilities

The package implements:

- finite application requests and cancellation;
- sender and receiver state;
- Reno, CUBIC and Prague congestion controllers;
- an ideal diagnostic mode;
- Runner-level open-loop UDP traffic;
- segmentation, ACK processing, SACK and retransmission;
- local or Link-carried acknowledgement paths;
- transport pacing and receiver-window enforcement;
- conversion between transport segments and Link submissions;
- conversion from transport progress to generic `NetworkStep` updates;
- lockstep advancement on an integer-TTI clock.

## Emulator responsibilities

FikoRE implements:

- cell scheduling and radio capacity;
- queue admission and service;
- delay-budget expiry;
- radio and queue losses;
- DualPI2 marking/dropping;
- UE mobility and radio state;
- retained per-object feedback through `fikore-control-1`.

The emulator does not implement TCP and does not decide when an application
object is complete.

## Application responsibilities

The application or harness decides:

- which objects to request;
- object sizes and request IDs;
- when to cancel;
- which UE and direction to use;
- when the common network clock should advance;
- how delivered and cancelled bytes contribute to application-level quality.

## Separation enforced by the Link

The Runner sends `Transmit` records and receives terminal `Arrival` records.
It does not read emulator queue internals to control TCP. TCP reacts to ACK,
SACK, CE and timeout observations delivered through the configured ACK path.

Two explicit exceptions exist:

- `cc="ideal"` can inspect network outcomes immediately; it is a diagnostic
  baseline, not TCP.
- telemetry may expose emulator counters for measurement, but Reno, CUBIC and
  Prague do not use those counters as congestion-control input.

## Units

| Quantity | Unit |
| :-- | :-- |
| Transport and Link time | integer TTI |
| Default TTI | 1 ms |
| Backend cadence | integer TTIs (`window_ttis`; 10 TTIs = 10 ms by default) |
| `NetworkStep` and event timestamps | seconds (`time_s`) |
| Segment, request and counters | bytes |
| Rates | bits per second |
| RTT/RTO controller state | seconds internally where documented |

The model's MSS is configured independently. `FikoreLink` does not currently
verify it against the emulator's runtime packet-size setting; this is documented
in [`LIMITATIONS.md`](LIMITATIONS.md).

## Supported use

The implemented path supports deterministic loopback tests, FikoRE-backed
offline co-simulation, multi-UE object requests, cancellation accounting and
the validated SFV v0.7.2 example.

It is a packet/segment-level research model, not a Linux socket stack. Current
non-guarantees are listed in [`LIMITATIONS.md`](LIMITATIONS.md), and proposed
extensions only in [`FUTURE-ROADMAP.md`](FUTURE-ROADMAP.md).
