# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""The request and delivery interface, checked against the loopback link."""
import os
import sys

from fikore_transport.backend import (BackendConfig, DownloadCancelled,
                                      DownloadCompleted, DownloadProgress,
                                      NetworkTelemetryReceived, TransportBackend,
                                      UeControl)
from fikore_transport.link import LoopbackConfig, LoopbackLink

MSS = 1500


def build(**kw):
    kw.setdefault("retain_request_history", True)
    kw.setdefault("retain_arrivals", True)
    link = LoopbackLink(LoopbackConfig(rate_bps=20e6, owd_ttis=10, mss=MSS,
                                       queue_bytes=128 * 1024))
    return TransportBackend(link, BackendConfig(mss=MSS, **kw))


def drain(backend, steps):
    events = []
    for _ in range(steps):
        events.extend(backend.advance().events)
    return events


def test_request_completes_with_monotonic_progress():
    backend = build()
    backend.submit_request(0, "seg-1", 300 * 1024)
    events = drain(backend, 300)

    progress = [e for e in events if isinstance(e, DownloadProgress)]
    completed = [e for e in events if isinstance(e, DownloadCompleted)]
    assert len(completed) == 1, "exactly one completion"
    assert [p.bytes_delivered for p in progress] == sorted(
        p.bytes_delivered for p in progress), "progress must be cumulative"
    assert progress[-1].bytes_delivered == 300 * 1024
    assert completed[0].time_s >= progress[-1].time_s
    assert all(e.time_s <= backend.runner.clock.s for e in events)


def test_time_starts_at_zero_and_advances_monotonically():
    backend = build(window_ttis=10)
    backend.submit_request(0, "a", 50 * 1024)
    times = [backend.advance().time_s for _ in range(5)]
    assert times == sorted(times)
    assert times[0] == 0.0
    assert times[1:] == [0.01, 0.02, 0.03, 0.04]


def test_final_window_is_clamped_and_emitted_once():
    class StateAwareLink(LoopbackLink):
        def __init__(self):
            super().__init__(LoopbackConfig(rate_bps=20e6, owd_ttis=10,
                                            mss=MSS, queue_bytes=128 * 1024))
            self.state_requests = 0

        def request_state_next_step(self):
            self.state_requests += 1

    link = StateAwareLink()
    backend = TransportBackend(link, BackendConfig(
        mss=MSS, window_ttis=10, horizon_ttis=15,
        retain_request_history=True, retain_arrivals=True))
    first = backend.advance()
    second = backend.advance()
    final = backend.advance()
    assert (first.time_s, second.time_s, final.time_s) == (0.0, 0.01, 0.015)
    assert not first.is_final and not second.is_final and final.is_final
    assert link.state_requests == 1
    try:
        backend.advance()
    except RuntimeError as exc:
        assert "final" in str(exc)
    else:
        raise AssertionError("backend advanced after its final NetworkStep")


def test_concurrent_requests_on_one_ue_share_the_link():
    backend = build()
    for i in range(3):
        backend.submit_request(0, f"seg-{i}", 120 * 1024)
    events = drain(backend, 400)
    completed = {e.request_id for e in events if isinstance(e, DownloadCompleted)}
    assert completed == {"seg-0", "seg-1", "seg-2"}


def test_cancellation_reports_the_drained_tail():
    backend = build()
    backend.submit_request(0, "big", 4 * 1024 * 1024)
    drain(backend, 20)
    backend.cancel_request(0, "big")
    events = drain(backend, 200)

    cancelled = [e for e in events if isinstance(e, DownloadCancelled)]
    assert len(cancelled) == 1
    request = backend.requests[(0, "big")]
    assert request.flow.in_network == 0, "the tail must have drained"
    assert 0 < cancelled[0].bytes_delivered < 4 * 1024 * 1024
    # Nothing more is emitted for a closed request.
    assert not [e for e in drain(backend, 20)
                if getattr(e, "request_id", None) == "big"]


def test_cancelling_an_unknown_or_finished_request_is_a_no_op():
    backend = build()
    backend.cancel_request(0, "nope")
    backend.submit_request(0, "x", 20 * 1024)
    drain(backend, 200)
    backend.cancel_request(0, "x")
    backend.cancel_request(0, "x")


def test_telemetry_carries_a_measured_round_trip():
    backend = build(telemetry_every_windows=5)
    backend.submit_request(0, "x", 400 * 1024)
    events = drain(backend, 60)
    telemetry = [e for e in events if isinstance(e, NetworkTelemetryReceived)]
    assert telemetry, "telemetry must be emitted"
    rtts = [t.fields.rtt_ms for t in telemetry if t.fields.rtt_ms]
    assert rtts and all(10.0 <= r <= 80.0 for r in rtts), rtts


def test_partial_final_window_uses_its_actual_telemetry_duration():
    backend = build()
    state = {(0, "dl"): {"delivered_bytes_total": 1000}}
    backend.runner.clock.tti = 10
    first = backend._telemetry_for(0, state, 0.010)
    state[(0, "dl")]["delivered_bytes_total"] = 1500
    backend.runner.clock.tti = 15
    partial = backend._telemetry_for(0, state, 0.015)
    assert abs(first.throughput_mbps - 0.8) < 1e-9
    assert abs(partial.throughput_mbps - 0.8) < 1e-9


def test_close_is_idempotent_and_control_is_accepted():
    backend = build()
    backend.set_ue_control(0, UeControl(priority=4.0, rmax_mbps=10.0))
    backend.close()
    backend.close()


def test_unknown_transport_is_rejected_not_silently_run_as_tcp():
    backend = build(transport="typo")
    try:
        backend.submit_request(0, "seg", 1000)
    except ValueError as exc:
        assert "unsupported transport" in str(exc)
    else:
        raise AssertionError("unknown transport silently selected TCP")


def test_persistent_connection_reuses_tcp_state_and_stream_offsets():
    backend = build()
    backend.submit_request(0, "first", 300 * 1024)
    first_events = drain(backend, 300)
    assert any(isinstance(event, DownloadCompleted)
               and event.request_id == "first" for event in first_events)

    first = backend.requests[(0, "first")]
    flow = first.flow
    flow_id = flow.sender.flow
    cwnd = flow.sender.cc.cwnd
    srtt = flow.sender.srtt_us
    assert srtt is not None
    assert flow_id in backend.runner.flows

    backend.submit_request(0, "second", 90 * 1024)
    second = backend.requests[(0, "second")]
    assert second.flow is flow
    assert second.stream_start == 300 * 1024
    assert flow.sender.cc.cwnd == cwnd
    assert flow.sender.srtt_us == srtt

    second_events = drain(backend, 200)
    progress = [event for event in second_events
                if isinstance(event, DownloadProgress)
                and event.request_id == "second"]
    assert progress
    assert progress[-1].bytes_delivered == 90 * 1024
    assert any(isinstance(event, DownloadCompleted)
               and event.request_id == "second" for event in second_events)
    assert flow.receiver.rcv_nxt == 390 * 1024
    assert set(backend.runner.flows) == {flow_id}


def test_persistent_pool_opens_for_concurrency_then_reuses_idle_flow():
    backend = build()
    backend.submit_request(0, "a", 120 * 1024)
    backend.submit_request(0, "b", 120 * 1024)
    first_ids = {
        backend.requests[(0, "a")].flow.sender.flow,
        backend.requests[(0, "b")].flow.sender.flow,
    }
    assert len(first_ids) == 2

    events = drain(backend, 400)
    completed = {event.request_id for event in events
                 if isinstance(event, DownloadCompleted)}
    assert completed == {"a", "b"}
    assert set(backend.runner.flows) == first_ids

    backend.submit_request(0, "c", 30 * 1024)
    assert backend.requests[(0, "c")].flow.sender.flow in first_ids
    assert set(backend.runner.flows) == first_ids


def test_persistent_pool_bounds_idle_connections_after_peak_concurrency():
    backend = build(max_idle_tcp_connections_per_ue=1)
    for name in ("a", "b", "c"):
        backend.submit_request(0, name, 120 * 1024)
    drain(backend, 400)
    assert len(backend.runner.flows) == 1
    assert len(backend._idle_connections[0]) == 1


def test_persistent_connection_can_be_reused_with_ack_tail_in_flight():
    link = LoopbackLink(LoopbackConfig(
        rate_bps=20e6, owd_ttis=50, mss=MSS, queue_bytes=128 * 1024))
    backend = TransportBackend(link, BackendConfig(
        mss=MSS, ack_over_link=True, retain_request_history=True,
        retain_arrivals=True))
    backend.submit_request(0, "first", 30 * 1024)
    for _ in range(100):
        events = backend.advance().events
        if any(isinstance(event, DownloadCompleted) for event in events):
            break
    else:
        raise AssertionError("first request did not complete")

    flow = backend.requests[(0, "first")].flow
    assert flow.acks_in_network > 0
    backend.submit_request(0, "second", 30 * 1024)
    assert backend.requests[(0, "second")].flow is flow
    events = drain(backend, 100)
    assert any(isinstance(event, DownloadCompleted)
               and event.request_id == "second" for event in events)


def test_fresh_mode_preserves_one_connection_per_object():
    backend = build(tcp_connection_mode="fresh")
    backend.submit_request(0, "first", 30 * 1024)
    first_flow = backend.requests[(0, "first")].flow.sender.flow
    drain(backend, 100)
    assert first_flow not in backend.runner.flows

    backend.submit_request(0, "second", 30 * 1024)
    second = backend.requests[(0, "second")]
    assert second.flow.sender.flow != first_flow
    assert second.stream_start == 0


def test_cancellation_retires_only_its_pooled_connection():
    backend = build()
    backend.submit_request(0, "cancel", 4 * 1024 * 1024)
    backend.submit_request(0, "keep", 300 * 1024)
    cancelled_flow = backend.requests[(0, "cancel")].flow.sender.flow
    kept_flow = backend.requests[(0, "keep")].flow.sender.flow
    drain(backend, 20)
    backend.cancel_request(0, "cancel")

    events = drain(backend, 400)
    assert any(isinstance(event, DownloadCancelled)
               and event.request_id == "cancel" for event in events)
    assert any(isinstance(event, DownloadCompleted)
               and event.request_id == "keep" for event in events)
    assert cancelled_flow not in backend.runner.flows
    assert kept_flow in backend.runner.flows

    backend.submit_request(0, "after", 30 * 1024)
    assert backend.requests[(0, "after")].flow.sender.flow == kept_flow


def test_cancellation_after_reuse_uses_object_local_progress():
    backend = build()
    backend.submit_request(0, "first", 30 * 1024)
    drain(backend, 100)
    first = backend.requests[(0, "first")]

    backend.submit_request(0, "cancel", 4 * 1024 * 1024)
    request = backend.requests[(0, "cancel")]
    assert request.flow is first.flow
    assert request.stream_start == 30 * 1024
    drain(backend, 20)
    backend.cancel_request(0, "cancel")
    events = drain(backend, 200)
    cancelled = [event for event in events
                 if isinstance(event, DownloadCancelled)
                 and event.request_id == "cancel"]
    assert len(cancelled) == 1
    assert 0 < cancelled[0].bytes_delivered < request.bytes_total
    assert request.flow.sender.flow not in backend.runner.flows


def test_idle_persistent_connection_remains_in_rtt_telemetry():
    backend = build()
    backend.submit_request(0, "one", 300 * 1024)
    drain(backend, 300)
    telemetry = backend._telemetry_for(0, None, backend.runner.clock.s)
    assert telemetry.rtt_ms is not None
    assert telemetry.retransmitted_bytes is not None


def test_retransmission_telemetry_survives_reuse_and_retirement():
    link = LoopbackLink(LoopbackConfig(
        rate_bps=20e6, owd_ttis=10, mss=MSS, queue_bytes=4 * MSS))
    backend = TransportBackend(link, BackendConfig(
        mss=MSS, rwnd=128 * 1024, retain_request_history=True,
        retain_arrivals=True))
    backend.submit_request(0, "lossy", 300 * 1024)
    events = drain(backend, 1000)
    assert any(isinstance(event, DownloadCompleted) for event in events)
    flow = backend.requests[(0, "lossy")].flow
    assert flow.sender.stats.retransmits > 0
    expected = flow.sender.stats.retransmits * MSS
    assert backend._telemetry_for(
        0, None, backend.runner.clock.s).retransmitted_bytes == expected

    backend.submit_request(0, "cancel", MSS)
    backend.cancel_request(0, "cancel")
    drain(backend, 2)
    assert flow.sender.flow not in backend.runner.flows
    assert backend._telemetry_for(
        0, None, backend.runner.clock.s).retransmitted_bytes == expected


def test_unknown_tcp_connection_mode_is_rejected():
    backend = build(tcp_connection_mode="typo")
    try:
        backend.submit_request(0, "seg", 1000)
    except ValueError as exc:
        assert "connection mode" in str(exc)
    else:
        raise AssertionError("unknown TCP connection mode was accepted")


def test_negative_idle_connection_limit_is_rejected():
    backend = build(max_idle_tcp_connections_per_ue=-1)
    try:
        backend.submit_request(0, "seg", 1000)
    except ValueError as exc:
        assert "idle TCP connections" in str(exc)
    else:
        raise AssertionError("negative idle connection limit was accepted")


def test_close_cleans_active_connections_callbacks_and_link_mappings():
    class TrackingLink(LoopbackLink):
        def __init__(self):
            super().__init__(LoopbackConfig(
                rate_bps=20e6, owd_ttis=10, mss=MSS,
                queue_bytes=128 * 1024))
            self.registered = set()

        def register_flow(self, flow, ue):
            self.registered.add((flow, ue))

        def unregister_flow(self, flow):
            self.registered = {
                item for item in self.registered if item[0] != flow
            }

    link = TrackingLink()
    backend = TransportBackend(link, BackendConfig(
        mss=MSS, ack_over_link=True, retain_request_history=False))
    backend.submit_request(0, "idle", 30 * 1024)
    drain(backend, 100)
    backend.submit_request(0, "active", 4 * 1024 * 1024)
    drain(backend, 2)
    assert backend._active_requests
    assert backend.runner.sched.next_tti() is not None
    assert link.registered

    backend.close()
    assert not backend.runner.flows
    assert not backend._connections
    assert not backend._active_requests
    assert not backend.requests
    assert not link.registered
    assert backend.runner.sched.next_tti() is None


def test_completed_objects_do_not_remain_in_the_per_tti_flow_registry():
    backend = build(tcp_connection_mode="fresh")
    for index in range(100):
        backend.submit_request(0, f"seg-{index}", 30 * 1024)
        for _ in range(100):
            events = backend.advance().events
            if any(isinstance(event, DownloadCompleted) for event in events):
                break
        else:
            raise AssertionError(f"seg-{index} did not complete")
        assert not backend.runner.flows
        assert not backend._active_requests
    # History remains available to callers without being visited on every slot.
    assert len(backend.requests) == 100


def test_default_backend_drops_heavy_history_but_never_reuses_an_id():
    link = LoopbackLink(LoopbackConfig(rate_bps=20e6, owd_ttis=10, mss=MSS,
                                       queue_bytes=128 * 1024))
    backend = TransportBackend(link, BackendConfig(mss=MSS))
    backend.submit_request(0, "seg", 30 * 1024)
    for _ in range(100):
        if any(isinstance(event, DownloadCompleted)
               for event in backend.advance().events):
            break
    assert not backend.requests
    assert not backend.runner.arrivals
    try:
        backend.submit_request(0, "seg", 1000)
    except ValueError:
        pass
    else:
        raise AssertionError("a completed request id was reused")


def test_ack_over_link_flow_retires_after_its_ack_tail():
    backend = build(ack_over_link=True, tcp_connection_mode="fresh")
    backend.submit_request(0, "seg", 30 * 1024)
    for _ in range(200):
        backend.advance()
        if backend.requests[(0, "seg")].closed and not backend.runner.flows:
            break
    assert backend.requests[(0, "seg")].closed
    assert not backend.runner.flows
    assert not backend._retiring_flows


def test_cancelled_ack_over_link_flow_keeps_mapping_until_ack_tail_drains():
    backend = build(ack_over_link=True)
    backend.submit_request(0, "seg", 300 * 1024)
    backend.advance()
    backend.advance()
    backend.cancel_request(0, "seg")
    cancelled = False
    for _ in range(200):
        events = backend.advance().events
        cancelled = cancelled or any(
            isinstance(event, DownloadCancelled) for event in events)
        if cancelled and not backend.runner.flows:
            break
    assert cancelled
    assert not backend.runner.flows
    assert not backend._retiring_flows


if __name__ == "__main__":
    failed = 0
    for name, fn in sorted(list(globals().items())):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print(f"ok   {name}")
            except AssertionError as exc:
                failed += 1
                print(f"FAIL {name}: {exc}")
    sys.exit(1 if failed else 0)
