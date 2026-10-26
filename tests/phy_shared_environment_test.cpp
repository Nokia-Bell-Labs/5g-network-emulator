#include <cassert>
#include <cmath>

#include <phy_shared/phy_shared.h>
#include <utils/rng_seed.h>

int main()
{
    set_rng_run_seed(20260927);

    ue_config terminal;
    terminal.mobility_c.random_v = true;
    terminal.ue_m.o2i = IN_BUILDING;
    scenario_config scenario(
        URBAN_MICROCELL,
        20.0f,
        10.0f,
        10.0f,
        "unused.json");
    phy_enb_config cell;
    cell.frequency = 3.5e9f;

    phy_shared first(7, terminal, scenario, cell);
    phy_shared repeated(7, terminal, scenario, cell);

    assert(
        first.get_building_penetration_sample()
        == repeated.get_building_penetration_sample());
    assert(
        first.get_vehicle_penetration_sample()
        == repeated.get_vehicle_penetration_sample());
    assert(first.get_indoor_depth(100.0f) == repeated.get_indoor_depth(100.0f));
    assert(first.get_indoor_depth(100.0f) == first.get_indoor_depth(100.0f));
    assert(first.get_indoor_depth(100.0f) >= 0.0f);
    assert(first.get_indoor_depth(100.0f) <= 25.0f);
    assert(first.get_indoor_depth(0.0f) == 0.0f);

    terminal.ue_m.o2i = IN_CAR;
    phy_shared umi_vehicle(8, terminal, scenario, cell);
    assert(umi_vehicle.get_o2i() == IN_CAR);

    terminal.ue_m.o2i = IN_BUILDING;
    scenario.type = INDOOR_SHOPPING_MALL;
    phy_shared shopping_indoor(9, terminal, scenario, cell);
    assert(shopping_indoor.get_o2i() == IN_BUILDING);

    scenario.type = URBAN_MICROCELL;
    double depth_sum = 0.0;
    double normal_sum = 0.0;
    double normal_squared_sum = 0.0;
    constexpr int samples = 1000;
    for (int index = 0; index < samples; index++)
    {
        phy_shared sample(100 + index, terminal, scenario, cell);
        const float depth = sample.get_indoor_depth(100.0f);
        const float normal = sample.get_building_penetration_sample();
        depth_sum += depth;
        normal_sum += normal;
        normal_squared_sum += normal * normal;
    }
    const double depth_mean = depth_sum / samples;
    const double normal_mean = normal_sum / samples;
    const double normal_variance =
        normal_squared_sum / samples - normal_mean * normal_mean;
    assert(std::fabs(depth_mean - 25.0 / 3.0) < 0.6);
    assert(std::fabs(normal_mean) < 0.1);
    assert(std::fabs(normal_variance - 1.0) < 0.15);
    return 0;
}
