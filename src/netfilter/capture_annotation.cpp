#include <atomic>
#include <mutex>
#include <stdexcept>

#include <netfilter/capture_annotation.h>

namespace
{
// A fixed-capacity list that becomes immutable at its first use. It is constant-initialised and
// trivially destructible apart from the mutex, which only registration and the one-off freeze
// take, so a function-local static of this type needs no guard and is safe to reach from the
// static initialisation of any translation unit. The reader threads read `count` and `fns` after an
// acquire load of `frozen`, with no lock.
template <typename Fn>
struct frozen_list
{
    std::mutex mtx;
    std::atomic<bool> frozen{false};
    std::atomic<std::uint64_t> failures{0};
    std::size_t count = 0;
    Fn fns[max_annotation_hooks] = {};

    void add(Fn fn)
    {
        if (fn == nullptr)
            throw std::invalid_argument("null function registered for packet annotation");
        std::lock_guard<std::mutex> lock(mtx);
        if (frozen.load(std::memory_order_relaxed))
            throw std::logic_error(
                "packet annotation registry is frozen: register before the first packet is handled");
        if (count == max_annotation_hooks)
            throw std::length_error("packet annotation registry is full");
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

frozen_list<packet_annotator> &annotators()
{
    static frozen_list<packet_annotator> list;
    return list;
}

frozen_list<ingest_observer> &observers()
{
    static frozen_list<ingest_observer> list;
    return list;
}
}

void register_annotator(packet_annotator fn)
{
    annotators().add(fn);
}

std::size_t annotator_count()
{
    return annotators().size();
}

std::uint64_t annotator_failures()
{
    return annotators().failures.load(std::memory_order_relaxed);
}

void annotate_packet(const nfq_packet_metadata &meta, ext_blob &blob) noexcept
{
    frozen_list<packet_annotator> &list = annotators();
    const std::size_t n = list.freeze();
    for (std::size_t i = 0; i < n; i++)
    {
        try
        {
            if (list.fns[i](meta, blob))
                return;
        }
        catch (...)
        {
            list.failures.fetch_add(1, std::memory_order_relaxed);
        }
        blob = ext_blob();
    }
}

void register_ingest_observer(ingest_observer fn)
{
    observers().add(fn);
}

std::size_t ingest_observer_count()
{
    return observers().size();
}

std::uint64_t ingest_observer_failures()
{
    return observers().failures.load(std::memory_order_relaxed);
}

std::size_t freeze_ingest_observers()
{
    return observers().freeze();
}

void notify_ingest_observers(
    std::uint32_t uid,
    int tx_dir,
    int queue_num,
    const ext_blob &blob) noexcept
{
    frozen_list<ingest_observer> &list = observers();
    const std::size_t n = list.freeze();
    for (std::size_t i = 0; i < n; i++)
    {
        try
        {
            list.fns[i](uid, tx_dir, queue_num, blob);
        }
        catch (...)
        {
            list.failures.fetch_add(1, std::memory_order_relaxed);
        }
    }
}
