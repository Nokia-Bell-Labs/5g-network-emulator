# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Human and machine-readable iperf-shaped output."""
from __future__ import annotations

from dataclasses import asdict
import json
from typing import Any

from .iperf_config import IperfConfig
from .iperf_session import SessionResult


def human(result: SessionResult) -> str:
    lines = [
        f"fikore-iperf3: offline {result.protocol.upper()} session",
        ("[ ID] Interval           Transfer     Bitrate"
         + ("         Retr   Cwnd       RTT" if result.protocol == "tcp"
            else "         Jitter    Lost/Total Datagrams")),
    ]
    many = len(result.totals["streams"]) > 1
    for interval in result.intervals:
        for stream in interval["streams"]:
            lines.append(_stream_line(stream, result.protocol))
        if many:
            lines.append(_sum_line(interval["sum"], interval["start"],
                                   interval["end"], result.protocol))
    lines.append("- " * 36)
    for stream in result.totals["streams"]:
        total = dict(stream)
        total["start"] = 0.0
        total["end"] = result.end_ttis / 1000.0
        lines.append(_stream_line(total, result.protocol))
    if many:
        lines.append(_sum_line(result.totals["sum"], 0.0,
                               result.end_ttis / 1000.0, result.protocol))
    conservation = result.conservation
    if conservation.get("available"):
        lines.append(
            "FikoRE bytes: submitted={submitted_bytes} delivered={delivered} "
            "dropped={dropped} expired={expired} in-flight={in_flight_bytes} "
            "=> {status}".format(
                **conservation,
                **conservation["terminal_bytes"],
                status=("conserved" if conservation["bytes_conserved"]
                        else "NOT CONSERVED")))
    if not result.drained:
        lines.append("WARNING: drain timeout expired with traffic still in flight")
    return "\n".join(lines)


def document(cfg: IperfConfig, result: SessionResult,
             metadata: dict[str, Any] | None = None,
             error: str | None = None) -> dict:
    connected = [
        {"socket": stream["socket"], "local_host": f"sim:{stream['ue_id']}",
         "remote_host": "fikore", "local_port": 0, "remote_port": 0}
        for stream in result.totals["streams"]
    ]
    start = {
        "version": "fikore-iperf3 0.1.0",
        "connected": connected,
        "test_start": {
            "protocol": cfg.protocol.upper(),
            "duration": result.active_ttis / 1000.0,
            "num_streams": len(connected),
            "reverse": cfg.direction == "ul",
            "blksize": cfg.length_bytes if cfg.protocol == "udp" else cfg.mss,
        },
    }
    end_streams = []
    for stream in result.totals["streams"]:
        sender = dict(stream)
        sender["bytes"] = stream["bytes_sent"]
        sender["bits_per_second"] = stream["send_bits_per_second"]
        sender["sender"] = True
        receiver = dict(stream)
        receiver["sender"] = False
        end_streams.append({"sender": sender, "receiver": receiver})
    sum_received = dict(result.totals["sum"])
    sum_received["sender"] = False
    sum_sent = dict(result.totals["sum"])
    sum_sent["bytes"] = result.totals["sum"]["bytes_sent"]
    sum_sent["bits_per_second"] = result.totals["sum"]["send_bits_per_second"]
    sum_sent["sender"] = True
    end = {
        "streams": end_streams,
        "sum_sent": sum_sent,
        "sum_received": sum_received,
    }
    fikore = {
        "schema_version": 1,
        "simulated_time_s": result.end_ttis / 1000.0,
        "active_time_s": result.active_ttis / 1000.0,
        "wall_time_s": result.wall_seconds,
        "drained": result.drained,
        "conservation": result.conservation,
        "configuration": _config_dict(cfg),
        **(metadata or {}),
    }
    value = {
        "start": start,
        "intervals": result.intervals,
        "end": end,
        "fikore": fikore,
    }
    if error is not None:
        value["error"] = error
    return value


def dumps(value: dict) -> str:
    return json.dumps(value, indent=2, sort_keys=True) + "\n"


def _config_dict(cfg: IperfConfig) -> dict:
    value = asdict(cfg)
    for key in ("scenario", "binary", "output_dir", "source_config"):
        if value[key] is not None:
            value[key] = str(value[key])
    value["section_overrides"] = {
        f"{section}.{key}": setting
        for (section, key), setting in cfg.section_overrides.items()
    }
    return value


def _stream_line(stream: dict, protocol: str) -> str:
    start, end = stream["start"], stream["end"]
    suffix = "  [drain]" if stream.get("drain") else ""
    base = (
        f"[{stream['socket']:3d}] {start:6.2f}-{end:6.2f} sec "
        f"{_amount(stream['bytes']):>11}  {_rate(stream['bits_per_second']):>16}")
    if protocol == "tcp":
        rtt = stream.get("srtt_ms")
        line = (
            f"{base} {stream.get('retransmits', 0):6d} "
            f"{_amount(stream.get('snd_cwnd', 0)):>10} "
            f"{rtt:7.2f} ms" if rtt is not None
            else f"{base} {stream.get('retransmits', 0):6d} "
                 f"{_amount(stream.get('snd_cwnd', 0)):>10}       -")
        return line + suffix
    packets = stream.get("packets", 0)
    lost = stream.get("lost_packets", 0)
    return (
        f"{base} {stream.get('jitter_ms', 0):7.3f} ms "
        f"{lost}/{packets} ({stream.get('lost_percent', 0):.1f}%)") + suffix


def _sum_line(total: dict, start: float, end: float, protocol: str) -> str:
    suffix = "  [drain]" if total.get("drain") else ""
    base = (
        f"[SUM] {start:6.2f}-{end:6.2f} sec "
        f"{_amount(total['bytes']):>11}  {_rate(total['bits_per_second']):>16}")
    if protocol == "tcp":
        return f"{base} {total.get('retransmits', 0):6d}{suffix}"
    return (
        f"{base} {total.get('jitter_ms', 0):7.3f} ms "
        f"{total.get('lost_packets', 0)}/{total.get('packets', 0)} "
        f"({total.get('lost_percent', 0):.1f}%){suffix}")


def _amount(value: float) -> str:
    for unit, divisor in (("GBytes", 1e9), ("MBytes", 1e6), ("KBytes", 1e3)):
        if value >= divisor:
            return f"{value / divisor:.2f} {unit}"
    return f"{int(value)} Bytes"


def _rate(value: float) -> str:
    for unit, divisor in (("Gbits/sec", 1e9), ("Mbits/sec", 1e6),
                          ("Kbits/sec", 1e3)):
        if value >= divisor:
            return f"{value / divisor:.2f} {unit}"
    return f"{value:.0f} bits/sec"
