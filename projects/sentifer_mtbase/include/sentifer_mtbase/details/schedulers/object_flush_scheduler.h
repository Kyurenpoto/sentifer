#pragma once
#include <memory_resource>

#include "../clocks.hpp"
#include "../scheduler.hpp"
#include "../scheduler_restriction.h"

namespace mtbase
{
    struct thread_local_scheduler;
    struct control_block;
    struct task_storage;

    struct object_flush_scheduler final :
        public scheduler
    {
        object_flush_scheduler(
            std::pmr::memory_resource* const res,
            task_storage* const taskStorage,
            const scheduler_restriction&& restricts) :
            scheduler{ res },
            storage{ taskStorage },
            restriction{ restricts }
        {}

        virtual ~object_flush_scheduler()
        {}

    public:
        void flush(thread_local_scheduler& threadSched);

    protected:
        void registerTaskImpl(task_flush_object_t* const task) override;

    private:
        void flushTasks(control_block& block);
        void executeTask(control_block& block);

        void invokeTask(
            control_block& block, task_t* const task) const;
        void invokeTask(
            control_block& block, task_flush_object_t* const task) const;

    private:
        task_storage* const storage;
        const scheduler_restriction restriction;
    };
}
