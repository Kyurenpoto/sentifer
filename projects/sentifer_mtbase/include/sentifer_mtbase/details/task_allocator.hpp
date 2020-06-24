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
        template<class Func, class TupleArgs>
        decltype(auto) new_func_task(Func&& func, TupleArgs&& args)
        {
            return generic_allocator::new_object<task_func_t<Func, TupleArgs>>(
                std::forward<Func>(func), std::forward<TupleArgs>(args));
        }

        template<class T, class Method, class TupleArgs>
        decltype(auto) new_method_task(
            T* const fromObj, Method&& method, TupleArgs&& args)
        {
            return generic_allocator::new_object<task_method_t<T, Method, TupleArgs>>(
                fromObj, std::forward<Method>(method), std::forward<TupleArgs>(args));
        }

        decltype(auto) new_flush_object_task(object_scheduler* const objectSched)
        {
            return generic_allocator::new_object<task_flush_object_t>(objectSched);
        }

        void delete_task(task_t* const task)
        {
            generic_allocator::delete_object(task);
        }
    };
}
