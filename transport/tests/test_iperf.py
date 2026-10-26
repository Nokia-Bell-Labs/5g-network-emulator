# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Offline iperf configuration, session and report tests."""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from fikore_transport.iperf_cli import _resolve_ues
from fikore_transport.iperf_config import (IperfConfig, add_override, load_config,
                                           parse_bytes, parse_rate)
from fikore_transport.iperf_report import document, human
from fikore_transport.iperf_session import IperfSession
from fikore_transport.link import LoopbackConfig, LoopbackLink
from fikore_transport.scenario import ScenarioDocument


ROOT = Path(__file__).resolve().parents[2]
SCENARIO = ROOT / "config" / "control_demo.ini"


def config(**values):
    base = IperfConfig(
        scenario=SCENARIO, binary=ROOT / "bin" / "fikore",
        duration_s=0.2, interval_s=0.1, drain_timeout_s=2.0,
        ues=["study"])
    for key, value in values.items():
        setattr(base, key, value)
    return base.validate()


def test_units_and_json_precedence():
    assert parse_bytes("128KiB") == 128 * 1024
    assert parse_rate("10M") == 10_000_000
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "run.json"
        path.write_text(json.dumps({
            "schema_version": 1,
            "scenario": str(SCENARIO),
            "run": {"duration_s": 3},
            "transport": {"protocol": "udp", "bitrate": "4M"},
            "ues": ["controlDemo"],
        }))
        cfg = load_config(path, config(), {"duration_s": 2.0})
        assert cfg.duration_s == 2.0
        assert cfg.protocol == "udp"
        assert cfg.bitrate_bps == 4_000_000


def test_malformed_config_types_and_structural_overrides_are_rejected():
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "run.json"
        path.write_text(json.dumps({
            "scenario": str(SCENARIO),
            "transport": {"ack_over_link": "false"},
            "ues": "controlDemo",
        }))
        try:
            load_config(path, config())
            assert False, "malformed JSON types were accepted"
        except ValueError:
            pass
    cfg = config()
    try:
        add_override(cfg, "controlDemo.n_ues=1", ue=True)
        assert False, "structural UE override was accepted"
    except ValueError as exc:
        assert "structural" in str(exc)


def test_ue_block_expands_to_concrete_targets():
    doc = ScenarioDocument.read(str(SCENARIO))
    concrete, blocks, physical = _resolve_ues(doc, ["controlDemo"])
    assert concrete == ["controlDemo_0", "controlDemo_1"]
    assert blocks == ["controlDemo"]
    assert physical == {"controlDemo_0": 0, "controlDemo_1": 1}


def test_cli_dry_run_materializes_without_starting_emulator():
    env = dict(os.environ, PYTHONPATH=str(ROOT / "transport"))
    completed = subprocess.run(
        [sys.executable, "-m", "fikore_transport.iperf_cli",
         "--dry-run", "--ue", "controlDemo_0", "-t", "0.1"],
        cwd=ROOT, env=env, text=True, capture_output=True)
    assert completed.returncode == 0, completed.stderr
    assert "period: -1" in completed.stdout
    assert "sync_mode: barrier" in completed.stdout
    assert completed.stderr == ""


def test_tcp_duration_stops_gracefully_and_reports_intervals():
    cfg = config(protocol="tcp", parallel_per_ue=2)
    session = IperfSession(
        cfg, LoopbackLink(LoopbackConfig(rate_bps=50e6, queue_bytes=256 * 1024)))
    try:
        result = session.run()
    finally:
        session.close()
    assert result.drained
    assert len(result.intervals) >= 2
    assert result.intervals[-1].get("drain") is True
    assert len(result.totals["streams"]) == 2
    assert result.totals["sum"]["bytes"] > 0
    assert sum(item["sum"]["bytes"] for item in result.intervals) == (
        result.totals["sum"]["bytes"])
    assert all(view.sender.complete() for view in session.views)
    text = human(result)
    assert "[SUM]" in text and "RTT" in text
    value = document(cfg, result)
    assert value["start"]["test_start"]["protocol"] == "TCP"
    assert value["fikore"]["drained"]


def test_finite_tcp_finishes_before_the_safety_horizon():
    cfg = config(protocol="tcp", bytes_total=64 * 1024, duration_s=2.0)
    session = IperfSession(cfg, LoopbackLink(LoopbackConfig(rate_bps=50e6)))
    try:
        result = session.run()
    finally:
        session.close()
    assert result.drained
    assert result.active_ttis < 2000
    assert result.totals["streams"][0]["bytes"] == 64 * 1024


def test_large_finite_tcp_is_not_cut_off_by_duration_default():
    cfg = config(
        protocol="tcp", bytes_total=1_000_000, duration_s=0.001,
        timeout_s=5.0)
    session = IperfSession(
        cfg, LoopbackLink(LoopbackConfig(rate_bps=10e6, queue_bytes=256 * 1024)))
    try:
        result = session.run()
    finally:
        session.close()
    assert result.drained
    assert result.active_ttis > 1
    assert result.totals["streams"][0]["bytes"] == 1_000_000


def test_udp_rate_loss_and_jitter_fields():
    cfg = config(protocol="udp", bitrate_bps=20e6, length_bytes=1200)
    session = IperfSession(
        cfg, LoopbackLink(LoopbackConfig(rate_bps=5e6, queue_bytes=16 * 1024)))
    try:
        result = session.run()
    finally:
        session.close()
    total = result.totals["streams"][0]
    assert result.drained
    assert total["bytes_sent"] > total["bytes"]
    assert total["lost_packets"] > 0
    assert "jitter_ms" in total
    assert "Lost/Total Datagrams" in human(result)


if __name__ == "__main__":
    failed = 0
    for name, fn in sorted(globals().copy().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"ok   {name}")
            except AssertionError as exc:
                failed += 1
                print(f"FAIL {name}: {exc}")
    raise SystemExit(failed)
