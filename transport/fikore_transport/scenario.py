#!/usr/bin/env python3
# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Lossless-enough editing of FikoRE's repeated-section INI format.

``configparser`` cannot represent repeated ``[UE]`` sections.  FikoRE relies on
those sections, so this module deliberately keeps the original lines and only
replaces or inserts requested key/value pairs.
"""
from __future__ import annotations

from dataclasses import dataclass
import re


_SECTION = re.compile(r"^\s*\[([^\]]+)\]\s*$")
_KEY = re.compile(r"^(\s*)([A-Za-z0-9_.-]+)(\s*:\s*)(.*?)(\s*)$")


@dataclass(frozen=True)
class Override:
    section: str
    key: str
    before: str | None
    after: str
    source: str
    ue_id: str | None = None


@dataclass
class _Block:
    name: str
    start: int
    end: int


class ScenarioDocument:
    """A line-preserving FikoRE configuration document."""

    def __init__(self, text: str) -> None:
        self._had_final_newline = text.endswith("\n")
        self.lines = text.splitlines()
        self.overrides: list[Override] = []
        self._validate()

    @classmethod
    def read(cls, path: str) -> "ScenarioDocument":
        with open(path, encoding="utf-8") as stream:
            return cls(stream.read())

    def render(self) -> str:
        text = "\n".join(self.lines)
        if self._had_final_newline or self.lines:
            text += "\n"
        return text

    def write(self, path: str) -> str:
        with open(path, "w", encoding="utf-8") as stream:
            stream.write(self.render())
        return path

    def section_names(self) -> list[str]:
        return [block.name for block in self._blocks()]

    def ue_ids(self) -> list[str]:
        return [self._ue_id(block) for block in self._blocks("UE")]

    def ue_count(self, ue_id: str) -> int:
        block = self._ue_block(ue_id)
        value = self._value(block, "n_ues")
        return int(value) if value is not None else 1

    def get(self, section: str, key: str) -> str | None:
        block = self._single_block(section)
        return self._value(block, key)

    def get_ue(self, ue_id: str, key: str) -> str | None:
        return self._value(self._ue_block(ue_id), key)

    def set(self, section: str, key: str, value: object,
            source: str = "configuration", create: bool = True) -> None:
        blocks = self._blocks(section)
        if not blocks:
            if not create:
                raise ValueError(f"missing [{section}] section")
            self._append_section(section)
            blocks = self._blocks(section)
        if len(blocks) != 1:
            raise ValueError(
                f"[{section}] is repeated; select a UE block explicitly")
        self._set_in_block(blocks[0], key, str(value), source)

    def set_ue(self, ue_id: str, key: str, value: object,
               source: str = "configuration") -> None:
        self._set_in_block(self._ue_block(ue_id), key, str(value), source, ue_id)

    def _validate(self) -> None:
        ids = self.ue_ids()
        duplicates = sorted({ue_id for ue_id in ids if ids.count(ue_id) > 1})
        if duplicates:
            raise ValueError(f"duplicate UE ids: {', '.join(duplicates)}")

    def _blocks(self, name: str | None = None) -> list[_Block]:
        starts: list[tuple[str, int]] = []
        for at, line in enumerate(self.lines):
            match = _SECTION.match(line)
            if match:
                starts.append((match.group(1), at))
        blocks = [
            _Block(section, start,
                   starts[index + 1][1] if index + 1 < len(starts) else len(self.lines))
            for index, (section, start) in enumerate(starts)
        ]
        if name is not None:
            blocks = [block for block in blocks if block.name == name]
        return blocks

    def _single_block(self, section: str) -> _Block:
        blocks = self._blocks(section)
        if not blocks:
            raise ValueError(f"missing [{section}] section")
        if len(blocks) != 1:
            raise ValueError(f"[{section}] occurs {len(blocks)} times")
        return blocks[0]

    def _ue_id(self, block: _Block) -> str:
        value = self._value(block, "ue_id")
        if value is None or not value:
            raise ValueError("[UE] section has no ue_id")
        return value

    def _ue_block(self, ue_id: str) -> _Block:
        matches = [
            block for block in self._blocks("UE") if self._ue_id(block) == ue_id
        ]
        if not matches:
            raise ValueError(
                f"unknown UE {ue_id!r}; available: {', '.join(self.ue_ids())}")
        if len(matches) != 1:
            raise ValueError(f"UE {ue_id!r} is ambiguous")
        return matches[0]

    def _value(self, block: _Block, key: str) -> str | None:
        matches = []
        for line in self.lines[block.start + 1:block.end]:
            match = _KEY.match(line)
            if match and match.group(2) == key:
                matches.append(match.group(4).strip())
        if len(matches) > 1:
            label = f"UE {self._ue_id(block)!r}" if block.name == "UE" else block.name
            raise ValueError(f"duplicate key {key!r} in {label}")
        return matches[0] if matches else None

    def _set_in_block(self, block: _Block, key: str, value: str, source: str,
                      ue_id: str | None = None) -> None:
        found: list[int] = []
        for at in range(block.start + 1, block.end):
            match = _KEY.match(self.lines[at])
            if match and match.group(2) == key:
                found.append(at)
        if len(found) > 1:
            raise ValueError(f"duplicate key {key!r} in [{block.name}]")
        before = None
        if found:
            at = found[0]
            match = _KEY.match(self.lines[at])
            assert match is not None
            before = match.group(4).strip()
            self.lines[at] = (
                f"{match.group(1)}{key}{match.group(3)}{value}{match.group(5)}")
        else:
            at = block.end
            while at > block.start + 1 and not self.lines[at - 1].strip():
                at -= 1
            self.lines.insert(at, f"{key}: {value}")
        self.overrides.append(
            Override(block.name, key, before, value, source, ue_id))

    def _append_section(self, section: str) -> None:
        if self.lines and self.lines[-1].strip():
            self.lines.append("")
        self.lines.extend([f"[{section}]"])
