# FikoRE control API

REST and WebSocket front end for the emulator's runtime control plane. It speaks NDJSON
to the emulator over a unix socket (or TCP) and collects its telemetry over UDP.

## What it is not

A dumb proxy on purpose. It keeps **no control state**: `GET /ue/{id}/state` is answered
by asking the emulator, never from memory, and nothing is reapplied when a connection
comes back. If the emulator restarts, that is a new session. Its only memory is the
telemetry cache.

There is no VQEG-specific logic here, and there should never be: that lives in the pilot
repository and consumes this API.

## Running it

```bash
./run_scripts/run_api.sh              # creates api/.venv on first run, serves on :8100
```

Configuration comes from the environment:

| Variable | Default | Meaning |
|---|---|---|
| `FIKORE_CONTROL_TRANSPORT` | `unix` | `unix` or `tcp` |
| `FIKORE_CONTROL_ADDRESS` | `/tmp/fikore-control.sock` | socket path, or TCP address |
| `FIKORE_CONTROL_PORT` | `8097` | TCP port of the control channel |
| `FIKORE_TELEMETRY_HOST` | `127.0.0.1` | UDP bind address of the collector |
| `FIKORE_TELEMETRY_PORT` | `8098` | UDP port of the collector |
| `FIKORE_API_HOST` / `FIKORE_API_PORT` | `127.0.0.1` / `8100` | where the API listens |

The emulator side needs a `[Control]` section and an extra UDP output in `[Monitoring]`
pointing at the collector. `config/control_demo.ini` has both.

## The dashboard

`http://localhost:8100/` serves a web dashboard: the telemetry panels, the UE
trajectory, and a control panel generated from `/schema`. It replaces the old
matplotlib tool and needs no display on the machine running the emulator, so SSH with
port forwarding is enough. See `wiki/Web-Dashboard.md`.

## Endpoints

| Method | Path | What it does |
|---|---|---|
| `GET` | `/` | the dashboard page |
| `POST` | `/control/ue/{id}` | body `{"priority": 4, ...}` |
| `POST` | `/control/batch` | body `{"cmds": [{"target": "ue/0", "set": {...}}]}` |
| `GET` | `/ue/{id}/state` | control state and counters from the emulator, metrics from the cache |
| `GET` | `/ues` | every UE and its control state |
| `GET` | `/schema` | the knob catalogue, proxied from `describe` |
| `GET` | `/scenario` | scenario and cell description, proxied from `get cell` |
| `GET` | `/scenario/map` | fading map metadata: cell size, count and percentiles |
| `GET` | `/scenario/map.bin` | the fading grid as raw `float32` little-endian |
| `GET` | `/telemetry/snapshot` | the whole telemetry cache |
| `POST` | `/sync/grant` | `{"until_tti": 1500}` or `{"until_t": 1.5}` |
| `WS` | `/stream` | telemetry as it arrives |
| `GET` | `/healthz` | link with the emulator and protocol version |

### `/stream`

One message per flush, every 200 ms, with the points coalesced:

```json
{"type": "telemetry", "points": [{"measurement": "ue_pdcp", "tags": {...}, "fields": {...}}]}
```

A client gets `{"type": "snapshot", "points": [...]}` first, so a page that joins
mid-run draws immediately instead of waiting a whole aggregation window. A viewer that
cannot keep up loses points rather than stalling the others.

```bash
curl -X POST localhost:8100/control/ue/0 -H 'content-type: application/json' \
     -d '{"priority": 8, "dl.rmax_mbps": 25}'
curl localhost:8100/ue/0/state
curl localhost:8100/schema
```

Units are the ones in the `.ini`, always: `mobility.speed_kmh` takes km/h, `dl.rmax_mbps`
takes Mbps. `GET /schema` is the authoritative list.

## Protocol version

`fikore_api/proto.py` and `include/utils/control/proto_version.h` must hold the same
string. There is no negotiation: the emulator and the API are built and deployed
together, and `api/tests/test_proto.py` fails if they drift apart.

## Tests

```bash
api/.venv/bin/python -m pytest api/tests -q
```

`test_integration.py` starts the emulator with `config/control_demo.ini` and drives it
through the API; it skips itself if `bin/fikore` has not been built.

The dashboard logic that has no DOM lives in `fikore_api/static/dash.js` and is tested
headlessly with node:

```bash
node api/tests/dash_logic_test.js
```

`make test` runs it too when a node interpreter is available.
