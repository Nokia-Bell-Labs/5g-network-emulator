/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#include <stdexcept>
#include <utility>

#include <mac_layer/metric_handler.h>

#include "observability.h"

//--------------------------------------------------------------------------------------------------
// "pf_counted": proportional fair, instrumented.
//
// The score is the one of the built-in PF, because it is the built-in PF: bind() asks the registry
// for it and wraps the callable it returns. The wrapper counts the call and passes the arguments
// and the result through untouched, so a run with metric_type 1000 schedules exactly like a run
// with metric_type 6.
//
// score() is called many times per TTI, for each UE and each resource block group, from the
// threads of the grid and the UEs, and several times for the same UE at once. The wrapper is
// therefore read-only apart from one relaxed atomic increment.
//--------------------------------------------------------------------------------------------------

namespace
{

bound_metric bind_counted_pf(const metric_params &params)
{
    const std::optional<metric_entry> pf = metric_registry::find(METRIC_PF);
    if (!pf)
        throw std::logic_error("the built-in PF metric is not registered");
    bound_metric inner =
        pf->bind ? pf->bind(params) : bound_metric{pf->score, pf->recipe};

    bound_metric counted;
    counted.recipe = inner.recipe;
    counted.score = [score = std::move(inner.score)](const metric_info &info, double current_t, int f)
    {
        observability::count_score(info.tx_dir, info.ue_id);
        return score(info, current_t, f);
    };
    return counted;
}

metric_entry counted_pf_entry()
{
    metric_entry entry;
    entry.id = observability::counted_pf_metric_id;
    entry.name = "pf_counted";
    // What the entry says before a handler binds it, the same as PF.
    entry.recipe = {1.0f, 1.0f, true, true};
    entry.throughput_based = true;
    entry.bind = bind_counted_pf;
    return entry;
}

const metric_registrar counted_pf_registration(counted_pf_entry());

} // namespace
