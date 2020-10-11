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
        std::atomic<Task*>* target = nullptr;
        Task* oldTask = nullptr;
        Task* newTask = nullptr;
        EventDeqCASState state = EventDeqCASState::PENDING;
    };

    template<size_t REAL_SIZE>
    struct EventDeqIndexDesc
    {
        using IndexType = EventDeqIndex<REAL_SIZE>;

        std::atomic<IndexType*>* target = nullptr;
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

        std::atomic<RecordType*> record = nullptr;

        bool casRecord(RecordType*& oldRecord, RecordType* const newRecord)
        {
            return record.compare_exchange_strong(
                oldRecord, newRecord, std::memory_order_acq_rel);
        }

        RecordType* loadRecord()
        {
            return record->load(std::memory_order_acquire);
        }

        void storeRecord(Record* const newRecord)
        {
            record.store(newRecord, std::memory_order_release);
        }
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
            tasks[idx].store(task, std::memory_order_release);
        }

        std::atomic<Task*>* targetTask(size_t idx)
            const noexcept
        {
            return &tasks[idx];
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
            index.store(idx, std::memory_order_release);
        }

        std::atomic<IndexType*>* targetIndex()
            const noexcept
        {
            return &index;
        }
    };

    struct ThreadLocalStorage
    {
        inline thread_local static void* index = nullptr;
        inline thread_local static void* record = nullptr;
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
    private:
        static constexpr size_t REAL_SIZE = MAX_SIZE + 1;

        std::array<std::atomic<RecordBoxType*>, REAL_SIZE> items;
        std::atomic<size_t> enqIdx = 0;
        std::atomic<size_t> deqIdx = 0;

    public:
        void enqueue(RecordBoxType* const box)
        {
            size_t oldEnqIdx = loadEnqIdx();
            size_t oldDeqIdx = loadDeqIdx();
            for (size_t i = oldEnqIdx;
                (i + 1) % REAL_SIZE != oldDeqIdx;
                i = (i + 1) % REAL_SIZE)
            {
                RecordBoxType* oldRecordBox = loadItem(i);
                if (oldRecordBox != nullptr)
                    continue;

                if (casItem(items[i], oldRecordBox, box))
                {
                    faaEnqIdx();

                    return;
                }
            }
        }

        [[nodiscard]]
        RecordBoxType* peek()
        {
            size_t oldEnqIdx = loadEnqIdx();
            size_t oldDeqIdx = loadDeqIdx();
            for (size_t i = oldDeqIdx; i != oldEnqIdx; i = (i + 1) % REAL_SIZE)
            {
                RecordBoxType* oldRecordBox = loadItem(i);
                if (oldRecordBox != nullptr)
                    return oldRecordBox;
            }

            return nullptr;
        }

        void dequeue(RecordBoxType* const box)
        {
            size_t oldDeqIdx = loadDeqIdx();
            RecordBoxType* oldRecordBox = loadItem(oldDeqIdx);
            if (oldRecordBox == nullptr || oldRecordBox != box)
                return;

            casItem(items[i], oldRecordBox, nullptr);
            faaDeqIdx();
        }

    private:
        [[nodiscard]]
        size_t loadEnqIdx()
        {
            return enqIdx.load(std::memory_order_acquire);
        }

        void faaEnqIdx()
        {
            while (true)
            {
                size_t oldEnqIdx = loadEnqIdx();
                size_t newEnqIdx = (oldEnqIdx + 1) % REAL_SIZE;
                if (enqIdx.compare_exchange_strong(
                    oldEnqIdx, newEnqIdx, std::memory_order_acq_rel))
                    return;
            }
        }

        [[nodiscard]]
        size_t loadDeqIdx()
        {
            return deqIdx.load(std::memory_order_acquire);
        }

        void faaDeqIdx()
        {
            while (true)
            {
                size_t oldDeqIdx = loadEnqIdx();
                size_t newDeqIdx = (oldDeqIdx + 1) % REAL_SIZE;
                if (enqIdx.compare_exchange_strong(
                    oldDeqIdx, newDeqIdx, std::memory_order_acq_rel))
                    return;
            }
        }

        [[nodiscard]]
        RecordBoxType* loadItem(size_t idx)
        {
            return items[idx].load(std::memory_order_acquire);
        }

        bool casItem(
            std::atomic<RecordBoxType*>& item,
            RecordBoxType*& oldItem,
            const RecordBoxType* newItem)
        {
            return item.compare_exchange_strong(
                oldItem, newItem, std::memory_order_acq_rel);
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
        using HelpQueueType = HelpQueue<Task, MAX_SIZE>;

    private:
        BaseType* base;
        HelpQueueType helper;

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

        void help(bool beingHelped, std::atomic<RecordType*>& box)
        {

        }

        void doHelpOp(std::atomic<RecordType*>& box)
        {

        }

        void preCASes(std::atomic<RecordType*>& box, RecordType* record)
        {

        }

        void executeCASes(RecordType* record)
        {

        }

        void postCASes(std::atomic<RecordType*>& box, RecordType* record)
        {

        }
    };
}
