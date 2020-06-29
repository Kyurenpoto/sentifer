#pragma once

#include "../clocks.hpp"
#include "../memory_managers.hpp"
#include "../base_structures.hpp"
#include "../schedulers/object_scheduler.h"

namespace mtbase
{
    struct task_transaction_t;

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

        template<class Func, class... Args>
        void scheduleFunc(
            Func&& func,
            Args&&... args);

        template<class Func, class... Args>
        void scheduleFuncCuttingIn(
            Func&& func,
            Args&&... args);

        template<class Func, class... Args>
        void scheduleFuncAfter(
            steady_tick after,
            Func&& func,
            Args&&... args);

        template<class Func, class... Args>
        void scheduleFuncAt(
            steady_tick at,
            Func&& func,
            Args&&... args);

        template<class T, class Method, class... Args>
        void scheduleMethod(
            T* const fromObj,
            Method&& method,
            Args&&... args);

        template<class T, class Method, class... Args>
        void scheduleMethodCuttingIn(
            T* const fromObj,
            Method&& method,
            Args&&... args);

        template<class T, class Method, class... Args>
        void scheduleMethodAfter(
            steady_tick after,
            T* const fromObj,
            Method&& method,
            Args&&... args);

        template<class T, class Method, class... Args>
        void scheduleMethodAt(
            steady_tick at,
            T* const fromObj,
            Method&& method,
            Args&&... args);

        bool tryTransferAuthority(task_transaction_t* const task);

    private:
        generic_allocator alloc;
        object_scheduler* sched = nullptr;
    };
}
