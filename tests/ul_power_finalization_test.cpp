#include <cassert>
#include <cmath>

#include <mac_layer/mac_definitions.h>
#include <phy_layer/phy_config.h>
#include <simulator/configuration_loader.h>
#include <ue/ue.h>

namespace
{
bool near(float actual, float expected, float tolerance = 1e-2f)
{
    return std::fabs(actual - expected) <= tolerance;
}

ue make_test_ue(ul_power_mode mode = ul_power_mode::fixed)
{
    ue_model model(
        1,
        -60.0f,
        mode,
        23.0f,
        0.8f,
        1,
        5,
        5,
        1.5f,
        OUTDOOR);

    ue_config ue_cfg;
    ue_cfg.ue_m = model;
    ue_cfg.delta_metric = 1.0f;
    ue_cfg.delay_t_metric = 0.1f;
    ue_cfg.beta_metric = 1.0f;
    ue_cfg.priority = 1.0f;
    ue_cfg.traffic_c.ul_target = 1.0f;
    ue_cfg.traffic_c.dl_target = 1.0f;
    ue_cfg.traffic_c.pkt_size = 12000;
    ue_cfg.mobility_c.init_pos = pos2d(100.0f, 0.0f);
    ue_cfg.mobility_c.random_init = false;
    ue_cfg.mobility_c.max_distance = 1000.0f;
    ue_cfg.mobility_c.speed = 0.0f;

    scenario_config scenario(
        URBAN_MACROCELL,
        20.0f,
        10.0f,
        25.0f,
        getBaseMapPath() + "URBAN_MACROCELL_3.5.json",
        true);

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
    phy.n_rbgs = 4;
    phy.n_sc_rbg = 12;

    pdcp_config pdcp;
    harq_config harq(1, 1, 1);
    return ue(
        0,
        ue_cfg,
        scenario,
        phy,
        pdcp,
        pdcp,
        harq,
        SIM_UE,
        nullptr,
        false);
}
} // namespace

int main()
{
    ue terminal = make_test_ue();
    terminal.init();
    terminal.add_current_t(0.0);
    terminal.step();

    assert(near(terminal.get_ul_tx_power_per_prb_dbm(), 23.0f));

    terminal.finalize_ul_allocation(4);
    assert(terminal.get_ul_scheduling_prbs() == 4);
    assert(near(terminal.get_ul_tx_power_per_prb_dbm(), 16.979f));

    terminal.finalize_ul_allocation(16);
    assert(terminal.get_ul_scheduling_prbs() == 16);
    assert(near(terminal.get_ul_tx_power_per_prb_dbm(), 10.959f));

    terminal.finalize_ul_allocation(0);
    assert(terminal.get_ul_scheduling_prbs() == 1);
    assert(near(terminal.get_ul_tx_power_per_prb_dbm(), 23.0f));

    ue fractional = make_test_ue(ul_power_mode::fractional);
    fractional.init();
    fractional.add_current_t(0.0);
    fractional.step();
    fractional.finalize_ul_allocation(4);
    const float four_prb_power =
        fractional.get_ul_tx_power_per_prb_dbm();
    fractional.finalize_ul_allocation(16);
    const float sixteen_prb_power =
        fractional.get_ul_tx_power_per_prb_dbm();
    assert(four_prb_power <= 23.0f);
    assert(sixteen_prb_power <= four_prb_power);
    assert(
        sixteen_prb_power
            + 10.0f * std::log10(16.0f)
        <= 23.0f + 1e-2f);

    return 0;
}
