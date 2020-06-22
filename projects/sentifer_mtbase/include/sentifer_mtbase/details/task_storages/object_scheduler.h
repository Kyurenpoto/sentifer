#pragma once

#include <memory_resource>

#include "../clocks.hpp"
#include "../task_storage.hpp"

namespace mtbase
{
    struct object_flush_scheduler;
    struct thread_local_scheduler;

    struct object_scheduler :
        public task_storage
    {
        object_scheduler(
            std::pmr::memory_resource* const res,
            object_flush_scheduler& objectFlushSched,
            const steady_tick maxOccupyTick,
            const steady_tick maxOccupyTickFlushing,
            const size_t maxFlushCount,
            const size_t maxFlushCountAtOnce) :
            task_storage{ res },
            flusher{ objectFlushSched },
            MAX_OCCUPY_TICK{ maxOccupyTick },
            MAX_OCCUPY_TICK_FLUSHING{ maxOccupyTickFlushing },
            MAX_FLUSH_COUNT{ maxFlushCount },
            MAX_FLUSH_COUNT_AT_ONCE{ maxFlushCountAtOnce }
        {}

        virtual ~object_scheduler() = default;

    public:
        void flush(thread_local_scheduler& threadSched)
        {
            if (!tryOwn())
                return;

            tickBeginOccupying = clock_t::getSteadyTick();
            tickFlushing = steady_tick{};
            cntFlushed = 0;
            flushOwned(threadSched);
        }

    protected:
        void registerTaskImpl(task_invoke_t* const task) override
        {
            if (!pushBaskTask(task))
            {
                task_storage::registerTaskImpl(task);
            }
        }

        virtual bool pushBaskTask(task_invoke_t* const task)
        {
            return false;
        }

        virtual task_invoke_t* popFrontTask()
        {
            return nullptr;
        }

    private:
        void flushOwned(thread_local_scheduler& threadSched);

        bool tryOwn() noexcept
        {
            bool oldIsOwned = false;
            return isOwned.compare_exchange_strong(oldIsOwned, true,
                std::memory_order_acq_rel, std::memory_order_acquire);
        }

        void flushTasks()
        {
            for (size_t i = 0;
                i < MAX_FLUSH_COUNT_AT_ONCE && !checkTransitionCount(); ++i)
                invokeTask();
        }

        bool checkTransitionTick(const steady_tick tickEnd) const noexcept
        {
            return tickFlushing > MAX_OCCUPY_TICK_FLUSHING ||
                tickEnd - tickBeginOccupying > MAX_OCCUPY_TICK;
        }

        bool checkTransitionCount() const noexcept
        {
            return cntFlushed >= MAX_FLUSH_COUNT;
        }

        void release() noexcept
        {
            isOwned.store(false, std::memory_order_release);
        }

        void invokeTask()
        {
            task_invoke_t* const task = popFrontTask();
            if (task == nullptr)
            {
                cntFlushed = MAX_FLUSH_COUNT;

                return;
            }

            task->invoke();
            destroyTask(task);

            ++cntFlushed;
        }

    private:
        std::atomic_bool isOwned{ false };
        size_t cntFlushed{ 0 };
        steady_tick tickBeginOccupying{ steady_tick{} };
        steady_tick tickFlushing{ steady_tick{} };
        object_flush_scheduler& flusher;
        const steady_tick MAX_OCCUPY_TICK;
        const steady_tick MAX_OCCUPY_TICK_FLUSHING;
        const size_t MAX_FLUSH_COUNT;
        const size_t MAX_FLUSH_COUNT_AT_ONCE;
    };
}
