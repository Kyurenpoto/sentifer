#pragma once
#include <memory_resource>

#include "../clocks.hpp"
#include "../scheduler.hpp"

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
        const steady_tick MAX_OCCUPY_TICK;
        const steady_tick MAX_OCCUPY_TICK_FLUSHING;
        const size_t MAX_FLUSH_COUNT;
        const size_t MAX_FLUSH_COUNT_AT_ONCE;
    };
}
