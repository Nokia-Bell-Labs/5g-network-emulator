# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear

"""REST and WebSocket front end for the FikoRE control plane.

A stateless proxy plus a telemetry cache, and nothing else. No VQEG logic, no shadow
copy of the control state, no reconciliation after a restart: GET /ue/{id}/state is
answered by asking the emulator, never from memory.
"""

from __future__ import annotations

import asyncio
import json
import os
import struct
from contextlib import asynccontextmanager
from typing import Any, AsyncIterator, Dict, List, Optional

from fastapi import Body, FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, Response
from fastapi.staticfiles import StaticFiles

from .control import ControlClient, ControlConfig, ControlError
from .proto import PROTO
from .telemetry import TelemetryCollector


def _env(name: str, default: str) -> str:
    return os.environ.get(name, default)


control = ControlClient(
    ControlConfig(
        transport=_env("FIKORE_CONTROL_TRANSPORT", "unix"),
        address=_env("FIKORE_CONTROL_ADDRESS", "/tmp/fikore-control.sock"),
        port=int(_env("FIKORE_CONTROL_PORT", "8097")),
    )
)
collector = TelemetryCollector(
    host=_env("FIKORE_TELEMETRY_HOST", "127.0.0.1"),
    port=int(_env("FIKORE_TELEMETRY_PORT", "8098")),
)

STATIC_DIR = os.path.join(os.path.dirname(__file__), "static")

# The fading map is immutable for a run, so it is read once and kept.
_fading_cache: Optional[Dict[str, Any]] = None

# The collector thread produces far more messages than a browser needs: with 21 UEs a
# window is about 200 points. They are coalesced here and flushed as one array, which is
# also what lets a page redraw once per batch instead of once per point.
FLUSH_INTERVAL_S = 0.2

_subscribers: List[asyncio.Queue] = []
_loop: Optional[asyncio.AbstractEventLoop] = None
_pending: List[Dict[str, Any]] = []


def _fan_out(entry: Dict[str, Any]) -> None:
    """Called from the collector thread, so it hops onto the event loop."""
    if _loop is None:
        return
    _loop.call_soon_threadsafe(_pending.append, entry)


def _broadcast(message: Dict[str, Any]) -> None:
    for queue in list(_subscribers):
        try:
            queue.put_nowait(message)
        except asyncio.QueueFull:
            # A viewer that cannot keep up loses points rather than stalling the others.
            pass


async def _flusher() -> None:
    while True:
        await asyncio.sleep(FLUSH_INTERVAL_S)
        if not _pending:
            continue
        batch = list(_pending)
        del _pending[:]
        _broadcast({"type": "telemetry", "points": batch})


@asynccontextmanager
async def lifespan(_: FastAPI) -> AsyncIterator[None]:
    global _loop
    _loop = asyncio.get_running_loop()
    collector.on_point = _fan_out
    collector.start()
    task = asyncio.create_task(_flusher())
    yield
    task.cancel()
    collector.stop()
    control.close()


app = FastAPI(title="FikoRE control API", version=PROTO, lifespan=lifespan)

# Mounted under /static and not at the root, so it cannot shadow the API routes.
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")


def _forward(call, *args) -> Dict[str, Any]:
    try:
        reply = call(*args)
    except ControlError as exc:
        raise HTTPException(status_code=503, detail=f"control channel: {exc}") from exc

    if reply.get("status") == "error":
        raise HTTPException(status_code=400, detail=reply.get("errors", reply))
    return reply


@app.get("/")
async def index() -> FileResponse:
    return FileResponse(os.path.join(STATIC_DIR, "index.html"))


@app.get("/scenario")
async def scenario() -> Dict[str, Any]:
    # Asked to the emulator every time: it is the source of truth, and the UI uses this to
    # configure itself (map apothem, metric type, real time or not).
    reply = _forward(control.get, "cell")
    info = dict(reply.get("result") or {})
    info["proto"] = PROTO
    info["tti"] = reply.get("tti")
    info["t"] = reply.get("t")
    return info


def _fading_map_path() -> str:
    reply = _forward(control.get, "cell")
    path = (reply.get("result") or {}).get("map_file") or ""
    if not path or not os.path.isfile(path):
        raise HTTPException(status_code=404, detail=f"fading map not available: {path or 'unset'}")
    return path


def _load_fading_map() -> Dict[str, Any]:
    global _fading_cache
    path = _fading_map_path()
    if _fading_cache is not None and _fading_cache["path"] == path:
        return _fading_cache

    with open(path, "r", encoding="utf-8") as handle:
        raw = json.load(handle)

    grid = raw.get("map") or []
    flat = [v for row in grid for v in row]
    if not flat:
        raise HTTPException(status_code=404, detail="fading map has no data")

    ordered = sorted(flat)
    # Percentile clipping, not min/max: these maps have a long tail (p0 around -170 dB
    # against a median near -142), so the extremes flatten the colour scale.
    _fading_cache = {
        "path": path,
        "cell_size": float(raw.get("cell_size") or 0.0),
        "cell_number": int(raw.get("cell_number") or 0),
        "p1": ordered[int(0.01 * (len(ordered) - 1))],
        "p50": ordered[int(0.50 * (len(ordered) - 1))],
        "p99": ordered[int(0.99 * (len(ordered) - 1))],
        "min": ordered[0],
        "max": ordered[-1],
        "values": flat,
    }
    return _fading_cache


@app.get("/scenario/map")
async def scenario_map() -> Dict[str, Any]:
    info = _load_fading_map()
    return {key: info[key] for key in
            ("cell_size", "cell_number", "p1", "p50", "p99", "min", "max")}


@app.get("/scenario/map.bin")
async def scenario_map_bin() -> Response:
    info = _load_fading_map()
    # Raw float32 little-endian, row major: 329 KB for a 290x290 map, fetched once and
    # coloured in the browser. Sending it as JSON would cost three times as much.
    payload = struct.pack("<%df" % len(info["values"]), *info["values"])
    return Response(content=payload, media_type="application/octet-stream")


@app.get("/telemetry/snapshot")
async def telemetry_snapshot() -> Dict[str, Any]:
    snapshot = collector.cache.snapshot()
    return {"points": list(snapshot.values()), "series": len(snapshot)}


@app.post("/control/ue/{ue_id}")
async def set_ue(ue_id: int, values: Dict[str, Any] = Body(...)) -> Dict[str, Any]:
    return _forward(control.set, f"ue/{ue_id}", values)


@app.post("/control/batch")
async def set_batch(payload: Dict[str, Any] = Body(...)) -> Dict[str, Any]:
    commands = payload.get("cmds")
    if not isinstance(commands, list) or not commands:
        raise HTTPException(status_code=400, detail="expected a non-empty 'cmds' list")
    return _forward(control.batch, commands)


@app.get("/ue/{ue_id}/state")
async def ue_state(ue_id: int) -> Dict[str, Any]:
    # Control state comes from the emulator every time; only the metrics come from cache.
    reply = _forward(control.get, f"ue/{ue_id}")
    result = reply.get("result") or []
    if not result:
        raise HTTPException(status_code=404, detail=f"unknown ue {ue_id}")

    control_state = dict(result[0])
    # The state block is read back from the emulator too: buffer occupancy, delivered and
    # lost bytes, and the rules in force. It is what a client pacing its own injection
    # needs, and unlike the telemetry cache it is exact and in simulated time.
    state = control_state.pop("state", {})

    return {
        "ue_id": ue_id,
        "control": control_state,
        "state": state,
        "telemetry": collector.cache.for_ue(ue_id),
        "tti": reply.get("tti"),
        "t": reply.get("t"),
    }


@app.get("/ues")
async def ues() -> Dict[str, Any]:
    reply = _forward(control.get, "ue/*")
    return {"ues": reply.get("result", []), "tti": reply.get("tti"), "t": reply.get("t")}


@app.get("/schema")
async def schema() -> Dict[str, Any]:
    reply = _forward(control.describe)
    return {"proto": PROTO, "params": reply.get("result", [])}


@app.post("/sync/grant")
async def grant(payload: Dict[str, Any] = Body(...)) -> Dict[str, Any]:
    if "until_tti" in payload:
        until_tti = int(payload["until_tti"])
    elif "until_t" in payload:
        until_tti = round(float(payload["until_t"]) * 1000.0)
    else:
        raise HTTPException(status_code=400, detail="expected until_tti or until_t")
    return _forward(control.grant, until_tti)


@app.get("/healthz")
async def healthz() -> Dict[str, Any]:
    try:
        reply = control.ping()
        linked, detail = True, None
        tti, t = reply.get("tti"), reply.get("t")
    except ControlError as exc:
        linked, detail, tti, t = False, str(exc), None, None

    return {
        "proto": PROTO,
        "emulator_linked": linked,
        "detail": detail,
        "tti": tti,
        "t": t,
        "telemetry_series": len(collector.cache.snapshot()),
    }


@app.websocket("/stream")
async def stream(ws: WebSocket) -> None:
    await ws.accept()
    queue: asyncio.Queue = asyncio.Queue(maxsize=1000)
    _subscribers.append(queue)
    try:
        # A snapshot first, so a page that connects mid-run draws something immediately
        # instead of waiting a whole aggregation window.
        await ws.send_json({"type": "snapshot", "points": list(collector.cache.snapshot().values())})
        while True:
            message = await queue.get()
            await ws.send_json(message)
    except WebSocketDisconnect:
        pass
    finally:
        if queue in _subscribers:
            _subscribers.remove(queue)
