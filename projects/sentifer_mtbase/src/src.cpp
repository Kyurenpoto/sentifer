#include "../include/sentifer_mtbase/mtbase.h"

void mtbase::task_flush_object_scheduler_t::invoke(
    thread_local_scheduler& threadSched)
{
    objectSched->flush(threadSched);
}

void mtbase::object_scheduler::flushOwned(thread_local_scheduler& threadSched)
{
    flushTasks();

    if (checkTransitionCount() || checkTransitionTick())
    {
        release();
        dist.registerTask(this);

        return;
    }

    threadSched.registerTask(this, &object_scheduler::flushOwned, threadSched);
}

