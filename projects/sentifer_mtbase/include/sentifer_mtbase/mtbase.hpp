#pragma once

#include <memory_resource>
#include <chrono>
#include <atomic>
#include <array>

#include "mimalloc.h"

namespace mtbase
{
#ifdef MTBASE_CUSTOM_MAX_THREAD
#define MTBASE_MAX_THREAD MTBASE_CUSTOM_MAX_THREAD
#else
#define MTBASE_MAX_THREAD 16
#endif

    constexpr size_t BASE_ALIGN = alignof(void*);

    static_assert(BASE_ALIGN == 8, "mtbase is only available on x64");

    namespace tls_variables
    {
        static int createThreadId()
        {
            static std::atomic_int threadIdCounter = -1;
            int oldThreadIdCounter, newThreadIdCounter;

            while (true)
            {
                oldThreadIdCounter = threadIdCounter.load();
                newThreadIdCounter = oldThreadIdCounter + 1;

                if (newThreadIdCounter >= MTBASE_MAX_THREAD)
                    return -1;

                if (threadIdCounter.compare_exchange_strong(oldThreadIdCounter, newThreadIdCounter))
                    break;
            }

            return newThreadIdCounter;
        }

        struct tls_variables
        {
            tls_variables() :
                threadId{ createThreadId() }
            {

            }

            int threadId;
        };

        thread_local tls_variables tls;
    }

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
        struct alignas(BASE_ALIGN) task_t
        {

        };

        enum PTR_ACCESS_TAG :
            size_t
        {
            PAT_NONE = 0,
            PAT_RESERVE = 1
        };

        template<class Ptr>
        struct tagged_ptr
        {
            static_assert(std::is_pointer_v<Ptr> &&
                alignof(std::remove_pointer_t<Ptr>) >= BASE_ALIGN);

            tagged_ptr(Ptr oldPtr) :
                ptr{ oldPtr }
            {}

            tagged_ptr(size_t tagged = 0) :
                taggedValue{ tagged }
            {}

            PTR_ACCESS_TAG getTag() const noexcept
            {
                return static_cast<PTR_ACCESS_TAG>((taggedValue & MASK_TAG));
            }

            Ptr getPtr() const noexcept
            {
                return changeTag(PAT_NONE).ptr;
            }

            size_t getTaggedValue() const noexcept
            {
                return taggedValue;
            }

            tagged_ptr changeTag(PTR_ACCESS_TAG tag) const noexcept
            {
                return tagged_ptr
                {
                    ((taggedValue & MASK_EXCEPT_TAG) |
                    (static_cast<size_t>(tag)))
                };
            }

            tagged_ptr changePtr(Ptr newPtr) const noexcept
            {
                return tagged_ptr{ newPtr }
                .changeTag(getTag());
            }

            bool commitTag(std::atomic_size_t& taggedPtr, PTR_ACCESS_TAG tag) noexcept
            {
                size_t oldTaggedValue = getTaggedValue();
                size_t newTaggedValue = changeTag(tag)
                    .getTaggedValue();

                return taggedPtr.compare_exchange_strong(oldTaggedValue, newTaggedValue);
            }

            bool commitPtr(std::atomic_size_t& taggedPtr, Ptr newPtr) const noexcept
            {
                size_t oldTaggedValue = getTaggedValue();
                size_t newTaggedValue = changePtr(newPtr)
                    .getTaggedValue();

                return taggedPtr.compare_exchange_strong(oldTaggedValue, newTaggedValue);
            }

            bool commit(
                std::atomic_size_t& taggedPtr,
                Ptr newPtr,
                PTR_ACCESS_TAG targetState) const noexcept
            {
                size_t oldTaggedValue = getTaggedValue();
                size_t newTaggedValue = changePtr(newPtr)
                    .changeTag(targetState)
                    .getTaggedValue();

                return taggedPtr.compare_exchange_strong(oldTaggedValue, newTaggedValue);
            }

        private:
            union
            {
                Ptr ptr;
                size_t taggedValue;
            };

            static constexpr size_t MASK_TAG = 0x0000'0000'0000'0007ULL;
            static constexpr size_t MASK_EXCEPT_TAG = 0xFFFF'FFFF'FFFF'FFF8ULL;
        };

        struct task_scheduler_base
        {
        protected:
            void help()
            {
                auto& rec = helpRecords[tls_variables::tls.threadId];
                if (rec.delay-- != 0)
                    return;

                auto desc = rec.reserveDesc(taggedDescs[tls_variables::tls.threadId]);
            }

        protected:
            using tagged_task_t = tagged_ptr<task_t*>;

            struct alignas(BASE_ALIGN * 8) slow_op_desc
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
                    OK_NONE,
                    OK_PUSH_FRONT,
                    OK_PUSH_BACK,
                    OK_POP_FRONT,
                    OK_POP_BACK
                };

                OP_PHASE phase = OP_BEGIN;
                OP_KIND op = OK_NONE;
                uint64_t oldFront = 0;
                uint64_t oldBack = 0;
                uint64_t newFront = 0;
                uint64_t newBack = 0;
                tagged_task_t oldTagged;
                task_t* task = nullptr;
            };

            using tagged_slow_op_desc = tagged_ptr<slow_op_desc*>;

            struct help_record
            {
                void reset(std::array<std::atomic_size_t, MTBASE_MAX_THREAD>& taggedDescs)
                {
                    curThreadId = (curThreadId + 1) % MTBASE_MAX_THREAD;
                    delay = HELPING_DELAY;

                    tagged_slow_op_desc oldTaggedDesc = reserveDesc(taggedDescs[curThreadId]);
                    lastPhase = oldTaggedDesc.getPtr()->phase;

                    releaseDesc(taggedDescs[curThreadId], oldTaggedDesc.changeTag(PAT_NONE));
                }

                tagged_slow_op_desc reserveDesc(std::atomic_size_t& taggedDesc)
                {
                    size_t oldTaggedValue = taggedDesc.load();
                    tagged_slow_op_desc oldTaggedDesc{ oldTaggedValue };
                    size_t newTaggedValue =
                        oldTaggedDesc.changeTag(PAT_RESERVE).getTaggedValue();
                    while (oldTaggedDesc.getTag() != PAT_NONE ||
                        !taggedDesc.compare_exchange_strong(oldTaggedValue, newTaggedValue))
                        oldTaggedDesc = tagged_slow_op_desc{ oldTaggedValue };

                    return tagged_slow_op_desc{ oldTaggedValue };
                }

                void releaseDesc(std::atomic_size_t& taggedDesc, const tagged_slow_op_desc taggedDescRelease)
                {
                    taggedDesc.store(taggedDescRelease.getTaggedValue());
                }

            public:
                slow_op_desc::OP_PHASE lastPhase = slow_op_desc::OP_BEGIN;
                size_t delay = HELPING_DELAY;

            private:
                static constexpr size_t HELPING_DELAY = 5;

                int curThreadId = -1;
            };

            std::array<slow_op_desc, MTBASE_MAX_THREAD> descs;
            std::array<std::atomic_size_t, MTBASE_MAX_THREAD> taggedDescs;
            std::array<help_record, MTBASE_MAX_THREAD> helpRecords;
        };

        template<std::uint32_t SIZE>
        struct task_scheduler :
            public task_scheduler_base
        {
            static_assert(SIZE <= 0xFFFF'FFFDU,
                "task_scheduler reserve 2 slots to prevent conflict of index pointers: front & back");
            static_assert(SIZE >= 64U);

            static constexpr uint32_t REAL_SIZE = SIZE + 2;

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
            struct fragmented_index
            {
                bool isFull() const noexcept
                {
                    return (front + REAL_SIZE - back) % REAL_SIZE == 1;
                }

                bool isEmpty() const noexcept
                {
                    return (back + REAL_SIZE - front) % REAL_SIZE == 1;
                }

                bool isValidIndex() const noexcept
                {
                    return front != back;
                }

                fragmented_index pushed_front() const noexcept
                {
                    return fragmented_index
                    {
                        .front = (front + REAL_SIZE - 1) % REAL_SIZE,
                        .back = back
                    };
                }

                fragmented_index pushed_back() const noexcept
                {
                    return fragmented_index
                    {
                        .front = front,
                        .back = (back + 1) % REAL_SIZE
                    };
                }

                fragmented_index poped_front() const noexcept
                {
                    return fragmented_index
                    {
                        .front = (front + 1) % REAL_SIZE,
                        .back = back
                    };
                }

                fragmented_index poped_back() const noexcept
                {
                    return fragmented_index
                    {
                        .front = front,
                        .back = (back + REAL_SIZE - 1) % REAL_SIZE
                    };
                }

            public:
                static constexpr uint64_t INIT_FRONT = 0;
                static constexpr uint64_t INIT_BACK = 1;

                uint64_t front = INIT_FRONT;
                uint64_t back = INIT_BACK;
            };

            struct combined_index
            {
                fragmented_index getOldIndex() const noexcept
                {
                    uint64_t oldIndex = index.load();

                    return fragmented_index
                    {
                        .front = (oldIndex & MASK_FRONT) >> SHIFT_FRONT,
                        .back = (oldIndex & MASK_FRONT) >> SHIFT_FRONT
                    };
                }

                bool setNewIndex(const fragmented_index& oldFragmented, const fragmented_index& newFragmented) noexcept
                {
                    uint64_t oldIndex = combineToIndex(oldFragmented);
                    uint64_t newIndex = combineToIndex(newFragmented);

                    return index.compare_exchange_strong(oldIndex, newIndex);
                }

            private:
                uint64_t combineToIndex(const fragmented_index fragmented) const noexcept
                {
                    return ((fragmented.front << SHIFT_FRONT) & (fragmented.back << SHIFT_BACK));
                }

            private:
                static constexpr uint64_t MASK_FRONT = 0xFFFF'FFFF'0000'0000ULL;
                static constexpr uint64_t MASK_BACK = 0x0000'0000'FFFF'FFFFULL;
                static constexpr uint64_t SHIFT_FRONT = 32;
                static constexpr uint64_t SHIFT_BACK = 0;

                std::atomic_uint64_t index{ combineToIndex(fragmented_index{}) };
            };

            #pragma region WAIT_FREE_FAST_PATH
            bool pushFrontFast(task_t* task)
            {
                fragmented_index oldFragmented{ index.getOldIndex() };
                if (oldFragmented.isFull())
                    return false;

                std::atomic_size_t& taggedTack = taggedTasks[oldFragmented.front];
                const tagged_task_t oldTagged{ taggedTack.load() };

                if (!isValidPushTagged(oldTagged))
                    return false;

                return exchangeFast(task,
                    oldFragmented, oldFragmented.pushed_front(),
                    taggedTack, oldTagged);
            }

            bool pushBackFast(task_t* task)
            {
                fragmented_index oldFragmented{ index.getOldIndex() };
                if (oldFragmented.isFull())
                    return false;

                std::atomic_size_t& taggedTack = taggedTasks[oldFragmented.back];
                const tagged_task_t oldTagged{ taggedTack.load() };

                if (!isValidPushTagged(oldTagged))
                    return false;

                return exchangeFast(task,
                    oldFragmented, oldFragmented.pushed_back(),
                    taggedTack, oldTagged);
            }

            task_t* popFrontFast()
            {
                fragmented_index oldFragmented{ index.getOldIndex() };
                if (oldFragmented.isEmpty())
                    return false;

                std::atomic_size_t& taggedTack = taggedTasks[oldFragmented.front];
                const tagged_task_t oldTagged{ taggedTack.load() };

                if (!isValidPopTagged(oldTagged))
                    return false;

                return getTaskIfSuccessed
                (
                    exchangeFast(nullptr,
                        oldFragmented, oldFragmented.poped_front(),
                        taggedTack, oldTagged) ?
                    oldTagged.getPtr() : nullptr;
                );
            }

            task_t* popBackFast()
            {
                fragmented_index oldFragmented{ index.getOldIndex() };
                if (oldFragmented.isEmpty())
                    return false;

                std::atomic_size_t& taggedTack = taggedTasks[oldFragmented.back];
                const tagged_task_t oldTagged{ taggedTack.load() };

                if (!isValidPopTagged(oldTagged))
                    return false;

                return getTaskIfSuccessed
                (
                    exchangeFast(nullptr,
                    oldFragmented, oldFragmented.poped_back(),
                    taggedTack, oldTagged)
                );
            }

            bool isValidPushTagged(const tagged_task_t& tagged) const noexcept
            {
                return tagged.getTag() == PAT_NONE &&
                    tagged.getPtr() == nullptr;
            }

            bool isValidPopTagged(const tagged_task_t& tagged) const noexcept
            {
                return tagged.getTag() == PAT_NONE &&
                    tagged.getPtr() != nullptr;
            }

            task_t* getTaskIfSuccessed(const tagged_task_t tagged, bool success)
            {
                return success ? tagged.getPtr() : nullptr;
            }

            bool exchangeFast(
                task_t* task,
                const fragmented_index& oldFragmented,
                const fragmented_index& newFragmented,
                std::atomic_size_t& taggedTask,
                const tagged_task_t oldTagged)
            {
                if (!newFragmented.isValidIndex())
                    return false;

                if (!oldTagged.commitTag(taggedTask, PAT_RESERVE))
                    return false;

                const tagged_task_t newTaggedReserve
                {
                    oldTagged
                    .changeTag(PAT_RESERVE)
                    .getTaggedValue()
                };

                if (!newTaggedReserve.commitPtr(taggedTask, task))
                {
                    while (!newTaggedReserve.commitTag(taggedTask, PAT_NONE));

                    return false;
                }

                const tagged_task_t newTaggedCommit
                {
                    newTaggedReserve
                    .changePtr(task)
                    .getTaggedValue()
                };

                if (!index.setNewIndex(oldFragmented, newFragmented))
                {
                    task_t* oldTask = oldTagged.getPtr();

                    while (!newTaggedCommit.commit(taggedTask, oldTask, PAT_NONE));

                    return false;
                }

                while (!newTaggedCommit.commitTag(taggedTask, PAT_NONE));

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
                
        private:
            static constexpr int MAX_RETRY_FAST = 4;
            combined_index index;
            mi_memory_resource& resource;
            std::array<std::atomic_size_t, REAL_SIZE> taggedTasks;
        };

        static mi_memory_resource res;
        static task_scheduler<(1 << 20)> tmp(res);
    }
}
