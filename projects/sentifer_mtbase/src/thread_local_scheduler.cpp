#include "../include/sentifer_mtbase/details/schedulers/thread_local_scheduler.h"

#include "../include/sentifer_mtbase/details/base_structures.hpp"
#include "../include/sentifer_mtbase/details/schedulers/object_flush_scheduler.h"

using namespace mtbase;

void thread_local_scheduler::flush()
{
    flusher.flush(*this);

    while (true)
    {
        flushRequested();

        // task stealing

        // default task
    }
}

void thread_local_scheduler::registerTaskImpl(task_invoke_t* const task)
{
    if (!storage->push_back(task))
    {
        alloc.delete_task(task);
    }
}

void thread_local_scheduler::flushRequested()
{
    while (true)
    {
        task_t* const task = storage->pop_front();
        if (task == nullptr)
            return;

        invokeTask(task);
        alloc.delete_task(task);
    }
}

void thread_local_scheduler::invokeTask(task_t* const task)
    const
{}

void thread_local_scheduler::invokeTask(task_invoke_t* const task)
    const
{
    task->invoke();
}
