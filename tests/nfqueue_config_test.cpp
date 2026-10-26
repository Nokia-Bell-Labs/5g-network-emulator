#include <cassert>
#include <fstream>
#include <stdexcept>
#include <string>

#include <simulator/configuration_loader.h>

namespace
{
void write_config(const std::string &path, const std::string &ues)
{
    std::ofstream out(path);
    out
        << "[Global]\n"
        << "duration: 0.001\n"
        << ues
        << "[Scenario]\n"
        << "map_file: include/maps_scenarios/"
           "macroscopic_fading_map_URBAN_MICROCELL_3.5.json\n";
}

bool rejected(const std::string &path, const std::string &ues)
{
    write_config(path, ues);
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

std::string real_ue(
    const std::string &id,
    int ul_queue,
    int dl_queue)
{
    return
        "[UE]\n"
        "ue_id: " + id + "\n"
        "ue_type: 0\n"
        "n_ues: 1\n"
        "ul_queue_n: " + std::to_string(ul_queue) + "\n"
        "dl_queue_n: " + std::to_string(dl_queue) + "\n";
}
} // namespace

int main()
{
    const std::string base = "build/tests/nfqueue_config_";
    assert(rejected(base + "negative.ini", real_ue("ue1", -1, 101)));
    assert(rejected(base + "same.ini", real_ue("ue1", 100, 100)));
    assert(rejected(
        base + "duplicate.ini",
        real_ue("ue1", 100, 101)
        + real_ue("ue2", 110, 101)));

    const std::string valid = base + "valid.ini";
    write_config(
        valid,
        real_ue("ue1", 100, 101)
        + real_ue("ue2", 110, 111));
    configuration_loader loader(valid);
    assert(loader.get_ue_c_list().size() == 2);
    return 0;
}
