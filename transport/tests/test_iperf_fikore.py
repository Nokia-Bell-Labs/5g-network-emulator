# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Emulator-backed acceptance tests for offline iperf sessions."""
import os
import tempfile
from pathlib import Path

from fikore_transport.emulator import Emulator, EmulatorConfig
from fikore_transport.fikore_link import FikoreLink
from fikore_transport.iperf_config import IperfConfig
from fikore_transport.iperf_session import IperfSession


ROOT = Path(os.environ.get(
    "FIKORE_DIR", str(Path(__file__).resolve().parents[2]))).resolve()
BINARY = ROOT / "bin" / "fikore"


class Skipped(Exception):
    pass


def run_session(cfg: IperfConfig, study_ues: list[str],
                target_to_physical: dict[str, int]):
    if not BINARY.is_file():
        raise Skipped(f"no emulator at {BINARY}")
    with tempfile.TemporaryDirectory(prefix="fikore-iperf-test-") as tmp:
        emulator = Emulator(EmulatorConfig(
            binary=str(BINARY), base_ini=str(cfg.scenario),
            socket_path=str(Path(tmp) / "control.sock"),
            duration_s=cfg.duration_s + cfg.drain_timeout_s + 1,
            work_dir=str(ROOT), n_ues=None, pkt_size_bits=cfg.mss * 8,
            log_path=str(Path(tmp) / "emulator.log"), study_ues=study_ues))
        link = FikoreLink(
            emulator, flow_to_ue={},
            ue_target_map={value: key for key, value in target_to_physical.items()})
        session = IperfSession(cfg, link, target_to_physical)
        try:
            return session.run()
        finally:
            session.close()


def test_tcp_reverse_parallel_multi_ue():
    cfg = IperfConfig(
        scenario=ROOT / "config" / "control_demo.ini", binary=BINARY,
        protocol="tcp", direction="ul", duration_s=0.1, interval_s=0.05,
        drain_timeout_s=2, parallel_per_ue=2,
        ues=["controlDemo_0", "controlDemo_1"])
    result = run_session(
        cfg, ["controlDemo"], {"controlDemo_0": 0, "controlDemo_1": 1})
    assert result.drained
    assert len(result.totals["streams"]) == 4
    assert result.conservation["bytes_conserved"]


def test_udp_converts_live_study_ue_and_preserves_accounting():
    cfg = IperfConfig(
        scenario=ROOT / "config" / "emulated_rural_n78_single_with_background.ini",
        binary=BINARY, protocol="udp", duration_s=0.1, interval_s=0.05,
        drain_timeout_s=2, bitrate_bps=2e6, ues=["capturedStudy"])
    result = run_session(cfg, ["capturedStudy"], {"capturedStudy": 0})
    assert result.drained
    assert result.totals["sum"]["bytes"] > 0
    assert result.conservation["bytes_conserved"]


if __name__ == "__main__":
    failed = 0
    for name, fn in sorted(globals().copy().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"ok   {name}")
            except Skipped as exc:
                print(f"SKIP {name}: {exc}")
            except AssertionError as exc:
                failed += 1
                print(f"FAIL {name}: {exc}")
    raise SystemExit(failed)
