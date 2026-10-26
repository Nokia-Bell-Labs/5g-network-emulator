# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""The integration, exercised against a real FikoRE process.

The loopback tests say whether the transport state machine is right. These say
whether the link that drives the emulator is right, which is a different question
and mostly a question about the control protocol: framing, lockstep, tag lifetime,
and whether a byte handed over is still accounted for at the end.

They need `bin/fikore`. Where there is no emulator they skip rather than fail, so
the loopback suite stays the one that has to pass everywhere.
"""
import os
import json
import shutil
import sys
import tempfile
import time
from pathlib import Path

from fikore_transport.backend import (BackendConfig, DownloadCompleted,
                                      TransportBackend)
from fikore_transport.cc import Cubic, Reno
from fikore_transport.emulator import Emulator, EmulatorConfig
from fikore_transport.fikore_link import FikoreLink, _Outstanding
from fikore_transport.link import Transmit

EMU = os.environ.get("FIKORE_DIR", str(Path(__file__).resolve().parents[2]))
BINARY = os.path.join(EMU, "bin/fikore")
BASE_INI = os.path.join(EMU, "config/control_demo.ini")


class Skipped(Exception):
    """Not a failure. There is no emulator on this machine."""


def build_link(n_ues=1, duration_s=6.0, delay_budget_s=0.3,
               ue_id_map=None, ue_target_map=None, **kw):
    if not os.path.exists(BINARY) or not os.path.exists(BASE_INI):
        raise Skipped(f"no emulator at {BINARY}; set FIKORE_DIR to run these")
    work = tempfile.mkdtemp(prefix="fikore-test-")
    cfg = EmulatorConfig(binary=BINARY, base_ini=BASE_INI,
                         socket_path=os.path.join(work, "control.sock"),
                         duration_s=duration_s, work_dir=EMU, n_ues=n_ues,
                         delay_budget_s=delay_budget_s,
                         log_path=os.path.join(work, "emulator.log"), **kw)
    link = FikoreLink(Emulator(cfg), flow_to_ue={}, ue_id_map=ue_id_map,
                      ue_target_map=ue_target_map)
    link._work_dir = work
    return link


def teardown(link, backend=None):
    (backend or link).close()
    shutil.rmtree(getattr(link, "_work_dir", ""), ignore_errors=True)


def run_backend(link, requests, max_windows, **kw):
    """Submit one request per (ue, id, size) and advance until all complete."""
    kw.setdefault("rwnd", 256 * 1024)
    kw.setdefault("retain_request_history", True)
    kw.setdefault("retain_arrivals", True)
    cc_factory = kw.pop("cc_factory", Cubic)
    backend = TransportBackend(link, BackendConfig(window_ttis=10, cc_factory=cc_factory,
                                                   **kw))
    for ue, name, size in requests:
        backend.submit_request(ue, name, size)
    done = {}
    for _ in range(max_windows):
        for event in backend.advance().events:
            if isinstance(event, DownloadCompleted):
                done[event.request_id] = event.time_s
        if len(done) == len(requests):
            break
    return backend, done


def accounted(link):
    return sum(link.terminal_bytes.values()) + link.in_flight_bytes


def command_only_link():
    """A wire-free link for deterministic command-queue unit tests."""
    link = object.__new__(FikoreLink)
    link._last_tti = -1
    link._scheduled_sets = {}
    link._queued_sets = []
    link._pending = {}
    link.flow_to_ue = {}
    link.flow_to_target = {}
    link._next_tag = 1
    link._outstanding = {}
    link.submitted_bytes = 0
    link._to_forget = []
    link.use_events = False
    return link


def test_scheduled_sets_are_exact_and_fifo_before_immediate_commands():
    link = command_only_link()
    link.schedule_set(3, "ue/study_0", {"priority": 2.0})
    link.schedule_set(3, "ue/study_0", {"dl.sinr_offset_db": -6.0})
    link.schedule_set(5, "ue/study_0", {"priority": 1.0})
    link._queued_sets.append(
        {"target": "ue/study_0", "set": {"dl.rmax_mbps": 25.0}}
    )

    before = link._commands_for(2)
    assert before[0]["set"] == {"dl.rmax_mbps": 25.0}
    assert before[-1] == {"op": "get", "target": "ue/*"}

    due = link._commands_for(3)
    assert [command["set"] for command in due[:-1]] == [
        {"priority": 2.0},
        {"dl.sinr_offset_db": -6.0},
    ]
    assert 5 in link._scheduled_sets


def test_scheduled_sets_reject_invalid_late_and_skipped_ttis():
    link = command_only_link()
    for bad_tti in (-1, 1.5, True):
        try:
            link.schedule_set(bad_tti, "ue/0", {"priority": 1.0})
            assert False, f"invalid TTI {bad_tti!r} was accepted"
        except ValueError:
            pass
    link._last_tti = 4
    try:
        link.schedule_set(4, "ue/0", {"priority": 1.0})
        assert False, "late scheduled set was accepted"
    except ValueError:
        pass

    link._last_tti = -1
    link.schedule_set(2, "ue/0", {"priority": 1.0})
    try:
        link._commands_for(3)
        assert False, "a skipped scheduled set was silently applied late"
    except RuntimeError as exc:
        assert "TTI 2 was skipped" in str(exc)


def test_a_real_transfer_completes_and_conserves_bytes():
    size = 400 * 1024
    link = build_link()
    try:
        backend, done = run_backend(link, [(0, "seg", size)], 600)
        receiver = backend.requests[(0, "seg")].flow.receiver
        assert done, "the transfer never completed"
        assert receiver.rcv_nxt == size, f"{receiver.rcv_nxt} of {size} arrived"
        # Every byte handed to the link is delivered, lost, or still in the
        # emulator. Nothing may simply stop being accounted for.
        assert link.submitted_bytes >= size
        assert accounted(link) == link.submitted_bytes, (
            f"{accounted(link)} accounted of {link.submitted_bytes} submitted")
        assert link.terminal_bytes["delivered"] >= size
        assert link.round_trips > 0
    finally:
        teardown(link, backend)


def test_two_objects_reuse_one_real_fikore_flow_and_conserve_bytes():
    first_size = 150 * 1024
    second_size = 90 * 1024
    link = build_link(duration_s=8.0)
    backend = TransportBackend(link, BackendConfig(
        window_ttis=10, rwnd=256 * 1024, cc_factory=Cubic,
        retain_request_history=True, retain_arrivals=True))
    try:
        backend.submit_request(0, "first", first_size)
        first_done = False
        for _ in range(400):
            events = backend.advance().events
            if any(isinstance(event, DownloadCompleted)
                   and event.request_id == "first" for event in events):
                first_done = True
                break
        assert first_done, "the first object never completed"

        first = backend.requests[(0, "first")]
        flow_id = first.flow.sender.flow
        cwnd = first.flow.sender.cc.cwnd
        srtt = first.flow.sender.srtt_us
        backend.submit_request(0, "second", second_size)
        second = backend.requests[(0, "second")]
        assert second.flow is first.flow
        assert second.stream_start == first_size
        assert second.flow.sender.cc.cwnd == cwnd
        assert second.flow.sender.srtt_us == srtt

        second_done = False
        for _ in range(400):
            events = backend.advance().events
            if any(isinstance(event, DownloadCompleted)
                   and event.request_id == "second" for event in events):
                second_done = True
                break
        assert second_done, "the second object never completed"
        assert second.flow.receiver.rcv_nxt == first_size + second_size
        assert set(link.flow_to_ue) == {flow_id}
        assert accounted(link) == link.submitted_bytes
    finally:
        teardown(link, backend)


def test_the_emulator_and_the_link_agree_on_the_bytes():
    """The link's account is its own bookkeeping; this is the emulator's."""
    link = build_link()
    try:
        backend, _ = run_backend(link, [(0, "seg", 300 * 1024)], 600)
        state = link.last_state[(0, "dl")]
        injected = state["injected_bytes_total"]
        assert abs(injected - link.submitted_bytes) < 1.0, (
            f"emulator took {injected}, the link sent {link.submitted_bytes}")
        settled = (state["delivered_bytes_total"] + state["dropped_bytes_total"]
                   + state["expired_bytes_total"] + state["pending_bytes"])
        assert settled <= injected + 1.0, f"{settled} settled of {injected}"
        # The drops are broken down, and the breakdown adds up to the sum.
        assert abs(state["dropped_bytes_total"]
                   - state["queue_dropped_bytes_total"]
                   - state["radio_dropped_bytes_total"]) < 1.0
    finally:
        teardown(link, backend)


def test_arrivals_come_back_on_the_slots_that_injected():
    """The regression test for the control framing.

    The emulator answers one acknowledgement per command, all carrying the
    message's id. A client that reads one per id closes the batch on the first
    inject's reply, which has no result, so a slot that injected anything reported
    no arrivals at all and the run learned everything one idle slot late.
    """
    link = build_link()
    link.register_flow(1, 0)
    try:
        slots = 150
        slots_with_arrivals = 0
        for tti in range(slots):
            link.submit([Transmit(flow=1, seq=tti, size=1500)], at_tti=tti + 1)
            if link.step(tti):
                slots_with_arrivals += 1
        # One segment goes in on every slot but the first and comes back a few
        # slots later, so almost every slot must report something.
        assert slots_with_arrivals > 100, (
            f"only {slots_with_arrivals} of {slots} slots reported an arrival "
            f"while injecting on every one of them")
        assert link.terminal_bytes["delivered"] > 100 * 1500
    finally:
        teardown(link)


def test_scheduled_sets_use_the_normal_acknowledged_control_path():
    link = build_link(duration_s=0.02)
    try:
        link.schedule_set(2, "ue/0", {"priority": 2.0})
        link.schedule_set(4, "ue/0", {"not.a.real.parameter": 1.0})
        for tti in range(4):
            link.step(tti)
        try:
            link.step(4)
            assert False, "a rejected scheduled set did not fail the run"
        except RuntimeError as exc:
            assert "not.a.real.parameter" in str(exc)
    finally:
        teardown(link)


def test_two_ues_share_the_cell_and_both_make_progress():
    link = build_link(n_ues=2, duration_s=8.0)
    try:
        backend = TransportBackend(link, BackendConfig(window_ttis=10,
                                                       rwnd=256 * 1024,
                                                       cc_factory=Cubic,
                                                       retain_request_history=True))
        backend.submit_request(0, "a", 300 * 1024)
        backend.submit_request(1, "b", 300 * 1024)
        flows = [backend.requests[(0, "a")].flow, backend.requests[(1, "b")].flow]

        done, last, together = {}, [0, 0], 0
        for _ in range(700):
            for event in backend.advance().events:
                if isinstance(event, DownloadCompleted):
                    done[event.request_id] = event.time_s
            now = [f.receiver.rcv_nxt for f in flows]
            if now[0] > last[0] and now[1] > last[1]:
                together += 1
            last = now
            if len(done) == 2:
                break

        assert set(done) == {"a", "b"}, f"only {sorted(done)} finished"
        assert together >= 2, (
            f"the two never advanced in the same window ({together}), so one was "
            f"waiting for the other rather than sharing the cell")
        for ue in (0, 1):
            assert link.last_state[(ue, "dl")]["delivered_bytes_total"] > 0
        assert accounted(link) == link.submitted_bytes
    finally:
        teardown(link, backend)


def test_sparse_external_ue_ids_map_to_dense_emulator_ids():
    link = build_link(n_ues=2, ue_id_map={1: 0, 3: 1},
                      ue_target_map={1: "controlDemo_0", 3: "controlDemo_1"})
    try:
        backend, done = run_backend(
            link, [(1, "one", 100 * 1024), (3, "three", 100 * 1024)], 300)
        assert set(done) == {"one", "three"}
        assert (1, "dl") in link.last_state
        assert (3, "dl") in link.last_state
        assert (0, "dl") not in link.last_state
        assert accounted(link) == link.submitted_bytes
    finally:
        teardown(link, backend)


def test_loss_is_reported_with_a_cause_and_the_transfer_still_completes():
    """A delay budget far below the standing queue makes the emulator discard,
    which is the only path on which the fate and the cause are decided."""
    link = build_link(duration_s=12.0, delay_budget_s=0.02)
    try:
        backend, done = run_backend(link, [(0, "bulk", 3 * 1024 * 1024)], 1100,
                                    rwnd=256 * 1024, cc_factory=Reno)
        lost = [a for a in backend.runner.arrivals if a.fate != "delivered"]
        assert lost, "the delay budget did not manage to lose anything"
        assert all(a.cause for a in lost), "a loss with no cause"
        assert all(a.cause in ("queue", "radio", "budget") for a in lost)
        assert sum(link.lost_bytes_by_cause.values()) == sum(a.size for a in lost)
        assert backend.requests[(0, "bulk")].flow.sender.stats.retransmits > 0
        assert done, "the transfer did not survive the losses"
        assert accounted(link) == link.submitted_bytes
    finally:
        teardown(link, backend)

def test_event_cursor_replays_until_the_client_acknowledges_it():
    """`after` means consumed, so losing one reply cannot lose its events."""
    if not os.path.exists(BINARY) or not os.path.exists(BASE_INI):
        raise Skipped(f"no emulator at {BINARY}; set FIKORE_DIR to run these")
    work = tempfile.mkdtemp(prefix="fikore-events-")
    emulator = Emulator(EmulatorConfig(
        binary=BINARY, base_ini=BASE_INI,
        socket_path=os.path.join(work, "control.sock"),
        duration_s=2.0, work_dir=EMU, n_ues=1, delay_budget_s=30.0,
        log_path=os.path.join(work, "emulator.log")))
    sock = emulator.connect()
    io = sock.makefile("rwb")

    def send(message):
        io.write((json.dumps(message) + "\n").encode())
        io.flush()

    def slot(tti, commands, message_id):
        send({"id": message_id, "at_tti": tti, "cmds": commands})
        send({"id": message_id + 1, "op": "grant", "until_tti": tti})
        replies = []
        for _ in range(len(commands) + 1):
            replies.append(json.loads(io.readline()))
        result = [r["result"] for r in replies
                  if r["id"] == message_id and "result" in r]
        assert len(result) == 1, replies
        return result[0]

    try:
        hello = json.loads(io.readline())
        assert hello["proto"] == "fikore-control-1"
        send({"proto": "fikore-control-1"})

        # The first call starts the subscription; history before it is intentionally
        # absent. Inject after arming it.
        assert slot(0, [{"op": "events", "after": 0}], 100)["events"] == []
        slot(1, [{"op": "inject", "target": "ue/0", "tag": 77,
                  "dl.bytes": 1500}, {"op": "events", "after": 0}], 102)

        first = None
        tti = 2
        while tti < 30 and first is None:
            reply = slot(tti, [{"op": "events", "after": 0}], 100 + tti * 2)
            if reply["events"]:
                first = reply
            tti += 1
        assert first is not None, "the injected segment produced no event"

        # Same cursor, byte-identical logical event. Only after returning the cursor
        # does the server discard it.
        replay = slot(tti, [{"op": "events", "after": 0}], 100 + tti * 2)
        tti += 1
        assert replay["cursor"] == first["cursor"]
        assert replay["events"] == first["events"]

        drained = slot(tti, [{"op": "events", "after": first["cursor"]}],
                       100 + tti * 2)
        tti += 1
        assert drained["cursor"] == first["cursor"]
        assert drained["events"] == []

        # Going backwards after acknowledging a cursor is rejected rather than
        # silently returning an incomplete replay.
        send({"id": 999, "at_tti": tti,
              "cmds": [{"op": "events", "after": 0}]})
        send({"id": 1000, "op": "grant", "until_tti": tti})
        replies = [json.loads(io.readline()), json.loads(io.readline())]
        event_ack = next(r for r in replies if r["id"] == 999)
        assert event_ack["status"] == "error"
        assert event_ack["errors"][0]["key"] == "after"
    finally:
        io.close()
        sock.close()
        emulator.close()
        shutil.rmtree(work, ignore_errors=True)


def test_event_sequence_gap_is_rejected_before_accounting():
    link = build_link()
    try:
        bad = {"cursor": 2, "events": [{
            "seq": 2, "target": "ue/0", "dir": "dl", "tag": 1,
            "delivered_bytes": 1500,
        }]}
        try:
            link._arrivals_from_events(bad, 0)
        except RuntimeError as exc:
            assert "sequence gap" in str(exc)
        else:
            raise AssertionError("missing event sequence was acknowledged")
        assert link._event_cursor == 0
        assert link.event_accounted_bytes == 0
    finally:
        teardown(link)


def test_bad_later_event_does_not_partially_commit_the_batch():
    link = build_link()
    try:
        link._outstanding[1] = _Outstanding(1, 0, 0, 1500, "dl", 0, "data", "0")
        link._outstanding[2] = _Outstanding(2, 0, 0, 1500, "dl", 0, "data", "0")
        bad = {"cursor": 2, "events": [
            {"seq": 1, "target": "ue/0", "dir": "dl", "tag": 1,
             "delivered_bytes": 1500},
            {"seq": 2, "target": "ue/0", "dir": "dl", "tag": 2,
             "delivered_bytes": -1},
        ]}
        try:
            link._arrivals_from_events(bad, 0)
        except RuntimeError as exc:
            assert "invalid delivered_bytes" in str(exc)
        else:
            raise AssertionError("bad event batch was accepted")
        assert link._event_cursor == 0
        assert link.event_accounted_bytes == 0
        assert set(link._outstanding) == {1, 2}
        assert not link._event_counters
    finally:
        teardown(link)


def test_bad_state_does_not_commit_valid_event_counters():
    link = build_link()
    try:
        link._outstanding[1] = _Outstanding(1, 0, 0, 1500, "dl", 0, "data", "0")
        bad = {
            "cursor": 1,
            "events": [{"seq": 1, "target": "ue/0", "dir": "dl",
                        "tag": 1, "delivered_bytes": 500}],
            "state": [{"target": "ue/0",
                       "dl": {"ce_packets_total": "not-an-integer"}}],
        }
        try:
            link._arrivals_from_events(bad, 0)
        except (RuntimeError, ValueError):
            pass
        else:
            raise AssertionError("bad compact state was accepted")
        assert link._event_cursor == 0
        assert link.event_accounted_bytes == 0
        assert not link._event_counters
        assert 1 in link._outstanding
    finally:
        teardown(link)


def test_async_event_overflow_requires_an_atomic_resync():
    if not os.path.exists(BINARY) or not os.path.exists(BASE_INI):
        raise Skipped(f"no emulator at {BINARY}; set FIKORE_DIR to run these")
    work = tempfile.mkdtemp(prefix="fikore-gap-")
    emulator = Emulator(EmulatorConfig(
        binary=BINARY, base_ini=BASE_INI,
        socket_path=os.path.join(work, "control.sock"),
        duration_s=3.0, work_dir=EMU, n_ues=1,
        log_path=os.path.join(work, "emulator.log"), max_object_events=2,
        extra={"period": "1", "sync_mode": "async"}))
    sock = emulator.connect()
    io = sock.makefile("rwb")

    def send(message):
        io.write((json.dumps(message) + "\n").encode())
        io.flush()

    try:
        assert json.loads(io.readline())["proto"] == "fikore-control-1"
        send({"proto": "fikore-control-1"})
        send({"id": 1, "op": "events", "after": 0})
        assert json.loads(io.readline())["status"] == "ok"

        send({"id": 2, "cmds": [
            {"op": "inject", "target": "ue/0", "tag": tag, "dl.bytes": 1500}
            for tag in (1, 2, 3)]})
        assert all(json.loads(io.readline())["status"] == "ok" for _ in range(3))
        time.sleep(0.2)

        send({"id": 3, "op": "events", "after": 0})
        gap = json.loads(io.readline())
        assert gap["status"] == "error"
        assert "resync" in gap["errors"][0]["reason"]

        # Snapshot and re-arm happen at one quiescent point. A separate get followed by
        # a reset would leave one unobserved slot between them in async mode.
        send({"id": 4, "op": "events", "after": 0, "resync": True})
        resync = json.loads(io.readline())
        assert resync["status"] == "ok"
        assert len(resync["result"]["snapshot"]) == 1
        assert resync["result"]["events"] == []
    finally:
        io.close()
        sock.close()
        emulator.close()
        shutil.rmtree(work, ignore_errors=True)


def test_barrier_event_overflow_aborts_before_feedback_is_lost():
    if not os.path.exists(BINARY) or not os.path.exists(BASE_INI):
        raise Skipped(f"no emulator at {BINARY}; set FIKORE_DIR to run these")
    work = tempfile.mkdtemp(prefix="fikore-gap-barrier-")
    log_path = os.path.join(work, "emulator.log")
    emulator = Emulator(EmulatorConfig(
        binary=BINARY, base_ini=BASE_INI,
        socket_path=os.path.join(work, "control.sock"),
        duration_s=3.0, work_dir=EMU, n_ues=1, log_path=log_path,
        max_object_events=2))
    sock = emulator.connect()
    io = sock.makefile("rwb")

    def send(message):
        io.write((json.dumps(message) + "\n").encode())
        io.flush()

    try:
        assert json.loads(io.readline())["proto"] == "fikore-control-1"
        send({"proto": "fikore-control-1"})
        send({"id": 1, "at_tti": 0, "cmds": [{"op": "events", "after": 0}]})
        send({"id": 2, "op": "grant", "until_tti": 0})
        assert all(json.loads(io.readline())["status"] == "ok" for _ in range(2))

        send({"id": 3, "at_tti": 1, "cmds": [
            {"op": "inject", "target": "ue/0", "tag": tag, "dl.bytes": 1500}
            for tag in (1, 2, 3)]})
        send({"id": 4, "op": "grant", "until_tti": 20})
        assert all(json.loads(io.readline())["status"] == "ok" for _ in range(4))

        for _ in range(100):
            if emulator.proc.poll() is not None:
                break
            time.sleep(0.02)
        assert emulator.proc.poll() == 76, (
            f"event-backlog abort returned {emulator.proc.poll()}, expected EX_PROTOCOL")
        assert "aborting the lockstep run before feedback is lost" in open(log_path).read()
    finally:
        io.close()
        sock.close()
        emulator.close()
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    failed = 0
    for name, fn in sorted(list(globals().items())):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"ok   {name}")
            except Skipped as exc:
                print(f"skip {name}: {exc}")
            except AssertionError as exc:
                failed += 1
                print(f"FAIL {name}: {exc}")
    sys.exit(1 if failed else 0)
