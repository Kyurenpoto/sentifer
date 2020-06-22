#pragma once

#include "../scheduler.hpp"

namespace mtbase
{
    struct thread_local_scheduler final :
        public scheduler
    {
    protected:
        void registerTaskImpl(task_invoke_t* const task) override
        {

        }
    };
}
