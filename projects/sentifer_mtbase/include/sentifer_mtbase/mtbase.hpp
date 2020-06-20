#pragma once

#include <memory_resource>
#include <chrono>
#include <atomic>
#include <array>
#include <tuple>

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

        struct generic_allocator
        {
            generic_allocator(const generic_allocator&) noexcept = default;

            generic_allocator(std::pmr::memory_resource * r) :
                res{ r }
            {}

        public:
            [[nodiscard]] void* allocate_bytes(size_t nbytes, size_t alignment = alignof(std::max_align_t))
            {
                return res->allocate(nbytes, alignment);
            }

            void deallocate_bytes(void* p, size_t nbytes, size_t alignment = alignof(std::max_align_t))
            {
                res->deallocate(p, nbytes, alignment);
            }

            template<class T>
            [[nodiscard]] T* allocate_object(size_t n = 1)
            {
                if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
                    throw std::bad_array_new_length{};

                return static_cast<T*>(allocate_bytes(n * sizeof(T), alignof(T)));
            }

            template<class T>
            void deallocate_object(T * p, size_t n = 1)
            {
                deallocate_bytes(p, n * sizeof(T), alignof(T));
            }

            template<class T, class... Args>
            void construct(T * p, Args &&... args)
            {
                new(p) T{ std::forward<Args>(args)... };
            }

            template<class T>
            void construct(T * p, T && other)
            {
                new(p) T{ other };
            }

            template<class T>
            void destroy(T * p)
            {
                p->~T();
            }

            template<class T, class... Args>
            [[nodiscard]] T* new_object(Args &&... args)
            {
                T* p = allocate_object<T>();

                try
                {
                    construct(p, std::forward<Args>(args)...);
                }
                catch (...)
                {
                    deallocate_object(p);
                    throw;
                }

                return p;
            }

            template<class T>
            [[nodiscard]] T* new_object(T && other)
            {
                T* p = allocate_object<T>();

                try
                {
                    construct(p, other);
                }
                catch (...)
                {
                    deallocate_object(p);
                    throw;
                }

                return p;
            }

            template<class T>
            void delete_object(T * p)
            {
                destroy(p);
                deallocate_object(p);
            }

            std::pmr::memory_resource* resource() const
            {
                return res;
            }

        private:
            std::pmr::memory_resource* res;
        };

        template<class T>
        struct mi_allocator
        {
            mi_allocator(mi_memory_resource* r) :
                genAlloc{ r }
            {}

        public:
            template<class... Args>
            [[nodiscard]] T* new_object(Args&&... args)
            {
                return genAlloc.new_object<T>(std::forward<Args>(args)...);
            }

            [[nodiscard]] T* new_object(T&& other)
            {
                return genAlloc.new_object<T>(other);
            }

            void delete_object(T* p)
            {
                genAlloc.delete_object(p);
            }

        private:
            generic_allocator genAlloc;
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
        using system_tick = std::chrono::nanoseconds;
        using steady_tick = std::chrono::nanoseconds;

        struct clock_t
        {
            static system_tick getSystemTick()
            {
                return getTick<std::chrono::system_clock>();
            }

            static steady_tick getSteadyTick()
            {
                return getTick<std::chrono::steady_clock>();
            }

        private:
            template<class Clock>
            static std::chrono::nanoseconds getTick()
            {
                while (!tryOwn());
             
                std::chrono::nanoseconds tick = Clock::now().time_since_epoch();
                
                release();
                
                return tick;
            }

            static bool tryOwn()
            {
                bool oldIsOwned = false;
                return isOwned.compare_exchange_strong(oldIsOwned, true,
                    std::memory_order_acq_rel, std::memory_order_acquire);
            }

            static void release()
            {
                isOwned.store(false, std::memory_order_release);
            }

        private:
            inline static std::atomic_bool isOwned{ false };
        };
    }

    inline namespace schedulers
    {
        struct task_t;

        template<size_t SIZE>
        struct task_wait_free_deque final
        {
            static_assert(SIZE >= BASE_ALIGN * 8);
            static_assert(SIZE <= 0xFFFF'FFFD);

        private:
            enum class OP :
                size_t
            {
                PUSH_FRONT,
                PUSH_BACK,
                POP_FRONT,
                POP_BACK
            };

            struct index_t
            {
                index_t move(OP op) const noexcept
                {
                    switch (op)
                    {
                    case OP::PUSH_FRONT:
                        return pushed_front();
                    case OP::PUSH_BACK:
                        return pushed_back();
                    case OP::POP_FRONT:
                        return poped_front();
                    case OP::POP_BACK:
                        return poped_back();
                    default:
                        return index_t{};
                    }
                }

                index_t pushed_front() const noexcept
                {
                    return index_t
                    {
                        .front = (front + REAL_SIZE - 1) % REAL_SIZE,
                        .back = back
                    };
                }

                index_t pushed_back() const noexcept
                {
                    return index_t
                    {
                        .front = front,
                        .back = (back + 1) % REAL_SIZE
                    };
                }

                index_t poped_front() const noexcept
                {
                    return index_t
                    {
                        .front = (front + 1) % REAL_SIZE,
                        .back = back
                    };
                }

                index_t poped_back() const noexcept
                {
                    return index_t
                    {
                        .front = front,
                        .back = (back + REAL_SIZE - 1) % REAL_SIZE
                    };
                }

                bool isFull() const noexcept
                {
                    return (front + REAL_SIZE - back) % REAL_SIZE == 1;
                }

                bool isEmpty() const noexcept
                {
                    return (back + REAL_SIZE - front) % REAL_SIZE == 1;
                }

                bool isValid(OP op) const noexcept
                {
                    if (front == back)
                        return false;

                    switch (op)
                    {
                    case OP::PUSH_FRONT:
                    case OP::PUSH_BACK:
                        return isFull();
                    case OP::POP_FRONT:
                    case OP::POP_BACK:
                        return isEmpty();
                    default:
                        return true;
                    }
                }

                size_t getTargetIndex(OP op) const noexcept
                {
                    switch (op)
                    {
                    case OP::PUSH_FRONT:
                    case OP::PUSH_BACK:
                        return front;
                    case OP::POP_FRONT:
                    case OP::POP_BACK:
                        return back;
                    default:
                        return 0;
                    }
                }

            public:
                const size_t front = 0;
                const size_t back = 1;
            };

            struct op_description
            {
                enum class PHASE :
                    size_t
                {
                    RESERVE,
                    COMPLETE,
                    FAIL
                };

            public:
                op_description rollbacked(
                    index_t* const oldIndexLoad,
                    index_t* const newIndexLoad)
                    const noexcept
                {
                    return op_description
                    {
                        .op = op,
                        .target = target,
                        .oldTask = oldTask,
                        .newTask = newTask,
                        .oldIndex = oldIndexLoad,
                        .newIndex = newIndexLoad
                    };
                }

                op_description completed(index_t* const oldIndexLoad) const noexcept
                {
                    return op_description
                    {
                        .phase = op_description::PHASE::COMPLETE,
                        .op = op,
                        .target = target,
                        .oldTask = oldTask,
                        .newTask = newTask,
                        .oldIndex = oldIndexLoad
                    };
                }

                op_description failed(index_t* const oldIndexLoad) const noexcept
                {
                    return op_description
                    {
                        .phase = op_description::PHASE::FAIL,
                        .op = op,
                        .target = target,
                        .oldTask = oldTask,
                        .newTask = newTask,
                        .oldIndex = oldIndexLoad
                    };
                }

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
            task_wait_free_deque(mi_memory_resource* res) :
                indexAllocator{ res },
                descAllocator{ res }
            {
                index_t* init = indexAllocator.new_object();
                index.store(init, std::memory_order_relaxed);
            }

            ~task_wait_free_deque()
            {
                indexAllocator.delete_object(index.load(std::memory_order_relaxed));
                descAllocator.delete_object(registered.load(std::memory_order_relaxed));
            }

            bool push_front(task_t* task)
            {
                op_description* const desc = createDesc(task, OP::PUSH_FRONT);

                bool result = (desc->phase == op_description::PHASE::COMPLETE);
                destroyDesc(desc);

                return result;
            }

            bool push_back(task_t* task)
            {
                op_description* const desc = createDesc(task, OP::PUSH_BACK);

                bool result = (desc->phase == op_description::PHASE::COMPLETE);
                destroyDesc(desc);

                return result;
            }

            task_t* pop_front()
            {
                op_description* const desc = createDesc(nullptr, OP::POP_FRONT);

                task_t* result = (desc->phase == op_description::PHASE::COMPLETE ?
                        desc->oldTask : nullptr);
                destroyDesc(desc);

                return result;
            }

            task_t* pop_back()
            {
                op_description* const desc = createDesc(nullptr, OP::POP_BACK);

                task_t* result = (desc->phase == op_description::PHASE::COMPLETE ?
                    desc->oldTask : nullptr);
                destroyDesc(desc);

                return result;
            }

        private:
            op_description* createDesc(task_t* task, OP op)
            {
                index_t* const oldIndex = index.load(std::memory_order_acquire);
                if (!oldIndex->isValid(op))
                    return nullptr;

                index_t* const newIndex = indexAllocator.new_object(oldIndex->move(op));
                std::atomic<task_t*>& target = tasks[oldIndex->getTargetIndex(op)];

                op_description* desc = descAllocator.new_object(op_description
                    {
                        .phase = op_description::PHASE::RESERVE,
                        .op = op,
                        .target = target,
                        .oldTask = target.load(std::memory_order_acquire),
                        .newTask = task,
                        .oldIndex = oldIndex,
                        .newIndex = newIndex
                    });

                applyDesc(desc);

                return desc;
            }

            void applyDesc(op_description*& desc)
            {
                op_description* helpDesc = registered.load(std::memory_order_acquire);
                if (helpDesc != nullptr)
                    help_registered(helpDesc);

                for (size_t i = 0; i < MAX_RETRY; ++i)
                    if (fast_path(desc))
                        return;

                slow_path(desc);
            }

            bool fast_path(op_description*& desc)
            {
                if (!tryCommitTask(desc))
                    return false;

                if (!tryCommitIndex(desc))
                {
                    op_description* oldDesc = rollbackTask(desc);
                    destroyDesc(oldDesc);

                    return false;
                }
                
                completeDesc(desc);

                return true;
            }

            void slow_path(op_description*& desc)
            {
                op_description* oldDesc = nullptr;
                while (true)
                {
                    if (tryRegister(oldDesc, desc))
                        break;

                    if (oldDesc == nullptr)
                        continue;

                    help_registered(oldDesc);
                }

                help_registered(desc);
            }

            void help_registered(op_description*& desc)
            {
                help_registered_progress(desc);
                help_registered_complete(desc);
            }

            void help_registered_progress(op_description*& desc) noexcept
            {
                while (true)
                {
                    if (desc->phase != op_description::PHASE::RESERVE)
                        return;

                    if (!tryCommitTask(desc))
                        continue;

                    if (tryCommitIndex(desc))
                        break;

                    op_description* oldDesc = rollbackTask(desc);
                    renewRegistered(oldDesc, desc);
                }
            }

            void help_registered_complete(op_description*& desc) noexcept
            {
                if (desc->phase == op_description::PHASE::FAIL)
                    return;

                while (desc->phase != op_description::PHASE::COMPLETE)
                {
                    op_description* oldDesc = desc;
                    desc = descAllocator.new_object(
                        oldDesc->completed(index.load(std::memory_order_acquire)));
                    renewRegistered(oldDesc, desc);
                }
            }

            bool tryCommitTask(op_description* const desc) noexcept
            {
                task_t* oldTask = desc->oldTask;

                if (tryEfficientCAS(desc->target, oldTask, desc->newTask))
                    return true;

                return oldTask == desc->newTask;
            }

            bool tryCommitIndex(op_description* const desc) noexcept
            {
                index_t* oldIndex = desc->oldIndex;

                if (tryEfficientCAS(index, oldIndex, desc->newIndex))
                    return true;

                return oldIndex == desc->newIndex;
            }

            [[nodiscard]] op_description* rollbackTask(op_description*& desc)
            {
                desc->target.store(desc->oldTask, std::memory_order_release);

                op_description* oldDesc = desc;
                index_t* const oldIndex = index.load(std::memory_order_acquire);
                if (oldIndex->isValid(oldDesc->op))
                {
                    index_t* const newIndex =
                        indexAllocator.new_object(oldIndex->move(oldDesc->op));
                    desc = descAllocator.new_object(oldDesc->rollbacked(oldIndex, newIndex));
                }
                else
                    desc = descAllocator.new_object(oldDesc->failed(oldIndex));
                return oldDesc;
            }

            void completeDesc(op_description*& desc)
            {
                op_description* const oldDesc = desc;
                indexAllocator.delete_object(oldDesc->oldIndex);

                desc = descAllocator.new_object(
                    oldDesc->completed(index.load(std::memory_order_acquire)));
                descAllocator.delete_object(oldDesc);
            }

            void renewRegistered(
                op_description* const oldDesc,
                op_description*& desc)
            {
                op_description* curDesc = oldDesc;
                op_description* const newDesc = desc;
                if (tryRegister(curDesc, newDesc))
                {
                    destroyDesc(oldDesc);

                    desc = newDesc;
                }
                else
                {
                    destroyDesc(oldDesc);
                    destroyDesc(newDesc);

                    desc = curDesc;
                }
            }

            bool tryRegister(
                op_description*& expected,
                op_description* const desired) noexcept
            {
                return tryEfficientCAS(registered, expected, desired);
            }

            template<class T>
            bool tryEfficientCAS(std::atomic<T*>& target, T*& expected, T* const desired) noexcept
            {
                return target.compare_exchange_strong(expected, desired,
                    std::memory_order_acq_rel, std::memory_order_acquire);
            }

            void destroyDesc(op_description* const desc)
            {
                op_description* oldDesc = desc;
                while (!tryRegister(oldDesc, nullptr))
                    if (oldDesc == desc)
                        break;

                index_t* const curIndex = index.load(std::memory_order_acquire);
                if (desc->oldIndex == curIndex)
                    indexAllocator.delete_object(desc->oldIndex);
                if (desc->newIndex == curIndex)
                    indexAllocator.delete_object(desc->newIndex);

                descAllocator.delete_object(desc);
            }

        private:
            static constexpr size_t REAL_SIZE = SIZE + 2;
            static constexpr size_t MAX_RETRY = 3;
            mi_allocator<index_t> indexAllocator;
            mi_allocator<op_description> descAllocator;
            std::atomic<index_t*> index;
            std::atomic<op_description*> registered{ nullptr };
            std::array<std::atomic<task_t*>, REAL_SIZE> tasks;
        };

        struct task_t
        {
            task_t(const size_t sizeImpl, const size_t alignImpl) :
                size{ sizeImpl },
                align{ alignImpl }
            {}

            virtual ~task_t()
            {}

        public:
            virtual void invoke() = 0;

        public:
            const size_t size;
            const size_t align;
        };

        template<class T, class Func, class TupleArgs>
        struct task_impl_t :
            task_t
        {
            task_impl_t(T* const fromObj, Func method, TupleArgs args) :
                task_t{ sizeof(task_impl_t), alignof(task_impl_t) },
                owner{ fromObj },
                invoked{ method },
                tupled{ args }
            {

            }

        public:
            void invoke() override
            {
                std::apply(invoked, std::tuple_cat(std::make_tuple(owner), tupled));
            }

        private:
            T* const owner;
            Func invoked;
            TupleArgs tupled;
        };

        struct scheduler
        {
            scheduler(std::pmr::memory_resource* const res) :
                alloc{ res }
            {}

        public:
            template<class T, class Func, class... Args>
            void registerTask(T* const fromObj, Func method, Args&&... args)
            {
                static_assert(std::is_member_function_pointer_v<Func>);
                static_assert(std::is_invocable_r_v<void, Func, T*, Args...>);

                registerTaskImpl(alloc.new_object(
                    task_impl_t{ fromObj, method, std::make_tuple(std::forward<Args>(args)...) }));
            }

        protected:
            void destroyTask(task_t* const task)
            {
                const size_t size = task->size;
                const size_t align = task->align;

                alloc.destroy(task);
                alloc.deallocate_bytes(task, size, align);
            }

            virtual void registerTaskImpl(task_t* const) = 0;

        private:
            generic_allocator alloc;
        };

        struct thread_local_scheduler final :
            public scheduler
        {
        protected:
            void registerTaskImpl(task_t* const task) override
            {

            }
        };

        struct object_sheduler final :
            public scheduler
        {
        public:
            void flush(thread_local_scheduler& threadSched)
            {
                if (!tryOwn())
                    return;

                // set tickBeginFlush
                cntFlushed = 0;
                flushOwned(threadSched);
            }

        protected:
            void registerTaskImpl(task_t* const task) override
            {
                taskDeq.push_back(task);
            }

        private:
            void flushOwned(thread_local_scheduler& threadSched)
            {
                flushTasks();

                if (checkTransitionCount() || checkTransitionTick())
                {
                    release();
                    return;
                }

                threadSched.registerTask(this, &object_sheduler::flushOwned, threadSched);
            }

            bool tryOwn()
            {
                bool oldIsOwned = false;
                return isOwned.compare_exchange_strong(oldIsOwned, true,
                    std::memory_order_acq_rel, std::memory_order_acquire);
            }

            void flushTasks()
            {
                for (size_t i = 0; i < MAX_FLUSH_COUNT_AT_ONCE && !checkTransitionCount(); ++i)
                {
                    task_t* const task = taskDeq.pop_front();
                    if (task == nullptr)
                    {
                        cntFlushed = MAX_FLUSH_COUNT;
                        break;
                    }

                    task->invoke();
                    destroyTask(task);
                    ++cntFlushed;
                }
            }

            bool checkTransitionTick()
            {
                if (MAX_FLUSH_TICK == std::chrono::nanoseconds{ 0 })
                    return false;

                // get current tick
                std::chrono::nanoseconds tickTransition;

                return tickTransition - tickBeginFlush > MAX_FLUSH_TICK;
            }

            bool checkTransitionCount()
            {
                return cntFlushed >= MAX_FLUSH_COUNT;
            }

            void release()
            {
                isOwned.store(false, std::memory_order_release);
            }

        private:
            task_wait_free_deque<(1 << 20)> taskDeq;
            std::atomic_bool isOwned{ false };
            size_t cntFlushed;
            std::chrono::nanoseconds tickBeginFlush;
            const std::chrono::nanoseconds MAX_FLUSH_TICK;
            const size_t MAX_FLUSH_COUNT;
            const size_t MAX_FLUSH_COUNT_AT_ONCE;
        };
    }
}
