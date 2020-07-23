#include <thread>
#include <vector>

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

void only_push_back(int idxBegin, int idxEnd)
{
    for (int i = idxBegin; i < idxEnd; ++i)
        deq.push_back(&task[i]);
}

void test_thread_n(int n, void(*f)(int, int))
{
    int m = 1'000'000 / n;
    std::vector<std::thread> t;

    for (int i = 0; i < n; ++i)
        t.emplace_back(std::thread{ [&]() { warmup(); f(i * m, (i + 1) * m); } });

    for (int i = 0; i < n; ++i)
        t[i].join();
}

void test_threads_to_n(int n, void(*f)(int, int))
{
    for (int i = 1; i <= n; ++i)
    {
        fmt::print("thread {} only push_back task...\n", i);

        std::chrono::nanoseconds begin = std::chrono::steady_clock::now().time_since_epoch();

        test_thread_n(i, f);

        std::chrono::nanoseconds end = std::chrono::steady_clock::now().time_since_epoch();

        fmt::print("Complete: {}ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());

        while (deq.pop_back() != nullptr);
    }
}

int main()
{
    test_threads_to_n(4, only_push_back);

    return 0;
}
