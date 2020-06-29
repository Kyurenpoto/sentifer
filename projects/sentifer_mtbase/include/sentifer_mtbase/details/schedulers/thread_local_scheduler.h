#pragma once

#include "invocable_scheduler.h"

namespace mtbase
{
    struct control_block;

    struct thread_local_scheduler final :
        public invocable_scheduler
    {
        void flush();

        control_block& getControlBlock(const scheduler* const) noexcept;

    protected:
        void registerTaskImpl(task_invoke_t* const task) override;

        void invokeTask(
            control_block& block,
            task_t* const task)
            const;
        void invokeTask(
            control_block& block,
            task_invoke_t* const task)
            const;
    };
}
