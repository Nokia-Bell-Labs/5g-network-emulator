# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Configuration and validation for the offline iperf-like runner."""
from __future__ import annotations

from dataclasses import dataclass, field, replace
import json
from pathlib import Path
import re
from typing import Any


_UNITS = {
    "": 1, "K": 1_000, "M": 1_000_000, "G": 1_000_000_000,
    "KI": 1024, "MI": 1024 ** 2, "GI": 1024 ** 3,
}


def parse_number(value: str | int | float, kind: str) -> int:
    if isinstance(value, (int, float)):
        result = int(value)
    else:
        match = re.fullmatch(r"\s*(\d+(?:\.\d+)?)\s*([kKmMgG](?:i)?)?[bB]?\s*",
                             value)
        if not match:
            raise ValueError(f"invalid {kind}: {value!r}")
        suffix = (match.group(2) or "").upper()
        result = int(float(match.group(1)) * _UNITS[suffix])
    if result <= 0:
        raise ValueError(f"{kind} must be positive")
    return result


def parse_bytes(value: str | int | float) -> int:
    return parse_number(value, "byte count")


def parse_rate(value: str | int | float) -> float:
    return float(parse_number(value, "bitrate"))


@dataclass
class IperfConfig:
    scenario: Path
    binary: Path
    protocol: str = "tcp"
    direction: str = "dl"
    duration_s: float = 10.0
    bytes_total: int | None = None
    interval_s: float = 1.0
    drain_timeout_s: float = 5.0
    timeout_s: float = 300.0
    parallel_per_ue: int = 1
    ues: list[str] = field(default_factory=list)
    bitrate_bps: float = 1_000_000.0
    length_bytes: int = 1200
    mss: int = 1500
    rwnd_bytes: int = 256 * 1024
    congestion: str = "cubic"
    ack_over_link: bool = False
    ecn: str = "not-ect"
    output_dir: Path | None = None
    json_output: bool = False
    quiet: bool = False
    section_overrides: dict[tuple[str, str], str] = field(default_factory=dict)
    ue_overrides: dict[str, dict[str, str]] = field(default_factory=dict)
    source_config: Path | None = None

    def validate(self, require_ues: bool = True) -> "IperfConfig":
        if self.protocol not in ("tcp", "udp"):
            raise ValueError("protocol must be tcp or udp")
        if self.direction not in ("dl", "ul"):
            raise ValueError("direction must be dl or ul")
        if self.duration_s <= 0:
            raise ValueError("duration must be positive")
        if self.interval_s <= 0:
            raise ValueError("interval must be positive")
        if self.drain_timeout_s < 0:
            raise ValueError("drain timeout must be non-negative")
        if self.timeout_s <= 0:
            raise ValueError("run timeout must be positive")
        if self.parallel_per_ue <= 0:
            raise ValueError("parallel streams must be positive")
        if self.mss <= 0 or self.length_bytes <= 0 or self.rwnd_bytes <= 0:
            raise ValueError("MSS, datagram length and window must be positive")
        if self.protocol == "udp" and self.bytes_total is not None:
            raise ValueError("finite-byte UDP runs are not supported; use duration")
        if self.protocol == "tcp" and self.congestion not in ("reno", "cubic", "prague"):
            raise ValueError("congestion must be reno, cubic or prague")
        if self.protocol == "udp" and self.bitrate_bps <= 0:
            raise ValueError("UDP bitrate must be positive")
        if self.bytes_total is not None and self.bytes_total <= 0:
            raise ValueError("byte count must be positive")
        if not self.scenario.is_file():
            raise ValueError(f"scenario does not exist: {self.scenario}")
        if require_ues and not self.ues:
            raise ValueError("at least one UE must be selected")
        return self


def load_config(path: Path | None, defaults: IperfConfig,
                overrides: dict[str, Any] | None = None) -> IperfConfig:
    cfg = replace(
        defaults, ues=list(defaults.ues),
        section_overrides=dict(defaults.section_overrides),
        ue_overrides={key: dict(value)
                      for key, value in defaults.ue_overrides.items()})
    if path is not None:
        path = path.resolve()
        raw = json.loads(path.read_text())
        if not isinstance(raw, dict):
            raise ValueError("run configuration must be a JSON object")
        allowed = {"schema_version", "scenario", "binary", "run", "transport",
                   "ues", "emulator_overrides"}
        unknown = set(raw) - allowed
        if unknown:
            raise ValueError(f"unknown config keys: {', '.join(sorted(unknown))}")
        if raw.get("schema_version", 1) != 1:
            raise ValueError("unsupported config schema_version")
        base = path.parent
        if "scenario" in raw:
            cfg.scenario = (base / raw["scenario"]).resolve()
        if "binary" in raw:
            cfg.binary = (base / raw["binary"]).resolve()
        run = raw.get("run", {})
        if not isinstance(run, dict):
            raise ValueError("run must be an object")
        _reject_unknown(run, {"duration_s", "bytes", "interval_s",
                              "drain_timeout_s", "timeout_s", "output_dir"}, "run")
        if "duration_s" in run and "bytes" in run:
            raise ValueError("run.duration_s and run.bytes are mutually exclusive")
        if "duration_s" in run:
            cfg.duration_s = float(run["duration_s"])
        if "bytes" in run:
            cfg.bytes_total = parse_bytes(run["bytes"])
        if "interval_s" in run:
            cfg.interval_s = float(run["interval_s"])
        if "drain_timeout_s" in run:
            cfg.drain_timeout_s = float(run["drain_timeout_s"])
        if "timeout_s" in run:
            cfg.timeout_s = float(run["timeout_s"])
        if "output_dir" in run:
            cfg.output_dir = (base / run["output_dir"]).resolve()
        transport = raw.get("transport", {})
        if not isinstance(transport, dict):
            raise ValueError("transport must be an object")
        _reject_unknown(
            transport,
            {"protocol", "direction", "parallel_per_ue", "bitrate",
             "length_bytes", "mss", "rwnd_bytes", "congestion",
             "ack_over_link", "ecn"},
            "transport")
        for key in ("protocol", "direction", "congestion", "ecn"):
            if key in transport:
                setattr(cfg, key, str(transport[key]).lower())
        if "parallel_per_ue" in transport:
            cfg.parallel_per_ue = int(transport["parallel_per_ue"])
        if "bitrate" in transport:
            cfg.bitrate_bps = parse_rate(transport["bitrate"])
        if "length_bytes" in transport:
            cfg.length_bytes = parse_bytes(transport["length_bytes"])
        if "mss" in transport:
            cfg.mss = parse_bytes(transport["mss"])
        if "rwnd_bytes" in transport:
            cfg.rwnd_bytes = parse_bytes(transport["rwnd_bytes"])
        if "ack_over_link" in transport:
            if not isinstance(transport["ack_over_link"], bool):
                raise ValueError("transport.ack_over_link must be boolean")
            cfg.ack_over_link = transport["ack_over_link"]
        if "ues" in raw:
            if not isinstance(raw["ues"], list) or not all(
                    isinstance(value, (str, int)) for value in raw["ues"]):
                raise ValueError("ues must be an array of strings")
            cfg.ues = [str(value) for value in raw["ues"]]
        emulator_overrides = raw.get("emulator_overrides", {})
        if not isinstance(emulator_overrides, dict):
            raise ValueError("emulator_overrides must be an object")
        for target, value in emulator_overrides.items():
            _add_emulator_override(cfg, target, value)
        cfg.source_config = path
    for key, value in (overrides or {}).items():
        if value is not None:
            setattr(cfg, key, value)
    return cfg.validate(require_ues=False)


def add_override(cfg: IperfConfig, expression: str, ue: bool = False) -> None:
    if "=" not in expression:
        raise ValueError(f"override must be TARGET.KEY=VALUE: {expression!r}")
    target, value = expression.split("=", 1)
    if ue:
        if "." not in target:
            raise ValueError(f"UE override must be UE_ID.KEY=VALUE: {expression!r}")
        ue_id, key = target.rsplit(".", 1)
        _validate_ue_override_key(key)
        cfg.ue_overrides.setdefault(ue_id, {})[key] = value
    else:
        _add_emulator_override(cfg, target, value)


def _add_emulator_override(cfg: IperfConfig, target: str, value: object) -> None:
    if target.startswith("UE:"):
        selector = target[3:]
        if "." not in selector:
            raise ValueError(f"invalid UE override target: {target!r}")
        ue_id, key = selector.rsplit(".", 1)
        _validate_ue_override_key(key)
        cfg.ue_overrides.setdefault(ue_id, {})[key] = str(value)
        return
    if "." not in target:
        raise ValueError(f"override must be SECTION.KEY: {target!r}")
    section, key = target.split(".", 1)
    if section == "UE":
        raise ValueError("UE overrides require UE:UE_ID.KEY")
    cfg.section_overrides[(section, key)] = str(value)


def _reject_unknown(value: dict, allowed: set[str], label: str) -> None:
    unknown = set(value) - allowed
    if unknown:
        raise ValueError(f"unknown {label} keys: {', '.join(sorted(unknown))}")


def _validate_ue_override_key(key: str) -> None:
    if key in ("ue_id", "n_ues"):
        raise ValueError(
            f"{key} is structural and cannot be overridden for an iperf run")
