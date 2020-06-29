#pragma once

#include "../clocks.hpp"
#include "../scheduler_restriction.h"
#include "invocable_scheduler.h"

namespace mtbase
{
    struct object_flush_scheduler;
    struct thread_local_scheduler;
    struct control_block;
    struct task_storage;

    struct object_scheduler :
        public invocable_scheduler
    {
        object_scheduler(
            std::pmr::memory_resource* const res,
            object_flush_scheduler& objectFlushSched,
            task_storage* const taskStorage,
            const scheduler_restriction& restricts) :
            invocable_scheduler{ res, taskStorage },
            flusher{ objectFlushSched },
            restriction{ restricts }
        {}

        virtual ~object_scheduler()
        {}

    public:
        void flush(thread_local_scheduler& threadSched);

    protected:
        void registerTaskImpl(task_invoke_t* const task) override;

    private:
        void flushOwned(thread_local_scheduler& threadSched);
        void flushTasks(control_block& block);
        void executeTask(control_block& block);

        void invokeTask(control_block& block, task_t* const task) const;
        void invokeTask(control_block& block, task_invoke_t* const task) const;

        bool tryOwn() noexcept;
        void release() noexcept;

    private:
        std::atomic_bool isOwned{ false };
        object_flush_scheduler& flusher;
        const scheduler_restriction restriction;
    };
}
