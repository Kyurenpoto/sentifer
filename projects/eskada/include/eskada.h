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

        [[nodiscard]]
        size_t targetIndex(EventDeqOp op)
        {
            switch (op)
            {
            case EventDeqOp::PUSH_FRONT:
                return front;
            case EventDeqOp::PUSH_BACK:
                return back;
            case EventDeqOp::POP_FRONT:
                return (front + 1) % REAL_SIZE;
            case EventDeqOp::POP_BACK:
                return (back - 1 + REAL_SIZE) % REAL_SIZE;
            default:
                return 0;
            }
        }
    };

    enum class EventDeqState :
        uint8_t
    {
        PENDING,
        SUCCESS,
        FAILED
    };

    template<class Task>
    struct EventDeqDesc
    {
        Task* oldTask = nullptr;
        Task* newTask = nullptr;
        EventDeqOp op = EventDeqOp::PUSH_FRONT;
        EventDeqState state = EventDeqState::PENDING;

        void updatePhase(const EventDeqState newPhase, EventDeqDesc& result)
            const noexcept
        {
            result = *this;
            result.state = newPhase;
        }
    };

    template<class Task, size_t MAX_SIZE>
    struct EventDeqRaw
    {
    private:
        static constexpr size_t REAL_SIZE = MAX_SIZE + 2;

    public:
        using IndexType = EventDeqIndex<REAL_SIZE>;
        using DescType = EventDeqDesc<Task>;

    private:
        std::atomic<IndexType*> index = nullptr;
        std::atomic<DescType*> registered = nullptr;
        std::array<std::atomic<Task*>, REAL_SIZE> tasks{ nullptr };

    public:
        bool casTask(size_t idx, Task*& oldTask, Task* const newTask)
        {
            return tasks[idx].compare_exchange_strong(
                oldTask, newTask, std::memory_order_acq_rel);
        }

        Task* loadTask(size_t idx)
        {
            return tasks[idx].load(std::memory_order_acquire);
        }

        void storeTask(size_t idx, Task* const task)
        {
            tasks[idx].store(task, std::memory_order::release);
        }

        bool casIndex(IndexType*& oldIndex, IndexType* const newIndex)
        {
            return index.compare_exchange_strong(
                oldIndex, newIndex, std::memory_order_acq_rel);
        }

        IndexType* loadIndex()
        {
            return index.load(std::memory_order_acquire);
        }

        void storeIndex(IndexType* const idx)
        {
            index.store(idx, std::memory_order::release);
        }

        bool casDesc(DescType*& oldDesc, DescType* const newDesc)
        {
            return registered.compare_exchange_strong(
                oldDesc, newDesc, std::memory_order_acq_rel);
        }

        DescType* loadDesc()
        {
            return registered.load(std::memory_order_acquire);
        }

        void storeDesc(DescType* const desc)
        {
            registered.store(desc, std::memory_order::release);
        }
    };

    template<class Task, size_t MAX_SIZE>
    struct EventDeqBase
    {
        using RawType = EventDeqRaw<Task, MAX_SIZE>;
        using IndexType = typename RawType::IndexType;
        using DescType = typename RawType::DescType;

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
            raw->storeIndex(initIndexPtr);
        }

        ~EventDeqBase()
        {
            IndexType* indexPtr = raw->loadIndex();
            destroy(indexPtr);

            DescType* descPtr = raw->loadDesc();
            destroy(descPtr);
        }

        EventDeqBase(const EventDeqBase&) = delete;
        EventDeqBase(EventDeqBase&&) = delete;
        EventDeqBase& operator= (const EventDeqBase&) = delete;
        EventDeqBase& operator= (EventDeqBase&&) = delete;

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
        bool tryCommitTask(DescType* const desc, size_t idx)
        {
            Task* oldTask = desc->oldTask;

            if (raw->casTask(idx, oldTask, desc->newTask))
                return true;

            return oldTask == desc->newTask;
        }

        void rollbackTask(DescType* const desc, size_t idx)
        {
            raw->storeTask(idx, desc->oldTask);
        }
    };

    struct ThreadLocalStorage
    {
        inline thread_local static void* index = nullptr;
    };

    template<class Task, size_t MAX_SIZE>
    struct EventDeqLF
    {
        using RawType = EventDeqRaw<Task, MAX_SIZE>;
        using BaseType = EventDeqBase<Task, MAX_SIZE>;
        using IndexType = typename RawType::IndexType;
        using DescType = typename RawType::DescType;

    private:
        RawType* raw;
        BaseType* base;

    public:
        void doLFOp(DescType& desc)
        {
            if (desc.state != EventDeqState::PENDING)
                return;

            IndexType* oldIndex = raw->loadIndex();
            if (oldIndex == nullptr)
            {
                desc.state = EventDeqState::FAILED;

                return;
            }

            IndexType oldIndexVal = *oldIndex;
            IndexType newIndexVal = oldIndexVal.move(desc.op);
            if (!oldIndexVal.isValid() || !newIndexVal.isValid())
            {
                desc.state = EventDeqState::FAILED;

                return;
            }

            size_t idx = oldIndexVal.targetIndex();
            desc.oldTask = raw->loadTask(idx);
            if ((desc.oldTask == nullptr) == (desc.newTask == nullptr))
            {
                desc.state = EventDeqState::FAILED;

                return;
            }

            if (!raw->casTask(idx, desc.oldTask, desc.newTask))
            {
                desc.state = EventDeqState::FAILED;

                return;
            }

            IndexType* newIndex =
                static_cast<IndexType*>(ThreadLocalStorage::index);
            newIndex->IndexType();
            newIndex->front = newIndexVal.front;
            newIndex->back = newIndexVal.back;
            if (!raw->casIndex(oldIndex, newIndex))
            {
                raw->storeTask(idx, desc.oldTask);
                newIndex->~IndexType();

                desc.state = EventDeqState::FAILED;

                return;
            }

            oldIndex->~Indextype();
            ThreadLocalStorage::index = static_cast<void*>(oldIndex);

            desc.state = EventDeqState::SUCCESS;
        }
    };
}
