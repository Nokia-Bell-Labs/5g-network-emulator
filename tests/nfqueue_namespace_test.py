#!/usr/bin/env python3
"""Privileged UDP/TCP NFQUEUE lifecycle smoke test.

Set FIKORE_RUN_PRIVILEGED_NFQUEUE=1 and run as root. The default test suite
skips this test because network namespaces and iptables are host mutations.
"""

from __future__ import annotations

import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
QUEUE_UL = 4100
QUEUE_DL = 4101
UDP_PORT = 39001
TCP_PORT = 39002


def require_environment() -> bool:
    if os.environ.get("FIKORE_RUN_PRIVILEGED_NFQUEUE") != "1":
        print("[SKIP] set FIKORE_RUN_PRIVILEGED_NFQUEUE=1")
        return False
    if os.geteuid() != 0:
        raise SystemExit("privileged NFQUEUE test must run as root")
    for command in ("ip", "iptables"):
        if shutil.which(command) is None:
            raise SystemExit(f"required command is missing: {command}")
    return True


def run(*args: str) -> None:
    subprocess.run(args, check=True, cwd=ROOT)


def config_text(run_id: str) -> str:
    map_path = (
        ROOT
        / "include/maps_scenarios/"
        "macroscopic_fading_map_URBAN_MICROCELL_2.38.json"
    )
    return f"""[Global]
duration: 2
period: 1
run_id: {run_id}
progress_log_period_s: -1
multithreading: false
threads: 0
verbose: false

[UE]
ue_id: nfqueueReal
ue_type: 0
n_ues: 1
ul_queue_n: {QUEUE_UL}
dl_queue_n: {QUEUE_DL}
n_antennas: 1
cqi_period: 1
ri_period: 1
random_v: false
log_freq: 1
log_ue: true
log_traffic: true
log_quality: false
traffic_type: 0
ul_target: 0
dl_target: 0
pkt_size: 12000
mobility_type: 0
pos_x: 50
pos_y: 0
random_init: false
speed: 0
max_distance: 100
priority: 1
pkt_delay_budget: 1
ue_height: 1.5
ue_location_type: outdoor

[Scenario]
scenario_type: 0
map_file: {map_path}

[eNBConfig]
modulation_m: 1
cqi_mode: 1
tx_power: 43
eNB_gain: 8.7
UT_gain: 0
frequency: 2380000000
bandwidth: 20000000

[PDCP_RLC]
backhaul_d: 0
backhaul_d_var: 0
order_pkts: true

[MACLayer]
metric_type: 5
mimo_layers: 1
n_ofdm_syms: 14
n_re_freq: 12
numerology: 1
max_rtx_ul: 4
max_rtx_dl: 4
harq_model: legacy_bler
mcs_tables: true
scheduling_mode: 1
scheduling_type: 0
scheduling_config: 1
duplexing_type: 1
ratio_DL_UL: 0.5

[PHYLayer]
interference_ues: 0
interference_eNBs: 0
interfered_bandwidth_ratio: 0
thermal_noise: -174
enb_noise_figure: 2
ut_noise_figure: 9
air_delay_var_ul: 0
rtx_period_ul: 0.004
rtx_period_var_ul: 0
rtx_proc_delay_ul: 0.001
rtx_proc_delay_var_ul: 0
air_delay_var_dl: 0
rtx_period_dl: 0.004
rtx_period_var_dl: 0
rtx_proc_delay_dl: 0.001
rtx_proc_delay_var_dl: 0
"""


def echo_service(stop: threading.Event) -> None:
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    udp.settimeout(0.1)
    tcp.settimeout(0.1)
    udp.bind(("127.0.0.1", UDP_PORT))
    tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    tcp.bind(("127.0.0.1", TCP_PORT))
    tcp.listen(4)
    while not stop.is_set():
        try:
            payload, peer = udp.recvfrom(65535)
            udp.sendto(payload, peer)
        except socket.timeout:
            pass
        try:
            connection, _ = tcp.accept()
        except socket.timeout:
            continue
        with connection:
            payload = connection.recv(65535)
            connection.sendall(payload)
    udp.close()
    tcp.close()


def client_traffic(namespace: str) -> None:
    code = f"""
import socket
udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
udp.settimeout(3)
udp.sendto(b'fikore-udp', ('127.0.0.1', {UDP_PORT}))
assert udp.recvfrom(1024)[0] == b'fikore-udp'
tcp = socket.create_connection(('127.0.0.1', {TCP_PORT}), timeout=3)
tcp.sendall(b'fikore-tcp')
assert tcp.recv(1024) == b'fikore-tcp'
tcp.close()
"""
    subprocess.run(
        ["ip", "netns", "exec", namespace, sys.executable, "-c", code],
        check=True,
        cwd=ROOT,
    )


def counter(line: str, name: str) -> int:
    match = re.search(rf"(?:^| ){re.escape(name)}:([0-9]+)", line)
    if match is None:
        raise AssertionError(f"missing {name} in final UE log line")
    return int(match.group(1))


def main() -> None:
    if not require_environment():
        return
    namespace = f"fikore-nfq-{os.getpid()}"
    run_id = f"nfqueue-test-{os.getpid()}"
    log_dir = ROOT / "logs" / run_id
    process: subprocess.Popen[str] | None = None
    server: subprocess.Popen[str] | None = None
    with tempfile.TemporaryDirectory(prefix="fikore-nfqueue-") as temporary:
        config = Path(temporary) / "nfqueue.ini"
        config.write_text(config_text(run_id))
        try:
            run("ip", "netns", "add", namespace)
            run("ip", "-n", namespace, "link", "set", "lo", "up")
            run(
                "ip", "netns", "exec", namespace,
                "iptables", "-I", "OUTPUT",
                "-p", "udp", "--dport", str(UDP_PORT),
                "-j", "NFQUEUE", "--queue-num", str(QUEUE_UL),
            )
            run(
                "ip", "netns", "exec", namespace,
                "iptables", "-I", "OUTPUT",
                "-p", "tcp", "--dport", str(TCP_PORT),
                "-j", "NFQUEUE", "--queue-num", str(QUEUE_UL),
            )
            server_code = (
                "from tests.nfqueue_namespace_test import "
                "echo_service; import threading; "
                "echo_service(threading.Event())"
            )
            server = subprocess.Popen(
                [
                    "ip", "netns", "exec", namespace,
                    sys.executable, "-c", server_code,
                ],
                cwd=ROOT,
                text=True,
            )
            process = subprocess.Popen(
                [
                    "ip", "netns", "exec", namespace,
                    str(ROOT / "bin/fikore"), str(config),
                ],
                cwd=ROOT,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            time.sleep(0.25)
            try:
                duplicate = subprocess.run(
                    [
                        "ip", "netns", "exec", namespace,
                        str(ROOT / "bin/fikore"), str(config),
                    ],
                    cwd=ROOT,
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    timeout=3,
                )
            except subprocess.TimeoutExpired as error:
                raise AssertionError(
                    "duplicate NFQUEUE bind did not fail fast"
                ) from error
            if duplicate.returncode == 0:
                raise AssertionError(
                    "duplicate NFQUEUE bind unexpectedly succeeded\n"
                    + duplicate.stdout[-4000:]
                )
            client_traffic(namespace)
            stdout, _ = process.communicate(timeout=10)
            if process.returncode != 0:
                raise AssertionError(stdout[-4000:])

            log = log_dir / "ue" / "ue_log_0.txt"
            final = log.read_text().splitlines()[-1]
            received = counter(final, "nfqrecvul")
            released = counter(final, "nfqrlsul")
            send_failures = counter(final, "nfqsfailul")
            assert received > 0
            assert received == released
            assert send_failures == 0
        finally:
            if process is not None and process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
            if server is not None and server.poll() is None:
                server.terminate()
                server.wait(timeout=5)
            subprocess.run(
                ["ip", "netns", "del", namespace],
                cwd=ROOT,
                check=False,
            )
            shutil.rmtree(log_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
