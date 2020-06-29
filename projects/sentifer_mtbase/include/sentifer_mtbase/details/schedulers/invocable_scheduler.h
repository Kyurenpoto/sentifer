#pragma once

#include "../scheduler.hpp"

namespace mtbase
{
    struct invocable_scheduler :
        public scheduler
    {
        invocable_scheduler(
            std::pmr::memory_resource* const res,
            task_storage* const taskStorage) :
            scheduler{ res, taskStorage }
        {}

        virtual ~invocable_scheduler()
        {}

    public:
        template<class Func, class... Args>
        void registerFuncTask(Func&& func, Args&&... args)
        {
            registerTaskImpl(alloc.new_func_task(
                std::forward<Func>(func),
                std::forward_as_tuple(std::forward<Args>(args)...)));
        }

        template<class T, class Method, class... Args>
        void registerMethodTask(T* const fromObj, Method&& method, Args&&... args)
        {
            registerTaskImpl(alloc.new_method_task(
                fromObj, std::forward<Method>(method),
                std::forward_as_tuple(std::forward<Args>(args)...)));
        }

    protected:
        virtual void registerTaskImpl(task_t* const task)
        {
            destroyTask(task);
        }

        virtual void registerTaskImpl(task_invoke_t* const task)
        {
            destroyTask(task);
        }
    };
}