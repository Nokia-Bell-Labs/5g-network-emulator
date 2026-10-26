#include <simulator/controller_registry.h>

#include <stdexcept>
#include <utility>

namespace
{
struct registration
{
    std::string name;
    controller_factory factory;
};

// Function-local on purpose: registrars in other translation units run during static
// initialisation in an unspecified order, and a namespace-scope vector here could still be
// unconstructed when the first of them calls register_controller().
std::vector<registration> &pending()
{
    static std::vector<registration> list;
    return list;
}
}

void register_controller(std::string name, controller_factory factory)
{
    if (name.empty())
        throw std::invalid_argument("controller name must not be empty");
    if (!factory)
        throw std::invalid_argument("controller '" + name + "' has no factory");

    std::vector<registration> &list = pending();
    for (const registration &r : list)
    {
        if (r.name == name)
            throw std::invalid_argument("controller '" + name + "' is already registered");
    }
    list.push_back({std::move(name), std::move(factory)});
}

std::vector<std::string> registered_controller_names()
{
    std::vector<std::string> names;
    for (const registration &r : pending())
        names.push_back(r.name);
    return names;
}

std::vector<controller_instance> instantiate_controllers(
    const std::string &config_file, const cell_info &cell, const std::vector<ue> &ues)
{
    std::vector<controller_instance> built;
    for (const registration &r : pending())
    {
        std::unique_ptr<controller> c = r.factory(config_file, cell, ues);
        if (c)
            built.push_back({r.name, std::move(c)});
    }
    return built;
}

void clear_registered_controllers()
{
    pending().clear();
}
