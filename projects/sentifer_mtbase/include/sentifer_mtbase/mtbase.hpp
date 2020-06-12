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
            tagged_task_t(task_t* task) :
                ptr{ task }
            {}

            tagged_task_t(size_t tagged = 0) :
                taggedValue{ tagged }
            {}

            enum MODIFY_STATE :
                std::size_t
            {
                MS_NONE = 0,
                MS_RESERVE = 1,
            };

            MODIFY_STATE getModifyState() const noexcept
            {
                return static_cast<MODIFY_STATE>((taggedValue & MASK_MODIFY_STATE));
            }

            task_t* getTask() const noexcept
            {
                return changeModifyState(MS_NONE).ptr;
            }

            size_t getTaggedValue() const noexcept
            {
                return taggedValue;
            }

            tagged_task_t changeModifyState(MODIFY_STATE modifyState) const noexcept
            {
                return tagged_task_t
                {
                    ((taggedValue & MASK_EXCEPT_MODIFY_STATE) |
                    (static_cast<size_t>(modifyState)))
                };
            }

            tagged_task_t changeTask(task_t* task) const noexcept
            {
                return tagged_task_t{ task }.changeModifyState(getModifyState());
            }

        private:
            union
            {
                task_t* ptr;
                size_t taggedValue;
            };

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
                for (auto& taggedTask : taggedTasks)
                {
                    task_t* task = tagged_task_t{ taggedTask.load() }.getTask();

                    if (task != nullptr)
                        resource.deallocate(task, sizeof(task_t), alignof(task_t));
                }
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
            bool pushFrontFast(task_t* task)
            {
                std::uint64_t oldFront, oldBack;
                getOldIndex(oldFront, oldBack);

                if (isFull(oldFront, oldBack))
                    return false;

                const tagged_task_t oldFrontTagged{ taggedTasks[oldFront].load() };

                if (oldFrontTagged.getModifyState() != tagged_task_t::MS_NONE)
                    return false;

                return exchangeFast(task, oldFront, oldBack, (oldFront + Size - 1) % Size, oldBack,
                    taggedTasks[oldFront], oldFrontTagged);
            }

            bool pushBackFast(task_t* task)
            {
                std::uint64_t oldFront, oldBack;
                getOldIndex(oldFront, oldBack);

                if (isFull(oldBack, oldFront))
                    return false;

                const tagged_task_t oldBackTagged{ taggedTasks[oldBack].load() };

                if (oldBackTagged.getModifyState() != tagged_task_t::MS_NONE)
                    return false;

                return exchangeFast(task, oldFront, oldBack, oldFront, (oldBack + 1) % Size,
                    taggedTasks[oldBack], oldBackTagged);
            }

            task_t* popFrontFast()
            {
                std::uint64_t oldFront, oldBack;
                getOldIndex(oldFront, oldBack);

                if (isEmpty(oldFront, oldBack))
                    return false;

                const tagged_task_t oldFrontTagged{ taggedTasks[oldFront].load() };

                if (oldFrontTagged.getModifyState() != tagged_task_t::MS_NONE)
                    return false;

                return exchangeFast(nullptr, oldFront, oldBack, (oldFront + 1) % Size, oldBack,
                    taggedTasks[oldFront], oldFrontTagged) ?
                    oldFrontTagged.getTask() : nullptr;
            }

            task_t* popBackFast()
            {
                std::uint64_t oldFront, oldBack;
                getOldIndex(oldFront, oldBack);

                if (isEmpty(oldFront, oldBack))
                    return false;

                const tagged_task_t oldBackTagged{ taggedTasks[oldBack].load() };

                if (oldBackTagged.getModifyState() != tagged_task_t::MS_NONE)
                    return false;

                return exchangeFast(nullptr, oldFront, oldBack, oldFront, (oldBack + Size - 1) % Size,
                    taggedTasks[oldBack], oldBackTagged) ?
                    oldBackTagged.getTask() : nullptr;
            }

            bool exchangeFast(
                task_t* task,
                const std::uint64_t oldFront,
                const std::uint64_t oldBack,
                const std::uint64_t newFront,
                const std::uint64_t newBack,
                std::atomic_size_t& taggedTask,
                const tagged_task_t oldTagged)
            {
                if (!commitTargetState(
                    taggedTask,
                    oldTagged,
                    tagged_task_t::MS_RESERVE))
                    return false;

                const tagged_task_t newTaggedReserve
                {
                    oldTagged
                    .changeModifyState(tagged_task_t::MS_RESERVE)
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
                        tagged_task_t::MS_NONE));

                    return false;
                }

                const tagged_task_t newTaggedCommit
                {
                    newTaggedReserve
                    .changeTask(task)
                    .getTaggedValue()
                };

                if (setNewIndex(oldFront, oldBack, newFront, newBack))
                {
                    task_t* oldTask = oldTagged.getTask();

                    while (!commit(
                        taggedTask,
                        newTaggedCommit,
                        oldTask, tagged_task_t::MS_NONE));

                    return false;
                }

                while (commitTargetState(
                    taggedTask,
                    newTaggedCommit,
                    tagged_task_t::MS_NONE));

                return true;
            }

            void getOldIndex(std::uint64_t& oldFront, std::uint64_t& oldBack) const noexcept
            {
                std::uint64_t oldIndex = index.load();
                
                oldFront = (oldIndex & MASK_FRONT) >> SHIFT_FRONT;
                oldBack = (oldIndex & MASK_BACK) >> SHIFT_BACK;
            }

            bool setNewIndex(const std::uint64_t oldFront, const std::uint64_t oldBack, const std::uint64_t newFront, const std::uint64_t newBack)
            {
                std::uint64_t oldIndex = (oldFront << SHIFT_FRONT) & (oldBack << SHIFT_BACK);
                std::uint64_t newIndex = (newFront << SHIFT_FRONT) & (newBack << SHIFT_BACK);

                return index.compare_exchange_strong(oldIndex, newIndex);
            }

            bool isFull(const std::uint64_t oldPushDir, const std::uint64_t oldPopDir) const noexcept
            {
                return (oldPopDir + Size - oldPushDir) % Size == 1;
            }

            bool isEmpty(const std::uint64_t oldPushDir, const std::uint64_t oldPopDir) const noexcept
            {
                return oldPushDir == oldPopDir;
            }

            bool commitTargetState(std::atomic_size_t& taggedTask, const tagged_task_t& oldTagged, tagged_task_t::MODIFY_STATE targetState) noexcept
            {
                std::size_t oldTaggedValue = oldTagged.getTaggedValue();
                std::size_t newTaggedValue =
                    oldTagged
                    .changeModifyState(targetState)
                    .getTaggedValue();

                if (!taggedTask
                    .compare_exchange_strong(oldTaggedValue, newTaggedValue))
                    return false;
            }

            bool commitTask(std::atomic_size_t& taggedTask, const tagged_task_t& oldTagged, task_t* task) noexcept
            {
                std::size_t oldTaggedValue = oldTagged.getTaggedValue();
                std::size_t newTaggedValue =
                    oldTagged
                    .changeTask(task)
                    .getTaggedValue();

                if (!taggedTask
                    .compare_exchange_strong(oldTaggedValue, newTaggedValue))
                    return false;
            }

            bool commit(std::atomic_size_t& taggedTask, const tagged_task_t& oldTagged, task_t* task, tagged_task_t::MODIFY_STATE targetState) noexcept
            {
                std::size_t oldTaggedValue = oldTagged.getTaggedValue();
                std::size_t newTaggedValue =
                    oldTagged
                    .changeTask(task)
                    .changeModifyState(targetState)
                    .getTaggedValue();

                if (!taggedTask
                    .compare_exchange_strong(oldTaggedValue, newTaggedValue))
                    return false;
            }

            bool pushFrontSlow(task_t* task);
            bool pushBackSlow(task_t* task);
            task_t* popFrontSlow();
            task_t* popBackSlow();

        private:
            static constexpr std::uint64_t MASK_FRONT = 0xFFFF'FFFF'0000'0000ULL;
            static constexpr std::uint64_t MASK_BACK = 0x0000'0000'FFFF'FFFFULL;
            static constexpr std::uint64_t MASK_INDEX = 0x0000'0000'FFFF'FFFFULL;
            static constexpr std::uint64_t SHIFT_FRONT = 32;
            static constexpr std::uint64_t SHIFT_BACK = 0;
            static constexpr int MAX_RETRY_FAST = 4;
            std::atomic_uint64_t index;
            std::array<std::atomic_size_t, Size> taggedTasks;
            mi_memory_resource& resource;
        };

        static mi_memory_resource res;
        static task_scheduler<(1 << 20)> tmp(res);
    }
}
