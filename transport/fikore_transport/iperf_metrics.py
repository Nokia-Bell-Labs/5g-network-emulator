# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Non-mutating interval snapshots for iperf-like sessions."""
from __future__ import annotations

from dataclasses import dataclass
from typing import Any


@dataclass
class FlowView:
    flow_id: int
    ue_id: str
    sender: Any
    receiver: Any


class IntervalRecorder:
    def __init__(self, protocol: str, flows: list[FlowView]) -> None:
        self.protocol = protocol
        self.flows = flows
        self.previous = {flow.flow_id: self._cumulative(flow) for flow in flows}
        self.previous_tti = 0
        self.intervals: list[dict] = []

    def sample(self, tti: int) -> dict:
        seconds = max((tti - self.previous_tti) / 1000.0, 1e-9)
        streams = []
        for flow in self.flows:
            current = self._cumulative(flow)
            before = self.previous[flow.flow_id]
            record = {
                "socket": flow.flow_id,
                "ue_id": flow.ue_id,
                "start": self.previous_tti / 1000.0,
                "end": tti / 1000.0,
                "seconds": seconds,
                "bytes": current["received_bytes"] - before["received_bytes"],
            }
            record["bits_per_second"] = record["bytes"] * 8.0 / seconds
            if self.protocol == "tcp":
                record.update({
                    "bytes_sent": current["sent_bytes"] - before["sent_bytes"],
                    "send_bits_per_second": (
                        current["sent_bytes"] - before["sent_bytes"]) * 8.0 / seconds,
                    "retransmits": current["retransmits"] - before["retransmits"],
                    "snd_cwnd": current["cwnd"],
                    "rtt_ms": current["rtt_ms"],
                    "srtt_ms": current["srtt_ms"],
                    "ce_marks": current["ce_marks"] - before["ce_marks"],
                })
            else:
                sent = current["sent_datagrams"] - before["sent_datagrams"]
                lost = current["lost_datagrams"] - before["lost_datagrams"]
                record.update({
                    "packets": sent,
                    "lost_packets": lost,
                    "lost_percent": lost / sent * 100.0 if sent else 0.0,
                    "jitter_ms": current["jitter_ms"],
                    "bytes_sent": current["sent_bytes"] - before["sent_bytes"],
                    "send_bits_per_second": (
                        current["sent_bytes"] - before["sent_bytes"]) * 8.0 / seconds,
                })
            streams.append(record)
            self.previous[flow.flow_id] = current
        interval = {
            "start": self.previous_tti / 1000.0,
            "end": tti / 1000.0,
            "streams": streams,
            "sum": self._sum(streams, seconds),
        }
        self.previous_tti = tti
        self.intervals.append(interval)
        return interval

    def totals(self, end_tti: int) -> dict:
        streams = []
        seconds = max(end_tti / 1000.0, 1e-9)
        for flow in self.flows:
            value = self._cumulative(flow)
            record = {
                "socket": flow.flow_id,
                "ue_id": flow.ue_id,
                "seconds": seconds,
                "bytes": value["received_bytes"],
                "bits_per_second": value["received_bytes"] * 8.0 / seconds,
            }
            if self.protocol == "tcp":
                record.update({
                    "bytes_sent": value["sent_bytes"],
                    "send_bits_per_second": value["sent_bytes"] * 8.0 / seconds,
                    "retransmits": value["retransmits"],
                    "snd_cwnd": value["cwnd"],
                    "rtt_ms": value["rtt_ms"],
                    "srtt_ms": value["srtt_ms"],
                    "ce_marks": value["ce_marks"],
                })
            else:
                sent = value["sent_datagrams"]
                lost = value["lost_datagrams"]
                record.update({
                    "packets": sent,
                    "received_packets": value["received_datagrams"],
                    "lost_packets": lost,
                    "lost_percent": lost / sent * 100.0 if sent else 0.0,
                    "jitter_ms": value["jitter_ms"],
                    "bytes_sent": value["sent_bytes"],
                    "send_bits_per_second": value["sent_bytes"] * 8.0 / seconds,
                    "owd_ms_min": value["owd_ms_min"],
                    "owd_ms_median": value["owd_ms_median"],
                    "owd_ms_max": value["owd_ms_max"],
                    "ce_marks": value["ce_marks"],
                })
            streams.append(record)
        return {"streams": streams, "sum": self._sum(streams, seconds)}

    def _cumulative(self, flow: FlowView) -> dict:
        sender, receiver = flow.sender, flow.receiver
        if self.protocol == "tcp":
            return {
                "received_bytes": receiver.rcv_nxt,
                "sent_bytes": sender.stats.bytes_sent,
                "retransmits": sender.stats.retransmits,
                "cwnd": int(sender.cc.cwnd),
                "rtt_ms": (sender.last_rtt_us / 1000.0
                           if sender.last_rtt_us is not None else None),
                "srtt_ms": (sender.srtt_us / 1000.0
                            if sender.srtt_us is not None else None),
                "ce_marks": sender.stats.ce_marks,
            }
        report = receiver.report()
        return {
            "received_bytes": report["bytes_received"],
            "sent_bytes": sender.stats.bytes_sent,
            "sent_datagrams": sender.stats.datagrams_sent,
            "received_datagrams": report["datagrams_received"],
            "lost_datagrams": report["lost"],
            "jitter_ms": report["jitter_ms"],
            "owd_ms_min": report["owd_ms_min"],
            "owd_ms_median": report["owd_ms_median"],
            "owd_ms_max": report["owd_ms_max"],
            "ce_marks": report["ce_marks"],
        }

    def _sum(self, streams: list[dict], seconds: float) -> dict:
        result = {
            "seconds": seconds,
            "bytes": sum(stream["bytes"] for stream in streams),
            "bits_per_second": sum(stream["bits_per_second"] for stream in streams),
        }
        if self.protocol == "tcp":
            result["bytes_sent"] = sum(stream["bytes_sent"] for stream in streams)
            result["send_bits_per_second"] = sum(
                stream["send_bits_per_second"] for stream in streams)
            result["retransmits"] = sum(stream["retransmits"] for stream in streams)
        else:
            packets = sum(stream["packets"] for stream in streams)
            lost = sum(stream["lost_packets"] for stream in streams)
            result.update({
                "packets": packets,
                "lost_packets": lost,
                "lost_percent": lost / packets * 100.0 if packets else 0.0,
                "jitter_ms": max(
                    (stream["jitter_ms"] for stream in streams), default=0.0),
                "bytes_sent": sum(stream["bytes_sent"] for stream in streams),
                "send_bits_per_second": sum(
                    stream["send_bits_per_second"] for stream in streams),
            })
        return result
