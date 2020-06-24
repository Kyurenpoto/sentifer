#pragma once

#include "memory_managers.hpp"
#include "tasks.hpp"

namespace mtbase
{
    struct task_allocator :
        private generic_allocator
    {
        task_allocator(std::pmr::memory_resource* r) :
            generic_allocator{ r }
        {}

    public:
        template<class Func, class... Args>
        task_invoke_t* new_func_task(Func func, Args&&... args)
        {
            using RetType =
                task_func_t<Func, decltype(
                    std::forward_as_tuple(std::forward<Args>(args)...))>;

            return generic_allocator::new_object<RetType>(
                func, std::forward_as_tuple(std::forward<Args>(args)...));
        }

        template<class T, class Method, class... Args>
        task_invoke_t* new_method_task(T* const fromObj, Method&& method, Args&&... args)
        {
            using RetType =
                task_method_t<T, Method, decltype(
                    std::forward_as_tuple(std::forward<Args>(args)...))>;

            return generic_allocator::new_object<RetType>(
                fromObj, method,
                std::forward_as_tuple(std::forward<Args>(args)...));
        }

        task_flush_object_t* new_flush_object_task(object_scheduler* const objectSched)
        {
            return generic_allocator::new_object<task_flush_object_t>(objectSched);
        }

        void delete_task(task_t* const task)
        {
            generic_allocator::delete_object(task);
        }
    };
}
