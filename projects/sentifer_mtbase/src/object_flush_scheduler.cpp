#include "../include/sentifer_mtbase/details/schedulers/object_flush_scheduler.h"

#include "../include/sentifer_mtbase/details/base_structures.hpp"
#include "../include/sentifer_mtbase/details/control_block.h"
#include "../include/sentifer_mtbase/details/schedulers/thread_local_scheduler.h"

using namespace mtbase;

void object_flush_scheduler::registerFlushObjectTask(object_scheduler* const objectSched)
{
    registerTaskImpl(alloc.new_flush_object_task(objectSched));
}

void object_flush_scheduler::flush(thread_local_scheduler& threadSched)
{
    control_block& block = threadSched.getControlBlock(this);

    flushTasks(block);

    block.release();

    threadSched.registerMethodTask(
        this, &object_flush_scheduler::flush, threadSched);
}

void object_flush_scheduler::registerTaskImpl(task_flush_object_t* const task)
{
    if (!storage->push_back(task))
    {
        alloc.delete_task(task);
    }
}

void object_flush_scheduler::flushTasks(control_block& block)
{
    for (size_t i = 0;
        i < restriction.MAX_FLUSH_COUNT_AT_ONCE &&
        !block.checkExpiredCount(restriction);
        ++i)
        executeTask(block);
}

void object_flush_scheduler::executeTask(control_block& block)
{
    task_t* const task = storage->pop_front();
    if (task == nullptr)
    {
        block.recordCountExpired(restriction);

        return;
    }

    invokeTask(block, task);
    alloc.delete_task(task);
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
