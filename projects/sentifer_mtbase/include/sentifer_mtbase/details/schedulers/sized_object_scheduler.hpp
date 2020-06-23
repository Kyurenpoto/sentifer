#pragma once

#include <memory_resource>

#include "../base_structures.hpp"
#include "object_scheduler.h"

namespace mtbase
{
    template<size_t MAX_TASK_STORAGE = (1 << 20)>
    struct sized_object_scheduler final :
        public object_scheduler
    {
        sized_object_scheduler(
            std::pmr::memory_resource* const res,
            object_flush_scheduler& objectFlushSched,
            const scheduler_restriction&& restricts) :
            object_scheduler{ res, objectFlushSched, std::move(restricts) },
            taskDeq{ res }
        {}

    protected:
        bool pushBaskTask(task_invoke_t* const task) override
        {
            return taskDeq.push_back(task);
        }

        task_invoke_t* popFrontTask() override
        {
            return toValidTask(taskDeq.pop_front());
        }

    private:
        task_invoke_t* toValidTask(task_t* const task) const noexcept
        {
            return nullptr;
        }

        task_invoke_t* toValidTask(task_invoke_t* const task) const noexcept
        {
            return task;
        }

    private:
        task_wait_free_deque<MAX_TASK_STORAGE> taskDeq;
    };
}
