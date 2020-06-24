#pragma once

#include <atomic>
#include <array>

#include "memory_managers.hpp"
#include "tasks.hpp"

namespace mtbase
{
    struct task_t;

    constexpr size_t BASE_ALIGN = alignof(void*);

    static_assert(BASE_ALIGN == 8, "mtbase is only available on x64");

    struct task_storage
    {
        virtual bool push_front(task_t* task) = 0;
        virtual bool push_back(task_t* task) = 0;
        virtual task_t* pop_front() = 0;
        virtual task_t* pop_back() = 0;
    };

    template<size_t SIZE>
    struct task_wait_free_deque final
    {
        static_assert(SIZE >= BASE_ALIGN * 8);
        static_assert(SIZE <= 0xFFFF'FFFD);

    private:
        enum class OP :
            size_t
        {
            PUSH_FRONT,
            PUSH_BACK,
            POP_FRONT,
            POP_BACK
        };

        struct index_t
        {
            index_t move(OP op) const noexcept
            {
                switch (op)
                {
                case OP::PUSH_FRONT:
                    return pushed_front();
                case OP::PUSH_BACK:
                    return pushed_back();
                case OP::POP_FRONT:
                    return poped_front();
                case OP::POP_BACK:
                    return poped_back();
                default:
                    return index_t{};
                }
            }

            index_t pushed_front() const noexcept
            {
                return index_t
                {
                    .front = (front + REAL_SIZE - 1) % REAL_SIZE,
                    .back = back
                };
            }

            index_t pushed_back() const noexcept
            {
                return index_t
                {
                    .front = front,
                    .back = (back + 1) % REAL_SIZE
                };
            }

            index_t poped_front() const noexcept
            {
                return index_t
                {
                    .front = (front + 1) % REAL_SIZE,
                    .back = back
                };
            }

            index_t poped_back() const noexcept
            {
                return index_t
                {
                    .front = front,
                    .back = (back + REAL_SIZE - 1) % REAL_SIZE
                };
            }

            bool isFull() const noexcept
            {
                return (front + REAL_SIZE - back) % REAL_SIZE == 1;
            }

            bool isEmpty() const noexcept
            {
                return (back + REAL_SIZE - front) % REAL_SIZE == 1;
            }

            bool isValid(OP op) const noexcept
            {
                if (front == back)
                    return false;

                switch (op)
                {
                case OP::PUSH_FRONT:
                case OP::PUSH_BACK:
                    return isFull();
                case OP::POP_FRONT:
                case OP::POP_BACK:
                    return isEmpty();
                default:
                    return true;
                }
            }

            size_t getTargetIndex(OP op) const noexcept
            {
                switch (op)
                {
                case OP::PUSH_FRONT:
                case OP::PUSH_BACK:
                    return front;
                case OP::POP_FRONT:
                case OP::POP_BACK:
                    return back;
                default:
                    return 0;
                }
            }

        public:
            const size_t front = 0;
            const size_t back = 1;
        };

        struct op_description
        {
            enum class PHASE :
                size_t
            {
                RESERVE,
                COMPLETE,
                FAIL
            };

        public:
            op_description rollbacked(
                index_t* const oldIndexLoad,
                index_t* const newIndexLoad)
                const noexcept
            {
                return op_description
                {
                    .op = op,
                    .target = target,
                    .oldTask = oldTask,
                    .newTask = newTask,
                    .oldIndex = oldIndexLoad,
                    .newIndex = newIndexLoad
                };
            }

            op_description completed(index_t* const oldIndexLoad) const noexcept
            {
                return op_description
                {
                    .phase = op_description::PHASE::COMPLETE,
                    .op = op,
                    .target = target,
                    .oldTask = oldTask,
                    .newTask = newTask,
                    .oldIndex = oldIndexLoad
                };
            }

            op_description failed(index_t* const oldIndexLoad) const noexcept
            {
                return op_description
                {
                    .phase = op_description::PHASE::FAIL,
                    .op = op,
                    .target = target,
                    .oldTask = oldTask,
                    .newTask = newTask,
                    .oldIndex = oldIndexLoad
                };
            }

        public:
            const PHASE phase{ PHASE::RESERVE };
            const OP op{ OP::PUSH_FRONT };
            std::atomic<task_t*>& target;
            task_t* const oldTask = nullptr;
            task_t* const newTask = nullptr;
            index_t* const oldIndex = nullptr;
            index_t* const newIndex = nullptr;
        };

    public:
        task_wait_free_deque(std::pmr::memory_resource* res) :
            indexAllocator{ res },
            descAllocator{ res }
        {
            index_t* init = indexAllocator.new_object();
            index.store(init, std::memory_order_relaxed);
        }

        ~task_wait_free_deque()
        {
            indexAllocator.delete_object(index.load(std::memory_order_relaxed));
            descAllocator.delete_object(registered.load(std::memory_order_relaxed));
        }

        bool push_front(task_t* task)
        {
            op_description* const desc = createDesc(task, OP::PUSH_FRONT);

            bool result = (desc->phase == op_description::PHASE::COMPLETE);
            destroyDesc(desc);

            return result;
        }

        bool push_back(task_t* task)
        {
            op_description* const desc = createDesc(task, OP::PUSH_BACK);

            bool result = (desc->phase == op_description::PHASE::COMPLETE);
            destroyDesc(desc);

            return result;
        }

        task_t* pop_front()
        {
            op_description* const desc = createDesc(nullptr, OP::POP_FRONT);

            task_t* result = (desc->phase == op_description::PHASE::COMPLETE ?
                desc->oldTask : nullptr);
            destroyDesc(desc);

            return result;
        }

        task_t* pop_back()
        {
            op_description* const desc = createDesc(nullptr, OP::POP_BACK);

            task_t* result = (desc->phase == op_description::PHASE::COMPLETE ?
                desc->oldTask : nullptr);
            destroyDesc(desc);

            return result;
        }

    private:
        op_description* createDesc(task_t* task, OP op)
        {
            index_t* const oldIndex = index.load(std::memory_order_acquire);
            if (!oldIndex->isValid(op))
                return nullptr;

            index_t* const newIndex = indexAllocator.new_object(oldIndex->move(op));
            std::atomic<task_t*>& target = tasks[oldIndex->getTargetIndex(op)];

            op_description* desc = descAllocator.new_object(op_description
                {
                    .phase = op_description::PHASE::RESERVE,
                    .op = op,
                    .target = target,
                    .oldTask = target.load(std::memory_order_acquire),
                    .newTask = task,
                    .oldIndex = oldIndex,
                    .newIndex = newIndex
                });

            applyDesc(desc);

            return desc;
        }

        void applyDesc(op_description*& desc)
        {
            op_description* helpDesc = registered.load(std::memory_order_acquire);
            if (helpDesc != nullptr)
                help_registered(helpDesc);

            for (size_t i = 0; i < MAX_RETRY; ++i)
                if (fast_path(desc))
                    return;

            slow_path(desc);
        }

        bool fast_path(op_description*& desc)
        {
            if (!tryCommitTask(desc))
                return false;

            if (!tryCommitIndex(desc))
            {
                op_description* oldDesc = rollbackTask(desc);
                destroyDesc(oldDesc);

                return false;
            }

            completeDesc(desc);

            return true;
        }

        void slow_path(op_description*& desc)
        {
            op_description* oldDesc = nullptr;
            while (true)
            {
                if (tryRegister(oldDesc, desc))
                    break;

                if (oldDesc == nullptr)
                    continue;

                help_registered(oldDesc);
            }

            help_registered(desc);
        }

        void help_registered(op_description*& desc)
        {
            help_registered_progress(desc);
            help_registered_complete(desc);
        }

        void help_registered_progress(op_description*& desc) noexcept
        {
            while (true)
            {
                if (desc->phase != op_description::PHASE::RESERVE)
                    return;

                if (!tryCommitTask(desc))
                    continue;

                if (tryCommitIndex(desc))
                    break;

                op_description* oldDesc = rollbackTask(desc);
                renewRegistered(oldDesc, desc);
            }
        }

        void help_registered_complete(op_description*& desc) noexcept
        {
            if (desc->phase == op_description::PHASE::FAIL)
                return;

            while (desc->phase != op_description::PHASE::COMPLETE)
            {
                op_description* oldDesc = desc;
                desc = descAllocator.new_object(
                    oldDesc->completed(index.load(std::memory_order_acquire)));
                renewRegistered(oldDesc, desc);
            }
        }

        bool tryCommitTask(op_description* const desc) noexcept
        {
            task_t* oldTask = desc->oldTask;

            if (tryEfficientCAS(desc->target, oldTask, desc->newTask))
                return true;

            return oldTask == desc->newTask;
        }

        bool tryCommitIndex(op_description* const desc) noexcept
        {
            index_t* oldIndex = desc->oldIndex;

            if (tryEfficientCAS(index, oldIndex, desc->newIndex))
                return true;

            return oldIndex == desc->newIndex;
        }

        [[nodiscard]] op_description* rollbackTask(op_description*& desc)
        {
            desc->target.store(desc->oldTask, std::memory_order_release);

            op_description* oldDesc = desc;
            index_t* const oldIndex = index.load(std::memory_order_acquire);
            if (oldIndex->isValid(oldDesc->op))
            {
                index_t* const newIndex =
                    indexAllocator.new_object(oldIndex->move(oldDesc->op));
                desc = descAllocator.new_object(oldDesc->rollbacked(oldIndex, newIndex));
            }
            else
                desc = descAllocator.new_object(oldDesc->failed(oldIndex));
            return oldDesc;
        }

        void completeDesc(op_description*& desc)
        {
            op_description* const oldDesc = desc;
            indexAllocator.delete_object(oldDesc->oldIndex);

            desc = descAllocator.new_object(
                oldDesc->completed(index.load(std::memory_order_acquire)));
            descAllocator.delete_object(oldDesc);
        }

        void renewRegistered(
            op_description* const oldDesc,
            op_description*& desc)
        {
            op_description* curDesc = oldDesc;
            op_description* const newDesc = desc;
            if (tryRegister(curDesc, newDesc))
            {
                destroyDesc(oldDesc);

                desc = newDesc;
            }
            else
            {
                destroyDesc(oldDesc);
                destroyDesc(newDesc);

                desc = curDesc;
            }
        }

        bool tryRegister(
            op_description*& expected,
            op_description* const desired) noexcept
        {
            return tryEfficientCAS(registered, expected, desired);
        }

        template<class T>
        bool tryEfficientCAS(std::atomic<T*>& target, T*& expected, T* const desired) noexcept
        {
            return target.compare_exchange_strong(expected, desired,
                std::memory_order_acq_rel, std::memory_order_acquire);
        }

        void destroyDesc(op_description* const desc)
        {
            op_description* oldDesc = desc;
            while (!tryRegister(oldDesc, nullptr))
                if (oldDesc == desc)
                    break;

            index_t* const curIndex = index.load(std::memory_order_acquire);
            if (desc->oldIndex == curIndex)
                indexAllocator.delete_object(desc->oldIndex);
            if (desc->newIndex == curIndex)
                indexAllocator.delete_object(desc->newIndex);

            descAllocator.delete_object(desc);
        }

    private:
        static constexpr size_t REAL_SIZE = SIZE + 2;
        static constexpr size_t MAX_RETRY = 3;
        obj_allocator<index_t> indexAllocator;
        obj_allocator<op_description> descAllocator;
        std::atomic<index_t*> index;
        std::atomic<op_description*> registered{ nullptr };
        std::array<std::atomic<task_t*>, REAL_SIZE> tasks;
    };
}
