#include <thread>
#include <vector>
#include <exception>

#include <fmt/core.h>

#include "sentifer_mtbase/mtbase.h"

constexpr size_t SZ = 100'000;
constexpr size_t SZ_WARM_UP = 10'000;

static mtbase::task_t task[SZ];
static std::pmr::synchronized_pool_resource res;
static mtbase::task_wait_free_deque<SZ> deq{ &res };

void only_push_front(int threadId, int idxBegin, int idxEnd)
{
    for (int i = idxBegin; i < idxEnd; ++i)
    {
        try
        {
            while (!deq.push_front(&task[i]));
        }
        catch (std::exception e)
        {
            throw std::exception{ fmt::format("Exception occured in thread {}, task {}", threadId, i).c_str() };
        }
    }
}

void only_push_back(int threadId, int idxBegin, int idxEnd)
{
    for (int i = idxBegin; i < idxEnd; ++i)
    {
        try
        {
            while (!deq.push_back(&task[i]));
        }
        catch (std::exception e)
        {
            throw std::exception{ fmt::format("Exception occured in thread {}, task {}", threadId, i).c_str() };
        }
    }
}

void only_pop_front(int threadId, int idxBegin, int idxEnd)
{
    for (int i = idxBegin; i < idxEnd; ++i)
    {
        try
        {
            while (deq.pop_front() == nullptr);
        }
        catch (std::exception e)
        {
            throw std::exception{ fmt::format("Exception occured in thread {}, task {}", threadId, i).c_str() };
        }
    }
}

void only_pop_back(int threadId, int idxBegin, int idxEnd)
{
    for (int i = idxBegin; i < idxEnd; ++i)
    {
        try
        {
            while (deq.pop_back() == nullptr);
        }
        catch (std::exception e)
        {
            throw std::exception{ fmt::format("Exception occured in thread {}, task {}", threadId, i).c_str() };
        }
    }
}

void test_thread_n(int n, void(*f)(int, int, int))
{
    int m = SZ / n;
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

void fullDeq()
{
    for (int i = 0; i < SZ; ++i)
    {
        bool result = deq.push_back(&task[i]);
    }
}

void emptyDeq()
{
    while (deq.pop_back() != nullptr);
}

void identityDeq()
{

}

void warmup()
{
    fmt::print("warm up begin\n");

    {
        std::thread t{ []() { only_push_back(0, 0, SZ_WARM_UP); } };
        t.join();
    }
    {
        std::thread t{ []() { only_pop_back(0, 0, SZ_WARM_UP); } };
        t.join();
    }
    {
        std::thread t{ []() { only_push_front(0, 0, SZ_WARM_UP); } };
        t.join();
    }
    {
        std::thread t{ []() { only_pop_front(0, 0, SZ_WARM_UP); } };
        t.join();
    }

    fmt::print("warm up end\n");
}

void test_threads_to_n(int n, void(*f)(int, int, int), void (*post)())
{
    for (int i = 1; i <= n; ++i)
    {
        fmt::print("{} thread only task...\n", i);

        std::chrono::nanoseconds begin = std::chrono::steady_clock::now().time_since_epoch();

        test_thread_n(i, f);

        std::chrono::nanoseconds end = std::chrono::steady_clock::now().time_since_epoch();

        fmt::print("Complete: {}ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count());

        post();
    }
}

int main()
{
    warmup();

    fmt::print("only_push_front begin\n");
    test_threads_to_n(6, only_push_front, emptyDeq);
    fmt::print("only_push_front end\n");

    fmt::print("only_push_back begin\n");
    test_threads_to_n(6, only_push_back, emptyDeq);
    fmt::print("only_push_back end\n");

    fullDeq();

    fmt::print("only_pop_front begin\n");
    test_threads_to_n(6, only_pop_front, fullDeq);
    fmt::print("only_pop_front end\n");

    fmt::print("only_pop_back begin\n");
    test_threads_to_n(6, only_pop_back, fullDeq);
    fmt::print("only_pop_back end\n");

    return 0;
}
