#include "../include/sentifer_mtbase/details/schedulers/object_scheduler.h"

#include "../include/sentifer_mtbase/details/schedulers/thread_local_scheduler.h"
#include "../include/sentifer_mtbase/details/schedulers/object_flush_scheduler.h"

using namespace mtbase;

void object_scheduler::flush(thread_local_scheduler& threadSched)
{
    if (!tryOwn())
        return;

    tickBeginOccupying = clock_t::getSteadyTick();
    tickFlushing = steady_tick{};
    cntFlushed = 0;

    flushOwned(threadSched);
}

void object_scheduler::registerTaskImpl(task_invoke_t* const task)
{
    if (!pushBaskTask(task))
    {
        scheduler::registerTaskImpl(task);
    }
}

bool object_scheduler::pushBaskTask(task_invoke_t* const task)
{
    return false;
}

task_invoke_t* object_scheduler::popFrontTask()
{
    return nullptr;
}

void object_scheduler::flushOwned(thread_local_scheduler& threadSched)
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

void object_scheduler::flushTasks()
{
    for (size_t i = 0;
        i < restriction.MAX_FLUSH_COUNT_AT_ONCE && !checkTransitionCount(); ++i)
        invokeTask();
}

void object_scheduler::invokeTask()
{
    task_invoke_t* const task = popFrontTask();
    if (task == nullptr)
    {
        cntFlushed = restriction.MAX_FLUSH_COUNT;

        return;
    }

    task->invoke();
    destroyTask(task);

    ++cntFlushed;
}

bool object_scheduler::tryOwn() noexcept
{
    bool oldIsOwned = false;
    return isOwned.compare_exchange_strong(oldIsOwned, true,
        std::memory_order_acq_rel, std::memory_order_acquire);
}

void object_scheduler::release() noexcept
{
    isOwned.store(false, std::memory_order_release);
}

bool object_scheduler::checkTransitionTick(const steady_tick tickEnd) const noexcept
{
    return tickFlushing > restriction.MAX_OCCUPY_TICK_FLUSHING ||
        tickEnd - tickBeginOccupying > restriction.MAX_OCCUPY_TICK;
}

bool object_scheduler::checkTransitionCount() const noexcept
{
    return cntFlushed >= restriction.MAX_FLUSH_COUNT;
}
