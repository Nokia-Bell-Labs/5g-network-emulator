# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear

"""The Python and C++ protocol constants must be the same string.

There is no negotiation: the emulator and the API are built together, so a mismatch is a
deployment error and this test is what catches it before deployment.
"""

import pathlib
import re

from fikore_api.proto import PROTO

HEADER = pathlib.Path(__file__).resolve().parents[2] / "include/utils/control/proto_version.h"


def test_proto_matches_the_cpp_header():
    text = HEADER.read_text()
    match = re.search(r'#define\s+FIKORE_CONTROL_PROTO\s+"([^"]+)"', text)
    assert match, f"FIKORE_CONTROL_PROTO not found in {HEADER}"
    assert match.group(1) == PROTO
