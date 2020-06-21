#pragma once

#include "../clocks.hpp"
#include "../memory_managers.hpp"
#include "../task_storage.hpp"
#include "../base_structures.hpp"
#include "thread_local_scheduler.h"

namespace mtbase
{
    struct object_scheduler_distributor;

    struct object_scheduler :
        public task_storage
    {
        object_scheduler(
            std::pmr::memory_resource* const res,
            object_scheduler_distributor& distributor,
            const steady_tick maxFlushTick,
            const size_t maxFlushCount,
            const size_t maxFlushCountAtOnce) :
            task_storage{ res },
            taskDeq{ res },
            dist{ distributor },
            MAX_FLUSH_TICK{ maxFlushTick },
            MAX_FLUSH_COUNT{ maxFlushCount },
            MAX_FLUSH_COUNT_AT_ONCE{ maxFlushCountAtOnce }
        {}

    public:
        void flush(thread_local_scheduler& threadSched)
        {
            if (!tryOwn())
                return;

            tickBeginFlush = clock_t::getSteadyTick();
            cntFlushed = 0;
            flushOwned(threadSched);
        }

    protected:
        void registerTaskImpl(task_t* const task) override
        {
            if (taskDeq.push_back(task))
            {
                task_storage::registerTaskImpl(task);
            }
        }

    private:
        void flushOwned(thread_local_scheduler& threadSched)
        {
            flushTasks();

            if (checkTransitionCount() || checkTransitionTick())
            {
                release();

                return;
            }

            threadSched.registerTaskMethod(this, &object_scheduler::flushOwned, threadSched);
        }

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
            {
                task_t* const task = taskDeq.pop_front();
                if (task == nullptr)
                {
                    cntFlushed = MAX_FLUSH_COUNT;
                    break;
                }

                task->invoke();
                destroyTask(task);
                ++cntFlushed;
            }
        }

        bool checkTransitionTick() const noexcept
        {
            return clock_t::getSteadyTick() - tickBeginFlush > MAX_FLUSH_TICK;
        }

        bool checkTransitionCount() const noexcept
        {
            return cntFlushed >= MAX_FLUSH_COUNT;
        }

        void release() noexcept
        {
            isOwned.store(false, std::memory_order_release);
        }

    private:
        static constexpr size_t SIZE_TASK_DEQ = (1 << 20);
        task_wait_free_deque<SIZE_TASK_DEQ> taskDeq;
        std::atomic_bool isOwned{ false };
        size_t cntFlushed{ 0 };
        steady_tick tickBeginFlush{ 0 };
        object_scheduler_distributor& dist;
        const steady_tick MAX_FLUSH_TICK;
        const size_t MAX_FLUSH_COUNT;
        const size_t MAX_FLUSH_COUNT_AT_ONCE;
    };

    template<size_t MAX_TASK_STORAGE = (1 << 20)>
    struct sized_object_scheduler :
        public object_scheduler
    {
        sized_object_scheduler(
            std::pmr::memory_resource* const res,
            object_scheduler_distributor& distributor,
            const steady_tick maxFlushTick,
            const size_t maxFlushCount,
            const size_t maxFlushCountAtOnce) :
            object_scheduler
        { res, distributor, maxFlushTick, maxFlushCount, maxFlushCountAtOnce }
        {}
    };
}
