#include <atomic>
#include <mutex>
#include <stdexcept>

#include <pdcp_layer/pkt_fate.h>

namespace
{
// A fixed-capacity list that becomes immutable at its first use. It is constant-initialised and
// trivially destructible apart from the mutex, which only registration and the one-off freeze
// take, so a function-local static of this type needs no guard and is safe to reach from the
// static initialisation of any translation unit. The UE threads read `count` and `fns` after an
// acquire load of `frozen`, with no lock.
struct observer_list
{
    std::mutex mtx;
    std::atomic<bool> frozen{false};
    std::atomic<std::uint64_t> failures{0};
    std::size_t count = 0;
    pkt_fate_observer fns[max_fate_observers] = {};

    void add(pkt_fate_observer fn)
    {
        if (fn == nullptr)
            throw std::invalid_argument("null function registered for packet fates");
        std::lock_guard<std::mutex> lock(mtx);
        if (frozen.load(std::memory_order_relaxed))
            throw std::logic_error(
                "packet fate registry is frozen: register before the first packet is handled");
        if (count == max_fate_observers)
            throw std::length_error("packet fate registry is full");
        fns[count++] = fn;
    }

    std::size_t freeze()
    {
        if (!frozen.load(std::memory_order_acquire))
        {
            std::lock_guard<std::mutex> lock(mtx);
            frozen.store(true, std::memory_order_release);
        }
        return count;
    }

    std::size_t size()
    {
        std::lock_guard<std::mutex> lock(mtx);
        return count;
    }
};

observer_list &observers()
{
    static observer_list list;
    return list;
}
}

void register_fate_observer(pkt_fate_observer fn)
{
    observers().add(fn);
}

std::size_t fate_observer_count()
{
    return observers().size();
}

std::uint64_t fate_observer_failures()
{
    return observers().failures.load(std::memory_order_relaxed);
}

std::size_t freeze_fate_observers()
{
    return observers().freeze();
}

void notify_pkt_fate(const pkt_fate_event &event) noexcept
{
    observer_list &list = observers();
    const std::size_t n = list.freeze();
    for (std::size_t i = 0; i < n; i++)
    {
        try
        {
            list.fns[i](event);
        }
        catch (...)
        {
            list.failures.fetch_add(1, std::memory_order_relaxed);
        }
    }
}
