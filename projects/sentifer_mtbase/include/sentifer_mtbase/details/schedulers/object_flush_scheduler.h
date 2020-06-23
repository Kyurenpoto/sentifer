#pragma once
#include <memory_resource>

#include "../clocks.hpp"
#include "../scheduler.hpp"
#include "../scheduler_restriction.h"

namespace mtbase
{
    struct thread_local_scheduler;
    struct control_block;

    struct object_flush_scheduler final :
        public scheduler
    {
        object_flush_scheduler(
            std::pmr::memory_resource* const res,
            const scheduler_restriction&& restricts) :
            scheduler{ res },
            restriction{ restricts }
        {}

        virtual ~object_flush_scheduler()
        {}

    public:
        void flush(thread_local_scheduler& threadSched);

    protected:
        void registerTaskImpl(task_flush_object_t* const task) override;

        virtual bool pushBaskTask(task_flush_object_t* const task);
        virtual task_t* popFrontTask();

    private:
        void flushTasks(control_block& block);
        void executeTask(control_block& block);

        void invokeTask(
            control_block& block, task_t* const task) const;
        void invokeTask(
            control_block& block, task_flush_object_t* const task) const;

    private:
        const scheduler_restriction restriction;
    };
}
