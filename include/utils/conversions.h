/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#define BIT2MBIT 0.000001
#define MBIT2BIT 1000000.0
#define S2MS 1000.0
#define KHZ2HZ 1000.0
#define MHZ2HZ 1000000.0
#define GHZ2HZ 1000000000.0

// One TTI, in the two units the emulator uses it in. Deliberately the nominal duration
// and not the wall clock, so that a rate cap behaves identically in fast mode and in
// real time.
constexpr unsigned int TTI_US = 1000;
constexpr float TTI_S = 0.001f;