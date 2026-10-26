// Controller registry: a per-TTI extension point called by simulator::step() after the control
// plane and before the MAC. Driven through simulator::run_steps, so the TTI counter is exactly
// the step index. The last test records how configuration_loader treats what it does not know,
// because a controller that reads a section of its own depends on it.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <mac_layer/mac_definitions.h>
#include <simulator/controller_registry.h>
#include <simulator/simulator.h>    // brings in configuration_loader.h, which has no include guard
#include <ue/ue.h>

namespace
{
const char *TIMELINE = "build/tests/controller_registry_timeline.ndjson";
const char *CONFIG = "build/tests/controller_registry.ini";
const char *MAP = "build/tests/controller_registry_map.json";

// Priority of ue/0 goes from 1 to 8 at TTI 10.
const char *TIMELINE_TEXT =
    "{\"id\":1,\"at_tti\":10,\"cmds\":[{\"target\":\"ue/0\",\"set\":{\"priority\":8.0}}]}\n";

bool near(double a, double b, double tol = 1e-9)
{
    return std::fabs(a - b) < tol;
}

void write_file(const char *path, const std::string &text)
{
    std::ofstream out(path);
    assert(out.is_open());
    out << text;
}

// The smoke scenario with the timeline swapped, the control plane switched on or off, and
// optionally a few extra lines after a given line.
void write_config(bool control_enabled, const std::string &after_line = "",
                  const std::string &inserted = "")
{
    std::ifstream base("tests/control_smoke.ini");
    assert(base.is_open());
    std::string line, text;
    while (std::getline(base, line))
    {
        if (line.rfind("timeline_file:", 0) == 0) line = std::string("timeline_file: ") + TIMELINE;
        if (line == "enabled: true" && !control_enabled) line = "enabled: false";
        text += line + "\n";
        if (line.rfind("scenario_type:", 0) == 0)
            text += std::string("map_file: ") + MAP + "\n";
        if (!after_line.empty() && line == after_line)
            text += inserted;
    }
    write_file(CONFIG, text);
}

// What a UE shows to a reader of the const list. The throughput average is written by the MAC
// step and the delivered bits by the UE step, so a snapshot taken at the wrong point of the
// TTI differs from the one taken at the right point.
struct ue_state
{
    float priority;
    float avg_tp_dl;
    std::uint64_t delivered_dl;
    bool operator==(const ue_state &o) const
    {
        return priority == o.priority && avg_tp_dl == o.avg_tp_dl && delivered_dl == o.delivered_dl;
    }
};

std::vector<ue_state> snapshot(const std::vector<ue> &ues)
{
    std::vector<ue_state> s;
    for (const ue &u : ues)
        s.push_back({u.overrides().priority, u.get_throughput_average_bits_per_tti(TX_DL),
                     u.get_delivered_bits_total(TX_DL)});
    return s;
}

// Everything the test controllers write down, in call order.
struct call_record
{
    std::string name;
    std::uint64_t tti;
    double sim_t;
    std::size_t n_ues;
    std::vector<ue_state> state;
};
std::vector<call_record> g_calls;
std::map<std::string, int> g_constructed;
std::vector<std::string> g_construction_order;

struct probe : public controller
{
    std::string name_;
    explicit probe(std::string n) : name_(std::move(n)) {}
    void on_tick(const tick_context &ctx) override
    {
        g_calls.push_back({name_, ctx.tti, ctx.sim_t, ctx.ues.size(), snapshot(ctx.ues)});
    }
};

controller_factory probe_factory(const std::string &name)
{
    return [name](const std::string &, const cell_info &, const std::vector<ue> &)
    {
        g_constructed[name]++;
        g_construction_order.push_back(name);
        return std::unique_ptr<controller>(new probe(name));
    };
}

void reset_probes()
{
    g_calls.clear();
    g_constructed.clear();
    g_construction_order.clear();
}

// A registrar at file scope, which is how an extension is expected to register itself.
struct static_registrar
{
    static_registrar() { register_controller("static_probe", probe_factory("static_probe")); }
} g_static_registrar;

std::vector<call_record> calls_of(const std::string &name)
{
    std::vector<call_record> out;
    for (const call_record &c : g_calls)
        if (c.name == name) out.push_back(c);
    return out;
}

// The registrar above ran before main, and the list it fills survives until it is cleared.
void test_file_scope_registration()
{
    const std::vector<std::string> names = registered_controller_names();
    assert(names.size() == 1);
    assert(names[0] == "static_probe");
}

// (b) and the ordering: built once, called once per TTI with a monotonic clock, after the
// control plane and before the MAC.
void test_controller_contract()
{
    const unsigned int n = 40;

    // Reference run, nothing registered: the state after k steps, k = 0..n.
    std::vector<std::vector<ue_state>> after;
    {
        std::vector<std::string> keep = registered_controller_names();
        assert(keep.size() == 1);
        clear_registered_controllers();
        write_file(TIMELINE, TIMELINE_TEXT);
        write_config(true);
        simulator ref(CONFIG);
        after.push_back(snapshot(*ref.ue_list()));
        for (unsigned int i = 0; i < n; i++)
        {
            ref.run_steps(1);
            after.push_back(snapshot(*ref.ue_list()));
        }
        assert(ref.steps_done() == n);
    }
    // The snapshot has to move from step to step, or "taken before the MAC" and "taken after
    // it" would be the same thing and the comparison below would prove nothing.
    unsigned int tp_moved = 0, delivered_moved = 0;
    for (unsigned int k = 1; k <= n; k++)
    {
        if (after[k][0].avg_tp_dl != after[k - 1][0].avg_tp_dl) tp_moved++;
        if (after[k][0].delivered_dl != after[k - 1][0].delivered_dl) delivered_moved++;
    }
    assert(tp_moved >= 10);
    assert(delivered_moved >= 3);

    register_controller("static_probe", probe_factory("static_probe"));
    reset_probes();
    write_config(true);
    simulator sim(CONFIG);

    // Built exactly once, when the simulator was, and not called yet.
    assert(g_constructed["static_probe"] == 1);
    assert(g_calls.empty());

    sim.run_steps(n);

    assert(g_constructed["static_probe"] == 1);
    assert(g_calls.size() == n);
    for (unsigned int k = 0; k < n; k++)
    {
        const call_record &c = g_calls[k];
        assert(c.tti == k);                                      // once per TTI, in order
        assert(near(c.sim_t, (double)((std::uint64_t)k * TTI_US) * US2S));
        if (k > 0) assert(c.sim_t > g_calls[k - 1].sim_t);
        assert(c.n_ues == 2);

        // Before the MAC and the UE step of this TTI: what it sees is exactly the state the
        // reference run had after k steps, no earlier and no later. The priority is left out
        // of the comparison: the control plane writes it at the start of the TTI, which is
        // the very thing checked on the next line.
        for (std::size_t u = 0; u < c.n_ues; u++)
        {
            assert(c.state[u].avg_tp_dl == after[k][u].avg_tp_dl);
            assert(c.state[u].delivered_dl == after[k][u].delivered_dl);
        }
        // After control_manager::tick(): the command due at TTI 10 is already applied.
        assert(near(c.state[0].priority, k >= 10 ? 8.0 : 1.0));
    }
    // The controller reads and nothing else: the run ends where the reference one did.
    assert(snapshot(*sim.ue_list()) == after[n]);
}

// (a) With nothing registered the loop is what it was, on the ticker path as well as on
// run_steps; and a registered controller changes neither the count nor the outcome.
void test_empty_registry_changes_nothing()
{
    clear_registered_controllers();
    assert(registered_controller_names().empty());
    reset_probes();
    write_file(TIMELINE, TIMELINE_TEXT);
    write_config(true);

    std::uint64_t iterations = 0;
    std::vector<ue_state> final_state;
    {
        simulator sim(CONFIG);
        sim.start();
        sim.join();
        iterations = sim.steps_done();
        final_state = snapshot(*sim.ue_list());
        assert(sim.exit_code() == 0);
    }
    // duration 0.05 s at 1 ms per step: the ticker runs while previous_ts < goal and stops
    // after the step whose timestamp reaches it, so the count includes both ends.
    assert(iterations == 51);

    register_controller("probe", probe_factory("probe"));
    {
        simulator sim(CONFIG);
        sim.start();
        sim.join();
        assert(sim.steps_done() == iterations);
        assert(snapshot(*sim.ue_list()) == final_state);
        assert(g_calls.size() == iterations);
    }
    clear_registered_controllers();
}

// (c) The control plane off: control_manager::tick() returns at once, and the controller is
// called all the same.
void test_runs_with_control_plane_disabled()
{
    clear_registered_controllers();
    reset_probes();
    register_controller("probe", probe_factory("probe"));
    write_file(TIMELINE, TIMELINE_TEXT);
    write_config(false);

    simulator sim(CONFIG);
    assert(!sim.control_plane().is_enabled());
    sim.run_steps(25);

    assert(g_calls.size() == 25);
    for (unsigned int k = 0; k < 25; k++)
    {
        assert(g_calls[k].tti == k);
        assert(near(g_calls[k].state[0].priority, 1.0));        // the timeline never ran
    }
    clear_registered_controllers();
}

// (d) Registration order is construction order and call order, within every TTI.
void test_registration_order()
{
    clear_registered_controllers();
    reset_probes();
    register_controller("zulu", probe_factory("zulu"));
    register_controller("alpha", probe_factory("alpha"));
    register_controller("mike", probe_factory("mike"));
    write_file(TIMELINE, TIMELINE_TEXT);
    write_config(true);

    simulator sim(CONFIG);
    assert((g_construction_order == std::vector<std::string>{"zulu", "alpha", "mike"}));

    sim.run_steps(5);
    assert(g_calls.size() == 15);
    const char *order[3] = {"zulu", "alpha", "mike"};
    for (unsigned int i = 0; i < g_calls.size(); i++)
    {
        assert(g_calls[i].name == order[i % 3]);
        assert(g_calls[i].tti == i / 3);
    }
    clear_registered_controllers();
}

// (e) A name is registered once; nothing half-registered is left behind by a refusal.
void test_duplicate_name_is_rejected()
{
    clear_registered_controllers();
    register_controller("dup", probe_factory("dup"));

    bool threw = false;
    try
    {
        register_controller("dup", probe_factory("dup"));
    }
    catch (const std::invalid_argument &e)
    {
        threw = true;
        assert(std::string(e.what()).find("dup") != std::string::npos);
    }
    assert(threw);

    threw = false;
    try { register_controller("", probe_factory("x")); }
    catch (const std::invalid_argument &) { threw = true; }
    assert(threw);

    threw = false;
    try { register_controller("no_factory", controller_factory()); }
    catch (const std::invalid_argument &) { threw = true; }
    assert(threw);

    assert((registered_controller_names() == std::vector<std::string>{"dup"}));
    clear_registered_controllers();
}

class thrower : public controller
{
public:
    void on_tick(const tick_context &ctx) override
    {
        if (ctx.tti == 3) throw std::runtime_error("model diverged");
    }
};

// A controller that throws ends the run: the exception comes out of step() with the name of
// the controller and the TTI, and the ones registered after it are not called for that TTI.
void test_throwing_controller_stops_the_run()
{
    clear_registered_controllers();
    reset_probes();
    register_controller("thrower", [](const std::string &, const cell_info &, const std::vector<ue> &)
                        { return std::unique_ptr<controller>(new thrower()); });
    register_controller("after", probe_factory("after"));
    write_file(TIMELINE, TIMELINE_TEXT);
    write_config(true);

    simulator sim(CONFIG);
    bool threw = false;
    try
    {
        sim.run_steps(10);
    }
    catch (const std::runtime_error &e)
    {
        threw = true;
        const std::string what = e.what();
        assert(what.find("thrower") != std::string::npos);
        assert(what.find("tti 3") != std::string::npos);
        assert(what.find("model diverged") != std::string::npos);
    }
    assert(threw);
    assert(calls_of("after").size() == 3);                      // TTIs 0..2 only
    clear_registered_controllers();
}

// A factory may decline, for a run that has nothing for it to do.
void test_factory_may_decline()
{
    clear_registered_controllers();
    reset_probes();
    register_controller("declines", [](const std::string &, const cell_info &, const std::vector<ue> &)
                        { return std::unique_ptr<controller>(); });
    register_controller("takes_part", probe_factory("takes_part"));
    write_file(TIMELINE, TIMELINE_TEXT);
    write_config(true);

    simulator sim(CONFIG);
    sim.run_steps(4);
    assert(g_calls.size() == 4);
    assert(g_calls[0].name == "takes_part");
    clear_registered_controllers();
}

// The factory gets the file it was asked to read and a cell and a UE list that are final.
void test_factory_arguments()
{
    clear_registered_controllers();
    std::string seen_file;
    std::size_t seen_ues = 0;
    double seen_duration = -1.0;
    register_controller("args", [&](const std::string &file, const cell_info &cell, const std::vector<ue> &ues)
                        {
                            seen_file = file;
                            seen_ues = ues.size();
                            seen_duration = cell.duration_s;
                            return std::unique_ptr<controller>();
                        });
    write_file(TIMELINE, TIMELINE_TEXT);
    write_config(true);
    simulator sim(CONFIG);
    assert(seen_file == CONFIG);
    assert(seen_ues == 2);
    assert(near(seen_duration, 0.05, 1e-6));
    clear_registered_controllers();
}

// Side check on how a controller can read a section of its own: configuration_loader skips a
// section header that no branch handles, and a key that its section does not know. Neither is
// an error; the keys the loader does know keep their values.
void test_loader_ignores_unknown_sections_and_keys()
{
    write_file(TIMELINE, TIMELINE_TEXT);
    write_config(true, "[Global]", "unknown_global_key: 42\n");
    {
        std::ifstream in(CONFIG);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        text += "\n[Ext]\nfoo: bar\nsecond_key: 1\n";
        write_file(CONFIG, text);
    }

    configuration_loader loader(CONFIG);
    assert(near(loader.get_duration(), 0.05, 1e-6));
    assert(loader.get_period() < 0);

    // And a whole simulator builds from it, which is what a controller's factory relies on.
    simulator sim(CONFIG);
    sim.run_steps(2);
    assert(sim.ue_list()->size() == 2);
}
}

int main()
{
    std::filesystem::create_directories("build/tests");
    write_file(
        MAP,
        "{\"schema_version\":2,"
        "\"metadata\":{\"grid_origin\":\"explicit-center-cell\"},"
        "\"cell_number\":3,\"cell_size\":500.0,"
        "\"map\":[[-80,-80,-80],[-80,-80,-80],[-80,-80,-80]]}\n");

    // First, while the file-scope registration is still the only one.
    test_file_scope_registration();
    test_controller_contract();
    test_empty_registry_changes_nothing();
    test_runs_with_control_plane_disabled();
    test_registration_order();
    test_duplicate_name_is_rejected();
    test_throwing_controller_stops_the_run();
    test_factory_may_decline();
    test_factory_arguments();
    test_loader_ignores_unknown_sections_and_keys();

    std::remove(TIMELINE);
    std::remove(CONFIG);
    std::remove(MAP);
    return 0;
}
