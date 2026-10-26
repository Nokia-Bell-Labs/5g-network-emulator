#include <cassert>
#include <fstream>
#include <stdexcept>
#include <string>

#include <simulator/configuration_loader.h>

namespace
{
void write_config(
    const std::string &path,
    const std::string &mac,
    const std::string &phy = "")
{
    std::ofstream out(path);
    out << "[Global]\n"
        << "duration: 0.001\n"
        << "[Scenario]\n"
        << "scenario_type: 1\n"
        << "[eNBConfig]\n"
        << "frequency: 3500000000\n"
        << "[MACLayer]\n"
        << mac
        << "[PHYLayer]\n"
        << phy;
}

bool rejected(const std::string &path)
{
    try
    {
        configuration_loader loader(path);
        (void)loader;
    }
    catch (const std::invalid_argument &)
    {
        return true;
    }
    return false;
}
} // namespace

int main()
{
    const std::string active_path =
        "build/tests/harq_config_active.ini";
    write_config(
        active_path,
        "harq_model: legacy_bler\n"
        "mcs_tables: true\n"
        "max_rtx_ul: 0\n"
        "max_rtx_dl: 1\n",
        "rtx_proc_delay_ul: 0.002\n"
        "rtx_proc_delay_var_ul: 0.007\n");
    configuration_loader active(active_path);
    const pdcp_config active_ul = active.get_pdcp_config_ul();
    const pdcp_config active_dl = active.get_pdcp_config_dl();
    assert(active_ul.model == harq_model::legacy_bler);
    assert(active_ul.max_rtx == 0);
    assert(active_dl.max_rtx == 1);
    assert(active_ul.rtx_proc_delay == 0.002f);
    assert(active_ul.rtx_proc_delay_var == 0.007f);

    const std::string disabled_path =
        "build/tests/harq_config_disabled.ini";
    write_config(
        disabled_path,
        "harq_model: disabled\n"
        "mcs_tables: false\n");
    configuration_loader disabled(disabled_path);
    assert(
        disabled.get_pdcp_config_ul().model
        == harq_model::disabled);

    const std::string incompatible_path =
        "build/tests/harq_config_incompatible.ini";
    write_config(
        incompatible_path,
        "harq_model: legacy_bler\n"
        "mcs_tables: false\n");
    assert(rejected(incompatible_path));

    const std::string negative_retry_path =
        "build/tests/harq_config_negative_retry.ini";
    write_config(
        negative_retry_path,
        "harq_model: legacy_bler\n"
        "mcs_tables: true\n"
        "max_rtx_ul: -1\n");
    assert(rejected(negative_retry_path));

    const std::string negative_delay_path =
        "build/tests/harq_config_negative_delay.ini";
    write_config(
        negative_delay_path,
        "harq_model: legacy_bler\n"
        "mcs_tables: true\n",
        "rtx_period_dl: -0.001\n");
    assert(rejected(negative_delay_path));

    return 0;
}
