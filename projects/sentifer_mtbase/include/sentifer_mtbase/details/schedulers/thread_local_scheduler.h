#pragma once

#include "../scheduler.hpp"

namespace mtbase
{
    struct control_block;

    struct thread_local_scheduler final :
        public scheduler
    {
        control_block& getControlBlock(const scheduler* const) noexcept;

    protected:
        void registerTaskImpl(task_invoke_t* const task) override;
    };
}
