#pragma once

#include "task_allocator.hpp"

namespace mtbase
{
    struct object_scheduler;
    struct task_storage;

    struct scheduler
    {
        scheduler(
            std::pmr::memory_resource* const res,
            task_storage* const taskStorage) :
            alloc{ res },
            storage{ taskStorage }
        {}

        virtual ~scheduler()
        {}

    protected:
        task_storage* const storage;
        task_allocator alloc;
    };
}
