# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear

"""UDP collector for the emulator's Influx line protocol, plus the only cache the API has.

It subscribes to an extra output in [Monitoring]; it does not replace the one feeding
Telegraf. Everything stored here is telemetry: no control state is ever cached, because
the emulator is the source of truth for that.
"""

from __future__ import annotations

import socket
import threading
import time
from typing import Any, Callable, Dict, List, Optional, Tuple


def parse_line(line: str) -> Optional[Tuple[str, Dict[str, str], Dict[str, Any], Optional[int]]]:
    """Parses one Influx line: measurement,tags fields timestamp.

    Returns None for anything that does not look like a measurement, which keeps a
    malformed datagram from taking the collector down.
    """
    line = line.strip()
    if not line:
        return None

    # Split on unescaped spaces: measurement+tags, fields, optional timestamp.
    parts: List[str] = []
    current = ""
    escaped = False
    for ch in line:
        if escaped:
            current += ch
            escaped = False
        elif ch == "\\":
            escaped = True
        elif ch == " " and len(parts) < 2:
            parts.append(current)
            current = ""
        else:
            current += ch
    parts.append(current)

    if len(parts) < 2:
        return None

    head, field_text = parts[0], parts[1]
    timestamp = None
    if len(parts) > 2 and parts[2].strip():
        try:
            timestamp = int(parts[2].strip())
        except ValueError:
            timestamp = None

    head_items = head.split(",")
    measurement = head_items[0]
    tags: Dict[str, str] = {}
    for item in head_items[1:]:
        if "=" in item:
            key, value = item.split("=", 1)
            tags[key] = value

    fields: Dict[str, Any] = {}
    for item in field_text.split(","):
        if "=" not in item:
            continue
        key, value = item.split("=", 1)
        if value.startswith('"'):
            fields[key] = value.strip('"')
        else:
            try:
                fields[key] = float(value.rstrip("i"))
            except ValueError:
                fields[key] = value

    return measurement, tags, fields, timestamp


class TelemetryCache:
    """Last value per series, where a series is measurement plus its tags."""

    def __init__(self) -> None:
        self._lock = threading.Lock()
        self._series: Dict[str, Dict[str, Any]] = {}

    @staticmethod
    def key(measurement: str, tags: Dict[str, str]) -> str:
        if not tags:
            return measurement
        rendered = ",".join(f"{k}={v}" for k, v in sorted(tags.items()))
        return f"{measurement},{rendered}"

    def update(self, measurement: str, tags: Dict[str, str], fields: Dict[str, Any],
               timestamp: Optional[int]) -> Dict[str, Any]:
        entry = {
            "measurement": measurement,
            "tags": tags,
            "fields": fields,
            "ts_ns": timestamp,
            "received_at": time.time(),
        }
        with self._lock:
            self._series[self.key(measurement, tags)] = entry
        return entry

    def snapshot(self) -> Dict[str, Dict[str, Any]]:
        with self._lock:
            return dict(self._series)

    def for_ue(self, ue_id: int) -> Dict[str, Dict[str, Any]]:
        wanted = str(ue_id)
        with self._lock:
            return {
                key: entry
                for key, entry in self._series.items()
                if entry["tags"].get("ue_id") == wanted
            }

    def ue_ids(self) -> List[int]:
        with self._lock:
            ids = {
                entry["tags"]["ue_id"]
                for entry in self._series.values()
                if "ue_id" in entry["tags"]
            }
        return sorted(int(x) for x in ids if x.isdigit())


class TelemetryCollector:
    """UDP listener feeding the cache, with an optional fan-out for the WebSocket."""

    def __init__(self, host: str = "127.0.0.1", port: int = 8098) -> None:
        self._host, self._port = host, port
        self._sock: Optional[socket.socket] = None
        self._thread: Optional[threading.Thread] = None
        self._running = False
        self.cache = TelemetryCache()
        self.on_point: Optional[Callable[[Dict[str, Any]], None]] = None

    def start(self) -> None:
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((self._host, self._port))
        self._sock.settimeout(0.5)
        self._running = True
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._running = False
        if self._thread is not None:
            self._thread.join(timeout=2.0)
        if self._sock is not None:
            self._sock.close()
            self._sock = None

    def _serve(self) -> None:
        assert self._sock is not None
        while self._running:
            try:
                data, _ = self._sock.recvfrom(65535)
            except socket.timeout:
                continue
            except OSError:
                return

            for raw in data.decode("utf-8", errors="replace").splitlines():
                parsed = parse_line(raw)
                if parsed is None:
                    continue
                entry = self.cache.update(*parsed)
                if self.on_point is not None:
                    self.on_point(entry)
