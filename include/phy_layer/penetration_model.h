#pragma once

#include <algorithm>
#include <cmath>

#include <phy_layer/phy_l_definitions.h>

inline float building_penetration_loss_db(
    int penetration_profile,
    float frequency_ghz,
    float normal_sample)
{
    if (penetration_profile == PENETRATION_NONE)
        return 0.0f;

    const float glass_loss = 2.0f + 0.2f * frequency_ghz;
    const float irr_glass_loss = 23.0f + 0.3f * frequency_ghz;
    const float concrete_loss = 5.0f + 4.0f * frequency_ghz;
    float wall_loss = 0.0f;
    float sigma_db = 0.0f;
    if (penetration_profile == PENETRATION_LOW_LOSS)
    {
        wall_loss =
            5.0f
            - 10.0f * std::log10(
                0.3f * std::pow(10.0f, -glass_loss / 10.0f)
                + 0.7f * std::pow(10.0f, -concrete_loss / 10.0f));
        sigma_db = 4.4f;
    }
    else if (penetration_profile == PENETRATION_HIGH_LOSS)
    {
        wall_loss =
            5.0f
            - 10.0f * std::log10(
                0.7f * std::pow(10.0f, -irr_glass_loss / 10.0f)
                + 0.3f * std::pow(10.0f, -concrete_loss / 10.0f));
        sigma_db = 6.5f;
    }
    return std::max(0.0f, wall_loss + sigma_db * normal_sample);
}

inline float vehicle_penetration_loss_db(
    int vehicle_profile,
    float normal_sample)
{
    const float mean_db =
        vehicle_profile == VEHICLE_METALLIZED ? 20.0f : 9.0f;
    return std::max(0.0f, mean_db + 5.0f * normal_sample);
}
