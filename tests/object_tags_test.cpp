// Per object accounting: the bits of each object are counted apart, every one of them
// ends in exactly one terminal state, and the sum over objects is the UE's total.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include <simulator/simulator.h>
#include <ue/ue.h>

namespace
{
const char *TIMELINE = "build/tests/object_tags_timeline.ndjson";
const char *CONFIG = "build/tests/object_tags.ini";

void write_file(const char *path, const std::string &text)
{
    std::ofstream out(path);
    assert(out.is_open());
    out << text;
}

// The control smoke scenario with both generators silenced, so that the only traffic is
// what the test injects and every byte can be accounted for by tag.
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

std::string inject(int tti, std::uint32_t tag, long bytes)
{
    return "{\"id\":" + std::to_string(tti) + ",\"at_tti\":" + std::to_string(tti)
         + ",\"cmds\":[{\"op\":\"inject\",\"target\":\"ue/0\",\"tag\":" + std::to_string(tag)
         + ",\"dl.bytes\":" + std::to_string(bytes) + "}]}\n";
}

const object_counters &counters(ue &u, std::uint32_t tag)
{
    const std::unordered_map<std::uint32_t, object_counters> &objects = u.pdcp_state(TX_DL).objects();
    std::unordered_map<std::uint32_t, object_counters>::const_iterator it = objects.find(tag);
    assert(it != objects.end());
    return it->second;
}

bool known(ue &u, std::uint32_t tag)
{
    const std::unordered_map<std::uint32_t, object_counters> &objects = u.pdcp_state(TX_DL).objects();
    return objects.find(tag) != objects.end();
}

double delivered(const object_counters &o) { return o.delivered_bits / 8.0; }
double lost(const object_counters &o) { return (o.dropped_bits() + o.expired_bits) / 8.0; }

// Traffic from the generator, and injection through the knob, carry no tag.
void test_untagged_traffic_creates_no_objects()
{
    write_file(TIMELINE,
               "{\"id\":1,\"at_tti\":10,\"cmds\":[{\"target\":\"ue/0\",\"set\":{\"dl.inject_bytes\":50000}}]}\n");
    write_config(TIMELINE, "2.0");

    simulator sim(CONFIG);
    ue &u = (*sim.ue_list())[0];

    sim.run_steps(300);
    assert(u.pdcp_state(TX_DL).delivered_bits_total() > 0.0);
    assert(u.pdcp_state(TX_DL).objects().empty());
}

// Two objects on one UE, each one accounted for on its own, and the two together
// accounting for everything the UE received.
void test_two_objects_are_counted_apart()
{
    const double first = 120000.0;
    const double second = 80000.0;

    write_file(TIMELINE, inject(10, 7, (long)first) + inject(10, 9, (long)second));
    write_config(TIMELINE, "0.0");

    simulator sim(CONFIG);
    ue &u = (*sim.ue_list())[0];

    sim.run_steps(10);
    assert(u.pdcp_state(TX_DL).objects().empty());   // not one step early

    sim.run_steps(1);
    assert(known(u, 7) && known(u, 9));
    assert(u.pdcp_state(TX_DL).injected_bits_total() / 8.0 == first + second);

    sim.run_steps(500);

    // Each object is conserved on its own: what went in came out or was lost.
    assert(std::fabs(delivered(counters(u, 7)) + lost(counters(u, 7)) - first) < 1500.0);
    assert(std::fabs(delivered(counters(u, 9)) + lost(counters(u, 9)) - second) < 1500.0);
    assert(delivered(counters(u, 7)) > first * 0.9);
    assert(delivered(counters(u, 9)) > second * 0.9);

    // And the objects account for the whole UE, with nothing attributed twice.
    const double per_ue = u.pdcp_state(TX_DL).delivered_bits_total() / 8.0;
    const double per_tag = delivered(counters(u, 7)) + delivered(counters(u, 9));
    assert(std::fabs(per_ue - per_tag) < 1500.0);
}

// An object whose size is not a whole number of packets is still conserved: fragments
// inherit the tag through the copy constructor, and a packet split between a delivery
// and a loss contributes to both counters.
void test_fragments_inherit_the_tag()
{
    const double odd = 3333.0;

    write_file(TIMELINE, inject(10, 4, (long)odd));
    write_config(TIMELINE, "0.0");

    simulator sim(CONFIG);
    ue &u = (*sim.ue_list())[0];
    sim.run_steps(300);

    const object_counters &o = counters(u, 4);
    assert(std::fabs(delivered(o) + lost(o) - odd) < 1.0);
    assert(delivered(o) == odd);
}

// Each loss is counted against the object whose bits were lost. That is attribution and
// not blame: the UE's queue is one FIFO, so an object that overfeeds it ages its
// neighbours' packets as well, and they expire behind it. Which is why the sender window
// that bounds injection has to be per UE and not per object.
void test_losses_are_counted_per_object()
{
    const double flood = 3000000.0;
    const double modest = 40000.0;

    write_file(TIMELINE, inject(10, 1, (long)flood) + inject(10, 2, (long)modest));
    write_config(TIMELINE, "0.0");

    simulator sim(CONFIG);
    ue &u = (*sim.ue_list())[0];
    sim.run_steps(3000);

    // The object that overfed loses bytes to the delay budget, and so does the one
    // queued behind it.
    assert(counters(u, 1).expired_bits > 0.0);
    assert(counters(u, 2).expired_bits > 0.0);

    // Each account is exact regardless, which is what lets the client recover its own
    // bytes without having to guess whose they were.
    assert(std::fabs(delivered(counters(u, 1)) + lost(counters(u, 1)) - flood) < 1500.0);
    assert(std::fabs(delivered(counters(u, 2)) + lost(counters(u, 2)) - modest) < 1500.0);

    const double per_ue = u.pdcp_state(TX_DL).expired_bits_total() / 8.0;
    const double per_tag = (counters(u, 1).expired_bits + counters(u, 2).expired_bits) / 8.0;
    assert(std::fabs(per_ue - per_tag) < 1500.0);
}

// The emulator cannot know that an object is finished, so the client says when its
// counters may go. Until then they stay readable.
void test_forget_releases_the_counters()
{
    write_file(TIMELINE,
               inject(10, 5, 60000)
               + "{\"id\":2,\"at_tti\":800,\"cmds\":[{\"op\":\"forget\",\"target\":\"ue/0\",\"tag\":5}]}\n");
    write_config(TIMELINE, "0.0");

    simulator sim(CONFIG);
    ue &u = (*sim.ue_list())[0];

    sim.run_steps(400);
    assert(known(u, 5));
    assert(delivered(counters(u, 5)) == 60000.0);

    sim.run_steps(500);
    assert(!known(u, 5));

    // And the UE's own counters are untouched by forgetting an object.
    assert(u.pdcp_state(TX_DL).delivered_bits_total() / 8.0 == 60000.0);
}

// Per object counters are cumulative and never go backwards, which is what lets a client
// diff two reads.
void test_counters_are_monotonic()
{
    write_file(TIMELINE, inject(10, 3, 100000) + inject(200, 3, 100000));
    write_config(TIMELINE, "0.0");

    simulator sim(CONFIG);
    ue &u = (*sim.ue_list())[0];

    double last_out = 0.0, last_lost = 0.0;
    for (int i = 0; i < 40; i++)
    {
        sim.run_steps(25);
        if (!known(u, 3)) continue;
        assert(delivered(counters(u, 3)) >= last_out);
        assert(lost(counters(u, 3)) >= last_lost);
        last_out = delivered(counters(u, 3));
        last_lost = lost(counters(u, 3));
    }
    assert(last_out + last_lost == 200000.0);
}
}

int main()
{
    test_untagged_traffic_creates_no_objects();
    test_two_objects_are_counted_apart();
    test_fragments_inherit_the_tag();
    test_losses_are_counted_per_object();
    test_forget_releases_the_counters();
    test_counters_are_monotonic();
    std::remove(TIMELINE);
    std::remove(CONFIG);
    return 0;
}
