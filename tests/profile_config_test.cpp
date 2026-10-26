#include <cassert>
#include <cmath>
#include <fstream>
#include <list>
#include <stdexcept>
#include <string>

#include <common/direction.h>
#include <mac_layer/resource_grid.h>
#include <simulator/configuration_loader.h>

namespace
{
bool near(double actual, double expected, double tolerance = 1e-4)
{
    return std::fabs(actual - expected) <= tolerance;
}

struct expected_profile
{
    const char *path;
    int scenario;
    double frequency_hz;
    int bandwidth_hz;
    int numerology;
    double tx_power_dbm;
    double enb_gain_dbi;
    double ue_gain_dbi;
    double enb_nf_db;
    double ue_nf_db;
    int frequency_rbs;
    int rbg_size;
    int frequency_rbgs;
    int o2i;
    const char *map_suffix;
    bool throughput_intra_tti_update = false;
    int penetration_profile = PENETRATION_NONE;
    int vehicle_profile = VEHICLE_STANDARD;
};

void check_profile(const expected_profile &expected)
{
    configuration_loader loader(expected.path);
    const phy_enb_config phy = loader.get_phy_enb_config();
    const mac_config mac = loader.get_mac_config();
    const scenario_config scenario = loader.get_scenario_config();

    assert(scenario.type == expected.scenario);
    assert(near(phy.frequency, expected.frequency_hz, 4096.0));
    assert(phy.bandwidth == expected.bandwidth_hz);
    assert(mac.bandwidth == expected.bandwidth_hz);
    assert(mac.numerology == expected.numerology);
    assert(near(mac.pf_alpha, 1.0));
    assert(near(mac.throughput_time_window_ms, 100.0));
    assert(
        mac.throughput_intra_tti_update
        == expected.throughput_intra_tti_update);
    assert(near(phy.tx_power, expected.tx_power_dbm));
    assert(near(phy.eNB_gain, expected.enb_gain_dbi));
    assert(near(phy.UT_gain, expected.ue_gain_dbi));
    assert(near(phy.figure_noise_enb, expected.enb_nf_db));
    assert(near(phy.figure_noise_ut, expected.ue_nf_db));
    assert(
        scenario.map_file.size() >= std::string(expected.map_suffix).size()
        && scenario.map_file.compare(
               scenario.map_file.size() - std::string(expected.map_suffix).size(),
               std::string(expected.map_suffix).size(),
               expected.map_suffix)
               == 0);

    const std::list<ue_full_config> ue_groups = loader.get_ue_c_list();
    assert(!ue_groups.empty());
    for (const ue_full_config &group : ue_groups)
    {
        assert(group.ue_c.ue_m.o2i == expected.o2i);
        if (mac.metric_type == METRIC_PF)
            assert(!group.ue_c.beta_metric_configured);
        assert(
            group.ue_c.ue_m.penetration_profile
            == expected.penetration_profile);
        assert(group.ue_c.ue_m.vehicle_profile == expected.vehicle_profile);
    }

    grid shape(
        TX_DL,
        mac.mimo_layers,
        mac.numerology,
        mac.n_re_freq,
        mac.n_ofdm_syms,
        mac.metric_type,
        mac.bandwidth,
        mac.scheduling_mode,
        mac.scheduling_type,
        mac.scheduling_config,
        mac.duplexing_type,
        mac.ratio_DL_UL,
        loader.get_tdd_config());

    assert(shape.get_n_freq_rb() == expected.frequency_rbs);
    assert(shape.get_rbg_size() == expected.rbg_size);
    assert(shape.get_n_freq_rbg() == expected.frequency_rbgs);
    assert(shape.get_n_time_rb() == (1 << expected.numerology));
}

void check_no_l4s_study(
    const char *path,
    const char *expected_study_id)
{
    configuration_loader loader(path);
    const std::list<ue_full_config> groups =
        loader.get_ue_c_list();
    assert(!groups.empty());
    assert(groups.front().id == expected_study_id);
    for (const ue_full_config &group : groups)
        assert(!group.ue_c.l4s_c.enabled);
}
} // namespace

int main()
{
    configuration_loader defaults;
    assert(defaults.get_mac_config().mimo_layers == 1);
    ue_full_config default_ue;
    assert(default_ue.ue_c.ue_m.n_antennas == 1);

    const expected_profile profiles[] = {
        {
            "config/offline_umi_n40_npn.ini",
            URBAN_MICROCELL,
            2.38e9,
            20000000,
            1,
            43.0,
            8.7,
            0.0,
            2.0,
            9.0,
            50,
            8,
            6,
            OUTDOOR,
            "macroscopic_fading_map_URBAN_MICROCELL_2.38.json",
            true,
        },
        {
            "config/offline_uma_n78_pedestrian.ini",
            URBAN_MACROCELL,
            3.5e9,
            100000000,
            1,
            46.0,
            8.7,
            0.0,
            2.0,
            9.0,
            273,
            16,
            17,
            OUTDOOR,
            "macroscopic_fading_map_URBAN_MACROCELL_3.5.json",
            true,
        },
        {
            "config/offline_rural_n78_vehicular.ini",
            RURAL_MACROCELL,
            3.5e9,
            100000000,
            1,
            46.0,
            8.7,
            0.0,
            2.0,
            9.0,
            273,
            1,
            273,
            IN_CAR,
            "macroscopic_fading_map_RURAL_MACROCELL_3.5.json",
        },
        {
            "config/offline_indoor_hotspot_n78_pedestrian.ini",
            INDOOR_OPEN_OFFICE,
            3.5e9,
            100000000,
            1,
            24.0,
            8.7,
            0.0,
            2.0,
            9.0,
            273,
            16,
            17,
            IN_BUILDING,
            "macroscopic_fading_map_INDOOR_OPEN_OFFICE_3.5.json",
            true,
        },
        {
            "config/emulated_rural_n78_single_with_background.ini",
            RURAL_MACROCELL,
            3.5e9,
            100000000,
            1,
            46.0,
            8.7,
            0.0,
            2.0,
            9.0,
            273,
            1,
            273,
            IN_CAR,
            "macroscopic_fading_map_RURAL_MACROCELL_3.5.json",
        },
        {
            "config/offline_umi_n258_fwa.ini",
            URBAN_MICROCELL,
            26e9,
            400000000,
            3,
            35.0,
            24.0,
            26.0,
            7.0,
            10.0,
            250,
            16,
            15,
            OUTDOOR,
            "macroscopic_fading_map_URBAN_MICROCELL_26.json",
            true,
        },
        {
            "config/offline_umi_n258_fwa_high_loss.ini",
            URBAN_MICROCELL,
            26e9,
            400000000,
            3,
            35.0,
            24.0,
            26.0,
            7.0,
            10.0,
            250,
            16,
            15,
            IN_BUILDING,
            "macroscopic_fading_map_URBAN_MICROCELL_26.json",
            true,
            PENETRATION_HIGH_LOSS,
        },
    };

    for (const expected_profile &profile : profiles)
        check_profile(profile);

    check_no_l4s_study(
        "config/offline_umi_n40_npn.ini",
        "studyNPN");
    check_no_l4s_study(
        "config/offline_uma_n78_pedestrian.ini",
        "studyPedestrian");
    check_no_l4s_study(
        "config/offline_rural_n78_vehicular.ini",
        "studyVehicular");
    check_no_l4s_study(
        "config/offline_indoor_hotspot_n78_pedestrian.ini",
        "studyIndoor");
    check_no_l4s_study(
        "config/offline_umi_n258_fwa.ini",
        "studyFWA");
    check_no_l4s_study(
        "config/emulated_rural_n78_single_with_background.ini",
        "capturedStudy");

    const std::string reordered_path =
        "build/tests/profile_config_reordered.ini";
    std::ofstream reordered(reordered_path);
    reordered
        << "[eNBConfig]\n"
        << "frequency: 26000000000\n"
        << "bandwidth: 400000000\n"
        << "[Scenario]\n"
        << "scenario_type: 0\n";
    reordered.close();
    configuration_loader reordered_loader(reordered_path);
    assert(
        reordered_loader.get_scenario_config().map_file.find(
            "URBAN_MICROCELL_26.json")
        != std::string::npos);

    const std::string explicit_path =
        "build/tests/profile_config_explicit_map.ini";
    std::ofstream explicit_config(explicit_path);
    explicit_config
        << "[Scenario]\n"
        << "scenario_type: 0\n"
        << "map_file: results/maps-v2-candidates/"
        << "macroscopic_fading_map_URBAN_MICROCELL_2.38.json\n"
        << "[eNBConfig]\n"
        << "frequency: 2380000000\n";
    explicit_config.close();
    configuration_loader explicit_loader(explicit_path);
    assert(
        explicit_loader.get_scenario_config().map_file
        == "results/maps-v2-candidates/"
           "macroscopic_fading_map_URBAN_MICROCELL_2.38.json");

    const std::string asymmetric_fdd_path =
        "build/tests/profile_config_asymmetric_fdd.ini";
    std::ofstream asymmetric_fdd(asymmetric_fdd_path);
    asymmetric_fdd
        << "[MACLayer]\n"
        << "duplexing_type: 1\n"
        << "ratio_DL_UL: 0.3\n";
    asymmetric_fdd.close();
    bool asymmetric_rejected = false;
    try
    {
        configuration_loader asymmetric_loader(asymmetric_fdd_path);
        (void)asymmetric_loader.get_mac_config();
    }
    catch (const std::invalid_argument &)
    {
        asymmetric_rejected = true;
    }
    assert(asymmetric_rejected);

    const std::string nearest_rejected_path =
        "build/tests/profile_config_nearest_rejected.ini";
    std::ofstream nearest_rejected(nearest_rejected_path);
    nearest_rejected
        << "[Scenario]\n"
        << "scenario_type: 0\n"
        << "[eNBConfig]\n"
        << "frequency: 2500000000\n";
    nearest_rejected.close();
    bool nearest_rejected_by_default = false;
    try
    {
        configuration_loader rejected_loader(nearest_rejected_path);
    }
    catch (const std::invalid_argument &)
    {
        nearest_rejected_by_default = true;
    }
    assert(nearest_rejected_by_default);

    const std::string nearest_allowed_path =
        "build/tests/profile_config_nearest_allowed.ini";
    std::ofstream nearest_allowed(nearest_allowed_path);
    nearest_allowed
        << "[Scenario]\n"
        << "scenario_type: 0\n"
        << "allow_nearest_map_fallback: true\n"
        << "[eNBConfig]\n"
        << "frequency: 2500000000\n";
    nearest_allowed.close();
    configuration_loader allowed_loader(nearest_allowed_path);
    assert(
        allowed_loader.get_scenario_config().map_file.find(
            "URBAN_MICROCELL_2.38.json")
        != std::string::npos);

    return 0;
}
