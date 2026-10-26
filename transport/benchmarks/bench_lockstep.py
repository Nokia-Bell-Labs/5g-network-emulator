#!/usr/bin/env python3
# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Measure the wall-clock cost of driving FikoRE one TTI at a time.

A Python TCP model has to react to each delivery within the same millisecond it
learns about it, so the question is whether a 1 TTI barrier window is affordable.
Four variants isolate where the time goes.
"""
import json
import os
import re
import socket
import subprocess
import time
from pathlib import Path

EMU = os.environ.get("FIKORE_DIR", str(Path(__file__).resolve().parents[2]))
PROTO = "fikore-control-1"
# Out of the way of the per-slot ids, which start at 1 and would otherwise reach it.
PRELOAD_ID = 10 ** 12


def make_ini(path, sock, duration_s, n_ues=1):
    src = open(os.path.join(EMU, "config/control_demo.ini")).read()
    subs = [
        (r"^duration: .*$", f"duration: {duration_s}"),
        (r"^period: .*$", "period: -1"),
        (r"^n_ues: .*$", f"n_ues: {n_ues}"),
        (r"^dl_target: .*$", "dl_target: 0.0"),
        (r"^ul_target: .*$", "ul_target: 0.0"),
        (r"^sync_mode: .*$", "sync_mode: barrier"),
        (r"^address: .*$", f"address: {sock}"),
        (r"^on_timeout: .*$", "on_timeout: abort"),
        (r"^progress_log_period_s: .*$", "progress_log_period_s: 0"),
    ]
    out = src
    for pat, rep in subs:
        out = re.sub(pat, rep, out, flags=re.M)
    # Monitoring off: its own UDP flush would pollute the measurement.
    out = out.replace("[Monitoring]\nenabled: true", "[Monitoring]\nenabled: false")
    open(path, "w").write(out)


class Session:
    def __init__(self, sock_path, duration_s, n_ues=1):
        self.ini = "/tmp/fikore-transport-bench/bench.ini"
        make_ini(self.ini, sock_path, duration_s, n_ues)
        if os.path.exists(sock_path):
            os.unlink(sock_path)
        self.log = open("/tmp/fikore-transport-bench/emu.log", "w")
        self.proc = subprocess.Popen([os.path.join(EMU, "bin/fikore"), self.ini],
                                     cwd=EMU, stdout=self.log, stderr=subprocess.STDOUT)
        for _ in range(500):
            if os.path.exists(sock_path):
                break
            time.sleep(0.01)
        else:
            raise RuntimeError("socket never appeared; see /tmp/fikore-transport-bench/emu.log")
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(sock_path)
        self.f = self.s.makefile("rwb")
        hello = json.loads(self.f.readline())
        assert hello.get("proto") == PROTO, hello
        self.send({"proto": PROTO})
        self.next_id = 1
        self.reply_bytes = 0

    def send(self, msg):
        self.f.write((json.dumps(msg) + "\n").encode())
        self.f.flush()

    def read_ack(self):
        line = self.f.readline()
        self.reply_bytes += len(line)
        return json.loads(line)

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass
        self.proc.terminate()
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()


def run(variant, duration_s=2.0, window=1, live_tags=1, read_state=True,
        n_ues=1, events=False):
    ttis = int(duration_s * 1000)
    sock = "/tmp/fikore-transport-bench/bench.sock"
    ses = Session(sock, duration_s, n_ues)

    # Preload live tags so that the objects map in every reply has a realistic size.
    preloaded = 0
    if live_tags > 1:
        cmds = [{"op": "inject", "target": f"ue/{u}", "tag": t, "dl.bytes": 1500}
                for u in range(n_ues) for t in range(2, live_tags + 1)]
        preloaded = len(cmds)
        ses.send({"id": PRELOAD_ID, "at_tti": 0, "cmds": cmds})

    t0 = time.perf_counter()
    tti = 0
    seq = 1
    cursor = 0
    while tti < ttis - window:
        last = tti + window - 1
        cmds = [{"op": "inject", "target": "ue/0", "tag": 1, "dl.bytes": 1500}]
        if events:
            cmds.append({"op": "events", "after": cursor})
        elif read_state:
            cmds.append({"op": "get", "target": "ue/*"})
        a_id, b_id = seq, seq + 1
        seq += 2
        ses.send({"id": a_id, "at_tti": last, "cmds": cmds})
        ses.send({"id": b_id, "op": "grant", "until_tti": last})
        # One acknowledgement per command, all carrying the message's id, so the
        # batch is only complete after len(cmds) of them. Stopping at the first
        # would leave the rest in the socket and measure the wrong thing: the
        # backlog would be paid for by a later slot, and the reply that actually
        # costs something, the state, would never be read at all.
        # The preload is applied on the first granted TTI, so its acknowledgements
        # are owed to the first round trip and to no other.
        owed = {a_id: len(cmds), b_id: 1, PRELOAD_ID: preloaded}
        preloaded = 0
        while sum(owed.values()):
            ack = ses.read_ack()
            assert owed.get(ack["id"], 0) > 0, ack
            assert ack["status"] == "ok", ack
            owed[ack["id"]] -= 1
            if events and ack["id"] == a_id and "result" in ack:
                cursor = int(ack["result"]["cursor"])
        tti += window
    wall = time.perf_counter() - t0

    rt = (ttis - window) // window
    per = wall / max(rt, 1) * 1e6
    print(f"{variant:<28} {wall:7.2f} s wall  {rt:6d} round trips  "
          f"{per:7.1f} us/trip  -> 300 s run: {wall / duration_s * 300:7.1f} s  "
          f"{ses.reply_bytes/1e6:6.2f} MB", flush=True)
    ses.close()
    return wall


if __name__ == "__main__":
    os.makedirs("/tmp/fikore-transport-bench", exist_ok=True)
    print("-- 1 UE")
    run("1 TTI, grant only", read_state=False)
    run("1 TTI, grant+get, 1 tag")
    run("1 TTI, grant+get, 100 tags", live_tags=100)
    run("1 TTI, events, 100 tags", live_tags=100, events=True)
    run("10 TTI window, grant+get", window=10)
    print("-- 4 UEs")
    run("1 TTI, grant only", read_state=False, n_ues=4)
    run("1 TTI, get, 1 tag/UE", n_ues=4)
    run("1 TTI, get, 20 tags/UE", live_tags=20, n_ues=4)
    run("1 TTI, get, 60 tags/UE", live_tags=60, n_ues=4)
    run("1 TTI, events, 60 tags/UE", live_tags=60, n_ues=4, events=True)
    run("10 TTI window, get, 20 tags/UE", window=10, live_tags=20, n_ues=4)
