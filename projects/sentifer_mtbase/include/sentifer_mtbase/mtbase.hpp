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
            virtual void* do_allocate(std::size_t bytes, std::size_t alignment) override
            {
                return mi_heap_zalloc_aligned(heap, bytes, alignment);
            }

            virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override
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
        struct alignas(alignof(void*)) task_t
        {

        };

        struct tagged_task_t
        {
            enum MODIFY_STATE :
                std::size_t
            {
                MS_NONE = 0
            };

            union
            {
                const task_t* ptr;
                const size_t taggedValue;
            };

            const MODIFY_STATE getModifyState() const noexcept
            {
                return static_cast<MODIFY_STATE>((taggedValue & MASK_MODIFY_STATE));
            }

            tagged_task_t changeModifyState(MODIFY_STATE modifyState) const noexcept
            {
                return tagged_task_t{ .taggedValue =
                    ((taggedValue & MASK_EXCEPT_MODIFY_STATE) |
                    (static_cast<size_t>(modifyState))) };
            }

            const task_t* getTask() const noexcept
            {
                return changeModifyState(MS_NONE).ptr;
            }

        private:
            static constexpr std::size_t MASK_MODIFY_STATE = 0x0000'0000'0000'0007ULL;
            static constexpr std::size_t MASK_EXCEPT_MODIFY_STATE = 0xFFFF'FFFF'FFFF'FFF8ULL;
        };

        template<std::uint32_t Size>
        struct task_scheduler final
        {
            task_scheduler(mi_memory_resource& res) :
                resource{ res }
            {}

            ~task_scheduler()
            {
                for (auto& ptr : tasks)
                    if (ptr != nullptr)
                        resource.deallocate(ptr, sizeof(task_t), alignof(task_t));
            }

            bool pushFront(task_t* task)
            {
                for (int i = 0; i < MAX_RETRY_FAST; ++i)
                    if (pushFrontFast(task))
                        return true;

                return pushFrontFast(task);
            }

            bool pushBack(task_t* task)
            {
                for (int i = 0; i < MAX_RETRY_FAST; ++i)
                    if (pushBackFast(task))
                        return true;

                return pushBackFast(task);
            }

            task_t* popFront()
            {
                for (int i = 0; i < MAX_RETRY_FAST; ++i)
                {
                    task_t* result = popFrontFast();
                    if (result != nullptr)
                        return result;
                }

                return popFrontFast();
            }

            task_t* popBack()
            {
                for (int i = 0; i < MAX_RETRY_FAST; ++i)
                {
                    task_t* result = popBackFast();
                    if (result != nullptr)
                        return result;
                }

                return popBackFast();
            }

        private:
            enum MODIFY_STATE :
                std::size_t
            {
                MS_NONE = 0
            };

            bool pushFrontFast(task_t* task)
            {
                std::uint64_t oldIndex = index.load();
                std::uint64_t oldFront = (oldIndex & MASK_FRONT) >> SHIFT_FRONT;
                task_t* oldFrontPtr = tasks[oldFront].load();
            }

            bool pushBackFast(task_t* task);
            task_t* popFrontFast();
            task_t* popBackFast();

            bool pushFrontSlow(task_t* task);
            bool pushBackSlow(task_t* task);
            task_t* popFrontSlow();
            task_t* popBackSlow();

        private:
            static constexpr std::uint64_t MASK_FRONT = 0xFFFF'FFFF'0000'0000ULL;
            static constexpr std::uint64_t MASK_BACK = 0x0000'0000'FFFF'FFFFULL;
            static constexpr std::uint64_t MASK_INDEX = 0x0000'0000'FFFF'FFFFULL;
            static constexpr std::uint64_t MASK_MODIFY_STATE = 0x0000'0000'0000'0007ULL;
            static constexpr std::uint64_t MASK_EXCEPT_MODIFY_STATE = 0xFFFF'FFFF'FFFF'FFF8ULL;
            static constexpr std::uint64_t SHIFT_FRONT = 32;
            static constexpr std::uint64_t SHIFT_BACK = 0;
            static constexpr int MAX_RETRY_FAST = 3;
            std::atomic_uint64_t index;
            std::array<std::atomic<task_t*>, Size> tasks;
            mi_memory_resource& resource;
        };

        static mi_memory_resource res;
        static task_scheduler<(1 << 20)> tmp(res);
    }
}
