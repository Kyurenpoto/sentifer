#pragma once

#include <memory_resource>
#include <chrono>
#include <atomic>
#include <array>

#include "mimalloc.h"

namespace mtbase
{
    inline namespace memory_managers
    {
        struct mi_memory_resource :
            public std::pmr::memory_resource
        {
            mi_memory_resource() :
                heap{ mi_heap_new() }
            {}

            virtual ~mi_memory_resource()
            {
                mi_heap_destroy(heap);
            }

        private:
            virtual void* do_allocate(size_t bytes, size_t alignment) override
            {
                return mi_heap_zalloc_aligned(heap, bytes, alignment);
            }

            virtual void do_deallocate(void* p, size_t bytes, size_t alignment) override
            {
                return mi_free(p);
            }

            virtual bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
            {
                return this == &other;
            }

            mi_heap_t* heap;
        };

        struct thread_local_heap :
            public mi_memory_resource
        {

        };

        struct object_heap :
            public mi_memory_resource
        {

        };

        struct manage_heap
        {

        };
    }

    inline namespace clocks
    {
        template<class Clock>
        struct clock_base
        {
            clock_base() noexcept
            {
                startSinceEpoch = baseSinceEpoch = Clock::now().time_since_epoch();
            }

            virtual ~clock_base() noexcept
            {}

            std::chrono::nanoseconds nowFromEpoch() noexcept
            {
                return Clock::now().time_since_epoch();
            }

            std::chrono::nanoseconds nowFromStart() noexcept
            {
                return nowFromEpoch() - startSinceEpoch;
            }

            std::chrono::nanoseconds nowFromBase() noexcept
            {
                return nowFromEpoch() - baseSinceEpoch;
            }

            void setBase(std::chrono::nanoseconds base) noexcept
            {
                baseSinceEpoch = base;
            }

            void resetBase() noexcept
            {
                baseSinceEpoch = startSinceEpoch;
            }

        private:
            std::chrono::nanoseconds startSinceEpoch;
            std::chrono::nanoseconds baseSinceEpoch;
        };

        inline namespace instanced
        {
            using thread_local_clock = clock_base<std::chrono::steady_clock>;
            using global_clock = clock_base<std::chrono::system_clock>;

            static thread_local thread_local_clock LocalClock;
            static global_clock GlobalClock;
        }
    }

    inline namespace schedulers
    {
        constexpr size_t BASE_ALIGN = alignof(void*);

        struct alignas(BASE_ALIGN) task_t
        {

        };

        struct task_scheduler_base
        {
        protected:
            enum MODIFY_STATE :
                size_t
            {
                MS_NONE = 0,
                MS_RESERVE = 1
            };

            template<class Ptr>
            struct tagged_ptr
            {
                tagged_ptr(Ptr* oldPtr) :
                    ptr{ oldPtr }
                {}

                tagged_ptr(size_t tagged = 0) :
                    taggedValue{ tagged }
                {}

                MODIFY_STATE getModifyState() const noexcept
                {
                    return static_cast<MODIFY_STATE>((taggedValue & MASK_MODIFY_STATE));
                }

                Ptr* getPtr() const noexcept
                {
                    return changeModifyState(MS_NONE).ptr;
                }

                size_t getTaggedValue() const noexcept
                {
                    return taggedValue;
                }

                tagged_ptr changeModifyState(MODIFY_STATE modifyState) const noexcept
                {
                    return tagged_ptr
                    {
                        ((taggedValue & MASK_EXCEPT_MODIFY_STATE) |
                        (static_cast<size_t>(modifyState)))
                    };
                }

                tagged_ptr changePtr(Ptr* newPtr) const noexcept
                {
                    return tagged_ptr{ newPtr }.changeModifyState(getModifyState());
                }

            private:
                union
                {
                    Ptr* ptr;
                    size_t taggedValue;
                };

                static constexpr size_t MASK_MODIFY_STATE = 0x0000'0000'0000'0007ULL;
                static constexpr size_t MASK_EXCEPT_MODIFY_STATE = 0xFFFF'FFFF'FFFF'FFF8ULL;
            };

            using tagged_task_t = tagged_ptr<task_t>;

            struct alignas(BASE_ALIGN) slow_op_desc
            {
                enum OP_PHASE :
                    size_t
                {
                    OP_BEGIN,
                    OP_RESERVE,
                    OP_COMMIT,
                    OP_MOVE,
                    OP_END
                };

                enum OP_KIND :
                    size_t
                {
                    OK_PUSH_FRONT,
                    OK_PUSH_BACK,
                    OK_POP_FRONT,
                    OK_POP_BACK
                };

                const OP_PHASE phase;
                const OP_KIND op;
                const uint64_t oldFront;
                const uint64_t oldBack;
                const uint64_t newFront;
                const uint64_t newBack;
                const tagged_task_t oldTagged;
                const task_t* task;
            };

            using tagged_slow_op_desc = tagged_ptr<slow_op_desc>;
        };

        template<std::uint32_t SIZE>
        struct alignas(BASE_ALIGN) task_scheduler final :
            public task_scheduler_base
        {
            task_scheduler(mi_memory_resource& res) :
                resource{ res }
            {}

            ~task_scheduler()
            {
                for (auto& taggedTask : taggedTasks)
                {
                    task_t* task = tagged_task_t{ taggedTask.load() }.getPtr();

                    if (task != nullptr)
                        resource.deallocate(task, sizeof(task_t), alignof(task_t));
                }
            }

            #pragma region WAIT_FREE_DEQUE_API
            bool pushFront(task_t* task)
            {
                for (int i = 0; i < MAX_RETRY_FAST; ++i)
                    if (pushFrontFast(task))
                        return true;

                return pushFrontSlow(task);
            }

            bool pushBack(task_t* task)
            {
                for (int i = 0; i < MAX_RETRY_FAST; ++i)
                    if (pushBackFast(task))
                        return true;

                return pushBackSlow(task);
            }

            task_t* popFront()
            {
                for (int i = 0; i < MAX_RETRY_FAST; ++i)
                {
                    task_t* result = popFrontFast();
                    if (result != nullptr)
                        return result;
                }

                return popFrontSlow();
            }

            task_t* popBack()
            {
                for (int i = 0; i < MAX_RETRY_FAST; ++i)
                {
                    task_t* result = popBackFast();
                    if (result != nullptr)
                        return result;
                }

                return popBackSlow();
            }
            // WAIT_FREE_DEQUE_API
            #pragma endregion

        private:
            #pragma region WAIT_FREE_FAST_PATH
            bool pushFrontFast(task_t* task)
            {
                uint64_t oldFront, oldBack;
                getOldIndex(oldFront, oldBack);

                if (isFull(oldFront, oldBack))
                    return false;

                const tagged_task_t oldFrontTagged{ taggedTasks[oldFront].load() };

                if (oldFrontTagged.getModifyState() != MS_NONE ||
                    oldFrontTagged.getPtr() != nullptr)
                    return false;

                return exchangeFast(task, oldFront, oldBack, (oldFront + SIZE - 1) % SIZE, oldBack,
                    taggedTasks[oldFront], oldFrontTagged);
            }

            bool pushBackFast(task_t* task)
            {
                uint64_t oldFront, oldBack;
                getOldIndex(oldFront, oldBack);

                if (isFull(oldFront, oldBack))
                    return false;

                const tagged_task_t oldBackTagged{ taggedTasks[oldBack].load() };

                if (oldBackTagged.getModifyState() != MS_NONE ||
                    oldBackTagged.getPtr() != nullptr)
                    return false;

                return exchangeFast(task, oldFront, oldBack, oldFront, (oldBack + 1) % SIZE,
                    taggedTasks[oldBack], oldBackTagged);
            }

            task_t* popFrontFast()
            {
                uint64_t oldFront, oldBack;
                getOldIndex(oldFront, oldBack);

                if (isEmpty(oldFront, oldBack))
                    return false;

                const tagged_task_t oldFrontTagged{ taggedTasks[oldFront].load() };

                if (oldFrontTagged.getModifyState() != MS_NONE ||
                    oldFrontTagged.getPtr() == nullptr)
                    return false;

                return exchangeFast(nullptr, oldFront, oldBack, (oldFront + 1) % SIZE, oldBack,
                    taggedTasks[oldFront], oldFrontTagged) ?
                    oldFrontTagged.getPtr() : nullptr;
            }

            task_t* popBackFast()
            {
                uint64_t oldFront, oldBack;
                getOldIndex(oldFront, oldBack);

                if (isEmpty(oldFront, oldBack))
                    return false;

                const tagged_task_t oldBackTagged{ taggedTasks[oldBack].load() };

                if (oldBackTagged.getModifyState() != MS_NONE ||
                    oldBackTagged.getPtr() == nullptr)
                    return false;

                return exchangeFast(nullptr, oldFront, oldBack, oldFront, (oldBack + SIZE - 1) % SIZE,
                    taggedTasks[oldBack], oldBackTagged) ?
                    oldBackTagged.getPtr() : nullptr;
            }

            bool exchangeFast(
                task_t* task,
                const uint64_t oldFront,
                const uint64_t oldBack,
                const uint64_t newFront,
                const uint64_t newBack,
                std::atomic_size_t& taggedTask,
                const tagged_task_t oldTagged)
            {
                if (!isValidIndex(newFront, newBack))
                    return false;

                if (!commitTargetState(
                    taggedTask,
                    oldTagged,
                    MS_RESERVE))
                    return false;

                const tagged_task_t newTaggedReserve
                {
                    oldTagged
                    .changeModifyState(MS_RESERVE)
                    .getTaggedValue()
                };

                if (!commitTask(
                    taggedTask,
                    newTaggedReserve,
                    task))
                {
                    while (!commitTargetState(
                        taggedTask,
                        newTaggedReserve,
                        MS_NONE));

                    return false;
                }

                const tagged_task_t newTaggedCommit
                {
                    newTaggedReserve
                    .changePtr(task)
                    .getTaggedValue()
                };

                if (!setNewIndex(oldFront, oldBack, newFront, newBack))
                {
                    task_t* oldTask = oldTagged.getPtr();

                    while (!commit(
                        taggedTask,
                        newTaggedCommit,
                        oldTask, MS_NONE));

                    return false;
                }

                while (!commitTargetState(
                    taggedTask,
                    newTaggedCommit,
                    MS_NONE));

                return true;
            }
            // WAIT_FREE_FAST_PATH
            #pragma endregion

            #pragma region WAIT_FREE_SLOW_PATH
            bool pushFrontSlow(task_t* task)
            {

            }

            bool pushBackSlow(task_t* task);
            
            task_t* popFrontSlow();
            
            task_t* popBackSlow();
            // WAIT_FREE_SLOW_PATH
            #pragma endregion

            #pragma region WAIT_FREE_UTILS
            bool isFull(const uint64_t oldFront, const uint64_t oldBack) const noexcept
            {
                return (oldFront + SIZE - oldBack) % SIZE == 1;
            }

            bool isEmpty(const uint64_t oldFront, const uint64_t oldBack) const noexcept
            {
                return (oldBack + SIZE - oldFront) % SIZE == 1;
            }

            bool isValidIndex(const uint64_t newFront, const uint64_t newBack) const noexcept
            {
                return newFront != newBack;
            }

            void getOldIndex(uint64_t& oldFront, uint64_t& oldBack) const noexcept
            {
                uint64_t oldIndex = index.load();
                
                oldFront = (oldIndex & MASK_FRONT) >> SHIFT_FRONT;
                oldBack = (oldIndex & MASK_BACK) >> SHIFT_BACK;
            }

            bool setNewIndex(const uint64_t oldFront, const uint64_t oldBack, const uint64_t newFront, const uint64_t newBack)
            {
                uint64_t oldIndex = (oldFront << SHIFT_FRONT) & (oldBack << SHIFT_BACK);
                uint64_t newIndex = (newFront << SHIFT_FRONT) & (newBack << SHIFT_BACK);

                return index.compare_exchange_strong(oldIndex, newIndex);
            }

            bool commitTargetState(std::atomic_size_t& taggedTask, const tagged_task_t& oldTagged, MODIFY_STATE targetState) noexcept
            {
                size_t oldTaggedValue = oldTagged.getTaggedValue();
                size_t newTaggedValue =
                    oldTagged
                    .changeModifyState(targetState)
                    .getTaggedValue();

                if (!taggedTask
                    .compare_exchange_strong(oldTaggedValue, newTaggedValue))
                    return false;
            }

            bool commitTask(std::atomic_size_t& taggedTask, const tagged_task_t& oldTagged, task_t* task) noexcept
            {
                size_t oldTaggedValue = oldTagged.getTaggedValue();
                size_t newTaggedValue =
                    oldTagged
                    .changePtr(task)
                    .getTaggedValue();

                if (!taggedTask
                    .compare_exchange_strong(oldTaggedValue, newTaggedValue))
                    return false;
            }

            bool commit(std::atomic_size_t& taggedTask, const tagged_task_t& oldTagged, task_t* task, MODIFY_STATE targetState) noexcept
            {
                size_t oldTaggedValue = oldTagged.getTaggedValue();
                size_t newTaggedValue =
                    oldTagged
                    .changePtr(task)
                    .changeModifyState(targetState)
                    .getTaggedValue();

                if (!taggedTask
                    .compare_exchange_strong(oldTaggedValue, newTaggedValue))
                    return false;
            }
            // WAIT_FREE_UTILS
            #pragma endregion

            #pragma region HYALINE_SIMPLE_BASIC
            // HYALINE_SIMPLE_BASIC
            #pragma endregion

        private:
            static constexpr uint64_t MASK_FRONT = 0xFFFF'FFFF'0000'0000ULL;
            static constexpr uint64_t MASK_BACK = 0x0000'0000'FFFF'FFFFULL;
            static constexpr uint64_t SHIFT_FRONT = 32;
            static constexpr uint64_t SHIFT_BACK = 0;
            static constexpr int MAX_RETRY_FAST = 4;
            std::atomic_uint64_t index;
            mi_memory_resource& resource;
            std::array<std::atomic_size_t, SIZE> taggedTasks;
        };

        static mi_memory_resource res;
        static task_scheduler<(1 << 20)> tmp(res);
    }
}
