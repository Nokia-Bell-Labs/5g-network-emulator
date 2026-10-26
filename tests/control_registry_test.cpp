// Catalogue of runtime control knobs: names, units, ranges and unit conversions.
// Same style as pdcp_flow_test.cpp: <cassert> and nothing else.
#include <cassert>
#include <cmath>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <simulator/configuration_loader.h>
#include <ue/ue.h>
#include <utils/control/param_registry.h>

namespace
{
bool near(double a, double b, double tol = 1e-4)
{
    return std::fabs(a - b) < tol;
}

// A UE cheap enough to build in a test: no logging, no map, simulated traffic.
ue make_ue(int id)
{
    // ue_model / phy_enb_config have plain uninitialized members: outside of
    // configuration_loader every field the PHY reads has to be set by hand, or the MCS
    // table lookup indexes with garbage.
    ue_model model(1 /*n_antennas*/, -60.0f /*nominal_pusch_p0*/, ul_power_mode::fixed,
                   23.0f /*tx_power_ul*/, 0.8f /*alpha_ul*/, 1 /*scaling_factor*/,
                   5 /*cqi_period*/, 5 /*ri_period*/, 1.5f /*ue_h*/, 10 /*o2i*/);

    ue_config ue_c;
    ue_c.ue_m = model;
    ue_c.delta_metric = 1.0f;
    ue_c.delay_t_metric = 0.1f;
    ue_c.beta_metric = 0.5f;
    ue_c.priority = 3.0f;
    ue_c.traffic_c.ul_target = 1.0f;
    ue_c.traffic_c.dl_target = 2.0f;
    ue_c.traffic_c.pkt_size = 12000;
    ue_c.mobility_c.init_pos = pos2d(100.0f, 0.0f);
    ue_c.mobility_c.max_distance = 5000.0f;
    ue_c.mobility_c.speed = 10.0f;

    // Real map: without it MapHandler leaves the apothem at 0 and every position gets
    // clamped to the origin. make test runs from the repo root.
    scenario_config scenario_c(URBAN_MACROCELL, 20.0f, 10.0f, 25.0f,
                               getBaseMapPath() + "URBAN_MACROCELL_3.5.json", true);

    phy_enb_config phy_c(46.0f /*tx_power*/, 1 /*modulation_m*/, 0.00005f /*target_ber*/,
                         1 /*cqi_mode*/, 3.5e9f /*frequency*/, 20000000 /*bandwidth*/,
                         1 /*mimo_l*/, METRIC_PF, 0 /*n_int_ues*/, 0 /*n_int_eNBs*/,
                         3500.0f /*d_interference*/, 0.1f /*interfered_ratio*/,
                         -174.0f /*thermal_noise*/, 2.0f /*figure_noise_enb*/,
                         9.0f /*figure_noise_ut*/, 8.7f /*eNB_gain*/, 0.0f /*UT_gain*/,
                         2.0f /*power_boost*/, 1 /*numerology*/);
    phy_c.n_rbgs = 4;
    phy_c.n_sc_rbg = 12;

    pdcp_config pdcp_c;
    harq_config harq_c(1, 1, 1);

    return ue(id, ue_c, scenario_c, phy_c, pdcp_c, pdcp_c, harq_c, SIM_UE, nullptr, false);
}

void test_catalogue_shape()
{
    const param_registry &reg = param_registry::instance();

    // The catalogue is deliberately small: eleven knobs, no more, until the plan says so.
    assert(reg.entries().size() == 14);

    const char *expected[] = {
        "priority", "enabled",
        "dl.rmax_mbps", "ul.rmax_mbps",
        "dl.sinr_offset_db", "ul.sinr_offset_db",
        "traffic.dl_target_mbps", "traffic.ul_target_mbps",
        "mobility.pos_x_m", "mobility.pos_y_m", "mobility.speed_kmh",
        "dl.inject_bytes", "ul.inject_bytes", "pkt_delay_budget_s"
    };
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
        assert(reg.find(expected[i]) != nullptr);

    assert(reg.find("rmax_mbps") == nullptr);      // no unprefixed directional form
    assert(reg.find("speed") == nullptr);          // no name without its unit suffix
    assert(reg.find("does_not_exist") == nullptr);

    // inject_bytes is the only incremental knob, and describe has to say so.
    assert(reg.find("dl.inject_bytes")->incremental);
    assert(!reg.find("priority")->incremental);

    // Every name either carries a unit suffix or is dimensionless by construction.
    for (size_t i = 0; i < reg.entries().size(); i++)
    {
        const param_entry &e = reg.entries()[i];
        const bool dimensionless = (e.name == "priority" || e.name == "enabled");
        assert(dimensionless == e.unit.empty());
    }
}

void test_range_validation()
{
    const param_registry &reg = param_registry::instance();
    std::string reason;

    const param_entry *sinr = reg.find("dl.sinr_offset_db");
    assert(reg.check(*sinr, param_value(0.0), reason));
    assert(reg.check(*sinr, param_value(-50.0), reason));
    assert(reg.check(*sinr, param_value(50.0), reason));
    assert(!reg.check(*sinr, param_value(50.1), reason));
    assert(!reg.check(*sinr, param_value(-50.1), reason));
    assert(!reason.empty());

    const param_entry *prio = reg.find("priority");
    assert(reg.check(*prio, param_value(0.0), reason));
    assert(!reg.check(*prio, param_value(-1.0), reason));
    assert(!reg.check(*prio, param_value(std::nan("")), reason));
    assert(!reg.check(*prio, param_value(std::string("mucha")), reason));

    const param_entry *en = reg.find("enabled");
    assert(reg.check(*en, param_value(true), reason));
    assert(!reg.check(*en, param_value(1.0), reason));   // a bool knob takes a bool
}

void test_apply_and_units()
{
    ue u = make_ue(0);
    const param_registry &reg = param_registry::instance();
    std::string reason;

    // priority is absolute: it starts at the .ini value and gets replaced.
    assert(near(u.overrides().priority, 3.0));
    assert(reg.find("priority")->apply(u, param_value(7.5), reason));
    assert(near(u.overrides().priority, 7.5));

    // 36 km/h must land as 10 m/s internally, and read back as 36.
    assert(reg.find("mobility.speed_kmh")->apply(u, param_value(36.0), reason));
    assert(near(u.mobility().get_speed(), 10.0));
    assert(near(reg.find("mobility.speed_kmh")->read(u), 36.0));

    // Mbps in, bits per second inside.
    assert(reg.find("dl.rmax_mbps")->apply(u, param_value(25.0), reason));
    assert(near(u.overrides().rmax_bps[TX_DL], 25e6, 1.0));
    assert(near(reg.find("dl.rmax_mbps")->read(u), 25.0));
    // A cap coming into force starts with one TTI worth of tokens, no accumulated burst.
    assert(near(u.overrides().rmax_tokens[TX_DL], 25e6 * 0.001, 1.0));

    assert(reg.find("traffic.ul_target_mbps")->apply(u, param_value(4.0), reason));
    assert(near(reg.find("traffic.ul_target_mbps")->read(u), 4.0));

    // Directions are independent.
    assert(reg.find("ul.sinr_offset_db")->apply(u, param_value(-12.0), reason));
    assert(near(u.overrides().sinr_offset_db[TX_UL], -12.0));
    assert(near(u.overrides().sinr_offset_db[TX_DL], 0.0));

    assert(reg.find("mobility.pos_x_m")->apply(u, param_value(-250.0), reason));
    assert(near(u.mobility().x(), -250.0));
    assert(near(u.mobility().y(), 0.0));

    // Injection is incremental and its read is the cumulative total, which is what lets
    // a client tell whether a retried injection actually landed.
    assert(reg.find("dl.inject_bytes")->apply(u, param_value(1500.0), reason));
    assert(reg.find("dl.inject_bytes")->apply(u, param_value(3000.0), reason));
    assert(near(reg.find("dl.inject_bytes")->read(u), 4500.0));
    assert(near(reg.find("ul.inject_bytes")->read(u), 0.0));

    assert(reg.find("pkt_delay_budget_s")->apply(u, param_value(2.0), reason));
    assert(near(reg.find("pkt_delay_budget_s")->read(u), 2.0));

    assert(reg.find("enabled")->apply(u, param_value(false), reason));
    assert(!u.is_enabled());
    assert(reg.find("enabled")->apply(u, param_value(true), reason));
    assert(u.is_enabled());
}

void test_extremes_do_not_crash()
{
    ue u = make_ue(1);
    const param_registry &reg = param_registry::instance();
    std::string reason;

    for (size_t i = 0; i < reg.entries().size(); i++)
    {
        const param_entry &e = reg.entries()[i];
        if (e.type == param_type::boolean)
        {
            assert(e.apply(u, param_value(true), reason));
            assert(e.apply(u, param_value(false), reason));
            continue;
        }
        // Unbounded ends are exercised with a large but sane magnitude; positions get
        // clamped to the apothem by the mobility model anyway.
        const double lo = std::isinf(e.min) ? -1e6 : e.min;
        const double hi = std::isinf(e.max) ? 1e6 : e.max;
        assert(e.apply(u, param_value(lo), reason));
        assert(e.apply(u, param_value(hi), reason));
        (void)e.read(u);
    }
}

void test_describe_is_valid_json()
{
    const std::string text = param_registry::instance().describe_json();
    nlohmann::json j = nlohmann::json::parse(text);
    assert(j.is_array());
    assert(j.size() == 14);

    bool found_speed = false;
    for (size_t i = 0; i < j.size(); i++)
    {
        assert(j[i].contains("name"));
        assert(j[i].contains("type"));
        assert(j[i].contains("unit"));
        assert(j[i].contains("description"));
        if (j[i]["name"] == "mobility.speed_kmh")
        {
            found_speed = true;
            assert(j[i]["unit"] == "km/h");
            assert(j[i]["type"] == "number");
        }
    }
    assert(found_speed);
}
}

int main()
{
    test_catalogue_shape();
    test_range_validation();
    test_apply_and_units();
    test_extremes_do_not_crash();
    test_describe_is_valid_json();
    return 0;
}
