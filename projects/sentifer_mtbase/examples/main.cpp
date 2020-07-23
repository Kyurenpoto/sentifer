#include <thread>

#include <fmt/core.h>

#include "sentifer_mtbase/mtbase.h"

mtbase::task_t task[1'000'000];
mtbase::task_wait_free_deque<1'000'000> deq{ std::pmr::get_default_resource() };

void warmup()
{
    int x = 0x12345678;
    for (int j = 0; j < 10; ++j)
        for (int i = 0; i < 1'000'000'000; ++i)
            x ^= (x * x);
}

int main()
{
    fmt::print("Warm up process...\n");
    warmup();
    fmt::print("Warm up complete\n");

    fmt::print("1 thread only push_back task...\n");

    std::chrono::nanoseconds begin = std::chrono::steady_clock::now().time_since_epoch();

    for (int i = 0; i < 1'000'000; ++i)
        deq.push_back(&task[i]);

    std::chrono::nanoseconds end = std::chrono::steady_clock::now().time_since_epoch();

    fmt::print("Complete: {}ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());

    return 0;
}
