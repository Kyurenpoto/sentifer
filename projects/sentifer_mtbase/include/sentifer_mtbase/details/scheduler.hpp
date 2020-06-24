#pragma once

#include <cstddef>
#include <memory_resource>

#include "task_allocator.hpp"

namespace mtbase
{
    struct object_scheduler;
    struct task_storage;

    struct scheduler
    {
        scheduler(std::pmr::memory_resource* const res) :
            alloc{ res }
        {}

        virtual ~scheduler()
        {}

    public:
        template<class Func, class... Args>
        void registerFuncTask(Func func, Args&&... args)
        {
            registerTaskImpl(alloc.new_func_task(func, std::forward<Args>(args)...));
        }

        template<class T, class Method, class... Args>
        void registerMethodTask(T* const fromObj, Method method, Args&&... args)
        {
            registerTaskImpl(alloc.new_method_task(
                fromObj, method, std::forward<Args>(args)...));
        }

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

        virtual void registerTaskImpl(task_invoke_t* const task)
        {
            destroyTask(task);
        }

        virtual void registerTaskImpl(task_flush_object_t* const task)
        {
            destroyTask(task);
        }

    private:
        task_allocator alloc;
    };
}
