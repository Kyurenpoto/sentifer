#pragma once

#include "clocks.hpp"

namespace mtbase
{
    struct scheduler;
    struct thread_local_scheduler;
    struct scheduler_restriction;

    struct control_block
    {
        void reset()
            noexcept;
        void release()
            noexcept;
        void recordTickFlushing(
            const steady_tick tickBegin,
            const steady_tick tickEnd)
            noexcept;
        void recordCountFlushing()
            noexcept;
        void recordCountExpired(const scheduler_restriction& restriction)
            noexcept;

        [[nodiscard]]
        bool checkExpiredTick(
            const scheduler_restriction& restriction,
            const steady_tick tickEnd)
            const noexcept;
        [[nodiscard]]
        bool checkExpiredCount(const scheduler_restriction& restriction)
            const noexcept;
        [[nodiscard]]
        bool checkExpired(
            const scheduler_restriction& restriction,
            const steady_tick tickEnd)
            const noexcept;

    public:
        thread_local_scheduler& owner;

    private:
        const scheduler* sched{ nullptr };
        size_t cntFlushed{ 0 };
        steady_tick tickBeginOccupying{ steady_tick{} };
        steady_tick tickFlushing{ steady_tick{} };
    };
}
