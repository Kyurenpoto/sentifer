#include <thread>
#include <vector>
#include <exception>

#include <fmt/core.h>

#include "sentifer_mtbase/mtbase.h"

constexpr size_t sz = 10'000'000;

static mtbase::task_t task[sz];
static mtbase::task_wait_free_deque<sz> deq{ std::pmr::get_default_resource() };

void only_push_back(int threadId, int idxBegin, int idxEnd)
{
    for (int i = idxBegin; i < idxEnd; ++i)
    {
        try
        {
            bool result = deq.push_back(&task[i]);
            //fmt::print("thread {} {} task {}\n", threadId, result ? "finish" : "error", i);
        }
        catch (std::exception e)
        {
            throw std::exception{ fmt::format("Exception occured in thread {}, task {}", threadId, i).c_str() };
        }
    }
}

void only_push_front(int threadId, int idxBegin, int idxEnd)
{
    for (int i = idxBegin; i < idxEnd; ++i)
    {
        try
        {
            bool result = deq.push_front(&task[i]);
            //fmt::print("thread {} {} task {}\n", threadId, result ? "finish" : "error", i);
        }
        catch (std::exception e)
        {
            throw std::exception{ fmt::format("Exception occured in thread {}, task {}", threadId, i).c_str() };
        }
    }
}

void test_thread_n(int n, void(*f)(int, int, int))
{
    int m = sz / n;
    std::vector<std::thread> t;
    t.reserve(n);

    for (int i = 0; i < n; ++i)
        t.emplace_back(std::thread{ [i, m, f]()
            {
                f(i, i * m, (i + 1) * m);
            } });

    for (int i = 0; i < n; ++i)
        t[i].join();
}

void test_threads_to_n(int n, void(*f)(int, int, int))
{
    for (int i = 1; i <= n; ++i)
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
    fmt::print("warm up begin\n");
    test_thread_n(1, only_push_back);
    while (deq.pop_back() != nullptr);
    fmt::print("warm up end\n");

    for (int i = 0; i < 4; ++i)
    {
        fmt::print("only_push_front begin\n");
        test_threads_to_n(4, only_push_front);
        fmt::print("only_push_front end\n");
    }

    return 0;
}
