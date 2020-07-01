#pragma once

#include "type_utils.hpp"
#include "clocks.hpp"

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
            task_func_t<Method, tuple_extend_front_t<FromType* const, TupleArgs>>
            {
                std::forward<Method>(method),
                std::tuple_cat(std::make_tuple(fromObj), args)
            }
        {
            static_assert(std::is_member_function_pointer_v<Method>);
        }
    };

    struct thread_local_scheduler;
    struct object_scheduler;

    struct task_flush_object_t :
        public task_t
    {
        task_flush_object_t(object_scheduler* const sched) :
            task_t{},
            objectSched{ sched }
        {}

    public:
        void invoke(thread_local_scheduler& threadSched);

    private:
        object_scheduler* const objectSched;
    };

    struct task_timed_invoke_t :
        public task_t
    {
        const steady_tick tickExpiredAt{ steady_tick{} };
        task_invoke_t* const targetTask{ nullptr };
    };

    struct timed_object_scheduler;

    struct task_flush_timed_object_t :
        public task_t
    {
        task_flush_timed_object_t(timed_object_scheduler* const sched) :
            task_t{},
            timedObjectSched{ sched }
        {}

    public:
        void invoke(thread_local_scheduler& threadSched);

    private:
        timed_object_scheduler* const timedObjectSched;
    };
}
