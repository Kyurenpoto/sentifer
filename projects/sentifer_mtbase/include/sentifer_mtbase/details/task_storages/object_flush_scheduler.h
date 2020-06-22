#pragma once
#include <memory_resource>

#include "../clocks.hpp"
#include "../task_storage.hpp"

namespace mtbase
{
    struct thread_local_scheduler;

    struct object_flush_scheduler final :
        public task_storage
    {
    protected:
        void registerTaskImpl(task_flush_object_t* const task) override
        {
            
        }
    };
}
