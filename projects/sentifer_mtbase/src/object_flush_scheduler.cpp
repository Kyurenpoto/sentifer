#include "../include/sentifer_mtbase/details/schedulers/object_flush_scheduler.h"

#include "../include/sentifer_mtbase/details/schedulers/thread_local_scheduler.h"

using namespace mtbase;

void object_flush_scheduler::flush(thread_local_scheduler& threadSched)
{
    control_block& block = threadSched.getControlBlock(this);

    flushTasks(block);

    block.release();

    threadSched.registerTask(this, &object_flush_scheduler::flush, threadSched);
}

void object_flush_scheduler::registerTaskImpl(task_flush_object_t* const task)
{
    if (!pushBaskTask(task))
    {
        destroyTask(task);
    }
}

bool object_flush_scheduler::pushBaskTask(task_flush_object_t* const task)
{
    return false;
}

task_t* object_flush_scheduler::popFrontTask()
{
    return nullptr;
}

void object_flush_scheduler::flushTasks(control_block& block)
{
    for (size_t i = 0;
        i < restriction.MAX_FLUSH_COUNT_AT_ONCE &&
        !block.checkTransitionCount(restriction);
        ++i)
        executeTask(block);
}

void object_flush_scheduler::executeTask(control_block& block)
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

void object_flush_scheduler::invokeTask(
    control_block& block,
    task_t* const task)
    const
{}

void object_flush_scheduler::invokeTask(
    control_block& block,
    task_flush_object_t* const task)
    const
{
    task->invoke(block.owner);
    block.recordCountFlushing();
}
