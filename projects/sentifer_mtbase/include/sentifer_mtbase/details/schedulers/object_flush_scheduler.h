#pragma once
#include <memory_resource>

#include "../clocks.hpp"
#include "../scheduler.hpp"
#include "../scheduler_restriction.h"

namespace mtbase
{
    struct thread_local_scheduler;

    struct object_flush_scheduler final :
        public scheduler
    {
    protected:
        void registerTaskImpl(task_flush_object_t* const task) override
        {
            
        }

    private:
        const scheduler_restriction restriction;
    };
}
