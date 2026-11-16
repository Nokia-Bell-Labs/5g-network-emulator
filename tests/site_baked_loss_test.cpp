#include <cassert>
#include <cmath>

#include <mac_layer/mac_definitions.h>
#include <phy_layer/penetration_model.h>
#include <phy_layer/phy_config.h>
#include <phy_layer/phy_layer.h>
#include <phy_shared/phy_shared.h>
#include <ue/ue_config.h>

namespace
{
ue_config make_ue(int location, int penetration)
{
    ue_model model(
        1,
        -60.0f,
        ul_power_mode::fixed,
        23.0f,
        0.8f,
        1,
        5,
        5,
        1.5f,
        location);
    model.penetration_profile = penetration;

    ue_config config;
    config.ue_m = model;
    config.delta_metric = 1.0f;
    config.delay_t_metric = 0.1;
    config.beta_metric = 1.0f;
    config.mobility_c.speed = 0.0f;
    config.mobility_c.speed_var = 0.0f;
    config.mobility_c.random_init = false;
    config.mobility_c.random_v = false;
    return config;
}

phy_enb_config make_enb()
{
    phy_enb_config phy(
        46.0f,
        1,
        0.00005f,
        1,
        3.5e9f,
        20000000,
        1,
        METRIC_PF,
        0,
        0,
        3500.0f,
        0.0f,
        -174.0f,
        2.0f,
        9.0f,
        8.7f,
        0.0f,
        2.0f,
        1);
    phy.n_rbgs = 1;
    phy.n_sc_rbg = 12;
    return phy;
}

float additive_loss(int location, int penetration, bool baked)
{
    scenario_config scenario(URBAN_MICROCELL, 10.0f, 10.0f, 25.0f, "unused.json", true);
    ue_config config = make_ue(location, penetration);
    phy_enb_config enb = make_enb();
    phy_shared shared(0, config, scenario, enb);
    phy_layer layer(
        TX_DL,
        0,
        scenario,
        config.get_phy_config(),
        enb,
        false,
        0);
    layer.init(1, enb.bandwidth, 1);
    if (baked)
        layer.set_building_loss_baked(true);
    const pos2d origin(0.0f, 0.0f);
    layer.estimate_channel_state(
        80.0f,
        shared,
        0.0f,
        location,
        TX_DL,
        origin,
        0.0,
        0.0f,
        0.0,
        0.0f);
    return layer.get_additive_loss_db();
}
} // namespace

int main()
{
    const float baked_indoor = additive_loss(IN_BUILDING, PENETRATION_LOW_LOSS, true);
    assert(baked_indoor == 0.0f);

    const float runtime_indoor = additive_loss(IN_BUILDING, PENETRATION_LOW_LOSS, false);
    assert(runtime_indoor > 0.0f);

    scenario_config scenario(URBAN_MICROCELL, 10.0f, 10.0f, 25.0f, "unused.json", true);
    ue_config vehicle_cfg = make_ue(IN_CAR, PENETRATION_NONE);
    phy_enb_config enb = make_enb();
    phy_shared shared(1, vehicle_cfg, scenario, enb);
    phy_layer layer(
        TX_DL,
        1,
        scenario,
        vehicle_cfg.get_phy_config(),
        enb,
        false,
        0);
    layer.init(1, enb.bandwidth, 1);
    layer.set_building_loss_baked(true);
    const pos2d origin(0.0f, 0.0f);
    layer.estimate_channel_state(
        80.0f,
        shared,
        0.0f,
        IN_CAR,
        TX_DL,
        origin,
        0.0,
        0.0f,
        0.0,
        0.0f);
    const float expected = vehicle_penetration_loss_db(
        VEHICLE_STANDARD, shared.get_vehicle_penetration_sample());
    assert(std::fabs(layer.get_additive_loss_db() - expected) < 1e-4f);
    return 0;
}
