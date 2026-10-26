#pragma once

#include <algorithm>
#include <cmath>

enum class ul_power_mode
{
    fixed = 0,
    fractional = 1
};

struct ul_power_allocation
{
    float total_dbm = 0.0f;
    float per_prb_dbm = 0.0f;
};

inline int normalized_prb_count(int allocated_prbs)
{
    return std::max(1, allocated_prbs);
}

inline float power_per_prb_dbm(float total_power_dbm, int allocated_prbs)
{
    return total_power_dbm
           - 10.0f * std::log10(static_cast<float>(normalized_prb_count(allocated_prbs)));
}

inline float downlink_power_per_prb_dbm(float total_power_dbm, int carrier_prbs)
{
    return power_per_prb_dbm(total_power_dbm, carrier_prbs);
}

inline float thermal_noise_per_prb_dbm(
    float noise_density_dbm_hz,
    float noise_figure_db,
    int numerology)
{
    const float prb_bandwidth_hz = 12.0f * 15000.0f * std::pow(2.0f, numerology);
    return noise_density_dbm_hz + noise_figure_db
           + 10.0f * std::log10(prb_bandwidth_hz);
}

inline float rayleigh_power_gain_db(float in_phase, float quadrature)
{
    const float unit_mean_power =
        (in_phase * in_phase + quadrature * quadrature) / 2.0f;
    return 10.0f * std::log10(std::max(unit_mean_power, 1e-12f));
}

inline ul_power_allocation calculate_ul_power_allocation(
    ul_power_mode mode,
    float tx_power_ul_dbm,
    float nominal_power_per_prb_dbm,
    int allocated_prbs)
{
    const int prbs = normalized_prb_count(allocated_prbs);
    const float requested_total_dbm =
        mode == ul_power_mode::fixed
            ? tx_power_ul_dbm
            : nominal_power_per_prb_dbm + 10.0f * std::log10(static_cast<float>(prbs));
    const float total_dbm = std::min(requested_total_dbm, tx_power_ul_dbm);
    return {total_dbm, power_per_prb_dbm(total_dbm, prbs)};
}
