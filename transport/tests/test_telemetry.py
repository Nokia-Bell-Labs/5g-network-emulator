# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Network telemetry sampled from the link, beside the byte path."""
from types import SimpleNamespace

import pytest

from fikore_transport.backend import BackendConfig, TransportBackend
from fikore_transport.clock import Clock
from fikore_transport.fikore_link import FikoreLink
from fikore_transport.link import LoopbackConfig, LoopbackLink
from fikore_transport.telemetry import (LinkCounters, NetworkTelemetrySampler,
                                        TransportStats)

MSS = 1500


def build(queue_bytes=128 * 1024, ce_threshold_ttis=None, **kw):
    link = LoopbackLink(LoopbackConfig(rate_bps=20e6, owd_ttis=10, mss=MSS,
                                       queue_bytes=queue_bytes,
                                       ce_threshold_ttis=ce_threshold_ttis))
    return TransportBackend(link, BackendConfig(mss=MSS, telemetry_every_windows=None,
                                                **kw))


def sample(backend, sampler, steps):
    samples = []
    for _ in range(steps):
        backend.advance()
        samples.extend(sampler.poll())
    return samples


class FakeLink:
    mss = MSS

    def __init__(self):
        self.counters = {}

    def network_counters(self, direction="dl"):
        return dict(self.counters)


def test_samples_fall_on_multiples_of_the_interval():
    backend = build()
    sampler = backend.telemetry_sampler([0], interval_s=0.1)
    backend.submit_request(0, "x", 400 * 1024)
    samples = sample(backend, sampler, 51)
    assert [round(s.time_s, 3) for s in samples] == [0.1, 0.2, 0.3, 0.4, 0.5]
    assert sampler.poll() == [], "nothing is due between two multiples"


def test_forced_poll_closes_a_partial_final_interval_over_its_duration():
    link = FakeLink()
    clock = Clock()
    sampler = NetworkTelemetrySampler(link, clock, ue_ids=[0], interval_s=0.010)
    clock.tti = 10
    link.counters = {0: LinkCounters(delivered_bytes=1000)}
    first = sampler.poll()
    clock.tti = 15
    link.counters = {0: LinkCounters(delivered_bytes=1500)}
    assert sampler.poll() == []
    partial = sampler.poll(force=True)
    assert abs(first[0].fields.throughput_mbps - 0.8) < 1e-9
    assert abs(partial[0].fields.throughput_mbps - 0.8) < 1e-9
    assert partial[0].time_s == 0.015
    assert sampler.poll(force=True) == [], "an empty interval is not reported"


def test_throughput_tracks_the_bottleneck_rate():
    backend = build()
    sampler = backend.telemetry_sampler([0], interval_s=0.5)
    backend.submit_request(0, "big", 16 * 1024 * 1024)
    samples = sample(backend, sampler, 300)
    steady = [s.fields.throughput_mbps for s in samples[1:]]
    assert steady and all(17.0 <= t <= 20.5 for t in steady), steady
    assert all(s.fields.ip_latency_ms >= 10.0 for s in samples)
    assert all(s.fields.queue_bytes >= 0 for s in samples)


def test_queue_overflow_shows_as_drops_and_retransmissions():
    backend = build(queue_bytes=4 * MSS, rwnd=128 * 1024)
    sampler = backend.telemetry_sampler([0], interval_s=0.2)
    backend.submit_request(0, "lossy", 2 * 1024 * 1024)
    samples = sample(backend, sampler, 200)
    assert any(s.fields.drop_rate > 0 for s in samples)
    assert samples[-1].fields.retransmitted_bytes > 0


def test_ecn_marks_show_as_ce_rate():
    backend = build(queue_bytes=512 * 1024, ce_threshold_ttis=2, ecn="ect1")
    sampler = backend.telemetry_sampler([0], interval_s=0.2)
    backend.submit_request(0, "x", 4 * 1024 * 1024)
    samples = sample(backend, sampler, 100)
    assert any((s.fields.ce_rate or 0.0) > 0 for s in samples)


def test_rtt_comes_from_the_transport_not_the_link():
    backend = build()
    with_stats = backend.telemetry_sampler([0], interval_s=0.1)
    without = NetworkTelemetrySampler(backend.link, backend.runner.clock,
                                      ue_ids=[0], interval_s=0.1)
    backend.submit_request(0, "x", 400 * 1024)
    rtts, bare = [], []
    for _ in range(50):
        backend.advance()
        rtts.extend(s.fields.rtt_ms for s in with_stats.poll())
        bare.extend(without.poll())
    assert rtts and all(r is not None and 10.0 <= r <= 80.0 for r in rtts), rtts
    assert bare and all(s.fields.rtt_ms is None for s in bare)
    assert all(s.fields.throughput_mbps is not None for s in bare)


def test_only_the_requested_ues_are_sampled():
    backend = build()
    sampler = backend.telemetry_sampler([1, 5], interval_s=0.1)
    backend.submit_request(0, "a", 200 * 1024)
    backend.submit_request(1, "b", 200 * 1024)
    samples = sample(backend, sampler, 20)
    assert {s.ue_id for s in samples} == {1, 5}
    idle = [s for s in samples if s.ue_id == 5]
    assert all(s.fields.throughput_mbps == 0.0 and s.fields.rtt_ms is None
               for s in idle)
    assert any(s.fields.throughput_mbps > 0 for s in samples if s.ue_id == 1)


def test_a_link_without_counters_still_reports_transport_stats():
    clock = Clock()
    sampler = NetworkTelemetrySampler(
        SimpleNamespace(mss=MSS), clock, ue_ids=[0], interval_s=0.01,
        transport_stats=lambda ue: TransportStats(rtt_ms=12.0, retransmitted_bytes=0))
    clock.tti = 10
    (only,) = sampler.poll()
    assert only.fields.rtt_ms == 12.0
    assert only.fields.throughput_mbps is None


def test_fikore_state_maps_to_link_counters():
    state = {
        (0, "dl"): {"delivered_bytes_total": 3000, "dropped_bytes_total": 100,
                    "expired_bytes_total": 50, "ce_packets_total": 2,
                    "pending_bytes": 4500, "latency_s": 0.004, "sinr_db": 17.5},
        (0, "ul"): {"delivered_bytes_total": 99},
        (-1, "dl"): {},
    }
    counters = FikoreLink.network_counters(SimpleNamespace(last_state=state), "dl")
    assert set(counters) == {0, -1}
    assert counters[0] == LinkCounters(delivered_bytes=3000.0, lost_bytes=150.0,
                                       ce_packets=2.0, queue_bytes=4500,
                                       pdcp_latency_ms=4.0, sinr_db=17.5)
    assert counters[-1] == LinkCounters()


def test_sub_millisecond_interval_is_rejected():
    with pytest.raises(ValueError, match="1 ms"):
        NetworkTelemetrySampler(FakeLink(), Clock(), ue_ids=[0], interval_s=0.0001)
