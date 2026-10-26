// ECN on injected traffic: the client says what its packets declare, the dual queue
// classifies on it, and the marks come back attributed to the object that earned them.
// Without this an L4S sender driving the emulator through the control plane has nothing
// to react to, because injected traffic would always be not-ECT and could only ever be
// dropped.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <simulator/simulator.h>
#include <ue/ue.h>
#include <utils/control/ndjson.h>

namespace
{
const char *TIMELINE = "build/tests/object_ecn_timeline.ndjson";
const char *CONFIG = "build/tests/object_ecn.ini";

void write_file(const char *path, const std::string &text)
{
    std::ofstream out(path);
    assert(out.is_open());
    out << text;
}

// The control smoke scenario with the generators silenced and the dual queue on, so
// that the only traffic is what the test injects and ECT(1) has somewhere to go.
// budget_s overrides pkt_delay_budget when positive.
void write_config(const std::string &timeline, double budget_s = -1.0)
{
    std::ifstream base("tests/control_smoke.ini");
    assert(base.is_open());

    std::string line, text;
    while (std::getline(base, line))
    {
        if (line.rfind("timeline_file:", 0) == 0) line = "timeline_file: " + timeline;
        if (line.rfind("dl_target:", 0) == 0) line = "dl_target: 0.0";
        if (line.rfind("ul_target:", 0) == 0) line = "ul_target: 0.0";
        if (budget_s > 0.0 && line.rfind("pkt_delay_budget:", 0) == 0)
            line = "pkt_delay_budget: " + std::to_string(budget_s);
        if (line.rfind("[eNBConfig]", 0) == 0)
            line = "l4s_dual_queue: true\nl4s_target_ms: 1.0\n" + line;
        text += line + "\n";
    }
    write_file(CONFIG, text);
}

std::string inject(int tti, std::uint32_t tag, long bytes, const char *ecn)
{
    std::string cmd = "{\"op\":\"inject\",\"target\":\"ue/0\",\"tag\":" + std::to_string(tag)
                    + ",\"dl.bytes\":" + std::to_string(bytes);
    if (ecn != nullptr) cmd += ",\"ecn\":\"" + std::string(ecn) + "\"";
    cmd += "}";
    return "{\"id\":" + std::to_string(tti) + ",\"at_tti\":" + std::to_string(tti)
         + ",\"cmds\":[" + cmd + "]}\n";
}

const object_counters &counters(ue &u, std::uint32_t tag)
{
    const std::unordered_map<std::uint32_t, object_counters> &objects = u.pdcp_state(TX_DL).objects();
    std::unordered_map<std::uint32_t, object_counters>::const_iterator it = objects.find(tag);
    assert(it != objects.end());
    return it->second;
}

// The field is named, not numbered: a client states what its traffic is and the
// emulator maps it. An unknown name is a rejected command, not a silent not-ECT.
void test_the_ecn_field_is_parsed_by_name()
{
    std::vector<command> cmds;
    std::string error;

    assert(ndjson::parse_line("{\"id\":1,\"cmds\":[{\"op\":\"inject\",\"target\":\"ue/0\","
                              "\"tag\":1,\"dl.bytes\":1000,\"ecn\":\"ect1\"}]}",
                              1, cmds, error));
    assert(cmds.size() == 1 && cmds[0].ecn == ECN_ECT1);

    cmds.clear();
    assert(ndjson::parse_line("{\"id\":2,\"cmds\":[{\"op\":\"inject\",\"target\":\"ue/0\","
                              "\"tag\":1,\"dl.bytes\":1000,\"ecn\":\"l4s\"}]}",
                              2, cmds, error));
    assert(cmds[0].ecn == ECN_ECT1);

    cmds.clear();
    assert(ndjson::parse_line("{\"id\":3,\"cmds\":[{\"op\":\"inject\",\"target\":\"ue/0\","
                              "\"tag\":1,\"dl.bytes\":1000}]}",
                              3, cmds, error));
    assert(cmds[0].ecn == ECN_NOT_ECT);   // saying nothing is what it always was

    cmds.clear();
    assert(!ndjson::parse_line("{\"id\":4,\"cmds\":[{\"op\":\"inject\",\"target\":\"ue/0\","
                               "\"tag\":1,\"dl.bytes\":1000,\"ecn\":\"ect2\"}]}",
                               4, cmds, error));
    assert(error.find("ecn") != std::string::npos);
}

void test_the_events_cursor_is_explicit()
{
    std::vector<command> cmds;
    std::string error;
    assert(ndjson::parse_line(
        "{\"id\":5,\"cmds\":[{\"op\":\"events\",\"after\":17,"
        "\"include_state\":true,\"resync\":true}]}",
        5, cmds, error));
    assert(cmds.size() == 1);
    assert(cmds[0].op == command_op::events);
    assert(cmds[0].target == "cell");
    assert(cmds[0].after == 17);
    assert(cmds[0].include_state);
    assert(cmds[0].resync_events);

    cmds.clear();
    error.clear();
    assert(!ndjson::parse_line("{\"id\":6,\"cmds\":[{\"op\":\"events\"}]}",
                               6, cmds, error));
    assert(error == "events needs an after cursor");

    // A bad command at the end of an envelope must not leak earlier commands into
    // the caller's inbox. The socket reports the envelope id, not its line number.
    cmds.clear();
    error.clear();
    std::uint64_t message_id = 0;
    assert(!ndjson::parse_line(
        "{\"id\":99,\"cmds\":["
        "{\"op\":\"inject\",\"target\":\"ue/0\",\"tag\":1,\"dl.bytes\":1000},"
        "{\"op\":\"events\"}]}",
        7, cmds, error, &message_id));
    assert(cmds.empty());
    assert(message_id == 99);
}

// An object that declares ECT(1) and overfeeds the cell is marked rather than dropped,
// which is the whole difference between a scalable sender and a classic one.
void test_ect1_is_marked_instead_of_dropped()
{
    const double flood = 2000000.0;

    write_file(TIMELINE, inject(10, 1, (long)flood, "ect1"));
    write_config(TIMELINE);

    simulator sim(CONFIG);
    ue &u = (*sim.ue_list())[0];
    sim.run_steps(3000);

    const object_counters &o = counters(u, 1);
    assert(o.ce_bits > 0.0);
    // Marks are a fraction of what arrived, not a restatement of it.
    assert(o.ce_bits <= o.delivered_bits);
    // And the object is still conserved: a mark is not a fate.
    assert(std::fabs((o.delivered_bits + o.dropped_bits() + o.expired_bits) / 8.0 - flood) < 1500.0);
}

// The same flood without the field earns no marks: not-ECT traffic can only be dropped,
// so a client that never asked for ECN sees exactly the behaviour it saw before.
void test_not_ect_traffic_is_never_marked()
{
    const double flood = 2000000.0;

    write_file(TIMELINE, inject(10, 2, (long)flood, nullptr));
    write_config(TIMELINE);

    simulator sim(CONFIG);
    ue &u = (*sim.ue_list())[0];
    sim.run_steps(3000);

    const object_counters &o = counters(u, 2);
    assert(o.ce_bits == 0.0);
    assert(o.dropped_bits() + o.expired_bits > 0.0);
}

// An AQM drop is not a late packet. The dual queue fires off its own target -1 ms here-
// and the delay budget is a different threshold three orders of magnitude away, so with
// the budget raised out of reach the AQM still drops and nothing expires. This is the
// configuration the co-simulation harness runs its driven UEs in, and the reason the two
// causes are counted apart rather than added together.
void test_the_aqm_drops_long_before_the_budget_would()
{
    const double flood = 2000000.0;

    write_file(TIMELINE, inject(10, 5, (long)flood, nullptr));
    write_config(TIMELINE, 60.0);

    simulator sim(CONFIG);
    ue &u = (*sim.ue_list())[0];
    sim.run_steps(3000);

    const object_counters &o = counters(u, 5);
    assert(o.queue_dropped_bits > 0.0);
    assert(o.expired_bits == 0.0);
    // Good channel at 300 m, so the losses are the queue's and not the link's.
    assert(o.radio_dropped_bits == 0.0);
    // And dropped_bytes, which is what the co-simulation spec closes an object with,
    // still means every non-expiry loss.
    assert(o.dropped_bits() == o.queue_dropped_bits);
}

// Two objects on one UE, one scalable and one classic, are marked apart. This is what a
// per UE mark counter could not give: a sender needs the marks of its own flow.
void test_marks_are_attributed_per_object()
{
    write_file(TIMELINE, inject(10, 3, 1000000, "ect1") + inject(10, 4, 1000000, nullptr));
    write_config(TIMELINE);

    simulator sim(CONFIG);
    ue &u = (*sim.ue_list())[0];
    sim.run_steps(3000);

    assert(counters(u, 3).ce_bits > 0.0);
    assert(counters(u, 4).ce_bits == 0.0);
}
}

int main()
{
    test_the_ecn_field_is_parsed_by_name();
    test_the_events_cursor_is_explicit();
    test_ect1_is_marked_instead_of_dropped();
    test_not_ect_traffic_is_never_marked();
    test_the_aqm_drops_long_before_the_budget_would();
    test_marks_are_attributed_per_object();
    std::remove(TIMELINE);
    std::remove(CONFIG);
    return 0;
}
