#include <cassert>
#include <fstream>
#include <stdexcept>
#include <string>

#include <phy_layer/penetration_model.h>
#include <phy_layer/phy_l_definitions.h>
#include <simulator/configuration_loader.h>

namespace
{
std::string write_config(
    const std::string &name,
    const std::string &key,
    const std::string &value)
{
    const std::string path = "build/tests/" + name + ".ini";
    std::ofstream out(path);
    out << "[UE]\n"
        << "ue_id: location-test\n"
        << "ue_type: 1\n"
        << "n_ues: 1\n"
        << key << ": " << value << "\n"
        << "[Scenario]\n"
        << "scenario_type: 0\n"
        << "[eNBConfig]\n"
        << "frequency: 2380000000\n";
    return path;
}

void expect_location(const std::string &value, int expected)
{
    configuration_loader loader(
        write_config("ue_location_type_" + value, "ue_location_type", value));
    const auto groups = loader.get_ue_c_list();
    assert(groups.size() == 1);
    assert(groups.front().ue_c.ue_m.o2i == expected);
}

void expect_rejected(
    const std::string &name,
    const std::string &key,
    const std::string &value)
{
    bool rejected = false;
    try
    {
        configuration_loader loader(write_config(name, key, value));
    }
    catch (const std::invalid_argument &)
    {
        rejected = true;
    }
    assert(rejected);
}
} // namespace

int main()
{
    expect_location("outdoor", OUTDOOR);
    expect_location("indoor", IN_BUILDING);
    expect_location("vehicle", IN_CAR);
    expect_location("random", LOCATION_RANDOM);

    expect_rejected(
        "ue_location_type_invalid",
        "ue_location_type",
        "underground");
    expect_rejected("removed_location_key", "location", "outdoor");

    configuration_loader legacy(
        write_config("legacy_o2i", "o2i", "11"));
    const auto legacy_groups = legacy.get_ue_c_list();
    assert(legacy_groups.front().ue_c.ue_m.o2i == IN_BUILDING);
    assert(
        legacy_groups.front().ue_c.ue_m.penetration_profile
        == PENETRATION_LEGACY_AUTO);
    return 0;
}
