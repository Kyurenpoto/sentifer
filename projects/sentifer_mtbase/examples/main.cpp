#include <thread>
#include <vector>
#include <exception>

#include <fmt/core.h>

#include "sentifer_mtbase/mtbase.h"

#ifdef _MSC_VER
#include <Windows.h>
#include <DbgHelp.h>
#pragma comment ( lib, "DbgHelp" )

LONG WINAPI UnhandledExceptFilter(PEXCEPTION_POINTERS  exceptionInfo)
{
    MINIDUMP_EXCEPTION_INFORMATION info = { 0 };
    info.ThreadId = ::GetCurrentThreadId(); // Threae ID
    info.ExceptionPointers = exceptionInfo; // Exception info
    info.ClientPointers = FALSE;

    std::wstring stemp(L"test.dmp");

    HANDLE hFile = CreateFileW(stemp.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal, &info, NULL, NULL);

    return 0L;
}

#endif

constexpr size_t sz = 1'000'000;

static mtbase::task_t task[sz];
static mtbase::task_wait_free_deque<sz> deq{ std::pmr::get_default_resource() };

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
            bool result = deq.push_back(&task[i]);
            fmt::print("thread {} {} task {}\n", threadId, result ? "finish" : "error", i);
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
                fmt::print("Thread {} warm up begin\n", i);
                //warmup();
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
#ifdef _MSC_VER
    SetUnhandledExceptionFilter(UnhandledExceptFilter);
#endif

    test_threads_to_n(4, only_push_back);

    return 0;
}
