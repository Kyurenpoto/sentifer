#include "../include/sentifer_mtbase/details/schedulers/object_scheduler.h"

#include "../include/sentifer_mtbase/details/schedulers/thread_local_scheduler.h"
#include "../include/sentifer_mtbase/details/schedulers/object_flush_scheduler.h"

using namespace mtbase;

void object_scheduler::flush(thread_local_scheduler& threadSched)
{
    if (!tryOwn())
        return;

    threadSched.getControlBlock(this).reset();

    flushOwned(threadSched);
}

void object_scheduler::registerTaskImpl(task_invoke_t* const task)
{
    if (!pushBaskTask(task))
    {
        destroyTask(task);
    }
}

bool object_scheduler::pushBaskTask(task_invoke_t* const task)
{
    return false;
}

task_t* object_scheduler::popFrontTask()
{
    return nullptr;
}

void object_scheduler::flushOwned(thread_local_scheduler& threadSched)
{
    const steady_tick tickBegin = clock_t::getSteadyTick();
    control_block& block = threadSched.getControlBlock(this);

    flushTasks(block);

    const steady_tick tickEnd = clock_t::getSteadyTick();
    
    block.recordTickFlushing(tickBegin, tickEnd);

    if (block.checkTransition(restriction, tickEnd))
    {
        block.release();
        release();
        flusher.registerTask(this);

        return;
    }

    threadSched.registerTask(this, &object_scheduler::flushOwned, threadSched);
}

void object_scheduler::flushTasks(control_block& block)
{
    for (size_t i = 0;
        i < restriction.MAX_FLUSH_COUNT_AT_ONCE &&
        !block.checkTransitionCount(restriction);
        ++i)
        executeTask(block);
}

void object_scheduler::executeTask(control_block& block)
{
    task_t* const task = popFrontTask();
    if (task == nullptr)
    {
        block.recordCountExpired(restriction);

        return;
    }

    invokeTask(block, task);
    destroyTask(task);
}

void object_scheduler::invokeTask(
    control_block& block,
    task_t* const task)
    const
{}

void object_scheduler::invokeTask(
    control_block& block,
    task_invoke_t* const task)
    const
{
    task->invoke();
    block.recordCountFlushing();
}

bool object_scheduler::tryOwn() noexcept
{
    bool oldOwned = false;
    return isOwned.compare_exchange_strong(oldOwned, true,
        std::memory_order_acq_rel, std::memory_order_acquire);
}

void object_scheduler::release() noexcept
{
    isOwned.store(false, std::memory_order_release);
}
