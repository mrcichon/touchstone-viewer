#pragma once
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace core {

template <typename Work>
void run_in_parallel(size_t count, Work work) {
    if (count == 0) return;
    size_t threads = std::thread::hardware_concurrency();
    if (threads == 0) threads = 1;
    if (threads > count) threads = count;
    std::atomic<size_t> next{0};
    auto take_items = [&] {
        for (size_t i = next.fetch_add(1); i < count; i = next.fetch_add(1)) work(i);
    };
    std::vector<std::thread> pool;
    for (size_t t = 1; t < threads; ++t) pool.emplace_back(take_items);
    take_items();
    for (std::thread& t : pool) t.join();
}

}  // namespace core
