# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Structured scenario editing and EmulatorConfig overlays."""
import os
import tempfile
from pathlib import Path

from fikore_transport.emulator import EmulatorConfig
from fikore_transport.scenario import ScenarioDocument


ROOT = Path(__file__).resolve().parents[2]


SAMPLE = """; heading
[Global]
duration: 300
period: 1

[UE]
ue_id: live
ue_type: 0
n_ues: 1
dl_target: 5
ul_target: 5
random_v: true
pkt_delay_budget: 0.3

[UE]
ue_id: background
ue_type: 1
n_ues: 10
dl_target: 20
ul_target: 0
random_v: true
pkt_delay_budget: 0.3

[Control]
enabled: false

[MACLayer]
metric_type: 5

[Monitoring]
enabled: true
"""


def test_repeated_ue_sections_are_addressed_by_id():
    doc = ScenarioDocument(SAMPLE)
    doc.set_ue("live", "ue_type", 1)
    doc.set_ue("live", "new_knob", "value")
    assert doc.get_ue("live", "ue_type") == "1"
    assert doc.get_ue("live", "new_knob") == "value"
    assert doc.get_ue("background", "ue_type") == "1"
    assert doc.get_ue("background", "new_knob") is None
    assert doc.render().startswith("; heading\n")


def test_unknown_and_duplicate_ues_are_rejected():
    doc = ScenarioDocument(SAMPLE)
    try:
        doc.set_ue("missing", "ue_type", 1)
        assert False, "unknown UE was accepted"
    except ValueError as exc:
        assert "unknown UE" in str(exc)
    try:
        ScenarioDocument(SAMPLE + "\n[UE]\nue_id: live\n")
        assert False, "duplicate UE id was accepted"
    except ValueError as exc:
        assert "duplicate UE ids" in str(exc)


def test_emulator_config_converts_only_selected_ue_and_preserves_source():
    with tempfile.TemporaryDirectory() as tmp:
        source = os.path.join(tmp, "source.ini")
        output = os.path.join(tmp, "effective.ini")
        Path(source).write_text(SAMPLE)
        cfg = EmulatorConfig(
            binary="/bin/false", base_ini=source,
            socket_path=os.path.join(tmp, "control.sock"),
            duration_s=10, n_ues=None, study_ues=["live"],
            section_overrides={("MACLayer", "metric_type"): "6"},
            ue_overrides={"live": {"pkt_delay_budget": "0.02"}},
        )
        cfg.render(output)
        effective = ScenarioDocument.read(output)
        assert Path(source).read_text() == SAMPLE
        assert effective.get("Global", "period") == "-1"
        assert effective.get("Control", "sync_mode") == "barrier"
        assert effective.get_ue("live", "ue_type") == "1"
        assert effective.get_ue("live", "dl_target") == "0.0"
        assert effective.get_ue("background", "dl_target") == "20"
        assert effective.get("MACLayer", "metric_type") == "6"
        assert cfg.last_overrides


def test_emulator_config_can_preserve_selected_ue_runtime_settings():
    with tempfile.TemporaryDirectory() as tmp:
        source = os.path.join(tmp, "source.ini")
        output = os.path.join(tmp, "effective.ini")
        Path(source).write_text(SAMPLE)
        cfg = EmulatorConfig(
            binary="/bin/false", base_ini=source,
            socket_path=os.path.join(tmp, "control.sock"),
            duration_s=10, n_ues=None, study_ues=["live"],
            delay_budget_s=None, random_v=None,
        )
        cfg.render(output)
        effective = ScenarioDocument.read(output)
        assert effective.get_ue("live", "random_v") == "true"
        assert effective.get_ue("live", "pkt_delay_budget") == "0.3"
        assert effective.get_ue("background", "random_v") == "true"
        assert effective.get_ue("background", "pkt_delay_budget") == "0.3"


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
