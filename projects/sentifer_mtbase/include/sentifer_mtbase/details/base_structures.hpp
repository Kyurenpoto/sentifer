#pragma once

#include <atomic>
#include <array>
#include <compare>

#include "memory_managers.hpp"

namespace mtbase
{
    struct task_t;

    constexpr size_t BASE_ALIGN = alignof(void*);

    static_assert(BASE_ALIGN == 8, "mtbase is only available on x64");

    struct task_storage
    {
    protected:
        enum class OP :
            size_t
        {
            NONE,
            PUSH_FRONT,
            PUSH_BACK,
            POP_FRONT,
            POP_BACK
        };

        struct index_t
        {
            auto operator<=> (const index_t&) const = default;

        public:
            size_t front = 0;
            size_t back = 1;
        };

        struct descriptor
        {
            enum class PHASE :
                size_t
            {
                RESERVE,
                COMPLETE,
                FAIL
            };

        public:
            [[nodiscard]]
            descriptor rollbacked(
                index_t* const oldIndexLoad,
                index_t* const newIndexLoad)
                const noexcept;
            [[nodiscard]]
            descriptor completed(index_t* const oldIndexLoad)
                const noexcept;
            [[nodiscard]]
            descriptor failed(index_t* const oldIndexLoad)
                const noexcept;

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
        task_storage(std::pmr::memory_resource* res) :
            alloc{ res }
        {
            index_t* init = new_index(index_t{});
            index.store(init, std::memory_order_relaxed);
        }

        virtual ~task_storage()
        {
            delete_index(index.load(std::memory_order_relaxed));

            if (registered != nullptr)
                delete_desc(registered.load(std::memory_order_relaxed));
        }

    public:
        [[nodiscard]]
        bool push_front(task_t* task);
        [[nodiscard]]
        bool push_back(task_t* task);
        [[nodiscard]]
        task_t* pop_front();
        [[nodiscard]]
        task_t* pop_back();

    protected:
        [[nodiscard]]
        descriptor* createDesc(task_t* task, OP op);
        void destroyDesc(descriptor* const desc);
        [[nodiscard]]
        index_t* new_index(index_t&& idx);
        void delete_index(index_t* const idx);
        [[nodiscard]]
        descriptor* new_desc(descriptor&& desc);
        void delete_desc(descriptor* const desc);

        [[nodiscard]]
        virtual index_t moveIndex(index_t* const origin, OP op)
            const noexcept = 0;
        [[nodiscard]]
        virtual size_t getTargetIndex(index_t* const idx, OP op)
            const noexcept = 0;
        [[nodiscard]]
        virtual bool isValidIndex(index_t* const idx, OP op)
            const noexcept = 0;
        [[nodiscard]]
        virtual std::atomic<task_t*>& getTask(size_t idx) = 0;

    private:
        void applyDesc(descriptor*& desc);
        bool fast_path(descriptor*& desc);
        void slow_path(descriptor*& desc);
        void help_registered(descriptor*& desc);
        void help_registered_progress(descriptor*& desc);
        void help_registered_complete(descriptor*& desc);
        [[nodiscard]]
        descriptor* rollbackTask(descriptor*& desc);
        void completeDesc(descriptor*& desc);
        void renewRegistered(
            descriptor* const oldDesc,
            descriptor*& desc);

        [[nodiscard]]
        bool trySetProgress(descriptor* const desc)
            noexcept;
        void releaseProgress(descriptor* const desc)
            noexcept;
        [[nodiscard]]
        std::atomic_bool& getTargetProgress(const OP op)
            noexcept;
        [[nodiscard]]
        bool tryCommit(descriptor*& desc)
            noexcept;
        [[nodiscard]]
        bool tryCommitWithRegistered(descriptor*& desc)
            noexcept;
        [[nodiscard]]
        bool tryCommitTask(descriptor* const desc)
            noexcept;
        [[nodiscard]]
        bool tryCommitIndex(descriptor* const desc)
            noexcept;
        [[nodiscard]]
        bool tryRegister(
            descriptor*& expected,
            descriptor* const desired)
            noexcept;

    private:
        static constexpr size_t MAX_RETRY = 4;

        std::atomic<index_t*> index;
        std::atomic<descriptor*> registered{ nullptr };
        std::atomic_bool progressFront{ false };
        std::atomic_bool progressBack{ false };
        generic_allocator alloc;
    };

    template<size_t SIZE>
    struct task_wait_free_deque final :
        public task_storage
    {
        static_assert(SIZE >= BASE_ALIGN * 8);
        static_assert(SIZE <= 0xFFFF'FFFD);

    public:
        task_wait_free_deque(std::pmr::memory_resource* res) :
            task_storage{ res }
        {}

        ~task_wait_free_deque()
        {}

    protected:
        [[nodiscard]]
        index_t moveIndex(index_t* const origin, OP op)
            const noexcept override
        {
            switch (op)
            {
            case OP::PUSH_FRONT:
                return index_t
                {
                    .front = (origin->front + REAL_SIZE - 1) % REAL_SIZE,
                    .back = origin->back
                };
            case OP::PUSH_BACK:
                return index_t
                {
                    .front = origin->front,
                    .back = (origin->back + 1) % REAL_SIZE
                };
            case OP::POP_FRONT:
                return index_t
                {
                    .front = (origin->front + 1) % REAL_SIZE,
                    .back = origin->back
                };
            case OP::POP_BACK:
                return index_t
                {
                    .front = origin->front,
                    .back = (origin->back + REAL_SIZE - 1) % REAL_SIZE
                };
            default:
                return index_t{};
            }
        }

        [[nodiscard]]
        size_t getTargetIndex(index_t* const idx, OP op)
            const noexcept override
        {
            switch (op)
            {
            case OP::PUSH_FRONT:
                return idx->front;
            case OP::PUSH_BACK:
                return idx->back;
            case OP::POP_FRONT:
                return (idx->front + 1) % REAL_SIZE;
            case OP::POP_BACK:
                return (idx->back + REAL_SIZE - 1) % REAL_SIZE;
            default:
                return 0;
            }
        }

        [[nodiscard]]
        bool isValidIndex(index_t* const idx, OP op)
            const noexcept override
        {
            switch (op)
            {
            case OP::PUSH_FRONT:
            case OP::POP_BACK:
                return (idx->back + REAL_SIZE - idx->front) % REAL_SIZE >= 2;
            case OP::PUSH_BACK:
            case OP::POP_FRONT:
                return (idx->front + REAL_SIZE - idx->back) % REAL_SIZE >= 2;
            default:
                return false;
            }
        }

        [[nodiscard]]
        std::atomic<task_t*>& getTask(size_t idx)
            override
        {
            return tasks[idx];
        }

    private:
        static constexpr size_t REAL_SIZE = SIZE + 2;

        std::array<std::atomic<task_t*>, REAL_SIZE> tasks;
    };
}
