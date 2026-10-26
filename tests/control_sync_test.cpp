// Barrier mode: credits gate simulated time, a lost peer fails open, and real time never
// blocks. Driven through simulator::run_steps with a client on its own thread.
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <nlohmann/json.hpp>
#include <simulator/simulator.h>
#include <utils/control/proto_version.h>

using json = nlohmann::json;

namespace
{
const char *SOCKET_PATH = "build/tests/control_sync.sock";
const char *CONFIG = "build/tests/control_sync.ini";

double ms_since(const std::chrono::steady_clock::time_point &t0)
{
    return std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
        std::chrono::steady_clock::now() - t0).count();
}

void write_config(const std::string &sync_mode, int period_ms, int timeout_ms,
                  const std::string &on_timeout, const std::string &on_peer_loss = "continue",
                  int max_cmds_per_tick = -1)
{
    std::ifstream base("tests/control_smoke.ini");
    assert(base.is_open());

    std::string line, text;
    while (std::getline(base, line))
    {
        if (line.rfind("period:", 0) == 0) line = "period: " + std::to_string(period_ms);
        if (line.rfind("transport:", 0) == 0) line = "transport: unix";
        if (line.rfind("timeline_file:", 0) == 0)
            line = std::string("address: ") + SOCKET_PATH
                 + "\nsync_mode: " + sync_mode
                 + "\ncredit_timeout_ms: " + std::to_string(timeout_ms)
                 + "\non_timeout: " + on_timeout
                 + "\non_peer_loss: " + on_peer_loss
                 + (max_cmds_per_tick > 0
                        ? "\nmax_cmds_per_tick: " + std::to_string(max_cmds_per_tick)
                        : "");
        text += line + "\n";
    }

    std::ofstream out(CONFIG);
    out << text;
}

// Minimal client: connect, handshake, then send lines. Lives in the test thread while the
// simulation runs on another one, or the other way around.
class client
{
public:
    bool connect_and_handshake()
    {
        fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd_ < 0) return false;

        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

        for (int attempt = 0; attempt < 100; attempt++)
        {
            if (::connect(fd_, (struct sockaddr *)&addr, sizeof(addr)) == 0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (attempt == 99) return false;
        }

        const std::string hello = read_line();
        if (hello.empty()) return false;
        json h = json::parse(hello);
        if (h["proto"].get<std::string>() != FIKORE_CONTROL_PROTO) return false;

        json reply;
        reply["proto"] = FIKORE_CONTROL_PROTO;
        write_line(reply.dump());
        return true;
    }

    void grant(std::int64_t until_tti)
    {
        json j;
        j["id"] = ++id_;
        j["op"] = "grant";
        j["until_tti"] = until_tti;
        write_line(j.dump());
    }

    std::string read_line()
    {
        std::string out;
        char ch = 0;
        while (::recv(fd_, &ch, 1, 0) == 1)
        {
            if (ch == '\n') return out;
            out.push_back(ch);
        }
        return out;
    }

    // For the cases that assert a reply does *not* come: without this, read_line blocks
    // in recv for good and the test hangs instead of failing.
    void set_read_timeout(int ms)
    {
        struct timeval tv;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    void write_line(const std::string &text)
    {
        const std::string line = text + "\n";
        ::send(fd_, line.data(), line.size(), MSG_NOSIGNAL);
    }

    void disconnect()
    {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }

private:
    int fd_ = -1;
    std::uint64_t id_ = 0;
};

// Simulated time advances exactly up to the granted TTI and stops there.
void test_credit_gates_simulated_time()
{
    write_config("barrier", -1, 400, "continue");
    simulator sim(CONFIG);
    assert(sim.control_plane().barrier_mode());

    client c;
    assert(c.connect_and_handshake());

    c.grant(5);
    const std::string ack = c.read_line();
    json a = json::parse(ack);
    assert(a["status"] == "ok");
    assert(a["credit_until_tti"] == 5);

    // TTIs 0..5 are paid for, so they run without waiting.
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    sim.run_steps(6);
    assert(ms_since(t0) < 400.0);

    // TTI 6 is not, so the barrier holds until the timeout expires.
    t0 = std::chrono::steady_clock::now();
    sim.run_steps(1);
    assert(ms_since(t0) >= 350.0);

    // Credit never moves backwards: a grant into the past is a no-op answered with ok.
    c.grant(2);
    a = json::parse(c.read_line());
    assert(a["status"] == "ok");
    assert(a["credit_until_tti"] == 5);

    c.disconnect();
}

// With on_peer_loss: continue, losing the peer releases the emulator, for good.
void test_lost_peer_can_fail_open()
{
    write_config("barrier", -1, 5000, "continue", "continue");
    simulator sim(CONFIG);

    client c;
    assert(c.connect_and_handshake());
    c.grant(3);
    c.read_line();
    sim.run_steps(4);

    c.disconnect();
    // The socket thread needs a moment to notice; the barrier would otherwise wait the
    // full five seconds.
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    sim.run_steps(50);
    assert(ms_since(t0) < 3000.0);
    assert(!sim.control_plane().barrier_mode());   // degraded, and it stays degraded
}

// By default, losing the peer ends the run instead of finishing it unsynchronised.
void test_lost_peer_aborts_by_default()
{
    write_config("barrier", -1, 5000, "continue", "abort");
    simulator sim(CONFIG);

    client c;
    assert(c.connect_and_handshake());
    c.grant(3);
    c.read_line();
    sim.run_steps(4);
    assert(!sim.control_plane().stop_requested());

    c.disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    sim.run_steps(1);
    assert(ms_since(t0) < 3000.0);                 // released, not waiting for the timeout
    assert(sim.control_plane().stop_requested());
    assert(sim.control_plane().stop_reason() == run_stop_reason::control_peer_lost);
    assert(sim.control_plane().barrier_mode());    // still in barrier: it stops, it does not degrade
}

// Barrier plus real time is refused at init: blocking the wall clock defeats the mode.
void test_barrier_is_refused_in_real_time()
{
    write_config("barrier", 1, 400, "continue");
    simulator sim(CONFIG);
    assert(!sim.control_plane().barrier_mode());

    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    sim.run_steps(20);
    assert(ms_since(t0) < 400.0);                  // never blocked
}

// on_timeout: abort asks for the run to end instead of carrying on uncontrolled.
void test_timeout_abort_requests_stop()
{
    write_config("barrier", -1, 200, "abort");
    simulator sim(CONFIG);

    client c;
    assert(c.connect_and_handshake());
    c.grant(1);
    c.read_line();

    sim.run_steps(2);
    assert(!sim.control_plane().stop_requested());

    sim.run_steps(1);                              // TTI 2, no credit
    assert(sim.control_plane().stop_requested());
    assert(sim.control_plane().stop_reason() == run_stop_reason::credit_timeout);

    c.disconnect();
}

// Commands belong to the connection that submitted them. A future command from a
// disconnected async controller must neither mutate state nor send its ack to the
// controller that connects afterwards.
void test_scheduled_command_does_not_cross_a_reconnection()
{
    write_config("async", -1, 400, "abort");
    simulator sim(CONFIG);
    const float original = (*sim.ue_list())[0].overrides().priority;

    client first;
    assert(first.connect_and_handshake());
    first.write_line(
        "{\"id\":77,\"at_tti\":10,\"cmds\":["
        "{\"target\":\"ue/0\",\"set\":{\"priority\":9}}]}");
    first.disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    client second;
    assert(second.connect_and_handshake());
    sim.run_steps(11);
    assert((*sim.ue_list())[0].overrides().priority == original);
    second.set_read_timeout(100);
    assert(second.read_line().empty());
    second.disconnect();
}

void test_credit_does_not_cross_a_reconnection()
{
    write_config("barrier", -1, 200, "abort", "abort");
    simulator sim(CONFIG);

    client first;
    assert(first.connect_and_handshake());
    first.grant(100);
    assert(json::parse(first.read_line())["credit_until_tti"] == 100);
    first.disconnect();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    client second;
    assert(second.connect_and_handshake());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    sim.run_steps(1);
    assert(sim.control_plane().stop_requested());
    assert(sim.control_plane().stop_reason() == run_stop_reason::credit_timeout);
    second.disconnect();
}

void test_textual_ue_id_addresses_the_configured_instance()
{
    write_config("async", -1, 400, "abort");
    simulator sim(CONFIG);
    assert((*sim.ue_list())[0].get_control_id() == "controlSmoke_0");
    assert((*sim.ue_list())[1].get_control_id() == "controlSmoke_1");

    client c;
    assert(c.connect_and_handshake());
    c.write_line(
        "{\"id\":88,\"at_tti\":0,\"cmds\":["
        "{\"target\":\"ue/controlSmoke_1\",\"set\":{\"priority\":7}}]}");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sim.run_steps(1);
    assert(json::parse(c.read_line())["status"] == "ok");
    assert((*sim.ue_list())[0].overrides().priority == 1.0f);
    assert((*sim.ue_list())[1].overrides().priority == 7.0f);

    c.write_line(
        "{\"id\":89,\"at_tti\":1,\"cmds\":["
        "{\"op\":\"get\",\"target\":\"ue/controlSmoke_1\"}]}");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sim.run_steps(1);
    const json ack = json::parse(c.read_line());
    assert(ack["result"][0]["target"] == "ue/1");
    assert(ack["result"][0]["ue_id"] == "controlSmoke_1");
    c.disconnect();
}
}

// A message carrying more commands than max_cmds_per_tick used to stall the barrier for
// the whole credit timeout. Deferring them left the client blocked on acknowledgements
// that only the next TTI could produce, and it could not grant that TTI without first
// stopping to read. The cap is for keeping a real-time run on schedule, and a barrier run
// is never in real time, so it no longer applies there.
void test_a_batch_over_the_cap_does_not_stall_the_barrier()
{
    const int cap = 4;
    const int commands = 10;

    write_config("barrier", -1, 3000, "continue", "continue", cap);
    simulator sim(CONFIG);
    assert(sim.control_plane().barrier_mode());

    client c;
    assert(c.connect_and_handshake());

    std::string cmds;
    for (int i = 0; i < commands; i++)
    {
        if (i > 0) cmds += ",";
        cmds += "{\"op\":\"inject\",\"target\":\"ue/0\",\"tag\":" + std::to_string(i + 1)
              + ",\"dl.bytes\":1000}";
    }
    c.write_line("{\"id\":1,\"at_tti\":2,\"cmds\":[" + cmds + "]}");
    c.grant(2);
    assert(json::parse(c.read_line())["status"] == "ok");   // the grant, from the socket thread

    // One reply per command, all of them from the TTI that was paid for. Counted rather
    // than read blindly: with the cap in force only `cap` of them would ever arrive, and
    // a test that hangs is worth less than one that fails.
    sim.run_steps(3);
    c.set_read_timeout(500);
    int replies = 0;
    while (replies < commands)
    {
        const std::string line = c.read_line();
        if (line.empty()) break;
        assert(json::parse(line)["status"] == "ok");
        replies++;
    }
    assert(replies == commands);

    c.disconnect();
}

int main()
{
    test_credit_gates_simulated_time();
    test_a_batch_over_the_cap_does_not_stall_the_barrier();
    test_lost_peer_can_fail_open();
    test_lost_peer_aborts_by_default();
    test_barrier_is_refused_in_real_time();
    test_timeout_abort_requests_stop();
    test_scheduled_command_does_not_cross_a_reconnection();
    test_credit_does_not_cross_a_reconnection();
    test_textual_ue_id_addresses_the_configured_instance();
    std::remove(CONFIG);
    return 0;
}
