// Client driven injection: bytes in, the same bytes out, and an honest account of what
// was lost and why.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include <simulator/simulator.h>
#include <ue/ue.h>

namespace
{
const char *TIMELINE = "build/tests/injected_timeline.ndjson";
const char *CONFIG = "build/tests/injected.ini";

void write_file(const char *path, const std::string &text)
{
    std::ofstream out(path);
    assert(out.is_open());
    out << text;
}

// The control smoke scenario with the uplink silenced and the downlink generator set by
// the caller, so that the only downlink traffic is what the test injects.
void write_config(const std::string &timeline, const std::string &dl_target)
{
    std::ifstream base("tests/control_smoke.ini");
    assert(base.is_open());

    std::string line, text;
    while (std::getline(base, line))
    {
        if (line.rfind("timeline_file:", 0) == 0) line = "timeline_file: " + timeline;
        if (line.rfind("dl_target:", 0) == 0) line = "dl_target: " + dl_target;
        if (line.rfind("ul_target:", 0) == 0) line = "ul_target: 0.0";
        text += line + "\n";
    }
    write_file(CONFIG, text);
}

double injected(ue &u, int tx_dir) { return u.pdcp_state(tx_dir).injected_bits_total() / 8.0; }
double delivered(ue &u, int tx_dir) { return u.pdcp_state(tx_dir).delivered_bits_total() / 8.0; }
double expired(ue &u, int tx_dir) { return u.pdcp_state(tx_dir).expired_bits_total() / 8.0; }
double dropped(ue &u, int tx_dir) { return u.pdcp_state(tx_dir).dropped_bits_total() / 8.0; }
double queue_dropped(ue &u, int tx_dir) { return u.pdcp_state(tx_dir).queue_dropped_bits_total() / 8.0; }
double radio_dropped(ue &u, int tx_dir) { return u.pdcp_state(tx_dir).radio_dropped_bits_total() / 8.0; }
double pending(ue &u, int tx_dir) { return u.pdcp_state(tx_dir).pending_bits() / 8.0; }

// With no generator and no injection, nothing is created.
void test_silent_by_default()
{
    write_file(TIMELINE, "\n");
    write_config(TIMELINE, "0.0");

    simulator sim(CONFIG);
    ue &u = (*sim.ue_list())[0];

    sim.run_steps(200);
    assert(injected(u, TX_DL) == 0.0);
    assert(delivered(u, TX_DL) == 0.0);
    assert(!u.has_packets(TX_DL));
}

// N bytes handed over are N bytes delivered, give or take the last partial packet, and
// then the UE goes quiet again.
void test_byte_conservation()
{
    const double payload = 200000.0;   // 200 kB, comfortably inside the delay budget

    write_file(TIMELINE,
               "{\"id\":1,\"at_tti\":10,\"cmds\":[{\"target\":\"ue/0\",\"set\":{\"dl.inject_bytes\":200000}}]}\n");
    write_config(TIMELINE, "0.0");

    simulator sim(CONFIG);
    ue &u = (*sim.ue_list())[0];

    sim.run_steps(10);
    assert(injected(u, TX_DL) == 0.0);          // not one step early

    sim.run_steps(1);
    assert(injected(u, TX_DL) == payload);

    sim.run_steps(400);

    const double out = delivered(u, TX_DL);
    const double lost = expired(u, TX_DL) + dropped(u, TX_DL);
    const double still_queued = pending(u, TX_DL);

    // Nothing is created and nothing vanishes: what came in is out, lost or still queued.
    assert(std::fabs((out + lost + still_queued) - payload) < 1500.0);
    assert(out > payload * 0.9);
    assert(still_queued < 1500.0);

    // And once it is through, the UE stops being schedulable.
    assert(!u.has_packets(TX_DL));

    // The uplink was never touched.
    assert(injected(u, TX_UL) == 0.0);
    assert(delivered(u, TX_UL) == 0.0);
}

// Injection adds to the configured traffic instead of replacing it.
void test_injection_adds_to_the_generator()
{
    write_file(TIMELINE,
               "{\"id\":1,\"at_tti\":50,\"cmds\":[{\"target\":\"ue/0\",\"set\":{\"dl.inject_bytes\":50000}}]}\n");
    write_config(TIMELINE, "5.0");

    simulator sim(CONFIG);
    ue &u = (*sim.ue_list())[0];

    sim.run_steps(50);
    const double before = delivered(u, TX_DL);
    assert(before > 0.0);                       // the generator is running on its own

    sim.run_steps(400);
    assert(delivered(u, TX_DL) > before + 40000.0);
    assert(injected(u, TX_DL) == 50000.0);      // only what was injected counts as such
}

// Overfeeding shows up as expired bytes, not as radio drops, and raising the budget is
// what makes a bulk transfer viable.
void test_delay_budget_is_the_binding_constraint()
{
    const std::string big = "{\"id\":1,\"at_tti\":10,\"cmds\":[{\"target\":\"ue/0\",\"set\":{\"dl.inject_bytes\":4000000}}]}\n";

    write_file(TIMELINE, big);
    write_config(TIMELINE, "0.0");
    simulator tight(CONFIG);
    ue &u_tight = (*tight.ue_list())[0];
    tight.run_steps(3000);

    // A good part of it evaporates, and the account still adds up.
    assert(expired(u_tight, TX_DL) > 4000000.0 * 0.2);
    assert(expired(u_tight, TX_DL) > dropped(u_tight, TX_DL));   // budget, not the radio
    assert(delivered(u_tight, TX_DL) < 4000000.0 * 0.9);
    assert(std::fabs(delivered(u_tight, TX_DL) + expired(u_tight, TX_DL)
                     + dropped(u_tight, TX_DL) + pending(u_tight, TX_DL) - 4000000.0) < 1500.0);

    // Same injection, budget raised first: now it gets through.
    write_file(TIMELINE,
               "{\"id\":1,\"at_tti\":1,\"cmds\":[{\"target\":\"ue/0\",\"set\":{\"pkt_delay_budget_s\":30.0}}]}\n" + big);
    write_config(TIMELINE, "0.0");
    simulator loose(CONFIG);
    ue &u_loose = (*loose.ue_list())[0];
    loose.run_steps(3000);

    // With room to spare, the whole object gets through and nothing expires.
    assert(delivered(u_loose, TX_DL) > 4000000.0 * 0.99);
    assert(delivered(u_loose, TX_DL) > delivered(u_tight, TX_DL));
    assert(expired(u_loose, TX_DL) == 0.0);
}

// The counters a client diffs must never go backwards.
void test_counters_are_monotonic()
{
    write_file(TIMELINE,
               "{\"id\":1,\"at_tti\":10,\"cmds\":[{\"target\":\"ue/0\",\"set\":{\"dl.inject_bytes\":100000}}]}\n"
               "{\"id\":2,\"at_tti\":60,\"cmds\":[{\"target\":\"ue/0\",\"set\":{\"dl.inject_bytes\":100000}}]}\n");
    write_config(TIMELINE, "1.0");

    simulator sim(CONFIG);
    ue &u = (*sim.ue_list())[0];

    double last_in = 0.0, last_out = 0.0, last_exp = 0.0, last_drop = 0.0;
    for (int i = 0; i < 40; i++)
    {
        sim.run_steps(25);
        assert(injected(u, TX_DL) >= last_in);
        assert(delivered(u, TX_DL) >= last_out);
        assert(expired(u, TX_DL) >= last_exp);
        assert(dropped(u, TX_DL) >= last_drop);
        last_in = injected(u, TX_DL);
        last_out = delivered(u, TX_DL);
        last_exp = expired(u, TX_DL);
        last_drop = dropped(u, TX_DL);
    }
    assert(last_in == 200000.0);
}

// Detaching a UE throws away whatever was still queued, and those bytes have to land
// somewhere. Two of them used to reach the per object counters and never the per UE
// totals, so the two disagreed on how much had been injected: the ingress queue, and the
// bits already granted on the air that were waiting out the backhaul delay with nobody
// left to call release() for them.
void test_detaching_accounts_for_what_it_throws_away()
{
    write_file(TIMELINE,
               "{\"id\":1,\"at_tti\":10,\"cmds\":[{\"target\":\"ue/0\",\"set\":{\"dl.inject_bytes\":4000000}}]}\n"
               "{\"id\":2,\"at_tti\":40,\"cmds\":[{\"target\":\"ue/0\",\"set\":{\"enabled\":false}}]}\n");
    write_config(TIMELINE, "0.0");

    simulator sim(CONFIG);
    ue &u = (*sim.ue_list())[0];

    // 4 MB against 30 TTIs of air: most of it is still queued when the detach lands.
    sim.run_steps(41);

    // The flush counts as a queue drop, not as a radio one, and nothing had time to expire.
    assert(queue_dropped(u, TX_DL) > 1000000.0);
    assert(radio_dropped(u, TX_DL) == 0.0);
    assert(expired(u, TX_DL) == 0.0);

    // And the per UE totals add up to what was injected, which is what the holes broke.
    const double accounted = delivered(u, TX_DL) + expired(u, TX_DL) + dropped(u, TX_DL)
                           + pending(u, TX_DL);
    assert(std::fabs(accounted - 4000000.0) < 1500.0);
}
}

int main()
{
    test_silent_by_default();
    test_byte_conservation();
    test_injection_adds_to_the_generator();
    test_delay_budget_is_the_binding_constraint();
    test_counters_are_monotonic();
    test_detaching_accounts_for_what_it_throws_away();
    std::remove(TIMELINE);
    std::remove(CONFIG);
    return 0;
}
