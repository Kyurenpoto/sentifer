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
            const steady_tick maxOccupyTick,
            const steady_tick maxOccupyTickFlushing,
            const size_t maxFlushCount,
            const size_t maxFlushCountAtOnce) :
            task_storage{ res },
            dist{ distributor },
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
            const steady_tick tickBegin = clock_t::getSteadyTick();
            for (size_t i = 0;
                i < MAX_FLUSH_COUNT_AT_ONCE && !checkTransitionCount(); ++i)
                invokeTask();

            tickFlushing += (clock_t::getSteadyTick() - tickBegin);
        }

        bool checkTransitionTick() const noexcept
        {
            return tickFlushing > MAX_OCCUPY_TICK_FLUSHING ||
                clock_t::getSteadyTick() - tickBeginOccupying > MAX_OCCUPY_TICK;
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
        object_scheduler_distributor& dist;
        const steady_tick MAX_OCCUPY_TICK;
        const steady_tick MAX_OCCUPY_TICK_FLUSHING;
        const size_t MAX_FLUSH_COUNT;
        const size_t MAX_FLUSH_COUNT_AT_ONCE;
    };

    template<size_t MAX_TASK_STORAGE = (1 << 20)>
    struct sized_object_scheduler final :
        public object_scheduler
    {
        sized_object_scheduler(
            std::pmr::memory_resource* const res,
            object_scheduler_distributor& distributor,
            const steady_tick maxOccupyTick,
            const steady_tick maxOccupyTickFlushing,
            const size_t maxFlushCount,
            const size_t maxFlushCountAtOnce) :
            object_scheduler
            {
                res,
                distributor,
                maxOccupyTick,
                maxOccupyTickFlushing,
                maxFlushCount,
                maxFlushCountAtOnce
            },
            taskDeq{ res }
        {}

    protected:
        bool pushBaskTask(task_invoke_t* const task) override
        {
            return taskDeq.push_back(task);
        }

        task_invoke_t* popFrontTask() override
        {
            return toValidTask(taskDeq.pop_front());
        }

    private:
        task_invoke_t* toValidTask(task_t* const task) const noexcept
        {
            return nullptr;
        }

        task_invoke_t* toValidTask(task_invoke_t* const task) const noexcept
        {
            return task;
        }

    private:
        task_wait_free_deque<MAX_TASK_STORAGE> taskDeq;
    };
}
