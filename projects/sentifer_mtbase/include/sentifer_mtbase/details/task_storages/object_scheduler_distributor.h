#pragma once

#include "../task_storage.hpp"

namespace mtbase
{
    struct object_scheduler_distributor final :
        public task_storage
    {
    protected:
        void registerTaskImpl(task_flush_object_scheduler_t* const task) override
        {
            
        }
    };
}
