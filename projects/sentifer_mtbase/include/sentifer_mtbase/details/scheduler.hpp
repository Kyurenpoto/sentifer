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

    public:
        void registerFlushObjectTask(object_scheduler* const objectSched)
        {
            registerTaskImpl(alloc.new_flush_object_task(objectSched));
        }

    protected:
        void destroyTask(task_t* const task)
        {
            alloc.delete_task(task);
        }

        virtual void registerTaskImpl(task_t* const task)
        {
            destroyTask(task);
        }

        virtual void registerTaskImpl(task_flush_object_t* const task)
        {
            destroyTask(task);
        }

    protected:
        task_storage* const storage;
        task_allocator alloc;
    };
}
