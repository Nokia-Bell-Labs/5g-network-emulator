#!/usr/bin/env python3
# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Process-level exit status contract for the emulator."""

import json
import re
import socket
import subprocess
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
BINARY = REPO / "bin" / "fikore"
TEMPLATE = REPO / "config" / "control_demo.ini"


def render(path: Path, **values: object) -> Path:
    text = TEMPLATE.read_text()
    for key, value in values.items():
        text = re.sub(rf"^{re.escape(key)}:.*$", f"{key}: {value}",
                      text, flags=re.MULTILINE)
    text = text.replace("[Monitoring]\nenabled: true",
                        "[Monitoring]\nenabled: false")
    path.write_text(text)
    return path


def run(config: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run([str(BINARY), str(config)], cwd=REPO,
                          capture_output=True, text=True, timeout=10)


def test_normal_completion_is_zero(work: Path) -> None:
    result = run(render(work / "normal.ini", duration=0.01, period=-1,
                        transport="none", enabled="false"))
    assert result.returncode == 0, result.stderr + result.stdout


def test_missing_config_is_noinput(work: Path) -> None:
    result = run(work / "missing.ini")
    assert result.returncode == 66, result.stderr + result.stdout


def test_invalid_cli_is_usage() -> None:
    result = subprocess.run([str(BINARY), "one.ini", "two.ini"], cwd=REPO,
                            capture_output=True, text=True, timeout=10)
    assert result.returncode == 64, result.stderr + result.stdout


def test_malformed_config_is_config_error(work: Path) -> None:
    result = run(render(work / "malformed.ini", duration="not-a-number"))
    assert result.returncode == 78, result.stderr + result.stdout


def test_fatal_runtime_error_is_software(work: Path) -> None:
    result = run(render(work / "runtime.ini", bandwidth=1))
    assert result.returncode == 70, result.stderr + result.stdout


def test_invalid_control_policy_is_config_error(work: Path) -> None:
    result = run(render(work / "bad-control.ini", sync_mode="sometimes"))
    assert result.returncode == 78, result.stderr + result.stdout


def test_invalid_control_enabled_is_config_error(work: Path) -> None:
    result = run(render(work / "bad-enabled.ini", enabled="ture"))
    assert result.returncode == 78, result.stderr + result.stdout


def test_malformed_timeline_is_config_error(work: Path) -> None:
    timeline = work / "bad.ndjson"
    timeline.write_text(
        '{"id":1,"cmds":[{"op":"ping","target":"cell"},'
        '{"op":"events"}]}\n')
    result = run(render(work / "bad-timeline.ini", transport="file",
                        timeline_file=timeline))
    assert result.returncode == 78, result.stderr + result.stdout


def test_unreadable_timeline_shape_is_config_error(work: Path) -> None:
    directory = work / "timeline-directory"
    directory.mkdir()
    result = run(render(work / "directory-timeline.ini", transport="file",
                        timeline_file=directory))
    assert result.returncode == 78, result.stderr + result.stdout


def test_control_socket_setup_failure_is_io_error(work: Path) -> None:
    result = run(render(work / "bad-socket.ini",
                        address="/no/such/fikore/directory/control.sock"))
    assert result.returncode == 74, result.stderr + result.stdout


def test_credit_timeout_is_tempfail(work: Path) -> None:
    result = run(render(work / "timeout.ini", duration=0.1, period=-1,
                        sync_mode="barrier", credit_timeout_ms=20,
                        on_timeout="abort", address=work / "timeout.sock"))
    assert result.returncode == 75, result.stderr + result.stdout
    assert "reason=credit_timeout" in result.stdout


def test_lost_control_peer_is_io_error(work: Path) -> None:
    sock_path = work / "peer.sock"
    config = render(work / "peer.ini", duration=1, period=-1,
                    sync_mode="barrier", credit_timeout_ms=5000,
                    on_peer_loss="abort", address=sock_path)
    process = subprocess.Popen([str(BINARY), str(config)], cwd=REPO,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True)
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        for _ in range(200):
            try:
                client.connect(str(sock_path))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                time.sleep(0.01)
        else:
            raise AssertionError("control socket did not become connectable")
        stream = client.makefile("rwb")
        hello = json.loads(stream.readline())
        assert hello["proto"] == "fikore-control-1"
        stream.write(b'{"proto":"fikore-control-1"}\n')
        stream.flush()
        stream.close()
        client.close()
        output, _ = process.communicate(timeout=10)
        assert process.returncode == 74, output
        assert "reason=control_peer_lost" in output
    finally:
        client.close()
        if process.poll() is None:
            process.kill()
            process.wait()


def main() -> int:
    tests = [
        test_normal_completion_is_zero,
        test_missing_config_is_noinput,
        test_malformed_config_is_config_error,
        test_fatal_runtime_error_is_software,
        test_invalid_control_policy_is_config_error,
        test_invalid_control_enabled_is_config_error,
        test_malformed_timeline_is_config_error,
        test_unreadable_timeline_shape_is_config_error,
        test_control_socket_setup_failure_is_io_error,
        test_credit_timeout_is_tempfail,
        test_lost_control_peer_is_io_error,
    ]
    with tempfile.TemporaryDirectory(prefix="fikore-exit-status-") as tmp:
        work = Path(tmp)
        for test in tests:
            test(work)
            print(f"ok   {test.__name__}")
        test_invalid_cli_is_usage()
        print("ok   test_invalid_cli_is_usage")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
