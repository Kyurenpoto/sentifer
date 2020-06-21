#include "../include/sentifer_mtbase/details/tasks.hpp"

#include "../include/sentifer_mtbase/details/task_storages/object_scheduler.hpp"

void mtbase::task_flush_object_scheduler_t::invoke(
    thread_local_scheduler& threadSched)
{
    objectSched->flush(threadSched);
}
