# Incremental Object Feedback

`get` remains the complete, stateless view for diagnostics and dashboards.
Transport models use the additive `events` operation, which carries only per-tag
counter movements.

## Request and response

```json
{"id":7,"cmds":[{"op":"events","after":123,"include_state":false}]}
```

```json
{
  "id":7,
  "status":"ok",
  "tti":940,
  "result":{
    "cursor":125,
    "events":[
      {
        "seq":124,
        "at_tti":939,
        "target":"ue/0",
        "ue_id":"car",
        "dir":"dl",
        "tag":8817,
        "delivered_bytes":1500,
        "ce_bytes":1500
      },
      {
        "seq":125,
        "at_tti":939,
        "target":"ue/1",
        "dir":"dl",
        "tag":730,
        "expired_bytes":1500
      }
    ]
  }
}
```

Zero counters are omitted. An event is a counter delta, not a declaration that
the object is complete: one tag may be injected more than once, and only the
client knows the total size. The client accumulates delivered, expired,
queue-dropped, radio-dropped and CE bytes and decides when its object is
terminal.

`target` remains the numeric scheduler index for backwards compatibility.
`ue_id` is the stable textual identifier from the configuration and may also be
used in commands (`ue/car`). Sections with `n_ues > 1` receive suffixes
`_0`, `_1`, and so on.

`at_tti` is when the counters moved. The acknowledgement TTI is when the client
learned about them; a causal transport model can only react at the latter.

With `include_state: true`, the result also contains compact cumulative queue,
latency, SINR and per-UE counters, without knobs or the live-object map. A
typical harness asks for it only on the last TTI of its telemetry window.

## Cursor and replay

`after` means “I consumed everything through this cursor”.

- The server removes only events with `seq <= after`.
- It returns every newer event and the current high-water mark in `cursor`.
- If a reply is lost, retrying the same `after` replays the same logical
  events, possibly followed by newer ones.
- The client advances its cursor only after parsing a complete successful
  response.
- Moving behind an already acknowledged cursor or ahead of the stream is an
  error.

The first call uses `after: 0` and starts the subscription at that quiescent
point. It is not retroactive. Until then, get-only clients pay no memory or
delta-accounting cost.

## Retention and resynchronisation

`[Control] max_object_events` bounds the replay log; the default is 65536
fixed-size entries.

- In barrier mode, overflow aborts the run before transport feedback is lost.
- In async mode, the run continues but `events` reports a gap until the client
  requests an atomic resynchronisation:

```json
{"id":8,"cmds":[{"op":"events","after":0,"resync":true}]}
```

The response carries `snapshot`, the same complete UE/object state as
`get ue/*`, clears the gap and rearms delta collection at the same quiescent
point. A separate `get` followed by reset would leave an unobserved interval in
async mode.

## Tag lifetime

The replay log contains copies of the deltas, so `forget` does not remove an
unacknowledged event. The normal order is:

1. observe that the tag is terminal;
2. advance the event cursor;
3. send `forget`.

## Framing reminder

A command envelope with N entries in `cmds` receives N acknowledgements. Every
one echoes the envelope `id`; `id` cannot distinguish commands inside the
batch. Count the replies in command order. A `grant` acknowledgement may arrive
before scheduled-command acknowledgements because it is produced by the socket
thread.
