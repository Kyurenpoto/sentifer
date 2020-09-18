#pragma once

#include <array>
#include <atomic>
#include <memory_resource>

namespace eskada
{
    enum class EventDeqOp :
        uint8_t
    {
        PUSH_FRONT,
        PUSH_BACK,
        POP_FRONT,
        POP_BACK
    };

    template<size_t REAL_SIZE>
    struct EventDeqIndex
    {
        size_t front = 0;
        size_t back = 1;

        void move(
            const EventDeqOp op,
            const size_t maxSize,
            EventDeqIndex& result)
            const noexcept
        {
            switch (op)
            {
            case EventDeqOp::PUSH_FRONT:
            {
                result = *this;
                result.front = (front - 1 + maxSize) % maxSize;
            }
            case EventDeqOp::PUSH_BACK:
            {
                result = *this;
                result.back = (back + 1) % maxSize;
            }
            case EventDeqOp::POP_FRONT:
            {
                result = *this;
                result.front = (front + 1) % maxSize;
            }
            case EventDeqOp::POP_BACK:
            {
                result = *this;
                result.back = (back - 1 + maxSize) % maxSize;
            }
            }
        }

        bool isValid()
            const noexcept
        {
            return front != back;
        }

        static EventDeqIndex* create(
            const EventDeqIndex& index,
            std::pmr::memory_resource* res)
        {
            try
            {
                EventDeqIndex* idx =
                    static_cast<EventDeqIndex*>(res->allocate(
                        sizeof(EventDeqIndex), alignof(EventDeqIndex)));
                new(idx) EventDeqIndex(index);

                return idx;
            }
            catch (...)
            {
                return nullptr;
            }
        }

        static void destroy(
            EventDeqIndex*& index,
            std::pmr::memory_resource* res)
        {
            if (index == nullptr)
                return;

            try
            {
                index->~EventDeqIndex();
                res->deallocate(
                    index, sizeof(EventDeqIndex), alignof(EventDeqIndex));
            }
            catch (...)
            {
            }
        }
    };

    enum class EventDeqPhase :
        uint8_t
    {
        TRYING,
        SUCCESS,
        FAILED
    };

    template<class Task, size_t REAL_SIZE>
    struct EventDeqDesc
    {
        EventDeqIndex<REAL_SIZE> oldIndex;
        EventDeqIndex<REAL_SIZE> newIndex;
        Task* oldTask;
        Task* newTask;
        EventDeqOp op = EventDeqOp::PUSH_FRONT;
        EventDeqPhase phase = EventDeqPhase::TRYING;

        void updatePhase(const EventDeqPhase newPhase, EventDeqDesc& result)
            const noexcept
        {
            result = *this;
            result.phase = newPhase;
        }

        void updateIndex(
            const EventDeqIndex<REAL_SIZE>& index,
            const size_t maxSize,
            EventDeqDesc& result)
            const noexcept
        {
            result = *this;
            result.oldIndex = index;
            index.move(result.op, maxSize, result.newIndex);
        }

        size_t targetIndex(const size_t maxSize)
        {
            switch (op)
            {
            case EventDeqOp::PUSH_FRONT:
                return oldIndex.front;
            case EventDeqOp::PUSH_BACK:
                return oldIndex.back;
            case EventDeqOp::POP_FRONT:
                return (oldIndex.front + 1) % maxSize;
            case EventDeqOp::POP_BACK:
                return (oldIndex.back - 1 + maxSize) % maxSize;
            }
        }

        static EventDeqDesc* create(
            EventDeqDesc& descriptor,
            std::pmr::memory_resource* res)
        {
            try
            {
                EventDeqDesc* desc =
                    static_cast<EventDeqDesc*>(res->allocate(
                        sizeof(EventDeqDesc), alignof(EventDeqDesc)));
                new(desc) EventDeqDesc(descriptor);

                return desc;
            }
            catch (...)
            {
                return nullptr;
            }
        }

        static void destroy(
            EventDeqDesc*& descriptor,
            std::pmr::memory_resource* res)
        {
            if (descriptor == nullptr)
                return;

            try
            {
                descriptor->~EventDeqDesc();
                res->deallocate(
                    descriptor, sizeof(EventDeqDesc), alignof(EventDeqDesc));
            }
            catch (...)
            {
            }
        }
    };

    template<class Task, size_t MAX_SIZE>
    struct EventDeqBase
    {
    private:
        constexpr static size_t REAL_SIZE = MAX_SIZE + 2;

    private:
        std::atomic<EventDeqIndex<REAL_SIZE>*> index;
        std::atomic<EventDeqDesc<Task, REAL_SIZE>*> registered = nullptr;
        std::pmr::memory_resource* res;
        std::array<std::atomic<Task*>, REAL_SIZE> tasks;

    public:
        EventDeqBase() :
            res(std::pmr::get_default_resource())
        {
            EventDeqIndex<REAL_SIZE> initIndex;
            EventDeqIndex<REAL_SIZE>* initIndexPtr =
                EventDeqIndex<REAL_SIZE>::create(initIndex, res);

            index.store(initIndexPtr, std::memory_order_relaxed);
        }

        ~EventDeqBase()
        {
            EventDeqIndex<REAL_SIZE>* indexPtr =
                index.load(std::memory_order_relaxed);
            EventDeqIndex<REAL_SIZE>::destroy(indexPtr);

            EventDeqDesc<Task, REAL_SIZE>* descPtr =
                registered.load(std::memory_order_relaxed);
            EventDeqDesc<Task, REAL_SIZE>::destroy(descPtr);
        }

        bool tryCommitTask(EventDeqDesc<Task, REAL_SIZE>* const desc)
        {
            Task* currTask = desc->oldTask;
            std::atomic<Task*>& target = tasks[desc->targetIndex(REAL_SIZE)];
            if (target.compare_exchange_strong(
                currTask, desc->newTask, std::memory_order_acq_rel))
                return true;

            return currTask == desc->newTask;
        }

        void rollbackTask(EventDeqDesc<Task, REAL_SIZE>* const desc)
        {
            Task* currTask = desc->newTask;
            std::atomic<Task*>& target = tasks[desc->targetIndex(REAL_SIZE)];
            target.compare_exchange_strong(
                currTask, desc->oldTask, std::memory_order_acq_rel);
        }

        bool tryCommitIndex(EventDeqDesc<Task, REAL_SIZE>* const desc)
        {
            EventDeqIndex<REAL_SIZE>* currIndex =
                index.load(std::memory_order_acquire);
            EventDeqIndex<REAL_SIZE> currIndexValue = *currIndex;
            if (desc->oldIndex != currIndexValue)
                return false;

            EventDeqIndex<REAL_SIZE>* newIndex =
                EventDeqIndex<REAL_SIZE>::create(desc->newIndex, res);
            if (index.compare_exchange_strong(
                currIndex, newIndex, std::memory_order_acq_rel))
            {
                EventDeqIndex<REAL_SIZE>::destroy(currIndex, res);

                return true;
            }

            EventDeqIndex<REAL_SIZE>::destroy(newIndex, res);

            return false;
        }

        bool tryCommitDesc(
            EventDeqDesc<Task, REAL_SIZE>*& oldDesc,
            EventDeqDesc<Task, REAL_SIZE>* const newDesc)
        {
            return registered.compare_exchange_strong(
                oldDesc, newDesc, std::memory_order_acq_rel);
        }
    };
}
