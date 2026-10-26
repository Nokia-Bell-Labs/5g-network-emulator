# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Starting the emulator and generating the `.ini` a co-simulated run needs.

The emulator owns the socket, so it is started first and the client connects to it.
Nothing here knows about transport or congestion control.
"""
from __future__ import annotations

import os
import socket
import subprocess
import time
from dataclasses import dataclass, field

from .scenario import Override, ScenarioDocument

PROTO = "fikore-control-1"


@dataclass
class EmulatorConfig:
    binary: str
    base_ini: str
    socket_path: str
    duration_s: float
    work_dir: str | None = None       # fading maps are resolved next to the binary
    n_ues: int | None = 1
    pkt_size_bits: int = 12000        # the MSS the transport model must match
    delay_budget_s: float | None = 30.0  # None preserves the scenario value
    random_v: bool | None = False        # None preserves the scenario value
    log_path: str | None = None
    max_object_events: int = 65536
    study_ues: list[str] | None = None
    section_overrides: dict[tuple[str, str], str] = field(default_factory=dict)
    ue_overrides: dict[str, dict[str, str]] = field(default_factory=dict)
    extra: dict[str, str] = field(default_factory=dict)
    last_overrides: list[Override] = field(default_factory=list, init=False)

    def render(self, path: str) -> str:
        doc = ScenarioDocument.read(self.base_ini)
        targets = list(self.study_ues) if self.study_ues is not None else doc.ue_ids()
        if not targets:
            raise ValueError("the scenario has no UE selected for co-simulation")
        for ue_id in targets:
            doc.get_ue(ue_id, "ue_id")  # validate before changing anything

        global_values = {
            "duration": f"{self.duration_s}",
            "period": "-1",             # fast mode; the barrier requires it
            "progress_log_period_s": "0",
        }
        control_values = {
            "enabled": "true",
            "transport": "unix",
            "address": self.socket_path,
            "sync_mode": "barrier",
            "on_timeout": "abort",
            "max_object_events": f"{self.max_object_events}",
        }
        for ue_id in targets:
            values = {}
            if self.delay_budget_s is not None:
                values["pkt_delay_budget"] = f"{self.delay_budget_s}"
            if self.random_v is not None:
                values["random_v"] = "true" if self.random_v else "false"
            if self.n_ues is not None:
                values["n_ues"] = f"{self.n_ues}"
            for key, value in values.items():
                doc.set_ue(ue_id, key, value, source="cosim")

        for (section, key), value in self.section_overrides.items():
            doc.set(section, key, value, source="configuration", create=False)
        for ue_id, values in self.ue_overrides.items():
            for key, value in values.items():
                doc.set_ue(ue_id, key, value, source="configuration")
        # Safety invariants win over user overrides.  Their before/after records make
        # conversion of a live scenario visible in the manifest.
        for key, value in global_values.items():
            doc.set("Global", key, value, source="forced-cosim")
        for key, value in control_values.items():
            doc.set("Control", key, value, source="forced-cosim")
        doc.set("Monitoring", "enabled", "false", source="forced-cosim")
        for ue_id in targets:
            for key, value in {
                    "ue_type": "1", "dl_target": "0.0", "ul_target": "0.0",
                    "pkt_size": f"{self.pkt_size_bits}"}.items():
                doc.set_ue(ue_id, key, value, source="forced-cosim")

        # Compatibility for callers that predate section-aware overrides.  The old
        # flat regex allowed these two control keys to override normal co-sim mode;
        # integration tests use that escape hatch to exercise async overflow.
        legacy_sections = {"period": "Global", "sync_mode": "Control"}
        for key, value in self.extra.items():
            section = legacy_sections.get(key)
            if section is not None:
                doc.set(section, key, value, source="legacy-extra")
            else:
                for ue_id in targets:
                    doc.set_ue(ue_id, key, value, source="legacy-extra")

        self.last_overrides = list(doc.overrides)
        return doc.write(path)


class Emulator:
    def __init__(self, cfg: EmulatorConfig) -> None:
        self.cfg = cfg
        self.ini = cfg.render(os.path.join(os.path.dirname(cfg.socket_path) or ".",
                                           "cosim-run.ini"))
        if os.path.exists(cfg.socket_path):
            os.unlink(cfg.socket_path)
        log = open(cfg.log_path or "/tmp/fikore-cosim.log", "w")
        self._log = log
        self.proc = subprocess.Popen([cfg.binary, self.ini],
                                     cwd=cfg.work_dir or os.path.dirname(cfg.binary) or ".",
                                     stdout=log, stderr=subprocess.STDOUT)

    def wait_for_socket(self, timeout_s: float = 10.0) -> None:
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            if os.path.exists(self.cfg.socket_path):
                return
            if self.proc.poll() is not None:
                raise RuntimeError(f"emulator exited early{self.log_tail()}")
            time.sleep(0.01)
        raise TimeoutError(f"socket never appeared{self.log_tail()}")

    def connect(self, timeout_s: float = 10.0) -> socket.socket:
        """The file exists from `bind`, but only `listen` makes it connectable, so a
        connection refused here is the emulator still starting up and not a failure."""
        self.wait_for_socket(timeout_s)
        deadline = time.time() + timeout_s
        while True:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                sock.connect(self.cfg.socket_path)
                return sock
            except (ConnectionRefusedError, FileNotFoundError):
                sock.close()
                if self.proc.poll() is not None:
                    raise RuntimeError(f"emulator exited early{self.log_tail()}")
                if time.time() >= deadline:
                    raise
                time.sleep(0.01)

    def log_tail(self, lines: int = 12) -> str:
        """Appended to every failure: a stack trace in the client says nothing about
        why the emulator refused, and the reason is always in its log."""
        try:
            self._log.flush()
            tail = open(self._log.name).read().splitlines()[-lines:]
        except OSError:
            return f" (see {self._log.name})"
        return f"\n--- tail of {self._log.name} ---\n" + "\n".join(tail)

    def close(self) -> None:
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self._log.close()
