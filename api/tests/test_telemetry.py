# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear

from fikore_api.telemetry import TelemetryCache, parse_line


def test_parse_influx_line():
    parsed = parse_line('ue_pdcp,ue_id=3,tx_dir=dl throughput_mbps_mean=12.500000 1700000000000000000')
    assert parsed is not None
    measurement, tags, fields, ts = parsed
    assert measurement == "ue_pdcp"
    assert tags == {"ue_id": "3", "tx_dir": "dl"}
    assert fields["throughput_mbps_mean"] == 12.5
    assert ts == 1700000000000000000


def test_parse_rejects_garbage():
    assert parse_line("") is None
    assert parse_line("no_fields") is None


def test_cache_indexes_by_ue():
    cache = TelemetryCache()
    cache.update("ue_pdcp", {"ue_id": "1", "tx_dir": "dl"}, {"throughput_mbps_mean": 5.0}, None)
    cache.update("ue_pdcp", {"ue_id": "2", "tx_dir": "dl"}, {"throughput_mbps_mean": 7.0}, None)
    assert cache.ue_ids() == [1, 2]
    assert len(cache.for_ue(1)) == 1
    assert not cache.for_ue(9)
