# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""The `Link` implementation that drives FikoRE over its control protocol.

One slot per round trip, because a transport model reacts within the slot in which
it learns something: the caller injects what the senders produced, grants exactly
one slot of credit, drains the per-tag counter movements, and turns them into
arrivals. A compact cumulative state snapshot is requested only once per harness
window for telemetry; the live object map is never serialised on this path.

Each segment travels under its own object tag, which is what makes a byte counter
into per-segment feedback: a tag whose terminal counters add up to its size has
finished, the counter it landed in says how, and `ce_bytes` says whether it was
marked on the way. Tags are released as soon as they are terminal, so the number of
live objects stays around the congestion window rather than growing with the
transfer.

Three properties of the wire protocol shape everything below, and none is obvious
from the message format:

* **The emulator answers per command, not per message.** A message of N commands
  comes back as N acknowledgements, all carrying the message's `id`, in the order
  the commands were written. Only the `get` carries a `result`. A client that reads
  one acknowledgement per `id` leaves N-1 in the socket and every later slot reads
  the previous slot's replies.
* **`grant` is answered from the socket thread and everything else at the quiescent
  point**, so the two messages of a slot must both be written before either reply is
  read, and the replies must be matched by `id` rather than by order.
* **Object events are cursor based.** `after` is the last cursor successfully
  consumed, not the cursor requested. The emulator retains everything newer, so a
  repeated request is a replay rather than a destructive second read.
"""
from __future__ import annotations

import json
import math
from dataclasses import dataclass

from .emulator import PROTO, Emulator
from .link import Arrival, Cause, Direction, Transmit

# The counters are carried as bits divided by eight, so a whole segment can come
# back a hair under its own size. The emulator's BIT_ROUND_MARGIN is 0.99 bits,
# therefore the byte-side tolerance must be no larger than 0.99 / 8. A whole byte
# closed tags while a legitimate 0.25-byte residual was still due on the next TTI.
EPS_BYTES = 0.99 / 8.0

# The emulator's name for each way of losing a byte, in the order the fate is
# decided. `dropped_bytes` is the sum of the two below it and is only read as a
# fallback, for the case of a build that does not break it down.
_CAUSES: tuple[tuple[str, str, Cause], ...] = (
    ("expired_bytes", "expired", "budget"),
    ("queue_dropped_bytes", "dropped", "queue"),
    ("radio_dropped_bytes", "dropped", "radio"),
)


@dataclass
class _Outstanding:
    flow: int
    ue: int
    seq: int
    size: int
    direction: Direction
    ts_us: int
    kind: str
    control_target: str


class FikoreLink:
    """Transport-facing view of a running emulator."""

    def __init__(self, emulator: Emulator, flow_to_ue: dict[int, int],
                 state_every_ttis: int = 10, use_events: bool = True,
                 ue_id_map: dict[int, int] | None = None,
                 ue_target_map: dict[int, str] | None = None) -> None:
        self.emulator = emulator
        self.flow_to_ue = flow_to_ue
        self.ue_id_map = dict(ue_id_map or {})
        if len(set(self.ue_id_map.values())) != len(self.ue_id_map):
            raise ValueError("UE mapping must be one-to-one")
        self._physical_to_external = {physical: external
                                      for external, physical in self.ue_id_map.items()}
        self.ue_target_map = dict(ue_target_map or {})
        self.flow_to_target: dict[int, str] = {}
        self.mss = emulator.cfg.pkt_size_bits // 8

        self.sock = emulator.connect()
        self.io = self.sock.makefile("rwb")
        hello = json.loads(self.io.readline() or b"{}")
        if hello.get("proto") != PROTO:
            raise RuntimeError(f"unexpected protocol: {hello}"
                               f"{emulator.log_tail()}")
        self._send({"proto": PROTO})

        self._next_id = 1
        self._next_tag = 1
        self._last_tti = -1
        self._outstanding: dict[int, _Outstanding] = {}
        self._pending: dict[int, list[Transmit]] = {}
        self._to_forget: list[tuple[str, int]] = []   # (control target, tag)
        self._queued_sets: list[dict] = []
        self._scheduled_sets: dict[int, list[dict]] = {}
        self._event_cursor = 0
        self._event_counters: dict[int, dict[str, float]] = {}
        self.state_every_ttis = state_every_ttis
        self._force_state = False
        self.use_events = use_events
        self.round_trips = 0
        self.received_bytes = 0
        self.event_accounted_bytes = 0.0
        self.max_events_per_reply = 0
        self.ce_by_ue: dict[tuple[int, Direction], int] = {}
        # Byte accounting of what this link was asked to carry, for whoever wants to
        # check that the run conserved bytes without going back to the emulator.
        self.submitted_bytes = 0
        self.terminal_bytes: dict[str, int] = {"delivered": 0, "dropped": 0,
                                               "expired": 0}
        self.lost_bytes_by_cause: dict[Cause, int] = {"queue": 0, "radio": 0,
                                                      "budget": 0}
        # Per (ue, direction) snapshot of the last state block, for whoever needs
        # the network's own view alongside the model's.
        self.last_state: dict[tuple[int, str], dict] = {}
        self.last_mobility: dict[int, dict[str, float]] = {}

    # -- Link interface -----------------------------------------------------------

    def register_flow(self, flow: int, ue: int) -> None:
        physical = self.ue_id_map.get(ue, ue)
        self.flow_to_ue[flow] = physical
        self.flow_to_target[flow] = self.ue_target_map.get(ue, str(physical))

    def unregister_flow(self, flow: int) -> None:
        self.flow_to_ue.pop(flow, None)
        self.flow_to_target.pop(flow, None)

    def set_params(self, ue: int, params: dict[str, float]) -> None:
        """Queued rather than sent: every command of a slot goes in one message,
        applied at the same quiescent point."""
        physical = self.ue_id_map.get(ue, ue)
        target = self.ue_target_map.get(ue, str(physical))
        self._queued_sets.append({"target": f"ue/{target}", "set": dict(params)})

    def schedule_set(self, at_tti: int, target: str,
                     params: dict[str, object]) -> None:
        """Queue one control ``set`` for an exact future quiescent point.

        The command is kept client-side until its TTI. Sending it to FikoRE early
        would deadlock a barrier run: FikoRE acknowledges scheduled commands only
        when they become due, while the client cannot grant that TTI until the
        outstanding acknowledgement has arrived.
        """
        if isinstance(at_tti, bool) or not isinstance(at_tti, int) or at_tti < 0:
            raise ValueError("scheduled set TTI must be a non-negative integer")
        if at_tti <= self._last_tti:
            raise ValueError(
                f"cannot schedule set for TTI {at_tti}, "
                f"already past {self._last_tti}"
            )
        if not isinstance(target, str) or not target:
            raise ValueError("scheduled set target must be a non-empty string")
        if not params:
            raise ValueError("scheduled set parameters must not be empty")
        self._scheduled_sets.setdefault(at_tti, []).append(
            {"target": target, "set": dict(params)}
        )

    def request_state_next_step(self) -> None:
        self._force_state = True

    def submit(self, items: list[Transmit], at_tti: int) -> None:
        if not items:
            return
        if at_tti <= self._last_tti:
            # There is no way to inject into a slot the emulator has already run, so
            # accepting these would lose them silently and break the byte account.
            raise ValueError(f"cannot submit into slot {at_tti}, "
                             f"already past {self._last_tti}")
        self._pending.setdefault(at_tti, []).extend(items)

    def step(self, until_tti: int) -> list[Arrival]:
        """Apply this slot's injections, grant it, and read what became terminal.

        The state comes back resolved at the same quiescent point at which the
        injections are applied, so what it reports finished during the previous
        slot. The arrival is stamped with the slot in which it was learnt, which is
        the only instant the model can legitimately act on.
        """
        if until_tti <= self._last_tti:
            raise ValueError(f"slots must advance: {until_tti} after {self._last_tti}")
        self._last_tti = until_tti

        cmds = self._commands_for(until_tti)
        batch_id, grant_id = self._next_id, self._next_id + 1
        self._next_id += 2
        # Both messages go out before either reply is read: the grant is answered by
        # the transport and the rest at the quiescent point, so waiting for the first
        # reply in order would block the run until the credit timeout.
        self._send({"id": batch_id, "at_tti": until_tti, "cmds": cmds})
        self._send({"id": grant_id, "op": "grant", "until_tti": until_tti})
        acks = self._read_acks({batch_id: len(cmds), grant_id: 1})
        self.round_trips += 1

        for ack, cmd in zip(acks[batch_id], cmds):
            if ack.get("status") != "ok":
                raise RuntimeError(f"the emulator rejected {cmd}: {ack}")
        if acks[grant_id][0].get("status") != "ok":
            raise RuntimeError(f"the grant was refused: {acks[grant_id][0]}")

        # The read operation is written last and is the only command carrying a result.
        result = acks[batch_id][-1].get("result") or {}
        if self.use_events:
            # Its cursor advances only after a complete reply was parsed, so retrying
            # after a lost reply asks for the same deltas rather than losing them.
            return self._arrivals_from_events(result, until_tti)
        return self._arrivals_from(result, until_tti)

    def close(self) -> None:
        try:
            self.io.close()
            self.sock.close()
        finally:
            self.emulator.close()

    # -- commands -----------------------------------------------------------------

    def _commands_for(self, tti: int) -> list[dict]:
        missed = [scheduled_tti for scheduled_tti in self._scheduled_sets
                  if scheduled_tti < tti]
        if missed:
            raise RuntimeError(
                f"scheduled control TTI {min(missed)} was skipped before {tti}"
            )
        cmds: list[dict] = self._scheduled_sets.pop(tti, [])
        cmds.extend(self._queued_sets)
        self._queued_sets.clear()
        for t in self._pending.pop(tti, []):
            ue = self.flow_to_ue[t.flow]
            control_target = self.flow_to_target.get(t.flow, str(ue))
            tag = self._next_tag
            self._next_tag += 1
            self._outstanding[tag] = _Outstanding(
                t.flow, ue, t.seq, t.size, t.direction, t.ts_us, t.kind,
                control_target)
            self.submitted_bytes += t.size
            inject = {"op": "inject", "target": f"ue/{control_target}", "tag": tag,
                      f"{t.direction}.bytes": t.size}
            if t.ecn != "not-ect":
                inject["ecn"] = t.ecn
            cmds.append(inject)

        # Barrier runs are never real-time, so the emulator deliberately applies every
        # command addressed to this TTI. Holding completed tags serves no purpose.
        for target, tag in self._to_forget:
            cmds.append({"op": "forget", "target": f"ue/{target}", "tag": tag})
        self._to_forget.clear()

        if self.use_events:
            events = {"op": "events", "after": self._event_cursor}
            # Backend telemetry is emitted after a window, so sample on its last slot
            # rather than its first; otherwise every observation is one window stale.
            if (self._force_state or
                    (self.state_every_ttis > 0
                     and (tti + 1) % self.state_every_ttis == 0)):
                events["include_state"] = True
                self._force_state = False
            cmds.append(events)
        else:
            cmds.append({"op": "get", "target": "ue/*"})
        return cmds

    # -- wire ---------------------------------------------------------------------

    def _send(self, msg: dict) -> None:
        self.io.write((json.dumps(msg) + "\n").encode())
        self.io.flush()

    def _read_acks(self, expected: dict[int, int]) -> dict[int, list[dict]]:
        """Read exactly the acknowledgements a slot is owed, one per command.

        Counting them is what keeps the stream aligned. Reading "one per id" leaves
        the rest behind and every later slot then reads a previous slot's replies,
        which is silent: the acknowledgements are well formed and say `ok`, they just
        describe a different instant.
        """
        got: dict[int, list[dict]] = {i: [] for i in expected}
        remaining = sum(expected.values())
        while remaining:
            line = self.io.readline()
            if not line:
                raise RuntimeError("the emulator closed the control channel"
                                   f"{self.emulator.log_tail()}")
            self.received_bytes += len(line)
            ack = json.loads(line)
            bucket = got.get(ack.get("id"))
            if bucket is None or len(bucket) >= expected[ack["id"]]:
                raise RuntimeError(f"unexpected acknowledgement, the control stream "
                                   f"is out of step: {ack}")
            bucket.append(ack)
            remaining -= 1
        return got

    def _arrivals_from(self, result: list[dict], tti: int) -> list[Arrival]:
        out: list[Arrival] = []
        for entry in result:
            physical = int(str(entry["target"]).split("/")[1])
            external = self._physical_to_external.get(physical, physical)
            for direction in ("dl", "ul"):
                state = entry.get("state", {}).get(direction, {})
                self.last_state[(external, direction)] = state
                self.ce_by_ue[(external, direction)] = int(
                    state.get("ce_packets_total", 0))
                for raw_tag, counters in (state.get("objects") or {}).items():
                    arrival = self._terminal(int(raw_tag), counters, physical, tti)
                    if arrival is not None:
                        out.append(arrival)
        # The object map comes back in the emulator's hash order, so without this
        # the receiver sees the segments of one slot shuffled, reports the gaps as
        # selective acknowledgements and the sender retransmits segments that were
        # never lost. Inside a slot the order is unobservable, so sequence order is
        # the only defensible one to present.
        out.sort(key=lambda a: (a.flow, a.kind, a.seq))
        return out

    def _arrivals_from_events(self, result: dict, tti: int) -> list[Arrival]:
        """Accumulate per-tag deltas and turn newly terminal tags into arrivals.

        The emulator deliberately does not decide that a tag is terminal: a client may
        inject the same tag more than once and only this side knows the segment size.
        The event stream therefore carries counter movements, not guessed completions.
        """
        cursor = int(result.get("cursor", self._event_cursor))
        if cursor < self._event_cursor:
            raise RuntimeError(f"event cursor moved backwards: {cursor} after "
                               f"{self._event_cursor}")
        events = result.get("events", [])
        expected_seq = self._event_cursor + 1
        for event in events:
            if int(event.get("seq", -1)) != expected_seq:
                raise RuntimeError(
                    f"event sequence gap: expected {expected_seq}, got {event}")
            expected_seq += 1
        if cursor != expected_seq - 1:
            raise RuntimeError(
                f"event cursor {cursor} does not match last sequence "
                f"{expected_seq - 1}")

        # Validate and stage the whole batch before mutating cursors, counters or tag
        # lifetime. A bad later event must not make replay double-account earlier ones.
        staged = {tag: dict(counters)
                  for tag, counters in self._event_counters.items()}
        terminal_order: list[tuple[int, int]] = []
        terminal_seen: set[int] = set()
        delta_accounted = 0.0
        counter_keys = ("delivered_bytes", "expired_bytes",
                        "queue_dropped_bytes", "radio_dropped_bytes", "ce_bytes")
        for event in events:
            tag = int(event["tag"])
            pending = self._outstanding.get(tag)
            if pending is None or tag in terminal_seen:
                raise RuntimeError(f"event for unknown or terminal tag {tag}: {event}")
            ue = int(str(event["target"]).split("/")[1])
            direction = str(event["dir"])
            if ue != pending.ue or direction != pending.direction:
                raise RuntimeError(f"event does not match tag {tag}: {event}")

            counters = staged.setdefault(tag, {})
            for key in counter_keys:
                value = float(event.get(key, 0.0))
                if not math.isfinite(value) or value < 0:
                    raise RuntimeError(f"invalid {key} for tag {tag}: {value}")
                counters[key] = counters.get(key, 0.0) + value
            counters["dropped_bytes"] = (counters["queue_dropped_bytes"]
                                         + counters["radio_dropped_bytes"])
            accounted = (counters["delivered_bytes"] + counters["dropped_bytes"]
                         + counters["expired_bytes"])
            if accounted > pending.size + EPS_BYTES:
                raise RuntimeError(
                    f"tag {tag} accounted {accounted} bytes for a "
                    f"{pending.size}-byte object")
            delta_accounted += (
                float(event.get("delivered_bytes", 0.0))
                + float(event.get("expired_bytes", 0.0))
                + float(event.get("queue_dropped_bytes", 0.0))
                + float(event.get("radio_dropped_bytes", 0.0)))
            if accounted + EPS_BYTES >= pending.size:
                terminal_seen.add(tag)
                terminal_order.append((tag, ue))

        staged_state: list[tuple[int, str, dict, int]] = []
        staged_mobility: list[tuple[int, dict[str, float]]] = []
        for entry in result.get("state", []):
            physical = int(str(entry["target"]).split("/")[1])
            ue = self._physical_to_external.get(physical, physical)
            for direction in ("dl", "ul"):
                state = entry.get(direction)
                if state is None:
                    continue
                if not isinstance(state, dict):
                    raise RuntimeError(f"invalid {direction} state for ue/{physical}")
                ce_packets = int(state.get("ce_packets_total", 0))
                staged_state.append((ue, direction, state, ce_packets))
            mobility = entry.get("mobility")
            if mobility is not None:
                if not isinstance(mobility, dict):
                    raise RuntimeError(f"invalid mobility state for ue/{physical}")
                parsed = {
                    key: float(mobility[key])
                    for key in ("pos_x_m", "pos_y_m", "speed_kmh")
                }
                if not all(math.isfinite(value) for value in parsed.values()):
                    raise RuntimeError(f"invalid mobility values for ue/{physical}")
                staged_mobility.append((ue, parsed))

        out: list[Arrival] = []
        self.max_events_per_reply = max(self.max_events_per_reply, len(events))
        self._event_counters = staged
        self.event_accounted_bytes += delta_accounted
        for ue, direction, state, ce_packets in staged_state:
            self.last_state[(ue, direction)] = state
            self.ce_by_ue[(ue, direction)] = ce_packets
        for ue, mobility in staged_mobility:
            self.last_mobility[ue] = mobility
        for tag, ue in terminal_order:
            arrival = self._terminal(tag, self._event_counters[tag], ue, tti)
            if arrival is None:
                raise RuntimeError(f"terminal tag {tag} did not close")
            self._event_counters.pop(tag, None)
            out.append(arrival)

        self._event_cursor = cursor
        out.sort(key=lambda a: (a.flow, a.kind, a.seq))
        return out

    def _terminal(self, tag: int, counters: dict, ue: int, tti: int) -> Arrival | None:
        """An object whose counters account for every byte it was given, or nothing.

        What is in none of the counters is still in flight; the emulator never learns
        how big an object is, so this is the only place that knows it has finished.
        """
        pending = self._outstanding.get(tag)
        if pending is None:
            return None
        delivered = float(counters.get("delivered_bytes", 0.0))
        lost = {name: float(counters.get(name, 0.0)) for name, _, _ in _CAUSES}
        # A build that does not break the drops down still reports their sum, and a
        # loss of unknown cause is better than an object that never closes.
        total_dropped = float(counters.get("dropped_bytes", 0.0))
        unattributed = total_dropped - lost["queue_dropped_bytes"] - lost["radio_dropped_bytes"]
        accounted = delivered + total_dropped + lost["expired_bytes"]
        if accounted + EPS_BYTES < pending.size:
            return None
        if accounted > pending.size + EPS_BYTES:
            raise RuntimeError(
                f"tag {tag} accounted {accounted} bytes for a "
                f"{pending.size}-byte object")

        del self._outstanding[tag]
        self._to_forget.append((pending.control_target, tag))

        if delivered + EPS_BYTES >= pending.size:
            fate, cause = "delivered", ""
        else:
            # Partial delivery is loss as far as the sender is concerned: the segment
            # has a hole and the receiver cannot use it. The cause that took the most
            # bytes is the one worth reporting.
            name, fate, cause = max(_CAUSES, key=lambda c: lost[c[0]])
            if unattributed > max(lost.values()) + EPS_BYTES:
                fate, cause = "dropped", ""
            self.lost_bytes_by_cause[cause] = (self.lost_bytes_by_cause.get(cause, 0)
                                               + pending.size)
        self.terminal_bytes[fate] += pending.size
        return Arrival(tti=tti, flow=pending.flow, seq=pending.seq, size=pending.size,
                       fate=fate, ce=float(counters.get("ce_bytes", 0.0)) > 0.0,
                       delivered_bytes=int(delivered), ts_us=pending.ts_us,
                       kind=pending.kind, cause=cause)

    # -- accounting ---------------------------------------------------------------

    @property
    def in_flight_bytes(self) -> int:
        """Submitted, not yet terminal. What the byte account is short of."""
        return sum(o.size for o in self._outstanding.values())
