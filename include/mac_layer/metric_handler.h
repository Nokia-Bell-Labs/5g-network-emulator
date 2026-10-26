/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#ifndef METRIC_HANDLER_H
#define METRIC_HANDLER_H

#include <math.h>
#include <string>
#include <algorithm>
#include <functional>
#include <optional>
#include <vector>
#include <iostream>
#include <math.h>

#include <common/direction.h>
#include <mac_layer/mac_definitions.h>

class max_metric_handler
{
private: 
    bool assigned = false; 
    float tp = 0.0; 
    float value = -1; 
    int index = -1; 
    int id = -1; 

public: 
    void reset();
    void assign(float _tp, float _value, int _index, int _id);
    bool evaluate(float metric);
    bool is_assigned() const;
    bool has_data() const;
    float get_tp() const;
    float get_value() const;
    int get_index() const;
    int get_id() const;
};

// Stores the metric info
struct metric_info{
    double req_time = 0; 
    float avrg_tp = 0; 
    float current_tp = 0; 
    double current_delay = 0; 
    float delta = 0.001; 
    double delay_t = 0.1; 
    int index = 0; 
    // Who is being scored and in which direction (TX_DL / TX_UL), for metrics that keep
    // state per UE and direction. The built-in metrics do not read them.
    int ue_id = -1;
    int tx_dir = TX_DL;
};

struct throughput_metric_recipe
{
    float rate_exponent = 0.0f;
    float history_exponent = 0.0f;
    bool uses_history = false;
    bool supports_provisional_history = false;
};

// Everything a metric may need from the configuration when a handler is built. The
// built-ins read pf_alpha only; the rest is carried for metrics registered from outside.
struct metric_params
{
    float bet_beta = 0.9f;
    double delay_t = 0.001;
    float delta = 0.8f;
    float pf_alpha = 1.0f;
};

typedef std::function<float(const metric_info &, double current_t, int f)> metric_score_fn;

// What a handler uses once a metric has been resolved.
struct bound_metric
{
    metric_score_fn score;
    throughput_metric_recipe recipe;
};

// A scheduler metric: an identifier, a score per UE and resource block group, and the
// recipe that tells phy_layer how to feed it.
struct metric_entry
{
    int id = -1;
    std::string name;
    metric_score_fn score;
    throughput_metric_recipe recipe;
    // Set for metrics that rank by rate: phy_layer evaluates them on every TTI instead of
    // reusing the value cached at the last CQI refresh. A metric whose recipe uses
    // throughput history is always evaluated that way.
    bool throughput_based = false;
    // Set for round robin: the score is not used, the turn is decided by get_rr_metric.
    bool round_robin = false;
    // Optional. For a metric whose score or recipe depends on the configuration (the
    // exponent of PF is configured), builds the callable and the recipe for one handler.
    // When empty, score and recipe are used as they are.
    std::function<bound_metric(const metric_params &)> bind;
};

// Metrics by identifier. The built-in metrics are registered first, through the same
// function as any other, so their identifiers cannot be taken. Registering is meant for
// static registrars in any translation unit: the list is built on first use, which makes
// the order of static initialisation irrelevant.
class metric_registry
{
public:
    // Throws std::invalid_argument if the identifier is already registered or the entry
    // has neither score nor bind. Registering while the program starts up terminates it
    // with that message, which is the intended outcome for a clash.
    static void register_metric(metric_entry entry);
    static std::optional<metric_entry> find(int id);
    // Registered identifiers, in registration order.
    static std::vector<int> ids();
};

// Registers at construction, for use as a namespace-scope object in a translation unit.
struct metric_registrar
{
    explicit metric_registrar(metric_entry entry);
};

// The six metrics return the base expression, without the UE priority. The priority is
// an absolute runtime knob (ue_overrides::priority) and is applied by phy_layer::get_metric
// at the point of reading, so that changing it takes effect on the next TTI instead of
// waiting for the next CQI refresh, which is when metric_v[] gets rewritten.
class metric_handler
{
public: 
    metric_handler(
        int _metric_type,
        float _bet_beta = 0.9,
        double _delay_t = 0.001,
        float _delta = 0.8,
        float _pf_alpha = 1.0);
private: 
    int rr_index = 0; 
    int prev_f = -1; 
    int metric_t; 
    bool round_robin = false;
    bool throughput_based = false;
    throughput_metric_recipe throughput_recipe;
    double delay_t; 
    float delta;
    // Resolved once at construction, so scoring costs one indirect call.
    metric_score_fn score_fn;

public: 
    void add_ue(std::string id);  
    void remove_ue(std::string id);
    float get_metric(const metric_info &metric_i, double current_t, int f);
    // The registered entry for an identifier. Throws std::invalid_argument for one that
    // is not registered: an unknown metric is a configuration error, not round robin.
    static metric_entry check_metric(int metric);
    // rank/n_enabled are the dense position of the UE among the enabled ones and how many
    // are enabled, not the UE id and the total count: a disabled UE must not take a turn.
    float get_rr_metric(int f, int rank, int n_enabled);
    bool is_rr();
    bool is_pf() const;
    bool is_throughput_metric() const;
    bool uses_throughput_history() const;
    bool supports_provisional_history() const;
    const throughput_metric_recipe &get_throughput_recipe() const;
};

#endif