# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear

"""NDJSON client for the emulator's control channel.

Deliberately dumb: it forwards, it does not remember. The emulator is the source of
truth, so this client keeps no shadow copy of what was sent and reapplies nothing when
the connection comes back. If the emulator restarts, that is a new session.
"""

from __future__ import annotations

import json
import socket
import threading
from dataclasses import dataclass
from typing import Any, Dict, List, Optional

from .proto import PROTO


class ControlError(RuntimeError):
    pass


@dataclass
class ControlConfig:
    transport: str = "unix"          # unix | tcp
    address: str = "/tmp/fikore-control.sock"
    port: int = 8097
    timeout_s: float = 5.0


class ControlClient:
    """One connection, one lock, request/response.

    The emulator accepts a single client, so serialising calls here is not a limitation:
    it is the same constraint expressed on this side.
    """

    def __init__(self, cfg: ControlConfig) -> None:
        self._cfg = cfg
        self._lock = threading.Lock()
        self._sock: Optional[socket.socket] = None
        self._file = None
        self._next_id = 0
        self.last_error: Optional[str] = None

    # -- connection ---------------------------------------------------------------

    def _connect(self) -> None:
        if self._cfg.transport == "unix":
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            target: Any = self._cfg.address
        else:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            target = (self._cfg.address, self._cfg.port)

        sock.settimeout(self._cfg.timeout_s)
        sock.connect(target)
        file = sock.makefile("rw")

        hello_line = file.readline()
        if not hello_line:
            raise ControlError("emulator closed the connection before the handshake")
        hello = json.loads(hello_line)
        if hello.get("proto") != PROTO:
            raise ControlError(
                f"protocol mismatch: emulator speaks {hello.get('proto')!r}, api speaks {PROTO!r}"
            )

        file.write(json.dumps({"proto": PROTO}) + "\n")
        file.flush()

        self._sock, self._file = sock, file
        self.last_error = None

    def _ensure_connected(self) -> None:
        if self._sock is not None:
            return
        self._connect()

    def _drop(self, error: str) -> None:
        self.last_error = error
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
        self._sock, self._file = None, None

    @property
    def connected(self) -> bool:
        return self._sock is not None

    def close(self) -> None:
        with self._lock:
            self._drop("closed")

    # -- request ------------------------------------------------------------------

    def _request(self, message: Dict[str, Any]) -> Dict[str, Any]:
        with self._lock:
            # One reconnection attempt: if the emulator restarted, the first write fails
            # and the retry lands on the new session.
            for attempt in (0, 1):
                try:
                    self._ensure_connected()
                    self._next_id += 1
                    message["id"] = self._next_id
                    assert self._file is not None
                    self._file.write(json.dumps(message) + "\n")
                    self._file.flush()
                    line = self._file.readline()
                    if not line:
                        raise ControlError("emulator closed the connection")
                    return json.loads(line)
                except (OSError, ControlError, json.JSONDecodeError) as exc:
                    self._drop(str(exc))
                    if attempt == 1:
                        raise ControlError(str(exc)) from exc
            raise ControlError("unreachable")

    # -- operations ---------------------------------------------------------------

    def set(self, target: str, values: Dict[str, Any]) -> Dict[str, Any]:
        return self._request({"cmds": [{"target": target, "set": values}]})

    def batch(self, commands: List[Dict[str, Any]]) -> Dict[str, Any]:
        return self._request({"cmds": commands})

    def get(self, target: str) -> Dict[str, Any]:
        return self._request({"op": "get", "target": target})

    def describe(self) -> Dict[str, Any]:
        return self._request({"op": "describe", "target": "cell"})

    def ping(self) -> Dict[str, Any]:
        return self._request({"op": "ping", "target": "cell"})

    def grant(self, until_tti: int) -> Dict[str, Any]:
        return self._request({"op": "grant", "until_tti": until_tti})
