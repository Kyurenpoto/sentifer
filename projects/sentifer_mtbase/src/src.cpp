#include "../include/sentifer_mtbase/mtbase.h"

void mtbase::task_flush_object_t::invoke(
    thread_local_scheduler& threadSched)
{
    objectSched->flush(threadSched);
}

void mtbase::object_scheduler::flushOwned(thread_local_scheduler& threadSched)
{
    const steady_tick tickBegin = clock_t::getSteadyTick();

    flushTasks();
    
    const steady_tick tickEnd = clock_t::getSteadyTick();
    tickFlushing += (tickEnd - tickBegin);

    if (checkTransitionCount() || checkTransitionTick(tickEnd))
    {
        release();
        flusher.registerTask(this);

        return;
    }

    threadSched.registerTask(this, &object_scheduler::flushOwned, threadSched);
}

