#include <thread>
#include <vector>
#include <exception>

#include <fmt/core.h>

#include "sentifer_mtbase/mtbase.h"

static mtbase::task_t task[1'000'000];
static mtbase::task_wait_free_deque<1'000'000> deq{ std::pmr::get_default_resource() };

void warmup()
{
    int x = 0x12345678;
    for (int j = 0; j < 10; ++j)
        for (int i = 0; i < 1'000'000'000; ++i)
            x ^= (x * x);
}

void only_push_back(int threadId, int idxBegin, int idxEnd)
{
    for (int i = idxBegin; i < idxEnd; ++i)
    {
        try
        {
            deq.push_back(&task[i]);
        }
        catch (std::exception e)
        {
            throw std::exception{ fmt::format("Exception occured in thread {}, task {}", threadId, i).c_str() };
        }

        fmt::print("thread {} finish task {}\n", threadId, i);
    }
}

void test_thread_n(int n, void(*f)(int, int, int))
{
    int m = 1'000'000 / n;
    std::vector<std::thread> t;
    t.reserve(n);

    for (int i = 0; i < n; ++i)
        t.emplace_back(std::thread{ [i, m, f]()
            {
                fmt::print("Thread {} warm up begin\n", i);
                warmup();
                fmt::print("Thread {} warm up end\n", i);
                f(i, i * m, (i + 1) * m);
            } });

    for (int i = 0; i < n; ++i)
        t[i].join();
}

void test_threads_to_n(int n, void(*f)(int, int, int))
{
    for (int i = n; i <= n; ++i)
    {
        fmt::print("{} thread only push_back task...\n", i);

        std::chrono::nanoseconds begin = std::chrono::steady_clock::now().time_since_epoch();

        test_thread_n(i, f);

        std::chrono::nanoseconds end = std::chrono::steady_clock::now().time_since_epoch();

        fmt::print("Complete: {}ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());

        while (deq.pop_back() != nullptr);
    }
}

int main()
{
    test_threads_to_n(3, only_push_back);

    return 0;
}
