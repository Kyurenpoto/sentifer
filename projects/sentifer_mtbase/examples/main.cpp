#include <thread>
#include <vector>
#include <exception>

#include <fmt/core.h>

#include "sentifer_mtbase/mtbase.h"

constexpr size_t sz = 10'000'000;

static mtbase::task_t task[sz];
static std::pmr::synchronized_pool_resource res;
static mtbase::task_wait_free_deque<sz> deq{ &res };

void only_push_front(int threadId, int idxBegin, int idxEnd)
{
    for (int i = idxBegin; i < idxEnd; ++i)
    {
        try
        {
            bool result = deq.push_front(&task[i]);
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
            bool result = deq.push_back(&task[i]);
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
            mtbase::task_t* result = deq.pop_front();
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
            mtbase::task_t* result = deq.pop_back();
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

void fullDeq()
{
    for (int i = 0; i < sz; ++i)
    {
        bool result = deq.push_front(&task[i]);
    }
}

void emptyDeq()
{
    while (deq.pop_back() != nullptr);
}

void identityDeq()
{

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
    fmt::print("warm up begin\n");
    test_thread_n(1, only_push_back);
    emptyDeq();
    fmt::print("warm up end\n");

    fmt::print("only_push_front begin\n");
    test_threads_to_n(4, only_push_front, emptyDeq);
    fmt::print("only_push_front end\n");

    fmt::print("only_push_back begin\n");
    test_threads_to_n(4, only_push_back, identityDeq);
    fmt::print("only_push_back end\n");

    fmt::print("only_pop_front begin\n");
    test_threads_to_n(4, only_pop_front, fullDeq);
    fmt::print("only_pop_front end\n");

    fmt::print("only_pop_back begin\n");
    test_threads_to_n(4, only_pop_back, identityDeq);
    fmt::print("only_pop_back end\n");

    return 0;
}
