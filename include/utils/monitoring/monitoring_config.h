#pragma once

#include <string>
#include <vector>
#include <map>

struct monitoring_output_config
{
    std::string name;
    std::string type; // udp, tcp, unix
    std::string address; // ip or unix path
    int port = 0; // for udp/tcp
};

struct monitoring_config
{
    bool enabled = false;
    int aggregation_window_ms = 1000;
    std::string default_metric_type = "gauge"; // gauge|counter
    bool emit_ue_phy = true;
    bool emit_ue_pdcp = true;
    bool emit_ue_queue = true;
    bool emit_ue_mobility = false;
    bool emit_l4s = true;
    bool emit_mac_scheduler = true;
    bool emit_emulator_runtime = true;
    bool emit_control = true;
    bool emit_runtime_debug_logs = false;
    bool emit_text_logs_compat = false;
    std::vector<monitoring_output_config> outputs;
    std::map<std::string,std::string> extra;
};
