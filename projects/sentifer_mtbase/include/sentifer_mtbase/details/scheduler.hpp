#pragma once

#include <cstddef>
#include <memory_resource>

#include "tasks.hpp"
#include "memory_managers.hpp"

namespace mtbase
{
    struct object_scheduler;

    struct scheduler
    {
        scheduler(std::pmr::memory_resource* const res) :
            alloc{ res }
        {}

        virtual ~scheduler()
        {}

    public:
        template<class Func, class... Args>
        void registerTask(Func func, Args&&... args)
        {
            static_assert(!std::is_member_function_pointer_v<Func>);
            static_assert(std::is_invocable_r_v<void, Func, decltype(args)...>);

            registerTaskImpl(alloc.new_object(
                task_func_t{ func,
                std::forward_as_tuple(std::forward<Args>(args)...) }));
        }

        template<class T, class Method, class... Args>
        void registerTask(T* const fromObj, Method method, Args&&... args)
        {
            static_assert(std::is_member_function_pointer_v<Method>);
            static_assert(std::is_invocable_r_v<void, Method, T* const, decltype(args)...>);

            registerTaskImpl(alloc.new_object(
                task_method_t{ fromObj, method,
                std::forward_as_tuple(std::forward<Args>(args)...) }));
        }

        void registerTask(object_scheduler* const objectSched)
        {
            registerTaskImpl(alloc.new_object(
                task_flush_object_t{ objectSched }));
        }

    protected:
        void destroyTask(task_t* const task)
        {
            const size_t size = task->size;
            const size_t align = task->align;

            alloc.destroy(task);
            alloc.deallocate_bytes(task, size, align);
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
        generic_allocator alloc;
    };
}
