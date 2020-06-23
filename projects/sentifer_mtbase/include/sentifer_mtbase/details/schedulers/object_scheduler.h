#pragma once

#include <memory_resource>

#include "../clocks.hpp"
#include "../scheduler.hpp"
#include "../scheduler_restriction.h"

namespace mtbase
{
    struct object_flush_scheduler;
    struct thread_local_scheduler;
    struct control_block;

    struct object_scheduler :
        public scheduler
    {
        object_scheduler(
            std::pmr::memory_resource* const res,
            object_flush_scheduler& objectFlushSched,
            const scheduler_restriction&& restricts) :
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
        void flushTasks(control_block& block);
        void invokeTask(control_block& block);

        bool tryOwn() noexcept;
        void release() noexcept;

    private:
        std::atomic_bool isOwned{ false };
        object_flush_scheduler& flusher;
        const scheduler_restriction restriction;
    };
}
