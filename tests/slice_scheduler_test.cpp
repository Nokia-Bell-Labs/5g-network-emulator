// Slices: the knobs that configure them, the checks on the quotas of the cell, and what
// the minimum quota, the maximum quota and the shared cap do to the scheduler.
// Driven through simulator::run_steps, like control_apply_test.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include <mac_layer/mac_definitions.h>
#include <simulator/simulator.h>
#include <ue/ue.h>

namespace
{
bool near(double a, double b, double tol = 1e-4)
{
    return std::fabs(a - b) < tol;
}

const char *TIMELINE = "build/tests/slice_scheduler_timeline.ndjson";
const char *CONFIG = "build/tests/slice_scheduler.ini";
const char *MAP = "build/tests/slice_scheduler_map.json";

void write_file(const char *path, const std::string &text)
{
    std::ofstream out(path);
    assert(out.is_open());
    out << text;
}

// The control smoke scenario: saturated UEs at the same position, proportional fair.
void write_config(const std::string &timeline, int n_ues = 2)
{
    std::ifstream base("tests/control_smoke.ini");
    assert(base.is_open());
    std::string line, text;
    while (std::getline(base, line))
    {
        if (line.rfind("timeline_file:", 0) == 0) line = "timeline_file: " + timeline;
        if (line.rfind("n_ues:", 0) == 0) line = "n_ues: " + std::to_string(n_ues);
        text += line + "\n";
        if (line.rfind("scenario_type:", 0) == 0)
            text += std::string("map_file: ") + MAP + "\n";
    }
    write_file(CONFIG, text);
}

std::string at(int tti, int id, const std::string &target, const std::string &set)
{
    return "{\"id\":" + std::to_string(id) + ",\"at_tti\":" + std::to_string(tti)
        + ",\"cmds\":[{\"target\":\"" + target + "\",\"set\":{" + set + "}}]}\n";
}

float share_of_ue0(simulator &sim)
{
    const float tp0 = (*sim.ue_list())[0].get_avg_tp(TX_DL);
    const float tp1 = (*sim.ue_list())[1].get_avg_tp(TX_DL);
    assert(tp0 + tp1 > 0.0f);
    return tp0 / (tp0 + tp1);
}

// The quota and the cap are set through one UE and land on every member, and a UE that
// joins later takes the configuration the slice already has.
void test_slice_configuration_is_shared()
{
    write_file(TIMELINE,
               at(1, 1, "ue/0", "\"slice_id\":3") +
               at(1, 2, "ue/1", "\"slice_id\":3") +
               at(2, 3, "ue/0", "\"slice.min_quota_pct\":30.0,\"slice.max_quota_pct\":60.0,\"dl.slice_rmax_mbps\":12.0") +
               at(4, 4, "ue/2", "\"slice_id\":3") +
               at(5, 5, "ue/1", "\"slice_id\":0"));
    write_config(TIMELINE, 3);

    simulator sim(CONFIG);
    ue &u0 = (*sim.ue_list())[0];
    ue &u1 = (*sim.ue_list())[1];
    ue &u2 = (*sim.ue_list())[2];

    sim.run_steps(3);
    assert(u0.overrides().slice_id == 3 && u1.overrides().slice_id == 3);
    assert(near(u1.overrides().slice_min_quota_pct, 30.0));
    assert(near(u1.overrides().slice_max_quota_pct, 60.0));
    assert(near(u1.overrides().slice_rmax_bps[TX_DL], 12e6, 1.0));
    assert(near(u1.overrides().slice_rmax_bps[TX_UL], 0.0));
    assert(near(u2.overrides().slice_min_quota_pct, 0.0));

    sim.run_steps(2);
    assert(near(u2.overrides().slice_min_quota_pct, 30.0));
    assert(near(u2.overrides().slice_rmax_bps[TX_DL], 12e6, 1.0));

    sim.run_steps(1);
    assert(u1.overrides().slice_id == 0);
    assert(near(u1.overrides().slice_min_quota_pct, 0.0));
    assert(near(u1.overrides().slice_max_quota_pct, 100.0));
    assert(near(u1.overrides().slice_rmax_bps[TX_DL], 0.0));
}

// Each rejected message leaves everything as it was: min above max, minimums of the cell
// above 100 %, a quota on a UE with no slice, a fractional slice id.
void test_invalid_slice_settings_are_rejected()
{
    write_file(TIMELINE,
               at(1, 1, "ue/0", "\"slice_id\":1,\"slice.min_quota_pct\":60.0") +
               at(1, 2, "ue/1", "\"slice_id\":2,\"slice.min_quota_pct\":30.0") +
               at(2, 3, "ue/1", "\"slice.min_quota_pct\":50.0") +
               at(2, 4, "ue/0", "\"slice.max_quota_pct\":40.0") +
               at(2, 5, "ue/2", "\"slice.min_quota_pct\":10.0") +
               at(2, 6, "ue/2", "\"slice_id\":1.5"));
    write_config(TIMELINE, 3);

    simulator sim(CONFIG);
    ue &u0 = (*sim.ue_list())[0];
    ue &u1 = (*sim.ue_list())[1];
    ue &u2 = (*sim.ue_list())[2];

    sim.run_steps(3);
    assert(u0.overrides().slice_id == 1 && near(u0.overrides().slice_min_quota_pct, 60.0));
    assert(u1.overrides().slice_id == 2 && near(u1.overrides().slice_min_quota_pct, 30.0));
    assert(near(u0.overrides().slice_max_quota_pct, 100.0));
    assert(u2.overrides().slice_id == 0);
    assert(near(u2.overrides().slice_min_quota_pct, 0.0));
}

// A slice with no quota and no cap schedules exactly as no slice at all.
void test_default_slices_change_nothing()
{
    write_file(TIMELINE,
               at(0, 1, "ue/0", "\"slice_id\":1") +
               at(0, 2, "ue/1", "\"slice_id\":2"));
    write_config(TIMELINE);
    simulator sliced(CONFIG);

    write_file(TIMELINE, "\n");
    write_config(TIMELINE);
    simulator plain(CONFIG);

    sliced.run_steps(1000);
    plain.run_steps(1000);
    for (int i = 0; i < 2; i++)
    {
        assert((*sliced.ue_list())[i].get_avg_tp(TX_DL) == (*plain.ue_list())[i].get_avg_tp(TX_DL));
        assert((*sliced.ue_list())[i].get_avg_tp(TX_UL) == (*plain.ue_list())[i].get_avg_tp(TX_UL));
    }
}

// Two identical saturated UEs split the grid in halves under proportional fair. A
// minimum of 80 % for the slice of one of them moves the split to at least 80/20.
void test_min_quota_is_guaranteed()
{
    write_file(TIMELINE,
               at(0, 1, "ue/0", "\"slice_id\":1,\"slice.min_quota_pct\":80.0"));
    write_config(TIMELINE);
    simulator sim(CONFIG);
    sim.run_steps(2000);

    const float share = share_of_ue0(sim);
    assert(share > 0.77f);
    assert(share < 0.95f);                  // the other one is not starved either
}

// A maximum of 20 % holds the UE of that slice to it even with nobody else asking more.
void test_max_quota_is_a_ceiling()
{
    write_file(TIMELINE,
               at(0, 1, "ue/0", "\"slice_id\":1,\"slice.max_quota_pct\":20.0"));
    write_config(TIMELINE);
    simulator sim(CONFIG);
    sim.run_steps(2000);

    const float share = share_of_ue0(sim);
    assert(share < 0.23f);
    assert(share > 0.10f);
}

// The shared cap holds the sum of the slice, not each UE: two UEs of the slice add up to
// the cap, and the direction without a cap is not touched.
void test_shared_cap_holds_the_slice_rate()
{
    write_file(TIMELINE,
               at(0, 1, "ue/*", "\"slice_id\":1") +
               at(0, 2, "ue/0", "\"dl.slice_rmax_mbps\":10.0"));
    write_config(TIMELINE);
    simulator sim(CONFIG);
    sim.run_steps(2000);

    const float tp0 = (*sim.ue_list())[0].get_avg_tp(TX_DL);
    const float tp1 = (*sim.ue_list())[1].get_avg_tp(TX_DL);
    assert(tp0 + tp1 <= 10.5f);
    assert(tp0 + tp1 > 7.0f);
    assert(tp0 > 2.0f && tp1 > 2.0f);
}
}

int main()
{
    write_file(
        MAP,
        "{\"schema_version\":2,"
        "\"metadata\":{\"grid_origin\":\"explicit-center-cell\"},"
        "\"cell_number\":3,\"cell_size\":500.0,"
        "\"map\":[[-80,-80,-80],[-80,-80,-80],[-80,-80,-80]]}\n");
    test_slice_configuration_is_shared();
    test_invalid_slice_settings_are_rejected();
    test_default_slices_change_nothing();
    test_min_quota_is_guaranteed();
    test_max_quota_is_a_ceiling();
    test_shared_cap_holds_the_slice_rate();
    std::remove(TIMELINE);
    std::remove(CONFIG);
    std::remove(MAP);
    return 0;
}
