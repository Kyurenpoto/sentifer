#pragma once

#include "type_utils.hpp"

namespace mtbase
{
    struct task_t
    {
        task_t(const size_t sizeImpl, const size_t alignImpl) :
            size{ sizeImpl },
            align{ alignImpl }
        {}

        virtual ~task_t()
        {}

    public:
        virtual void invoke() = 0;

    public:
        const size_t size;
        const size_t align;
    };

    template<class Func, class TupleArgs>
    struct task_func_t :
        public task_t
    {
        task_func_t(Func func, TupleArgs args) :
            task_t{ sizeof(task_func_t), alignof(task_func_t) },
            invoked{ func },
            tupled{ args }
        {
            static_assert(is_tuple_invocable_r_v<void, Func, TupleArgs>);
        }

    public:
        void invoke() override
        {
            std::apply(invoked, tupled);
        }

    private:
        Func invoked;
        TupleArgs tupled;
    };

    template<class FromType, class Method, class TupleArgs>
    struct task_method_t :
        public task_func_t<Method,
        tuple_extend_front_t<FromType* const, TupleArgs>>
    {
        task_method_t(FromType* const fromObj, Method method, TupleArgs args) :
            task_func_t<Method,
            tuple_extend_front_t<FromType* const, TupleArgs>>
        { method, std::tuple_cat(std::make_tuple(fromObj), args) }
        {}
    };

    struct thread_local_scheduler;
    struct object_scheduler;

    struct task_flush_object_scheduler_t :
        task_t
    {
        task_flush_object_scheduler_t(object_scheduler* const objectScheduler) :
            task_t
        {
            sizeof(task_flush_object_scheduler_t),
            alignof(task_flush_object_scheduler_t)
        },
            objectSched{ objectScheduler }
        {}

    public:
        void invoke(thread_local_scheduler& threadSched);

    private:
        object_scheduler* const objectSched;
    };
}
