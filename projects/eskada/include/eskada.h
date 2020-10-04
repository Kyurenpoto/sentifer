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

        EventDeqIndex move(const EventDeqOp op)
            const noexcept
        {
            EventDeqIndex result = *this;

            switch (op)
            {
            case EventDeqOp::PUSH_FRONT:
                result.front = (front - 1 + REAL_SIZE) % REAL_SIZE;
                break;
            case EventDeqOp::PUSH_BACK:
                result.back = (back + 1) % REAL_SIZE;
                break;
            case EventDeqOp::POP_FRONT:
                result.front = (front + 1) % REAL_SIZE;
                break;
            case EventDeqOp::POP_BACK:
                result.back = (back - 1 + REAL_SIZE) % REAL_SIZE;
                break;
            default:
                break;
            }

            return result;
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

    public:
        EventDeqBase(RawType* raw_) :
            raw(raw_)
        {
            if (raw_ == nullptr)
                std::abort();
        }

        ~EventDeqBase() = default;

        EventDeqBase(const EventDeqBase&) = delete;
        EventDeqBase(EventDeqBase&&) = delete;
        EventDeqBase& operator= (const EventDeqBase&) = delete;
        EventDeqBase& operator= (EventDeqBase&&) = delete;

        [[nodiscard]]
        bool isValidIndex(
            const DescType& desc,
            IndexType*& oldIndex,
            IndexType& oldIndexVal,
            IndexType& newIndexVal)
        {
            oldIndex = raw->loadIndex();
            if (oldIndex == nullptr)
                std::abort();

            oldIndexVal = *oldIndex;
            newIndexVal = oldIndexVal.move(desc.op);

            return oldIndexVal.isValid() && newIndexVal.isValid();
        }

        [[nodiscard]]
        bool tryCommitTask(DescType& desc, size_t idx)
        {
            desc.oldTask = raw->loadTask(idx);
            if ((desc.oldTask == nullptr) == (desc.newTask == nullptr))
                return false;

            return raw->casTask(idx, desc.oldTask, desc.newTask);
        }

        [[nodiscard]]
        bool tryCommitIndex(IndexType*& oldIndex, const IndexType& newIndexVal)
        {
            IndexType* newIndex =
                static_cast<IndexType*>(ThreadLocalStorage::index);
            if (newIndex == nullptr)
                std::abort();

            new(newIndex) IndexType(newIndexVal);

            return raw->casIndex(oldIndex, newIndex);
        }

        void rollbackCommits(const DescType& desc, size_t idx)
        {
            raw->storeTask(idx, desc.oldTask);

            IndexType* newIndex =
                static_cast<IndexType*>(ThreadLocalStorage::index);
            if (newIndex == nullptr)
                std::abort();

            newIndex->~IndexType();
        }

        void updateTLS(IndexType*& oldIndex)
        {
            if (oldIndex == nullptr)
                std::abort();

            oldIndex->~IndexType();
            ThreadLocalStorage::index = static_cast<void*>(oldIndex);
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
        BaseType* base;

    public:
        EventDeqLF(BaseType* base_) :
            base(base_)
        {
            if (base_ == nullptr)
                std::abort();
        }

        ~EventDeqLF() = default;

        EventDeqLF(const EventDeqLF&) = delete;
        EventDeqLF(EventDeqLF&&) = delete;
        EventDeqLF& operator= (const EventDeqLF&) = delete;
        EventDeqLF& operator= (EventDeqLF&&) = delete;

        void doLFOp(DescType& desc)
        {
            if (desc.state != EventDeqState::PENDING)
                return;

            IndexType* oldIndex;
            IndexType oldIndexVal, newIndexVal;
            if (!base->isValidIndex(desc, oldIndex, oldIndexVal, newIndexVal))
            {
                desc.state = EventDeqState::FAILED;

                return;
            }

            size_t idx = oldIndexVal.targetIndex();
            if (!base->tryCommitTask(desc, idx))
            {
                desc.state = EventDeqState::FAILED;

                return;
            }

            if (!base->tryCommitIndex(oldIndex, newIndexVal))
            {
                base->rollbackCommits(desc, idx);

                desc.state = EventDeqState::FAILED;

                return;
            }

            base->updateTLS(oldIndex);

            desc.state = EventDeqState::SUCCESS;
        }
    };
}
