// Catalogue of runtime control knobs: names, units, ranges and unit conversions.
// Same style as pdcp_flow_test.cpp: <cassert> and nothing else.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <unistd.h>

#include <nlohmann/json.hpp>
#include <simulator/simulator.h>   // brings configuration_loader.h, which has no include guard
#include <ue/ue.h>
#include <utils/control/param_registry.h>

namespace
{
// Knobs added from file-scope static objects, the way an extension registers them. They
// write into variables the test can see, which is all an apply function has to do to be
// observable. They are in the catalogue of this whole test binary, hence the counts below.
const size_t N_BUILTIN = 14;
const size_t N_REGISTERED = 3;

double g_gain_db = 0.0;
int g_gain_calls = 0;
std::string g_label;
std::string g_short;
int g_label_ue = -1;

param_entry make_gain_entry()
{
    param_entry e;
    e.name = "test.knob_gain_db";
    e.type = param_type::number;
    e.unit = "dB";
    e.min = -10.0;
    e.max = 10.0;
    e.description = "Test knob: a bounded number written to a test variable.";
    e.apply = [](ue &, const param_value &v, std::string &reason) {
        const double *d = std::get_if<double>(&v);
        if (d == nullptr) { reason = "expected a number"; return false; }
        g_gain_db = *d;
        g_gain_calls++;
        return true;
    };
    e.read = [](ue &) { return g_gain_db; };
    return e;
}

param_entry make_text_entry(const std::string &name, std::string &target, size_t max_length)
{
    param_entry e;
    e.name = name;
    e.type = param_type::text;
    e.max_length = max_length;
    e.description = "Test knob: free text written to a test variable.";
    e.apply = [&target](ue &u, const param_value &v, std::string &reason) {
        const std::string *str = std::get_if<std::string>(&v);
        if (str == nullptr) { reason = "expected a string"; return false; }
        target = *str;
        g_label_ue = u.get_id();
        return true;
    };
    e.read_text = [&target](ue &) { return target; };
    return e;
}

struct registrar
{
    registrar()
    {
        param_registry::add(make_gain_entry());
        param_registry::add(make_text_entry("test.knob_label", g_label, 4096));
        param_registry::add(make_text_entry("test.knob_short", g_short, 8));
    }
};
const registrar test_registrar;

// Refusals are exercised here, while add() is still open: once the registry is built
// every call is refused for that reason alone and says nothing about the others.
struct add_refusals
{
    bool builtin_name = false;
    bool duplicate = false;
    bool bad_names = true;
    bool no_apply = false;
    bool bad_range = false;
    bool zero_length = false;
};
add_refusals g_refusals;

template <class F>
bool throws_invalid_argument(F f)
{
    try { f(); }
    catch (const std::invalid_argument &) { return true; }
    return false;
}

struct refusal_probe
{
    refusal_probe()
    {
        g_refusals.builtin_name = throws_invalid_argument([] {
            param_registry::add(make_text_entry("priority", g_short, 8));
        });
        g_refusals.duplicate = throws_invalid_argument([] {
            param_registry::add(make_gain_entry());
        });

        const char *bad[] = {"", "Priority", "9lives", "a b", "dl..x", "trailing.", "a-b", "ünï"};
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        {
            const std::string name = bad[i];
            if (!throws_invalid_argument([&] { param_registry::add(make_text_entry(name, g_short, 8)); }))
                g_refusals.bad_names = false;
        }

        g_refusals.no_apply = throws_invalid_argument([] {
            param_entry e = make_gain_entry();
            e.name = "test.knob_no_apply";
            e.apply = nullptr;
            param_registry::add(e);
        });
        g_refusals.bad_range = throws_invalid_argument([] {
            param_entry e = make_gain_entry();
            e.name = "test.knob_bad_range_db";
            e.min = 5.0;
            e.max = -5.0;
            param_registry::add(e);
        });
        g_refusals.zero_length = throws_invalid_argument([] {
            param_registry::add(make_text_entry("test.knob_zero_length", g_short, 0));
        });
    }
};
const refusal_probe test_refusal_probe;

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

    // The built-in catalogue is deliberately small: fourteen knobs, no more, until the
    // plan says so. The registered ones of this test come after them.
    assert(reg.entries().size() == N_BUILTIN + N_REGISTERED);

    // The names of the built-in knobs as the constructor registers them. The order is
    // part of the contract, since describe lists them in it.
    const char *expected[] = {
        "priority", "enabled",
        "dl.rmax_mbps", "dl.inject_bytes", "dl.sinr_offset_db", "traffic.dl_target_mbps",
        "ul.rmax_mbps", "ul.inject_bytes", "ul.sinr_offset_db", "traffic.ul_target_mbps",
        "mobility.pos_x_m", "mobility.pos_y_m", "pkt_delay_budget_s", "mobility.speed_kmh"
    };
    assert(sizeof(expected) / sizeof(expected[0]) == N_BUILTIN);
    for (size_t i = 0; i < N_BUILTIN; i++)
    {
        assert(reg.find(expected[i]) != nullptr);
        assert(reg.entries()[i].name == expected[i]);
    }

    // Registered knobs follow, in the order they were added.
    assert(reg.entries()[N_BUILTIN].name == "test.knob_gain_db");
    assert(reg.entries()[N_BUILTIN + 1].name == "test.knob_label");
    assert(reg.entries()[N_BUILTIN + 2].name == "test.knob_short");

    assert(reg.find("rmax_mbps") == nullptr);      // no unprefixed directional form
    assert(reg.find("speed") == nullptr);          // no name without its unit suffix
    assert(reg.find("does_not_exist") == nullptr);

    // inject_bytes is the only incremental knob, and describe has to say so.
    assert(reg.find("dl.inject_bytes")->incremental);
    assert(!reg.find("priority")->incremental);

    // Every built-in name either carries a unit suffix or is dimensionless by construction.
    for (size_t i = 0; i < N_BUILTIN; i++)
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
    // A budget that a float cannot hold must reach the expiry decision as it was set.
    assert(reg.find("pkt_delay_budget_s")->apply(u, param_value(0.123456789012345), reason));
    assert(reg.find("pkt_delay_budget_s")->read(u) == 0.123456789012345);
    assert(u.get_pkt_delay_budget() == 0.123456789012345);

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
        if (e.type == param_type::text)
        {
            assert(e.apply(u, param_value(std::string("x")), reason));
            continue;
        }
        // Unbounded ends are exercised with a large but sane magnitude; positions get
        // clamped to the apothem by the mobility model anyway.
        const double lo = std::isinf(e.min) ? -1e6 : e.min;
        const double hi = std::isinf(e.max) ? 1e6 : e.max;
        assert(e.apply(u, param_value(lo), reason));
        assert(e.apply(u, param_value(hi), reason));
        if (e.read) (void)e.read(u);
    }
}

void test_describe_is_valid_json()
{
    const std::string text = param_registry::instance().describe_json();
    nlohmann::json j = nlohmann::json::parse(text);
    assert(j.is_array());
    assert(j.size() == N_BUILTIN + N_REGISTERED);

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

const nlohmann::json *find_described(const nlohmann::json &j, const std::string &name)
{
    for (size_t i = 0; i < j.size(); i++)
        if (j[i]["name"] == name) return &j[i];
    return nullptr;
}

void test_registered_knobs_are_described()
{
    const nlohmann::json j = nlohmann::json::parse(param_registry::instance().describe_json());

    // Built-ins keep their place at the head of the list.
    assert(j[0]["name"] == "priority");
    assert(j[N_BUILTIN - 1]["name"] == "mobility.speed_kmh");

    const nlohmann::json *gain = find_described(j, "test.knob_gain_db");
    assert(gain != nullptr);
    assert((*gain)["type"] == "number");
    assert((*gain)["unit"] == "dB");
    assert((*gain)["min"] == -10.0);
    assert((*gain)["max"] == 10.0);
    assert(!gain->contains("max_length"));

    // A text knob has a type and a length bound, and no numeric range.
    const nlohmann::json *label = find_described(j, "test.knob_label");
    assert(label != nullptr);
    assert((*label)["type"] == "text");
    assert((*label)["unit"] == "");
    assert((*label)["max_length"] == 4096);
    assert(!label->contains("min"));
    assert(!label->contains("max"));

    assert((*find_described(j, "test.knob_short"))["max_length"] == 8);

    // The built-in entries do not grow a field they never had.
    const nlohmann::json *speed = find_described(j, "mobility.speed_kmh");
    assert(!speed->contains("max_length"));
}

void test_registered_number_knob()
{
    ue u = make_ue(2);
    const param_registry &reg = param_registry::instance();
    std::string reason;
    const param_entry *e = reg.find("test.knob_gain_db");
    assert(e != nullptr);

    assert(reg.check(*e, param_value(-3.5), reason));
    assert(!reg.check(*e, param_value(10.5), reason));           // same range check as a built-in
    assert(!reg.check(*e, param_value(std::nan("")), reason));
    assert(!reg.check(*e, param_value(true), reason));
    assert(!reg.check(*e, param_value(std::string("loud")), reason));
    assert(reason == "expected a number");

    const int calls = g_gain_calls;
    assert(e->apply(u, param_value(-3.5), reason));
    assert(g_gain_calls == calls + 1);
    assert(near(g_gain_db, -3.5));

    param_value out;
    assert(reg.read_value(*e, u, out));
    assert(std::holds_alternative<double>(out) && near(std::get<double>(out), -3.5));
}

void test_registered_text_knob()
{
    ue u = make_ue(3);
    const param_registry &reg = param_registry::instance();
    std::string reason;
    const param_entry *e = reg.find("test.knob_label");
    assert(e != nullptr);

    // Quotes, backslashes, newlines and multi-byte characters are ordinary content.
    const std::string awkward = "a \"quoted\" \\ path\nline two\t\xC3\xA9 \xE6\xBC\xA2 \xF0\x9F\x98\x80";
    assert(reg.check(*e, param_value(awkward), reason));
    assert(reg.check(*e, param_value(std::string()), reason));    // empty clears
    assert(e->apply(u, param_value(awkward), reason));
    assert(g_label == awkward);
    assert(g_label_ue == 3);

    param_value out;
    assert(reg.read_value(*e, u, out));
    assert(std::holds_alternative<std::string>(out) && std::get<std::string>(out) == awkward);

    // A number or a boolean is not a text, and a string is not a number or a boolean.
    assert(!reg.check(*e, param_value(1.0), reason));
    assert(reason == "expected a string");
    assert(!reg.check(*e, param_value(true), reason));
    assert(!reg.check(*reg.find("priority"), param_value(std::string("1")), reason));
    assert(!reg.check(*reg.find("enabled"), param_value(std::string("true")), reason));
    assert(!reg.check(*reg.find("test.knob_gain_db"), param_value(std::string("1")), reason));

    // Bytes that are not UTF-8 would not survive the journal.
    assert(!reg.check(*e, param_value(std::string("\xC3\x28")), reason));
    assert(!reg.check(*e, param_value(std::string("\xC0\xAF")), reason));      // overlong
    assert(!reg.check(*e, param_value(std::string("\xED\xA0\x80")), reason));  // surrogate
    assert(!reg.check(*e, param_value(std::string("\xE6\xBC")), reason));      // truncated

    // The length bound counts bytes, is inclusive, and the default is 4096.
    assert(reg.check(*e, param_value(std::string(4096, 'x')), reason));
    assert(!reg.check(*e, param_value(std::string(4097, 'x')), reason));
    assert(!reason.empty());

    const param_entry *shorter = reg.find("test.knob_short");
    assert(reg.check(*shorter, param_value(std::string("12345678")), reason));
    assert(!reg.check(*shorter, param_value(std::string("123456789")), reason));
    assert(!reg.check(*shorter, param_value(std::string("\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9")), reason));  // 10 bytes, 5 characters
}

void test_add_is_strict()
{
    assert(g_refusals.builtin_name);
    assert(g_refusals.duplicate);
    assert(g_refusals.bad_names);
    assert(g_refusals.no_apply);
    assert(g_refusals.bad_range);
    assert(g_refusals.zero_length);

    // The registry is built by now, and it is immutable from then on.
    bool refused = false;
    try { param_registry::add(make_text_entry("test.knob_late", g_short, 8)); }
    catch (const std::logic_error &) { refused = true; }
    assert(refused);
    assert(param_registry::instance().find("test.knob_late") == nullptr);
    assert(param_registry::instance().entries().size() == N_BUILTIN + N_REGISTERED);
}

// Replay of a registered knob through the control plane: a script sets it, the journal
// records it, and the journal played back as a script applies it again.
std::string read_file(const std::string &path)
{
    std::ifstream in(path);
    assert(in.is_open());
    std::stringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

void write_file(const std::string &path, const std::string &text)
{
    std::ofstream out(path);
    assert(out.is_open());
    out << text;
}

void write_config(const std::string &path, const std::string &map, const std::string &timeline,
                  const std::string &journal)
{
    std::ifstream base("tests/control_smoke.ini");
    assert(base.is_open());
    std::string line, text;
    while (std::getline(base, line))
    {
        if (line.rfind("timeline_file:", 0) == 0)
            line = "timeline_file: " + timeline + "\njournal_file: " + journal;
        text += line + "\n";
        if (line.rfind("scenario_type:", 0) == 0) text += "map_file: " + map + "\n";
    }
    write_file(path, text);
}

std::string script_line(int id, int at_tti, const std::string &target, const nlohmann::json &set)
{
    nlohmann::json cmd;
    cmd["target"] = target;
    cmd["set"] = set;
    nlohmann::json line;
    line["id"] = id;
    line["at_tti"] = at_tti;
    line["cmds"] = nlohmann::json::array({cmd});
    return line.dump() + "\n";
}

void test_journal_and_replay_apply_registered_knobs()
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("fikore_registry_test_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const std::string map = (dir / "map.json").string();
    const std::string timeline = (dir / "timeline.ndjson").string();
    const std::string journal = (dir / "journal.ndjson").string();
    const std::string replayed_journal = (dir / "journal_replay.ndjson").string();
    const std::string config = (dir / "run.ini").string();

    write_file(map,
        "{\"schema_version\":2,"
        "\"metadata\":{\"grid_origin\":\"explicit-center-cell\"},"
        "\"cell_number\":3,\"cell_size\":500.0,"
        "\"map\":[[-80,-80,-80],[-80,-80,-80],[-80,-80,-80]]}\n");

    const std::string awkward = "{\"w\":[1,2],\"s\":\"q\\\"uote\\\\\"}\nsecond line \xC3\xA9 \xE6\xBC\xA2";
    nlohmann::json first;
    first["test.knob_gain_db"] = -3.5;
    first["test.knob_label"] = awkward;
    nlohmann::json too_long;
    too_long["test.knob_short"] = "123456789";          // one byte over its bound
    nlohmann::json wrong_type;
    wrong_type["test.knob_gain_db"] = "loud";
    nlohmann::json after;
    after["test.knob_gain_db"] = 7.0;
    after["priority"] = 2.0;                              // a built-in in the same message

    write_file(timeline,
        script_line(1, 3, "ue/1", first)
        + script_line(2, 4, "ue/0", too_long)
        + script_line(3, 5, "ue/0", wrong_type)
        + script_line(4, 8, "ue/0", after));
    write_config(config, map, timeline, journal);

    g_gain_db = 0.0; g_label.clear(); g_short.clear(); g_label_ue = -1;
    {
        simulator sim(config);
        sim.run_steps(4);                                // TTIs 0..3
        assert(near(g_gain_db, -3.5));
        assert(g_label == awkward);
        assert(g_label_ue == 1);                         // the target reached the apply function

        sim.run_steps(2);                                // TTIs 4, 5: both rejected
        assert(g_short.empty());
        assert(near(g_gain_db, -3.5));

        sim.run_steps(4);                                // TTI 8
        assert(near(g_gain_db, 7.0));
        assert(near((*sim.ue_list())[0].overrides().priority, 2.0));
    }

    // The journal holds every applied line, text included, as a valid script line.
    const std::string recorded = read_file(journal);
    assert(!recorded.empty());
    {
        std::istringstream in(recorded);
        std::string line;
        bool found = false;
        while (std::getline(in, line))
        {
            const nlohmann::json entry = nlohmann::json::parse(line);
            const nlohmann::json &set = entry["cmds"][0]["set"];
            if (entry["at_tti"] == 3)
            {
                assert(set["test.knob_label"] == awkward);
                assert(set["test.knob_gain_db"] == -3.5);
                found = true;
            }
        }
        assert(found);
    }

    // Replay: the journal is the script of a fresh run, and it lands the same values.
    write_file(timeline, recorded);
    write_config(config, map, timeline, replayed_journal);
    g_gain_db = 0.0; g_label.clear(); g_short.clear(); g_label_ue = -1;
    {
        simulator sim(config);
        sim.run_steps(4);
        assert(near(g_gain_db, -3.5));
        assert(g_label == awkward);
        assert(g_label_ue == 1);
        sim.run_steps(6);
        assert(near(g_gain_db, 7.0));
        assert(g_short.empty());
    }

    // And the replayed run journals the same text again: a text value is stable under
    // record and replay.
    {
        std::istringstream a(recorded), b(read_file(replayed_journal));
        std::string la, lb;
        while (std::getline(a, la))
        {
            assert(std::getline(b, lb));
            const nlohmann::json ja = nlohmann::json::parse(la), jb = nlohmann::json::parse(lb);
            assert(ja["at_tti"] == jb["at_tti"]);
            assert(ja["cmds"] == jb["cmds"]);
        }
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
}
}

int main()
{
    test_catalogue_shape();
    test_range_validation();
    test_apply_and_units();
    test_extremes_do_not_crash();
    test_describe_is_valid_json();
    test_registered_knobs_are_described();
    test_registered_number_knob();
    test_registered_text_knob();
    test_add_is_strict();
    test_journal_and_replay_apply_registered_knobs();
    return 0;
}
