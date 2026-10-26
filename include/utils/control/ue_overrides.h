/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <common/direction.h>

//--------------------------------------------------------------------------------------------------
// ue_overrides(): per-UE runtime control state. Plain data, owned by the ue it belongs
// to, contiguous and free of pointers, so that reading it from the scheduling hot path
// costs one scalar load.
//
// Writing: only control_manager::tick(), at the start of simulator::step(), where every
// worker thread of the pools is quiescent. Never from the socket thread and never from
// inside a parallel phase.
//
// Reading: each subsystem at its own point of consumption, by value. Nothing keeps a
// pointer to this struct: ue_handler::add_ues grows a std::vector<ue> without reserve,
// so any pointer taken during construction would dangle after a reallocation.
//
// Arrays indexed by tx_dir: 0 = TX_DL, 1 = TX_UL (common/direction.h).
//--------------------------------------------------------------------------------------------------
struct ue_overrides
{
    // Absolute scheduling priority. Replaces the .ini value, it does not multiply it.
    // Initialized from ue_config::priority so that an emulator without a control plane
    // behaves exactly as before. Applied in phy_layer::get_metric.
    float priority = 1.0f;

    // Logical attach/detach. A disabled UE is removed from the simulation for all
    // purposes: it drops its buffers, generates no traffic, emits no telemetry and does
    // not count towards the round robin rotation.
    bool enabled = true;

    // Additive offset on the derived SINR, in dB. Applied in phy_layer::sinr_power_model,
    // which also shifts the reported RSRP: an offset that stands for power or losses
    // moves both.
    float sinr_offset_db[2] = {0.0f, 0.0f};

    // Rate cap as a token bucket. rmax_bps == 0 means no cap. The bucket is refilled once
    // per TTI by control_manager::tick and drained by the bits granted over the air in
    // ue::handle_pkt. Tokens may go negative: the UE then waits until the refill brings
    // them back above zero, which keeps the long run average at rmax_bps without having
    // to split an RBG.
    float rmax_bps[2] = {0.0f, 0.0f};
    float rmax_tokens[2] = {0.0f, 0.0f};

    // Derived state, not a knob: dense position of this UE among the enabled ones.
    // Recomputed by control_manager::tick whenever the enabled set changes. Round robin
    // rotates over this instead of over the UE id, which is what makes a disabled UE
    // stop taking a turn. How many are enabled is not stored here: it is one number for
    // the whole cell, and the scheduler counts it once per TTI and passes it down.
    int rr_rank = -1;
};
