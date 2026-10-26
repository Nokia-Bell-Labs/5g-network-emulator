/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <chrono>
#include <cstdint>

//--------------------------------------------------------------------------------------------------
// Seeding policy for the emulator's pseudo-random generators.
//
// A run is reproducible when [Global] seed is set, and also when random_v is false, which is
// equivalent to seed: 0. With random_v: true and no seed the generators are seeded from the wall
// clock, so every run explores a different realization.
//
// Reproducibility is not the absence of stochasticity: the configured variances describe the
// model and are honoured in every case. Two runs with the same seed are identical; two runs with
// different seeds are two different realizations of the same scenario, which is what a seed sweep
// needs.
//
// Every generator draws from its own stream. A stream is identified by a domain and an index, and
// the two are mixed with the run seed, so that no two generators share a sequence and neighbouring
// indices do not produce correlated ones.
//--------------------------------------------------------------------------------------------------

enum rng_domain
{
    RNG_PHY_LAYER = 1,
    RNG_PHY_SHARED,
    RNG_PDCP,
    RNG_MOBILITY,
    RNG_TRAFFIC
};

#define PHY_STREAM_FADING 0
#define PHY_STREAM_INTERFERENCE 1
#define PHY_STREAM_CQI_DISTANCE 2

// Set once from [Global] seed, before any UE is built and before any worker thread starts.
inline std::uint64_t &rng_run_seed()
{
    static std::uint64_t value = 0;
    return value;
}

inline void set_rng_run_seed(std::uint64_t seed) { rng_run_seed() = seed; }

//--------------------------------------------------------------------------------------------------
// rng_seed(): seed of one stream.
// Input:
//      stochastics: value of random_v for the UE.
//      domain: subsystem the generator belongs to.
//      index: identifier of the generator within its domain, unique within the run.
//--------------------------------------------------------------------------------------------------
inline unsigned int rng_seed(bool stochastics, int domain, std::uint64_t index)
{
    std::uint64_t key = rng_run_seed();

    if (key == 0 && stochastics)
        key = (std::uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

    // splitmix64 over the run seed and the stream, so that seed 17 and seed 18 are unrelated and
    // two streams of the same run are unrelated too.
    std::uint64_t x = key + 0x9E3779B97F4A7C15ULL * ((std::uint64_t)domain << 40 | index);
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    x = x ^ (x >> 31);

    return (unsigned int)(x & 0xFFFFFFFFULL);
}

// Generators living below PDCP: one stream per UE, direction and role, so that two generators of
// the same UE never share a sequence.
#define PDCP_STREAM_PACKET_HANDLER 0
#define PDCP_STREAM_HARQ 1
#define PDCP_STREAM_L4S 2

inline unsigned int pdcp_rng_seed(bool stochastics, int ue_id, int tx_dir, int stream)
{
    return rng_seed(stochastics, RNG_PDCP, 8ULL * (std::uint64_t)ue_id + 2ULL * (std::uint64_t)stream + (std::uint64_t)tx_dir);
}

inline unsigned int phy_rng_seed(
    bool stochastics,
    int ue_id,
    int tx_dir,
    int stream)
{
    return rng_seed(
        stochastics,
        RNG_PHY_LAYER,
        8ULL * (std::uint64_t)ue_id
            + 4ULL * (std::uint64_t)tx_dir
            + (std::uint64_t)stream);
}
