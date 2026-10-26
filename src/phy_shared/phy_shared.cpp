#include <phy_shared/phy_shared.h>
#include <phy_layer/phy_l_definitions.h>
#include <utils/conversions.h>
#include <utils/rng_seed.h>
#include <utils/terminal_logging.h>

phy_shared::phy_shared(int _ue_id, ue_config ue_c, scenario_config _scenario_c, phy_enb_config _phy_enb_config)
    : gen(rng_seed(ue_c.mobility_c.random_v, RNG_PHY_SHARED, (std::uint64_t)_ue_id))
{
    scenario = _scenario_c.type;
    eNB_h = _scenario_c.eNB_h;
    freq_ghz = _phy_enb_config.frequency / GHZ2HZ;
    o2i = (ue_c.ue_m.o2i == 13) ? compute_outdoor_to_indoor() : ue_c.ue_m.o2i; 
    o2i = verify_outdoor_to_indoor();
    correlation_distance = get_correlation_distance();
    building_penetration_sample = normal_stochastics(gen);
    vehicle_penetration_sample = normal_stochastics(gen);
    const float maximum_depth =
        scenario == RURAL_MACROCELL
            ? 10.0f
            : (
                  scenario == URBAN_MICROCELL
                          || scenario == URBAN_MACROCELL
                      ? 25.0f
                      : 0.0f);
    for (float &candidate : indoor_depth_candidates)
    {
        const float first = uniform_stochastics(gen) * maximum_depth;
        const float second = uniform_stochastics(gen) * maximum_depth;
        candidate = std::min(first, second);
    }
}

int phy_shared::get_o2i(){
    return o2i;
}

float phy_shared::get_building_penetration_sample() const
{
    return building_penetration_sample;
}

float phy_shared::get_vehicle_penetration_sample() const
{
    return vehicle_penetration_sample;
}

float phy_shared::get_indoor_depth(float distance) const
{
    if (distance <= 0.0f)
        return 0.0f;
    for (float candidate : indoor_depth_candidates)
    {
        if (candidate < distance)
            return candidate;
    }
    return std::min(indoor_depth_candidates.back(), distance);
}

int phy_shared::compute_outdoor_to_indoor()
{
    float random = uniform_stochastics(gen);

    switch (scenario)
    {
    case RURAL_MACROCELL:
        return (random <= 0.5) ? IN_BUILDING : IN_CAR;

    case URBAN_MICROCELL:
    case URBAN_MACROCELL:
        return (random <= 0.8) ? IN_BUILDING : OUTDOOR;

    default:
        return OUTDOOR;
    }
}

int phy_shared::verify_outdoor_to_indoor()
{
    // Explicit canonical environment states are never rewritten based on the
    // scenario. The scenario-specific probabilities apply only to `random`.
    if (o2i == OUTDOOR || o2i == IN_BUILDING || o2i == IN_CAR)
        return o2i;

    return compute_outdoor_to_indoor();
}

float phy_shared::get_correlation_distance()
{
    switch (scenario)
    {
    case RURAL_MACROCELL:
        return (o2i != OUTDOOR) ? 120 : 37;

    case URBAN_MACROCELL:
        return (o2i != OUTDOOR) ? 7 : 37;

    case URBAN_MICROCELL:
        return (o2i != OUTDOOR) ? 7 : 10;

    case INDOOR_OPEN_OFFICE:
    case INDOOR_MIXED_OFFICE:
        return 6;

    case INDOOR_SHOPPING_MALL:
        return 10;

    default:
        return 0; 
    }
}

// getMacroFading removed from phy_shared: callers should use MapHandler directly from ue
