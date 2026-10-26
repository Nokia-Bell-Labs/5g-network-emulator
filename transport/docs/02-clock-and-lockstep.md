# Clock and Lockstep — As Built

**Status:** implemented fixed-step advancement
**Code:** `fikore_transport/runner.py`, `fikore_transport/backend.py`,
`fikore_transport/fikore_link.py`

## Time domains

The transport and Link use absolute integer TTIs. The default configuration is
1 ms per TTI. NetworkBackend callers advance by configured TTI windows and
receive `time_s` timestamps in seconds.

A typical SFV caller advances every 10 ms:

```text
harness:        0 ms ───────── 10 ms ───────── 20 ms
transport TTI:    0 1 2 3 4 5 6 7 8 9 10 ... 19 20
```

The 10 ms value is an application cadence, not an emulator slot size. The
backend executes every intervening transport TTI.

## Advance contract

`Runner.tick()` advances exactly one TTI. `Runner.run_until(last_tti)` repeatedly
ticks through an inclusive absolute boundary.

`TransportBackend.advance()` has no time argument. Each call advances
`BackendConfig.window_ttis` (10 by default), except that `horizon_ttis` clamps
the final window. Its first call emits the initial `NetworkStep` at time zero
without ticking. All flows and UEs in that backend share the same Runner clock.

## One transport TTI

At each TTI the Runner:

1. asks the Link for arrivals visible through the current TTI;
2. turns data arrivals into ACK work and processes Link-carried ACK arrivals;
3. runs due scheduled callbacks, including local ACKs and timers;
4. asks every sender for newly permitted/retransmitted segments;
5. submits those segments for the next TTI;
6. increments the clock.

The exact order is fixed in code and tests. No wall-clock sleep controls
simulated time.

## FikoRE barrier

For a FikoRE-backed step, `FikoreLink`:

1. sends scheduled commands for the boundary;
2. sends an absolute `grant`;
3. reads every scheduled-command acknowledgement;
4. reads the grant acknowledgement;
5. fetches retained object events;
6. converts complete segment outcomes to `Arrival` records;
7. acknowledges consumed feedback with the next cursor.

The grant is the synchronization barrier. A successful response means the
emulator reached the requested quiescent boundary, not merely that a command
was accepted.

## Feedback lag

Network work occurring in TTI `n` is collected by the emulator at the
quiescent point of TTI `n+1`. Feedback contains both occurrence and observation
time. The transport cannot react before the result is observable.

Local ACK mode still honours configured propagation/ACK delay, but avoids
sending an ACK packet through the Link. Link ACK mode creates reverse-path
segments and therefore models reverse contention and loss.

## Multi-UE synchronization

All UEs attached to one `TransportBackend` advance on one clock. Requests
submitted before the same boundary compete concurrently in FikoRE. The backend
does not advance UE A to completion and then run UE B.

## Scheduler helper

`Scheduler.next_tti()` exists and returns the next known scheduled action. The
current model does not use it to skip idle TTIs; advancement remains fixed-step.
This is a current limitation, not an active execution path.

## Failure semantics

The Python integration raises an exception on:

- malformed or incomplete protocol replies;
- backward time;
- connection loss without successful protocol recovery;
- inconsistent event cursors;
- segment over-accounting or another violated runtime invariant.

The emulator itself exits with status 76 on barrier event-retention overflow
before feedback is lost. Top-level validation scripts convert failed final
conservation checks into non-zero status where they perform that check. A raw
Python exception and an emulator process exit are distinct failure channels;
partial results from either must not be treated as successful evidence.

## Evidence

- Deterministic loopback progression:
  `transport/tests/test_loopback_transfer.py`
- FikoRE barrier/replay progression:
  `transport/tests/test_fikore_link.py`
- Emulator framing and reconnect:
  `tests/control_sync_test.cpp`
- 300 s fixed-step runs:
  `transport/benchmarks/validate_scale.py`

Idle skipping and event-stopping multi-TTI grants are not implemented; their
acceptance criteria are in [`FUTURE-ROADMAP.md`](FUTURE-ROADMAP.md).
