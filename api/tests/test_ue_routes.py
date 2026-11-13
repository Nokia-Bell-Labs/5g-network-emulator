# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear

"""UE routes accept textual control ids as well as numeric indices."""

from __future__ import annotations

from typing import Any, Dict
from unittest.mock import MagicMock

import pytest
from fastapi.testclient import TestClient


@pytest.fixture
def mocked_client(monkeypatch):
    knobs: Dict[str, Dict[str, Any]] = {
        "0": {"target": "ue/0", "ue_id": "controlDemo_0", "priority": 1.0, "state": {"dl": {}}},
        "3": {
            "target": "ue/3",
            "ue_id": "imsi_001010000000001",
            "priority": 1.0,
            "state": {"dl": {}},
        },
    }

    def resolve_key(target: str) -> str:
        if target == "ue/0":
            return "0"
        if target == "ue/controlDemo_0":
            return "0"
        if target == "ue/imsi_001010000000001":
            return "3"
        if target == "ue/3":
            return "3"
        raise KeyError(target)

    def get(target: str) -> Dict[str, Any]:
        key = resolve_key(target)
        return {
            "status": "ok",
            "result": [dict(knobs[key])],
            "tti": 0,
            "t": 0.0,
        }

    def set(target: str, values: Dict[str, Any]) -> Dict[str, Any]:
        key = resolve_key(target)
        knobs[key].update(values)
        return {"status": "ok", "tti": 0, "t": 0.0}

    import fikore_api.main as main

    stub = MagicMock()
    stub.get.side_effect = get
    stub.set.side_effect = set
    stub.close = MagicMock()
    monkeypatch.setattr(main, "control", stub)

    main.collector.cache.update(
        "ue_pdcp",
        {"ue_id": "3", "tx_dir": "dl"},
        {"throughput_mbps_mean": 9.0},
        None,
    )

    with TestClient(main.app) as client:
        yield client


def test_textual_ue_id_set_and_state(mocked_client):
    textual = "imsi_001010000000001"
    assert mocked_client.post(f"/control/ue/{textual}", json={"priority": 6.0}).status_code == 200
    body = mocked_client.get(f"/ue/{textual}/state").json()
    assert body["ue_id"] == textual
    assert body["control"]["priority"] == 6.0
    assert body["control"]["target"] == "ue/3"
    assert len(body["telemetry"]) == 1
    assert next(iter(body["telemetry"].values()))["tags"]["ue_id"] == "3"


def test_numeric_ue_id_response_is_int(mocked_client):
    body = mocked_client.get("/ue/0/state").json()
    assert body["ue_id"] == 0
    assert isinstance(body["ue_id"], int)


def test_textual_name_distinct_from_index(mocked_client):
    textual = "controlDemo_0"
    assert mocked_client.post(f"/control/ue/{textual}", json={"priority": 3.5}).status_code == 200
    body = mocked_client.get(f"/ue/{textual}/state").json()
    assert body["ue_id"] == textual
    assert body["control"]["priority"] == pytest.approx(3.5)
    assert body["control"]["target"] == "ue/0"
