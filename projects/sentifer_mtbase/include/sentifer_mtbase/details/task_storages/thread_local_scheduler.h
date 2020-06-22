#pragma once

#include "../task_storage.hpp"

namespace mtbase
{
    struct thread_local_scheduler final :
        public task_storage
    {
    protected:
        void registerTaskImpl(task_flush_object_t* const task) override
        {

        }
    };
}
