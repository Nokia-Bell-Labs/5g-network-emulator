#include <cassert>
#include <fstream>
#include <stdexcept>
#include <string>

#include <map/map_handler.h>
#include <simulator/configuration_loader.h>

namespace
{
void write_file(const std::string &path, const std::string &body)
{
    std::ofstream out(path);
    out << body;
}
} // namespace

int main()
{
    const std::string path = "build/tests/site_space_map.json";
    write_file(
        path,
        "{"
        "\"schema_version\":2,"
        "\"metadata\":{"
        "\"grid_origin\":\"explicit-center-cell\","
        "\"o2i_model\":\"baked-geometric\","
        "\"frequency_ghz\":3.5"
        "},"
        "\"cell_number\":5,"
        "\"cell_size\":10.0,"
        "\"map\":["
        "[0,0,0,0,0],[0,0,0,0,0],[0,0,0,0,0],[0,0,0,0,0],[0,0,0,0,0]"
        "],"
        "\"space\":["
        "[0,0,0,0,0],[0,0,1,0,0],[0,1,2,3,0],[0,0,1,0,0],[0,0,0,0,0]"
        "]"
        "}");

    MapHandler map(path);
    assert(map.hasSpace());
    assert(map.buildingLossBaked());
    assert(map.spaceAt(0.0f, 0.0f) == 2);
    assert(map.spaceAt(6.0f, 0.0f) == 3);
    assert(map.spaceAt(4.0f, 0.0f) == 2);
    assert(map.getMacroFadingValue(0.0f, 0.0f) == 0.0f);
    assert(!map_frequency_disagrees(path, 3.5e9f));
    assert(map_frequency_disagrees(path, 2.6e9f));

    const std::string catalog = "build/tests/site_catalog_map.json";
    write_file(
        catalog,
        "{"
        "\"schema_version\":2,"
        "\"metadata\":{\"grid_origin\":\"explicit-center-cell\"},"
        "\"cell_number\":3,"
        "\"cell_size\":1.0,"
        "\"map\":[[1,2,3],[4,5,6],[7,8,9]]"
        "}");
    MapHandler plain(catalog);
    assert(!plain.hasSpace());
    assert(!plain.buildingLossBaked());
    assert(plain.spaceAt(0.0f, 0.0f) == -1);
    assert(plain.getMacroFadingValue(0.0f, 0.0f) == 5.0f);
    assert(!map_frequency_disagrees(catalog, 3.5e9f));

    const std::string broken = "build/tests/site_space_mismatch.json";
    write_file(
        broken,
        "{"
        "\"schema_version\":2,"
        "\"metadata\":{\"grid_origin\":\"explicit-center-cell\"},"
        "\"cell_number\":3,"
        "\"cell_size\":1.0,"
        "\"map\":[[1,2,3],[4,5,6],[7,8,9]],"
        "\"space\":[[0,1],[2,3]]"
        "}");
    bool rejected = false;
    try
    {
        MapHandler mismatch(broken);
    }
    catch (const std::runtime_error &)
    {
        rejected = true;
    }
    assert(rejected);
    return 0;
}
