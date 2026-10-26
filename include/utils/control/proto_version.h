/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

// Single definition of the control protocol version. The emulator and the Python API are
// built and deployed together, so there is no negotiation and no backwards compatibility:
// on connect both sides state this string and a mismatch closes the connection.
//
// Bump it whenever the wire format changes. api/fikore_api/proto.py reads this same value
// at build time and the integration test checks that they agree.
#define FIKORE_CONTROL_PROTO "fikore-control-1"
