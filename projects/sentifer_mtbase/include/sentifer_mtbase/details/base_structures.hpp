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

        struct alignas(BASE_ALIGN * 2) index_t
        {
            auto operator<=> (const index_t&) const = default;

            const size_t front = 0;
            const size_t back = 1;
        };

        struct alignas(BASE_ALIGN * 8) descriptor
        {
            enum class PHASE :
                size_t
            {
                RESERVE,
                COMPLETE,
                FAIL
            };

        public:
            auto operator<=> (const descriptor&) const = default;

        public:
            [[nodiscard]]
            descriptor copied(
                const index_t& oldIndexLoad,
                const index_t& newIndexLoad,
                task_t* const oldTaskLoad)
                const noexcept;
            [[nodiscard]]
            descriptor rollbacked(
                const index_t& oldIndexLoad,
                const index_t& newIndexLoad,
                task_t* const oldTaskLoad)
                const noexcept;
            [[nodiscard]]
            descriptor completed()
                const noexcept;
            [[nodiscard]]
            descriptor failed()
                const noexcept;

        public:
            const PHASE phase{ PHASE::RESERVE };
            const OP op{ OP::PUSH_FRONT };
            task_t* const oldTask{ nullptr };
            task_t* const newTask{ nullptr };
            const index_t oldIndex{ index_t{} };
            const index_t newIndex{ index_t{} };
        };

    public:
        task_storage(std::pmr::memory_resource* res) :
            alloc{ res }
        {
            index_t idx{ index_t{} };
            index_t* init = new_index(idx);
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
        bool push_front(task_t* const task);
        [[nodiscard]]
        bool push_back(task_t* const task);
        [[nodiscard]]
        task_t* pop_front();
        [[nodiscard]]
        task_t* pop_back();

    protected:
        [[nodiscard]]
        descriptor* createDesc(task_t* const task, OP op);
        void destroyDesc(descriptor* const desc);
        [[nodiscard]]
        index_t* new_index(const index_t& idx);
        void delete_index(index_t* const idx);
        [[nodiscard]]
        descriptor* new_desc(const descriptor& desc);
        [[nodiscard]]
        descriptor* copy_desc(descriptor* const desc);
        void delete_desc(descriptor* const desc);

        [[nodiscard]]
        virtual std::atomic<task_t*>& getElementRef(
            const index_t& idx,
            OP op) = 0;

        [[nodiscard]]
        virtual index_t moveIndex(const index_t& origin, OP op)
            const noexcept = 0;
        [[nodiscard]]
        virtual bool isValidIndex(const index_t& idx, OP op)
            const noexcept = 0;

    private:
        void applyDesc(descriptor*& desc);
        bool fast_path(descriptor*& desc);
        void slow_path(descriptor*& desc);
        void helpRegistered(descriptor*& desc);
        [[nodiscard]]
        bool tryCommit(descriptor*& desc);
        [[nodiscard]]
        bool tryCommitWithRegistered(descriptor*& desc);
        [[nodiscard]]
        descriptor* rollbackTask(descriptor*& desc);
        [[nodiscard]]
        descriptor* refreshIndex(descriptor*& desc);
        void completeDesc(descriptor*& desc);
        void renewRegistered(
            descriptor* const oldDesc,
            descriptor*& desc);

        [[nodiscard]]
        bool trySetProgress(const OP op)
            noexcept;
        void releaseProgress(const OP op)
            noexcept;
        [[nodiscard]]
        std::atomic_bool& getTargetProgress(const OP op)
            noexcept;
        [[nodiscard]]
        bool tryCommitTask(descriptor* const desc)
            noexcept;
        [[nodiscard]]
        bool tryCommitIndex(descriptor* const desc)
            noexcept;
        bool tryRegister(
            descriptor*& expected,
            descriptor* const desired)
            noexcept;

    private:
        static constexpr size_t MAX_RETRY = 4;

        std::atomic_size_t cnt = 0;
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
        {
            for (auto& x : tasks)
                x.store(nullptr, std::memory_order_relaxed);
        }

        ~task_wait_free_deque()
        {}

    protected:
        [[nodiscard]]
        std::atomic<task_t*>& getElementRef(const index_t& idx, OP op)
            override
        {
            switch (op)
            {
            case OP::PUSH_FRONT:
                return tasks[idx.front];
            case OP::PUSH_BACK:
                return tasks[idx.back];
            case OP::POP_FRONT:
                return tasks[(idx.front + 1) % REAL_SIZE];
            case OP::POP_BACK:
                return tasks[(idx.back + REAL_SIZE - 1) % REAL_SIZE];
            default:
                return tasks[0];
            }
        }

        [[nodiscard]]
        index_t moveIndex(const index_t& origin, OP op)
            const noexcept override
        {
            switch (op)
            {
            case OP::PUSH_FRONT:
                return index_t
                {
                    .front = (origin.front + REAL_SIZE - 1) % REAL_SIZE,
                    .back = origin.back
                };
            case OP::PUSH_BACK:
                return index_t
                {
                    .front = origin.front,
                    .back = (origin.back + 1) % REAL_SIZE
                };
            case OP::POP_FRONT:
                return index_t
                {
                    .front = (origin.front + 1) % REAL_SIZE,
                    .back = origin.back
                };
            case OP::POP_BACK:
                return index_t
                {
                    .front = origin.front,
                    .back = (origin.back + REAL_SIZE - 1) % REAL_SIZE
                };
            default:
                return index_t{};
            }
        }

        [[nodiscard]]
        bool isValidIndex(const index_t& idx, OP op)
            const noexcept override
        {
            if (idx.front >= REAL_SIZE || idx.back >= REAL_SIZE)
                return false;

            switch (op)
            {
            case OP::PUSH_FRONT:
            case OP::PUSH_BACK:
                return (idx.front + REAL_SIZE - idx.back) % REAL_SIZE != 1;
            case OP::POP_FRONT:
            case OP::POP_BACK:
                return (idx.back + REAL_SIZE - idx.front) % REAL_SIZE != 1;
            default:
                return false;
            }
        }

    private:
        static constexpr size_t REAL_SIZE = SIZE + 2;

        std::array<std::atomic<task_t*>, REAL_SIZE> tasks;
    };
}
