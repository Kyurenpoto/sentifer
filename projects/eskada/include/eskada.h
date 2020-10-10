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
        EventDeqIndex prev(const EventDeqOp op)
            const noexcept
        {
            EventDeqIndex result = *this;

            switch (op)
            {
            case EventDeqOp::PUSH_FRONT:
            case EventDeqOp::POP_FRONT:
                result.front = (front + 1) % REAL_SIZE;
                break;
            case EventDeqOp::PUSH_BACK:
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
        bool isFull()
            const noexcept
        {
            return front == (back + 1) % REAL_SIZE;
        }

        [[nodiscard]]
        bool isEmpty()
            const noexcept
        {
            return (front + 1) % REAL_SIZE == back;
        }

        [[nodiscard]]
        size_t targetIndex(EventDeqOp op)
            const noexcept
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

        [[nodiscard]]
        size_t currentIndex(EventDeqOp op)
            const noexcept
        {
            switch (op)
            {
            case EventDeqOp::PUSH_FRONT:
            case EventDeqOp::POP_FRONT:
                return front;
            case EventDeqOp::PUSH_BACK:
            case EventDeqOp::POP_BACK:
                return back;
            default:
                return 0;
            }
        }
    };

    enum class EventDeqCASState :
        uint8_t
    {
        PENDING,
        SUCCESS,
        FAILURE
    };

    enum class EventDeqRecordState :
        uint8_t
    {
        RESTART,
        PENDING,
        COMPLETED
    };

    template<class Task>
    struct EventDeqTaskDesc
    {
        std::atomic<Task*>* target;
        Task* oldTask = nullptr;
        Task* newTask = nullptr;
        EventDeqCASState state = EventDeqCASState::PENDING;
    };

    template<size_t REAL_SIZE>
    struct EventDeqIndexDesc
    {
        using IndexType = EventDeqIndex<REAL_SIZE>;

        std::atomic<IndexType*>* target;
        IndexType* oldIndex = nullptr;
        IndexType* newIndex = nullptr;
        EventDeqCASState state = EventDeqCASState::PENDING;
    };

    template<class Task, size_t REAL_SIZE>
    struct EventDeqRecord
    {
        Task* input = nullptr;
        Task* output = nullptr;
        size_t ownerTid = 0;
        EventDeqTaskDesc<Task>* taskDesc = nullptr;
        EventDeqIndexDesc<REAL_SIZE>* indexDesc = nullptr;
        EventDeqOp op = EventDeqOp::PUSH_BACK;
        std::atomic<EventDeqRecordState> state = EventDeqRecordState::PENDING;
    };

    template<class Task, size_t REAL_SIZE>
    struct EventDeqRecordBox
    {
        using RecordType = EventDeqRecord<Task, REAL_SIZE>;

        RecordType* record = nullptr;
    };

    template<class Task, size_t MAX_SIZE>
    struct EventDeqRaw
    {
    private:
        static constexpr size_t REAL_SIZE = MAX_SIZE + 2;

    public:
        using IndexType = EventDeqIndex<REAL_SIZE>;
        using RecordType = EventDeqRecord<Task, REAL_SIZE>;
        using RecordBoxType = EventDeqRecordBox<Task, REAL_SIZE>;

    private:
        std::atomic<IndexType*> index = nullptr;
        std::atomic<RecordType*> registered = nullptr;
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

        bool casDesc(RecordType*& oldDesc, RecordType* const newDesc)
        {
            return registered.compare_exchange_strong(
                oldDesc, newDesc, std::memory_order_acq_rel);
        }

        RecordType* loadDesc()
        {
            return registered.load(std::memory_order_acquire);
        }

        void storeDesc(RecordType* const record)
        {
            registered.store(record, std::memory_order::release);
        }
    };

    template<class Task, size_t MAX_SIZE>
    struct EventDeqBase
    {
        using RawType = EventDeqRaw<Task, MAX_SIZE>;
        using IndexType = typename RawType::IndexType;
        using RecordType = typename RawType::RecordType;
        using RecordBoxType = typename RawType::RecordBoxType;

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
            const RecordType& record,
            IndexType*& oldIndex,
            IndexType& oldIndexVal,
            IndexType& newIndexVal)
            const
        {
            oldIndex = raw->loadIndex();
            if (oldIndex == nullptr)
                std::abort();

            oldIndexVal = *oldIndex;
            newIndexVal = oldIndexVal.move(record.op);

            return oldIndexVal.isValid() && newIndexVal.isValid();
        }

        [[nodiscard]]
        bool isNotProgressed(
            const RecordType& record,
            const IndexType& oldIndexVal)
        {
            IndexType prevIndexVal = oldIndexVal.prev(record.op);
            Task* currTask = raw->loadTask(oldIndexVal.currentIndex(record.op));
            Task* prevTask = raw->loadTask(prevIndexVal.currentIndex(record.op));

            return currTask == nullptr &&
                (oldIndexVal.isEmpty() || prevTask != nullptr);
        }

        [[nodiscard]]
        bool tryCommitTask(RecordType& record, size_t idx)
        {
            record.output = raw->loadTask(idx);
            if ((record.output == nullptr) == (record.input == nullptr))
                return false;

            return raw->casTask(idx, record.output, record.input);
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

        void rollbackCommits(const RecordType& record, size_t idx)
        {
            raw->storeTask(idx, record.output);

            IndexType* newIndex =
                static_cast<IndexType*>(ThreadLocalStorage::index);
            if (newIndex == nullptr)
                std::abort();

            newIndex->~IndexType();
        }

        void updateTLS(IndexType*& oldIndex)
            const
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
        using RecordType = typename RawType::RecordType;

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

        void doLFOp(RecordType& record)
        {
            if (record.state != EventDeqRecordState::PENDING)
                return;

            IndexType* oldIndex = nullptr;
            IndexType oldIndexVal;
            IndexType newIndexVal;
            if (!base->isValidIndex(record, oldIndex, oldIndexVal, newIndexVal))
            {
                record.state = EventDeqRecordState::RESTART;

                return;
            }

            if (!base->isNotProgressed(record, oldIndexVal))
            {
                record.state = EventDeqRecordState::RESTART;

                return;
            }

            size_t idx = oldIndexVal.targetIndex(record.op);
            if (!base->tryCommitTask(record, idx))
            {
                record.state = EventDeqRecordState::RESTART;

                return;
            }

            if (!base->tryCommitIndex(oldIndex, newIndexVal))
            {
                base->rollbackCommits(record, idx);

                record.state = EventDeqRecordState::RESTART;

                return;
            }

            base->updateTLS(oldIndex);

            record.state = EventDeqRecordState::COMPLETED;
        }
    };

    template<class RecordBoxType, size_t MAX_SIZE>
    struct HelpQueue
    {
        using RecordType = typename RecordBoxType::RecordType;

        [[nodiscard]]
        bool enqueue(RecordType* const record)
        {

        }

        [[nodiscard]]
        RecordBoxType* peek()
        {

        }

        void dequeue(RecordBoxType* const obx)
        {

        }
    };

    template<class Task, size_t MAX_SIZE>
    struct EventDeqWF
    {
        using RawType = EventDeqRaw<Task, MAX_SIZE>;
        using BaseType = EventDeqBase<Task, MAX_SIZE>;
        using IndexType = typename RawType::IndexType;
        using RecordType = typename RawType::RecordType;
        using RecordBoxType = typename RawType::RecordBoxType;

    private:
        BaseType* base;

    public:
        EventDeqWF(BaseType* base_) :
            base(base_)
        {
            if (base_ == nullptr)
                std::abort();
        }

        ~EventDeqWF() = default;

        EventDeqWF(const EventDeqWF&) = delete;
        EventDeqWF(EventDeqWF&&) = delete;
        EventDeqWF& operator= (const EventDeqWF&) = delete;
        EventDeqWF& operator= (EventDeqWF&&) = delete;

        void doWFOp(RecordType& record)
        {

        }

        void help(bool beingHelped, RecordBoxType* box)
        {

        }

        void doHelpOp(RecordBoxType* box)
        {

        }

        void preCASes(RecordBoxType* box, RecordType* record)
        {

        }

        void executeCASes(RecordType* record)
        {

        }

        void postCASes(RecordBoxType* box, RecordType* record)
        {

        }
    };
}
