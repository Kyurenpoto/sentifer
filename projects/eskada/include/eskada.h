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

        void move(const EventDeqOp op, EventDeqIndex& result)
            const noexcept
        {
            switch (op)
            {
            case EventDeqOp::PUSH_FRONT:
            {
                result = *this;
                result.front = (front - 1 + REAL_SIZE) % REAL_SIZE;
            }
            case EventDeqOp::PUSH_BACK:
            {
                result = *this;
                result.back = (back + 1) % REAL_SIZE;
            }
            case EventDeqOp::POP_FRONT:
            {
                result = *this;
                result.front = (front + 1) % REAL_SIZE;
            }
            case EventDeqOp::POP_BACK:
            {
                result = *this;
                result.back = (back - 1 + REAL_SIZE) % REAL_SIZE;
            }
            }
        }

        [[nodiscard]]
        bool isValid()
            const noexcept
        {
            return front != back;
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
        Task* oldTask = nullptr;
        Task* newTask = nullptr;
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

        size_t targetIndex()
        {
            switch (op)
            {
            case EventDeqOp::PUSH_FRONT:
                return oldIndex.front;
            case EventDeqOp::PUSH_BACK:
                return oldIndex.back;
            case EventDeqOp::POP_FRONT:
                return (oldIndex.front + 1) % REAL_SIZE;
            case EventDeqOp::POP_BACK:
                return (oldIndex.back - 1 + REAL_SIZE) % REAL_SIZE;
            default:
                return 0;
            }
        }
    };

    template<class Task, size_t MAX_SIZE>
    struct EventDeqRaw
    {
    private:
        constexpr static size_t REAL_SIZE = MAX_SIZE + 2;

    public:
        using IndexType = EventDeqIndex<REAL_SIZE>;
        using DescType = EventDeqDesc<Task, REAL_SIZE>;

    private:
        std::atomic<IndexType*> index = nullptr;
        std::atomic<DescType*> registered = nullptr;
        std::array<std::atomic<Task*>, REAL_SIZE> tasks{ nullptr };

    public:
        bool tryCommitTask(size_t idx, Task*& oldTask, Task* const newTask)
        {
            return tasks[idx].compare_exchange_strong(
                oldTask, newTask, std::memory_order_acq_rel);
        }

        Task* loadTask(size_t idx)
        {
            return tasks[idx].load(std::memory_order_acquire);
        }

        void storeTask(size_t idx, Task* task)
        {
            tasks[idx].store(task, std::memory_order::release);
        }

        bool tryCommitIndex(IndexType*& oldIndex, IndexType* const newIndex)
        {
            return index.compare_exchange_strong(
                oldIndex, newIndex, std::memory_order_acq_rel);
        }

        IndexType* loadIndex()
        {
            return index.load(std::memory_order_acquire);
        }

        void storeIndex(IndexType* idx)
        {
            index.store(idx, std::memory_order::release);
        }

        bool tryCommitDesc(DescType*& oldDesc, DescType* const newDesc)
        {
            return registered.compare_exchange_strong(
                oldDesc, newDesc, std::memory_order_acq_rel);
        }

        DescType* loadDesc()
        {
            return registered.load(std::memory_order_acquire);
        }

        void storeDesc(DescType* desc)
        {
            registered.store(desc, std::memory_order::release);
        }
    };

    template<class Task, size_t MAX_SIZE>
    struct EventDeqBase :
        public EventDeqRaw<Task, MAX_SIZE>
    {
        using BaseType = EventDeqRaw<Task, MAX_SIZE>;
        using IndexType = typename BaseType::IndexType;
        using DescType = typename BaseType::DescType;

    private:
        std::pmr::memory_resource* res;

    public:
        EventDeqBase() :
            res(std::pmr::get_default_resource())
        {
            IndexType initIndex;
            IndexType* initIndexPtr = create(initIndex);
            this->storeIndex(initIndexPtr);
        }

        ~EventDeqBase()
        {
            IndexType* indexPtr = this->loadIndex();
            destroy(indexPtr);

            DescType* descPtr = this->loadDesc();
            destroy(descPtr);
        }

        EventDeqBase(const EventDeqBase&) = delete;
        EventDeqBase(EventDeqBase&&) = delete;
        EventDeqBase& operator= (const EventDeqBase&) = delete;
        EventDeqBase& operator= (EventDeqBase&&) = delete;

        template<class T>
        T* create(const T& origin)
        {
            if constexpr (!std::is_same_v<T, IndexType> &&
                !std::is_same_v<T, DescType>)
                return nullptr;

            try
            {
                T* idx = static_cast<T*>(res->allocate(sizeof(T), alignof(T)));
                new(idx) T(origin);

                return idx;
            }
            catch (...)
            {
                return nullptr;
            }
        }

        template<class T>
        void destroy(T*& origin)
        {
            if constexpr (!std::is_same_v<T, IndexType> &&
                !std::is_same_v<T, DescType>)
                return;

            if (origin == nullptr)
                return;

            try
            {
                origin->~T();
                res->deallocate(origin, sizeof(T), alignof(T));
            }
            catch (...)
            {
            }
        }

        bool tryCommitTask(DescType* const desc)
        {
            Task* oldTask = desc->oldTask;
            
            return tryCommitTask(desc->targetIndex(), oldTask, desc->newTask);
        }

        void rollbackTask(DescType* const desc)
        {
            this->storeTask(desc->targetIndex(), desc->oldTask);
        }

        bool tryCommitIndex(DescType* const desc)
        {
            IndexType* oldIndex = loadIndex();
            IndexType oldIndexValue = *oldIndex;
            if (desc->oldIndex != oldIndexValue)
                return false;

            IndexType* newIndex = create(desc->newIndex);
            if (tryCommitIndex(oldIndex, newIndex))
            {
                destroy(oldIndex);

                return true;
            }

            destroy(newIndex);

            return false;
        }
    };
}
