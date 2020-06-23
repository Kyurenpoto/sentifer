#pragma once

#include <memory_resource>

#include "../clocks.hpp"
#include "../scheduler.hpp"
#include "../scheduler_restriction.h"

namespace mtbase
{
    struct object_flush_scheduler;
    struct thread_local_scheduler;

    struct object_scheduler :
        public scheduler
    {
        object_scheduler(
            std::pmr::memory_resource* const res,
            object_flush_scheduler& objectFlushSched,
            const scheduler_restriction restricts) :
            scheduler{ res },
            flusher{ objectFlushSched },
            restriction{ restricts }
        {}

        virtual ~object_scheduler() = default;

    public:
        void flush(thread_local_scheduler& threadSched);

    protected:
        void registerTaskImpl(task_invoke_t* const task) override;

        virtual bool pushBaskTask(task_invoke_t* const task);
        virtual task_invoke_t* popFrontTask();

    private:
        void flushOwned(thread_local_scheduler& threadSched);
        void flushTasks();
        void invokeTask();

        bool tryOwn() noexcept;
        void release() noexcept;

        bool checkTransitionTick(const steady_tick tickEnd) const noexcept;
        bool checkTransitionCount() const noexcept;

    private:
        std::atomic_bool isOwned{ false };
        size_t cntFlushed{ 0 };
        steady_tick tickBeginOccupying{ steady_tick{} };
        steady_tick tickFlushing{ steady_tick{} };
        object_flush_scheduler& flusher;
        const scheduler_restriction restriction;
    };
}
