#include "../include/sentifer_mtbase/details/tasks.hpp"

#include "../include/sentifer_mtbase/details/task_storages/thread_local_scheduler.h"
#include "../include/sentifer_mtbase/details/task_storages/object_scheduler.h"

using namespace mtbase;

void task_flush_object_t::invoke(thread_local_scheduler& threadSched)
{
    objectSched->flush(threadSched);
}
