# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear

"""Control protocol version.

Single source on the Python side. It must match FIKORE_CONTROL_PROTO in
include/utils/control/proto_version.h: the emulator and the API are built and deployed
together, so there is no negotiation and a mismatch is a deployment error.

api/tests/test_proto.py reads the C++ header and checks that both agree.
"""

PROTO = "fikore-control-1"
