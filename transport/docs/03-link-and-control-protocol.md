# Link and Control Protocol — As Built

**Status:** implemented
**Code:** `fikore_transport/link.py`, `fikore_transport/fikore_link.py`,
`src/utils/control/`
**Tests:** `test_loopback_transfer.py`, `test_fikore_link.py`,
`control_sync_test.cpp`, `object_ecn_test.cpp`

## Link contract

```python
class Link(Protocol):
    mss: int
    def submit(self, items: list[Transmit], at_tti: int) -> None: ...
    def step(self, until_tti: int) -> list[Arrival]: ...
    def close(self) -> None: ...
```

`Transmit` is one abstract segment. `Arrival` is its terminal network outcome:
delivered, queue-dropped, radio-dropped or expired, plus CE, timestamp and
direction metadata.

Two implementations exist:

- `LoopbackLink`: deterministic FIFO bottleneck with configured rate, queue,
  propagation, delay budget and CE threshold.
- `FikoreLink`: one object tag per segment over `fikore-control-1`.

## FikoreLink mapping

| Model operation | Control operation |
| :-- | :-- |
| Submit a segment | `inject` with UE, tag, direction, bytes and optional ECN |
| Advance one slot | `grant` with absolute `until_tti` |
| Read feedback | cell-wide `events` with the consumed cursor |
| Release counters | `forget` after terminal feedback is acknowledged |
| Change UE parameters | `set` |
| Read telemetry | `events` with `include_state` |

The client writes the scheduled command batch and the grant before reading
either response. `grant` is handled by the socket thread; scheduled commands
are handled at the next quiescent simulation point.

## Acknowledgement framing

An envelope with N commands receives N acknowledgements. Every acknowledgement
echoes the envelope `id`; replies inside that ID stay in command order, while a
grant with another ID may interleave.

`FikoreLink._read_acks()` therefore knows the expected count per ID and drains
all replies. Reading one line per ID desynchronises every later slot.

Commands, acknowledgements and barrier credit also carry an internal connection
generation. Work from a disconnected controller cannot mutate state or send an
acknowledgement to its successor.

## Incremental feedback

```json
{"id":7,"cmds":[{"op":"events","after":123,"include_state":false}]}
```

The result contains per-tag counter deltas:

```json
{
  "cursor":125,
  "events":[
    {"seq":124,"at_tti":939,"target":"ue/0","ue_id":"car",
     "dir":"dl","tag":8817,"delivered_bytes":1500,"ce_bytes":1500}
  ]
}
```

The emulator does not infer object completion: a tag may receive more than one
injection and only the client knows the total size. `FikoreLink` accumulates
deltas until delivered + dropped + expired equals the segment size.

`after` means “consumed through this cursor”. Later events are retained and the
same cursor replays them after a lost response. Event batches are validated
completely before counters, cursor or tag lifetime are mutated.

The first call uses `after: 0` and starts the subscription at that quiescent
point. `include_state: true` adds cumulative queue/radio/mobility state without
the full object map.

## Retention and resynchronisation

`max_object_events` defaults to 65536.

- Barrier overflow aborts with process status 76 before feedback is lost.
- Async overflow creates an explicit gap.
- `{"op":"events","after":0,"resync":true}` returns an atomic full snapshot,
  clears the gap and restarts delta collection.

## UE identity

Numeric targets remain valid:

```text
ue/0
```

The textual `ue_id` from the configuration is also valid:

```text
ue/car
ue/car_0
ue/car_1
```

Replies preserve numeric `target` and add textual `ue_id`. The compact state
reports `pos_x_m`, `pos_y_m` and `speed_kmh`.

## ECN and loss attribution

`inject` accepts `not-ect`, `ect0`, `ect1`/`l4s` and `ce`. Per-tag feedback
separates:

- delivered bytes;
- expired bytes;
- queue-dropped bytes;
- radio-dropped bytes;
- delivered CE bytes.

`dropped_bytes` remains the queue + radio sum for compatibility. CE is a subset
of delivered bytes, not a terminal fate.

## Timing

Counter changes produced in TTI `n` are collected at the quiescent point of
TTI `n+1`. Each event carries occurrence TTI; the acknowledgement carries
observation TTI. The transport reacts only at observation time.

## Evidence

- Framing/reconnection/barrier: `tests/control_sync_test.cpp`
- Parser and ECN: `tests/object_ecn_test.cpp`
- Replay/resync/overflow/conservation: `transport/tests/test_fikore_link.py`
- Performance: `transport/benchmarks/bench_lockstep.py`
- Scale results: `transport/benchmarks/results/scale-*.json`

See the emulator-side
[`runtime-control-events.md`](../../docs/runtime-control-events.md) for the
normative event contract. Non-implemented protocol ideas are listed only in
[`FUTURE-ROADMAP.md`](FUTURE-ROADMAP.md).
