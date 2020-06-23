#pragma once

#include "../clocks.hpp"
#include "../scheduler.hpp"
#include "../scheduler_restriction.h"

namespace mtbase
{
    struct control_block
    {
        void reset() noexcept
        {
            tickBeginOccupying = clock_t::getSteadyTick();
            tickFlushing = steady_tick{};
            cntFlushed = 0;
        }

        void release() noexcept
        {
            sched = nullptr;
        }

        void recordTickFlushing(
            const steady_tick tickBegin,
            const steady_tick tickEnd)
            noexcept
        {
            tickFlushing += (tickEnd - tickBegin);
        }

        void recordCountFlushing() noexcept
        {
            ++cntFlushed;
        }

        void recordCountExpired(const scheduler_restriction& restriction) noexcept
        {
            cntFlushed = restriction.MAX_FLUSH_COUNT;
        }

        bool checkTransitionTick(
            const scheduler_restriction& restriction,
            const steady_tick tickEnd)
            const noexcept
        {
            return tickFlushing > restriction.MAX_OCCUPY_TICK_FLUSHING ||
                tickEnd - tickBeginOccupying > restriction.MAX_OCCUPY_TICK;
        }

        bool checkTransitionCount(const scheduler_restriction& restriction)
            const noexcept
        {
            return cntFlushed >= restriction.MAX_FLUSH_COUNT;
        }

        bool checkTransition(
            const scheduler_restriction& restriction,
            const steady_tick tickEnd)
            const noexcept
        {
            return checkTransitionCount(restriction) ||
                checkTransitionTick(restriction, tickEnd);
        }

    public:
        const scheduler* sched{ nullptr };
        size_t cntFlushed{ 0 };
        steady_tick tickBeginOccupying{ steady_tick{} };
        steady_tick tickFlushing{ steady_tick{} };
    };

    struct thread_local_scheduler final :
        public scheduler
    {
        control_block& getControlBlock(const scheduler* const) noexcept;

    protected:
        void registerTaskImpl(task_invoke_t* const task) override;
    };
}
