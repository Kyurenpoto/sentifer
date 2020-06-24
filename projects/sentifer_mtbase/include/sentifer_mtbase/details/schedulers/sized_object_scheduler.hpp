#pragma once

#include <memory_resource>

#include "../base_structures.hpp"
#include "object_scheduler.h"

namespace mtbase
{
    template<size_t MAX_TASK_STORAGE = (1 << 20)>
    struct sized_object_scheduler final :
        public object_scheduler
    {
        sized_object_scheduler(
            std::pmr::memory_resource* const res,
            object_flush_scheduler& objectFlushSched,
            const scheduler_restriction&& restricts) :
            object_scheduler
            {
                res,
                objectFlushSched,
                new task_wait_free_deque<MAX_TASK_STORAGE>{ res },
                std::move(restricts)
            }
        {}
    };
}
