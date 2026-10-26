#pragma once

#include <algorithm>
#include <cmath>

class throughput_history_state
{
public:
    explicit throughput_history_state(float time_window_ms = 100.0f)
        : time_window_ms_(time_window_ms)
    {
    }

    void set_time_window_ms(float value)
    {
        time_window_ms_ = value;
    }

    void prepare(float standalone_rate)
    {
        if (initialized_)
            return;
        average_throughput_ = std::max(standalone_rate, epsilon_);
        initialized_ = true;
    }

    void update(float effective_bits, bool active)
    {
        if (!active)
            return;
        if (!initialized_)
            prepare(effective_bits);
        average_throughput_ =
            (1.0f - coefficient()) * average_throughput_
            + coefficient() * effective_bits;
    }

    void reset()
    {
        average_throughput_ = 0.0f;
        initialized_ = false;
    }

    bool initialized() const { return initialized_; }
    float average_throughput() const { return average_throughput_; }
    float time_window_ms() const { return time_window_ms_; }
    float epsilon() const { return epsilon_; }

    float projected_average(float current_tti_bits) const
    {
        if (!initialized_)
            return 0.0f;
        return std::max(
            (1.0f - coefficient()) * average_throughput_
                + coefficient() * current_tti_bits,
            epsilon_);
    }

private:
    float coefficient() const
    {
        return 1.0f - std::exp(-1.0f / time_window_ms_);
    }

    static constexpr float epsilon_ = 1e-6f;
    float time_window_ms_ = 100.0f;
    float average_throughput_ = 0.0f;
    bool initialized_ = false;
};
