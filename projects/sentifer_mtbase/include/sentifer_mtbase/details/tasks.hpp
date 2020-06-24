#pragma once

#include "type_utils.hpp"

namespace mtbase
{
    struct task_t
    {
        virtual ~task_t()
        {}
    };

    struct task_invoke_t :
        public task_t
    {
        virtual ~task_invoke_t()
        {}

    public:
        virtual void invoke() = 0;
    };

    template<class Func, class TupleArgs>
    struct task_func_t :
        public task_invoke_t
    {
        task_func_t(Func&& func, TupleArgs&& args) :
            task_invoke_t{},
            invoked{ func },
            tupled{ args }
        {
            static_assert(is_tuple_invocable_r_v<void, Func, TupleArgs>);
        }

        virtual ~task_func_t()
        {}

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
        task_method_t(FromType* const fromObj, Method&& method, TupleArgs&& args) :
            task_func_t<Method,
            tuple_extend_front_t<FromType* const, TupleArgs>>
        { std::forward<Method>(method), std::tuple_cat(std::make_tuple(fromObj), args) }
        {}
    };

    struct thread_local_scheduler;
    struct object_scheduler;

    struct task_flush_object_t :
        task_t
    {
        task_flush_object_t(object_scheduler* const objectScheduler) :
            task_t{},
            objectSched{ objectScheduler }
        {}

    public:
        void invoke(thread_local_scheduler& threadSched);

    private:
        object_scheduler* const objectSched;
    };
}
