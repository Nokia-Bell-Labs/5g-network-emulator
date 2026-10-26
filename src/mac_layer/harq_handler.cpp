/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#include <mac_layer/harq_handler.h>
#include <phy_layer/phy_l_definitions.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
std::uint32_t derived_seed(std::uint32_t base, std::uint32_t stream)
{
    std::seed_seq sequence{base, stream, 0x9e3779b9U};
    std::array<std::uint32_t, 1> output{};
    sequence.generate(output.begin(), output.end());
    return output[0];
}

int checked_rbg_index(int rbg_prbs)
{
    for (int index = 0; index < 5; index++)
    {
        if (RBG_S[index] == rbg_prbs)
            return index;
    }
    throw std::out_of_range(
        "legacy_bler requires an RBG size of 1, 2, 4, 8, or 16 PRBs");
}
} // namespace

harq_handler::harq_handler(int _max_rtx, float _air_delay,
                 float _rtx_period, float _rtx_period_var,
                 float _rtx_p_delay, float _rtx_p_delay_var,
                 unsigned int _seed, int _verbosity,
                 harq_model _model, std::size_t _max_queue_blocks)
    : max_rtx(_max_rtx),
      air_delay_var(_air_delay),
      rtx_p_delay(_rtx_p_delay),
      rtx_p_delay_var(_rtx_p_delay_var),
      rtx_period(_rtx_period),
      rtx_period_var(_rtx_period_var),
      verbosity(_verbosity),
      model(_model),
      max_queue_blocks(_max_queue_blocks),
      bler_generator(derived_seed(_seed, 0x48415251U)),
      air_delay_generator(derived_seed(_seed, 0x41495244U)),
      rtx_period_generator(derived_seed(_seed, 0x52545850U)),
      processing_delay_generator(derived_seed(_seed, 0x50524f43U))
{
    if (max_rtx < 0)
        throw std::invalid_argument("max_rtx must be non-negative");
    if (max_queue_blocks == 0)
        throw std::invalid_argument("HARQ queue capacity must be positive");
    for (float value : {
             air_delay_var,
             rtx_period,
             rtx_period_var,
             rtx_p_delay,
             rtx_p_delay_var})
    {
        if (!std::isfinite(value) || value < 0.0f)
            throw std::invalid_argument(
                "HARQ delays must be finite and non-negative seconds");
    }
}

void harq_handler::step(float t)
{
    if (!std::isfinite(t))
        throw std::invalid_argument("HARQ time must be finite");
    current_t = t;
}

bool harq_handler::enqueue_retry(
    harq_pkt &pkt,
    float distance,
    int retry_ordinal)
{
    if (harq_buffer.size() >= max_queue_blocks)
        return false;
    if (!std::isfinite(distance) || distance < 0.0f)
        throw std::invalid_argument(
            "HARQ distance must be finite and non-negative");
    if (retry_ordinal < 1 || retry_ordinal > max_rtx)
        throw std::out_of_range(
            "HARQ retry ordinal is outside configured max_rtx");
    pkt.attempt_ordinal = retry_ordinal;
    pkt.distance = distance;
    pkt.t_out = current_t + emulate_ack_delay(distance);
    harq_buffer.push_back(std::move(pkt));
    high_water_blocks = std::max(high_water_blocks, harq_buffer.size());
    return true;
}

bool harq_handler::is_pkt_ready() const
{
    return !harq_buffer.empty() && harq_buffer.front().t_out <= current_t;
}

harq_pkt harq_handler::get_pkt()
{
    if (!is_pkt_ready())
        throw std::logic_error("no HARQ block is ready");
    harq_pkt pkt = std::move(harq_buffer.front());
    harq_buffer.pop_front();
    return pkt;
}

bool harq_handler::pop_pkt_older_than(float oldest_allowed_ip_t, harq_pkt& out_pkt)
{
    for(std::deque<harq_pkt>::iterator it = harq_buffer.begin(); it != harq_buffer.end(); ++it)
    {
        if(it->ip_t >= oldest_allowed_ip_t) continue;

        out_pkt = std::move(*it);
        harq_buffer.erase(it);
        return true;
    }

    return false;
}

float harq_handler::get_oldest_t() const
{
    return harq_buffer.empty()
               ? current_t
               : harq_buffer.front().current_t;
}

const harq_pkt* harq_handler::peek_oldest() const
{
    if(harq_buffer.empty()) return nullptr;
    return &harq_buffer.front();
}

std::uint64_t harq_handler::queued_bits() const
{
    std::uint64_t bits = 0;
    for (const harq_pkt &pkt : harq_buffer)
    {
        if (pkt.bits
            > std::numeric_limits<std::uint64_t>::max() - bits)
            throw std::overflow_error("HARQ queued bit count overflow");
        bits += pkt.bits;
    }
    return bits;
}

float harq_handler::emulate_ack_delay(float distance)
{
    const float propagation = std::max(
        0.0f,
        static_cast<float>(2.0 * distance / SPEED_OF_LIGHT)
            + air_delay_var * signed_unit(air_delay_generator));
    const float feedback = std::max(
        0.0f,
        rtx_period
            + rtx_period_var * signed_unit(rtx_period_generator));
    const float processing = std::max(
        0.0f,
        rtx_p_delay
            + rtx_p_delay_var
                * signed_unit(processing_delay_generator));
    return propagation + feedback + processing;
}

void harq_handler::init(int _mod_i, int _rbg_prbs)
{
    if (_mod_i < 0 || _mod_i >= 2)
        throw std::out_of_range(
            "legacy_bler modulation index must be 0 or 1");
    (void)checked_rbg_index(_rbg_prbs);
    mod_i = _mod_i;
    rbg_prbs = _rbg_prbs;
    lookup_context_initialized = true;
}

double harq_handler::legacy_bler(
    int modulation_index,
    int rbg_prbs,
    int layers,
    int mcs,
    float sinr)
{
    if (modulation_index < 0 || modulation_index >= 2)
        throw std::out_of_range(
            "legacy_bler modulation index must be 0 or 1");
    const int rbg_index = checked_rbg_index(rbg_prbs);
    if (layers < 1 || layers > 4)
        throw std::out_of_range(
            "legacy_bler supports one through four layers");
    if (mcs < 0 || mcs >= 28)
        throw std::out_of_range(
            "legacy_bler supports MCS indexes 0 through 27");
    if (!std::isfinite(sinr))
        throw std::invalid_argument("legacy_bler SINR must be finite");
    const int sinr_index = SINR_TO_INDEX(sinr);
    const double bler = LEGACY_BLER_MCS_SINR[
        modulation_index][rbg_index][layers - 1][mcs][sinr_index];
    if (!std::isfinite(bler) || bler < 0.0 || bler > 1.0)
        throw std::runtime_error(
            "legacy_bler table contains an invalid probability");
    return bler;
}

double harq_handler::legacy_failure_probability(
    double bler,
    int attempt_ordinal)
{
    if (!std::isfinite(bler))
        throw std::invalid_argument("BLER must be finite");
    if (attempt_ordinal < 0)
        throw std::out_of_range(
            "HARQ attempt ordinal must be non-negative");
    const double bounded_bler = std::clamp(bler, 0.0, 1.0);
    const double success =
        std::pow(1.0 - bounded_bler, attempt_ordinal + 1);
    const double failure =
        (1.0 - success) * std::pow(0.8, attempt_ordinal - 1);
    return std::clamp(failure, 0.0, 1.0);
}

double harq_handler::draw_outcome()
{
    if (!scripted_outcomes.empty())
    {
        const double outcome = scripted_outcomes.front();
        scripted_outcomes.pop_front();
        return outcome;
    }
    return unit_probability(bler_generator);
}

bool harq_handler::get_rtx(
    int mcs,
    float sinr,
    int attempt_ordinal,
    int layers)
{
    if (model == harq_model::disabled)
        return false;
    if (!lookup_context_initialized)
        throw std::logic_error(
            "legacy_bler lookup context was not initialized");
    const double probability = legacy_failure_probability(
        legacy_bler(mod_i, rbg_prbs, layers, mcs, sinr),
        attempt_ordinal);
    return probability > 0.0 && draw_outcome() <= probability;
}

void harq_handler::set_scripted_outcomes(
    const std::vector<double> &outcomes)
{
    scripted_outcomes.clear();
    for (double outcome : outcomes)
    {
        if (!std::isfinite(outcome)
            || outcome < 0.0
            || outcome > 1.0)
            throw std::invalid_argument(
                "scripted HARQ outcomes must be in [0, 1]");
        scripted_outcomes.push_back(outcome);
    }
}
