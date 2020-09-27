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

        [[nodiscard]]
        bool operator==(const EventDeqIndex& other)
            const noexcept
        {
            return front == other.front && back == other.back;
        }

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
    struct EventDeqBasicOp
    {
        constexpr static size_t REAL_SIZE = MAX_SIZE + 2;
    
        using IndexType = EventDeqIndex<REAL_SIZE>;
        using DescType = EventDeqDesc<Task, REAL_SIZE>;

        virtual bool casTask(size_t idx, Task*& oldTask, Task* newTask) = 0;
        virtual Task* loadTask(size_t idx) = 0;
        virtual void storeTask(size_t idx, Task* task) = 0;
        virtual bool casIndex(IndexType*& oldIndex, IndexType* newIndex) = 0;
        virtual IndexType* loadIndex() = 0;
        virtual void storeIndex(IndexType* idx) = 0;
        virtual bool casDesc(DescType*& oldDesc, DescType* newDesc) = 0;
        virtual DescType* loadDesc() = 0;
        virtual void storeDesc(DescType* desc) = 0;
    };

    template<class Task, size_t MAX_SIZE>
    struct EventDeqRaw :
        EventDeqBasicOp<Task, MAX_SIZE>
    {
    public:
        using OpType = EventDeqBasicOp<Task, MAX_SIZE>;
        using IndexType = typename OpType::IndexType;
        using DescType = typename OpType::DescType;

    private:
        std::atomic<IndexType*> index = nullptr;
        std::atomic<DescType*> registered = nullptr;
        std::array<std::atomic<Task*>, OpType::REAL_SIZE> tasks{ nullptr };

    public:
        bool casTask(size_t idx, Task*& oldTask, Task* const newTask)
            override
        {
            return tasks[idx].compare_exchange_strong(
                oldTask, newTask, std::memory_order_acq_rel);
        }

        Task* loadTask(size_t idx)
            override
        {
            return tasks[idx].load(std::memory_order_acquire);
        }

        void storeTask(size_t idx, Task* const task)
            override
        {
            tasks[idx].store(task, std::memory_order::release);
        }

        bool casIndex(IndexType*& oldIndex, IndexType* const newIndex)
            override
        {
            return index.compare_exchange_strong(
                oldIndex, newIndex, std::memory_order_acq_rel);
        }

        IndexType* loadIndex()
            override
        {
            return index.load(std::memory_order_acquire);
        }

        void storeIndex(IndexType* const idx)
            override
        {
            index.store(idx, std::memory_order::release);
        }

        bool casDesc(DescType*& oldDesc, DescType* const newDesc)
            override
        {
            return registered.compare_exchange_strong(
                oldDesc, newDesc, std::memory_order_acq_rel);
        }

        DescType* loadDesc()
            override
        {
            return registered.load(std::memory_order_acquire);
        }

        void storeDesc(DescType* const desc)
            override
        {
            registered.store(desc, std::memory_order::release);
        }
    };

    template<class Task, size_t MAX_SIZE>
    struct EventDeqBase :
        EventDeqBasicOp<Task, MAX_SIZE>
    {
        using RawType = EventDeqRaw<Task, MAX_SIZE>;
        using OpType = EventDeqBasicOp<Task, MAX_SIZE>;
        using IndexType = typename OpType::IndexType;
        using DescType = typename OpType::DescType;

    private:
        RawType* raw;
        std::pmr::memory_resource* res;

    public:
        EventDeqBase(
            RawType* base,
            std::pmr::memory_resource* upstream =
            std::pmr::get_default_resource()) :
            raw(base),
            res(upstream)
        {
            if (base == nullptr)
                throw std::invalid_argument{ "base is nullptr" };

            IndexType initIndex;
            IndexType* initIndexPtr = create(initIndex);
            storeIndex(initIndexPtr);
        }

        ~EventDeqBase()
        {
            IndexType* indexPtr = loadIndex();
            destroy(indexPtr);

            DescType* descPtr = loadDesc();
            destroy(descPtr);
        }

        EventDeqBase(const EventDeqBase&) = delete;
        EventDeqBase(EventDeqBase&&) = delete;
        EventDeqBase& operator= (const EventDeqBase&) = delete;
        EventDeqBase& operator= (EventDeqBase&&) = delete;

        bool casTask(size_t idx, Task*& oldTask, Task* const newTask)
            override
        {
            return raw->casTask(idx, oldTask, newTask);
        }

        Task* loadTask(size_t idx)
            override
        {
            return raw->loadTask(idx);
        }

        void storeTask(size_t idx, Task* const task)
            override
        {
            raw->storeTask(idx, task);
        }

        bool casIndex(IndexType*& oldIndex, IndexType* const newIndex)
            override
        {
            return raw->casIndex(oldIndex, newIndex);
        }

        IndexType* loadIndex()
            override
        {
            return raw->loadIndex();
        }

        void storeIndex(IndexType* const idx)
            override
        {
            raw->storeIndex(idx);
        }

        bool casDesc(DescType*& oldDesc, DescType* const newDesc)
            override
        {
            return raw->casDesc(oldDesc, newDesc);
        }

        DescType* loadDesc()
            override
        {
            return raw->loadDesc();
        }

        void storeDesc(DescType* const desc)
            override
        {
            raw->storeDesc(desc);
        }

        template<class T>
        [[nodiscard]]
        T* create(const T& origin)
        {
            if constexpr (!std::is_same_v<T, IndexType> &&
                !std::is_same_v<T, DescType>)
                return nullptr;
            else
            {
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
        }

        template<class T>
        void destroy(T*& origin)
        {
            if constexpr (!std::is_same_v<T, IndexType> &&
                !std::is_same_v<T, DescType>)
                return;
            else
            {
                if (origin == nullptr)
                    return;

                origin->~T();
                res->deallocate(origin, sizeof(T), alignof(T));
                origin = nullptr;
            }
        }

        [[nodiscard]]
        bool tryCommitTask(DescType* const desc)
        {
            Task* oldTask = desc->oldTask;
            
            if (casTask(desc->targetIndex(), oldTask, desc->newTask))
                return true;

            return oldTask == desc->newTask;
        }

        void rollbackTask(DescType* const desc)
        {
            storeTask(desc->targetIndex(), desc->oldTask);
        }

        [[nodiscard]]
        bool tryCommitIndex(DescType* const desc)
        {
            IndexType* oldIndex = loadIndex();
            IndexType oldIndexValue = *oldIndex;
            if (desc->newIndex == oldIndexValue)
                return true;
            if (desc->oldIndex != oldIndexValue)
                return false;

            IndexType* tmpIndex = oldIndex;
            IndexType* newIndex = create(desc->newIndex);
            if (casIndex(oldIndex, newIndex))
            {
                destroy(oldIndex);

                return true;
            }

            destroy(newIndex);
            destroy(tmpIndex);

            return false;
        }
    };
}
