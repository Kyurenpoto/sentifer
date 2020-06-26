#pragma once

#include "../memory_managers.hpp"
#include "../base_structures.hpp"
#include "../schedulers/object_scheduler.h"

namespace mtbase
{
    template<size_t MAX_STORAGE_SIZE = (1 << 20)>
    struct schedulable_object
    {
    private:
        using storage_type = task_wait_free_deque<MAX_STORAGE_SIZE>;
    public:
        schedulable_object(
            std::pmr::memory_resource* res,
            object_flush_scheduler& objectFlushSched,
            const scheduler_restriction&& restricts) :
            alloc{ res }
        {
            sched = alloc.new_object<object_scheduler>(
                alloc.resource(),
                objectFlushSched,
                alloc.new_object<storage_type>(alloc.resource()),
                restricts);
        };

    private:
        generic_allocator alloc;
        object_scheduler* sched = nullptr;
    };
}
