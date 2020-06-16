#pragma once

#include <memory_resource>
#include <chrono>
#include <atomic>
#include <array>
#include <optional>

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

        template<class T>
        struct mi_allocator
        {
            using value_type = T;

        public:
            mi_allocator(const mi_allocator&) noexcept = default;

            template<class U>
            mi_allocator(const mi_allocator<U>& other) noexcept :
                res{ other.res }
            {}

            mi_allocator(mi_memory_resource* r) :
                res{ r }
            {}

        public:
            [[nodiscard]] T* allocate_object(size_t n = 1)
            {
                if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
                    throw std::bad_array_new_length{};

                return static_cast<T*>(res->allocate(n * sizeof(T), alignof(T)));
            }

            void deallocate_object(T* p, size_t n = 1)
            {
                res->deallocate(p, n * sizeof(T), alignof(T));
            }

            template<class... Args>
            void construct(T* p, Args&&... args)
            {
                new(p) T{ std::forward<Args>(args)... };
            }

            void construct(T* p, T&& other)
            {
                new(p) T{ other };
            }

            void destroy(T* p)
            {
                p->~T();
            }

            template<class... Args>
            [[nodiscard]] T* new_object(Args&&... args)
            {
                T* p = allocate_object();
                
                try
                {
                    construct(p, std::forward<Args>(ctor_args)...);
                }
                catch (...)
                {
                    deallocate_object(p);
                    throw;
                }
                
                return p;
            }

            [[nodiscard]] T* new_object(T&& other)
            {
                T* p = allocate_object();

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

            void delete_object(T* p)
            {
                destroy(p);
                deallocate_object(p);
            }

            mi_memory_resource* resource() const
            {
                return res;
            }

        private:
            mi_memory_resource* res;
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

        template<size_t SIZE>
        struct task_scheduler final
        {
            static_assert(SIZE >= BASE_ALIGN * 8);
            static_assert(SIZE <= 0xFFFF'FFFD);

        private:
            struct index_t
            {
                const size_t front = 0;
                const size_t back = 1;
            };

            struct op_description
            {
                enum class PHASE :
                    size_t
                {
                    RESERVE,
                    EXECUTE,
                    COMMIT,
                    ROLLBACK,
                    COMPLETE
                };

                enum class OP :
                    size_t
                {
                    NONE,
                    PUSH_FRONT,
                    PUSH_BACK,
                    POP_FRONT,
                    POP_BACK
                };

                const PHASE phase{ PHASE::RESERVE };
                const OP op{ OP::NONE };
                std::atomic<task_t*>& target;
                task_t* const oldTask = nullptr;
                task_t* const newTask = nullptr;
                index_t* const oldIndex = nullptr;
                index_t* const newIndex = nullptr;
            };

        public:
            task_scheduler(mi_memory_resource* res) :
                indexAllocator{ res },
                descAllocator{ res }
            {
                index_t* init = indexAllocator.new_object();
                index.store(init);
            }

            ~task_scheduler()
            {

            }

        private:
            bool fast_path(op_description*& desc) noexcept
            {
                task_t* oldTask = desc->oldTask;
                if (!desc->target.compare_exchange_strong(oldTask, desc->newTask))
                    return false;

                index_t* oldIndex = desc->oldIndex;
                if (!index.compare_exchange_strong(oldIndex, desc->newIndex))
                {
                    desc->target.store(oldTask);

                    op_description* oldDesc = desc;
                    desc = descAllocator.new_object(op_description
                        {
                            .phase = op_description::PHASE::COMPLETE,
                            .op = oldDesc->op,
                            .target = oldDesc->target,
                            .oldIndex = oldIndex
                        });
                    descAllocator.delete_object(oldDesc);

                    return false;
                }

                indexAllocator.delete_object(oldIndex);

                op_description* oldDesc = desc;
                desc = descAllocator.new_object(op_description
                    {
                        .phase = op_description::PHASE::COMPLETE,
                        .op = oldDesc->op,
                        .target = oldDesc->target,
                        .oldTask = oldTask
                    });
                descAllocator.delete_object(oldDesc);

                return true;
            }

            void slow_path(op_description*& desc) noexcept
            {
                op_description* oldDesc = nullptr;
                while (true)
                {
                    if (registered.compare_exchange_strong(oldDesc, desc))
                        break;

                    if (oldDesc == nullptr)
                        continue;

                    if (oldDesc->phase != op_description::PHASE::COMPLETE)
                        help_registered(oldDesc);
                }

                help_registered(desc);
            }

            void help_registered(op_description* const desc) noexcept
            {
                op_description* oldDesc = desc;
                while (true)
                {
                    // process
                }

                // release
            }

        private:
            static constexpr size_t REAL_SIZE = SIZE + 2;
            mi_allocator<index_t> indexAllocator;
            mi_allocator<op_description> descAllocator;
            std::atomic<index_t*> index;
            std::atomic<op_description*> registered{ nullptr };
            std::array<std::atomic<task_t*>, REAL_SIZE> tasks;
        };

        mi_memory_resource r;
        task_scheduler<(1 << 10)> temp{ &r };
    }
}
