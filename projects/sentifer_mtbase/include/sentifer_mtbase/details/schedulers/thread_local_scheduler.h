#pragma once

#include "invocable_scheduler.h"

namespace mtbase
{
    struct control_block;
    struct object_flush_scheduler;

    struct thread_local_scheduler :
        public invocable_scheduler
    {
        thread_local_scheduler(
            std::pmr::memory_resource* const res,
            object_flush_scheduler& objectFlushSched,
            task_storage* const taskStorage) :
            invocable_scheduler{ res, taskStorage },
            flusher{ objectFlushSched }
        {}

        virtual ~thread_local_scheduler()
        {}

    public:
        void flush();

        virtual control_block& getControlBlock(const scheduler* const sched)
            noexcept = 0;

    protected:
        void registerTaskImpl(task_invoke_t* const task)
            override;

    private:
        void flushRequested();

        void invokeTask(task_t* const task)
            const;
        void invokeTask(task_invoke_t* const task)
            const;

    private:
        object_flush_scheduler& flusher;
    };
}
