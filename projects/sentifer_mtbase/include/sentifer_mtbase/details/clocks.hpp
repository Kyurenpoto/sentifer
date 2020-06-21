#pragma once

#include <chrono>
#include <atomic>

namespace mtbase
{
    using system_tick = std::chrono::nanoseconds;
    using steady_tick = std::chrono::nanoseconds;

    struct clock_t
    {
        static system_tick getSystemTick() noexcept
        {
            return getTick<std::chrono::system_clock>();
        }

        static steady_tick getSteadyTick() noexcept
        {
            return getTick<std::chrono::steady_clock>();
        }

    private:
        template<class Clock>
        static std::chrono::nanoseconds getTick() noexcept
        {
            while (!tryOwn());

            std::chrono::nanoseconds tick = Clock::now().time_since_epoch();

            release();

            return tick;
        }

        static bool tryOwn() noexcept
        {
            bool oldIsOwned = false;
            return isOwned.compare_exchange_strong(oldIsOwned, true,
                std::memory_order_acq_rel, std::memory_order_acquire);
        }

        static void release() noexcept
        {
            isOwned.store(false, std::memory_order_release);
        }

    private:
        inline static std::atomic_bool isOwned{ false };
    };
}
