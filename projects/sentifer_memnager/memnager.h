// inspired from https://www.qt.io/blog/a-fast-and-thread-safe-pool-allocator-for-qt-part-1

#pragma once

#include <cassert>

#define assert_msg(exp, msg) assert(((void)msg, exp))

#include <cstddef>
#include <array>
#include <vector>
#include <memory_resource>
#include <concepts>
#include <span>

//#include "wil/resource.h"

namespace memnager
{
    inline namespace utils
    {
        template<class To, class From>
        To* force_ptr_cast(From* ptr)
        {
            return static_cast<To*>(static_cast<void*>(ptr));
        }

        template<class T>
        size_t ptr_to_size_t(T ptr)
        {
            if constexpr (std::is_pointer_v<T>)
            {
                return reinterpret_cast<size_t>(reinterpret_cast<void*>(ptr));
            }
            else
            {
                return reinterpret_cast<size_t>(nullptr);
            }
        }
    }

    inline namespace mem_block
    {
        constexpr size_t MaxAlign = 8192;

        template<size_t BlockSize>
        struct alignas(BlockSize < MaxAlign ? BlockSize : MaxAlign) MemBlock
        {
            std::array<std::byte, BlockSize> memory;
        };

        inline namespace instanced_mem_block
        {
            constexpr size_t PageSize = 4096;
            constexpr size_t CacheLineSize = 64;
            constexpr size_t AllocUnitSize = PageSize * 1024;

            using Page = MemBlock<PageSize>;
            using CacheLine = MemBlock<CacheLineSize>;
            using AllocUnit = MemBlock<AllocUnitSize>;
        }

        inline namespace segments
        {
            template<size_t N>
            using micro_segment = MemBlock<(1 << N)>;
            template<size_t N>
            using cacheline_segment = std::array<CacheLine, N>;
            template<size_t N>
            using page_segment = std::array<Page, N>;
            template<size_t N>
            using macro_segment = std::array<Page, N>;
        }
    }

    inline namespace aligned_ptr
    {
        template<class MemBlockType>
        struct AlignedPtr final
        {
            MemBlockType* ptr = nullptr;

            bool isValid()
            {
                return ptr_to_size_t(ptr) % alignof(MemBlockType) == 0;
            }
        };

        inline namespace instanced_aligned_ptr
        {
            using PageAlignedPtr = AlignedPtr<Page>;
            using CacheAlignedPtr = AlignedPtr<CacheLine>;
            using UnitAlignedPtr = AlignedPtr<AllocUnit>;
        }
    }

    struct memblock_pool_resource :
        public std::pmr::memory_resource
    {
        virtual ~memblock_pool_resource()
        {
            release();
        }

        virtual void release()
        {}

        std::pmr::memory_resource* upstream_resource()
        {
            return upstream;
        }

    protected:
        std::pmr::memory_resource* upstream = nullptr;
    };

    inline namespace resources
    {
        struct page_pool_resource :
            public memblock_pool_resource
        {
        private:
            virtual void* do_allocate(std::size_t bytes, std::size_t alignment) override
            {
                return nullptr;
            }

            virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override
            {

            }

            virtual bool do_is_equal(const std::pmr::memory_resource& other) const noexcept
            {
                return this == &other;
            }
        };

        struct cacheline_pool_resource :
            public memblock_pool_resource
        {
        private:
            virtual void* do_allocate(std::size_t bytes, std::size_t alignment) override
            {
                return nullptr;
            }

            virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override
            {

            }

            virtual bool do_is_equal(const std::pmr::memory_resource& other) const noexcept
            {
                return this == &other;
            }
        };

        struct limited_pool_resource :
            public memblock_pool_resource
        {
        private:
            virtual void* do_allocate(std::size_t bytes, std::size_t alignment) override
            {
                return nullptr;
            }

            virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override
            {

            }

            virtual bool do_is_equal(const std::pmr::memory_resource& other) const noexcept
            {
                return this == &other;
            }
        };

        struct unlimited_pool_resource :
            public memblock_pool_resource
        {
        private:
            virtual void* do_allocate(std::size_t bytes, std::size_t alignment) override
            {
                return nullptr;
            }

            virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override
            {

            }

            virtual bool do_is_equal(const std::pmr::memory_resource& other) const noexcept
            {
                return this == &other;
            }
        };

        struct mem_pool_resource :
            public memblock_pool_resource
        {
        private:
            virtual void* do_allocate(std::size_t bytes, std::size_t alignment) override
            {
                if (bytes > 84 * PageSize)
                    return unlimited.allocate(bytes, alignment);

                return limited.allocate(bytes, alignment);
            }

            virtual void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override
            {
                if (bytes > 84 * PageSize)
                    unlimited.deallocate(p, bytes, alignment);
            }

            virtual bool do_is_equal(const std::pmr::memory_resource& other) const noexcept
            {
                return this == &other;
            }

            limited_pool_resource limited;
            unlimited_pool_resource unlimited;
        };
    }

    inline namespace dependent_mem_block_pool
    {
        template<class MemBlockType, class HyperMemBlockType>
        struct DependentMemBlockPool
        {
            using MemBlockAlignedPtr = AlignedPtr<MemBlockType>;
            using HyperMemBlockAlignedPtr = AlignedPtr<HyperMemBlockType>;

        private:
            static constexpr size_t NodeArrayCnt = sizeof(HyperMemBlockType) / sizeof(MemBlockType);

            union alignas(alignof(MemBlockType)) Node
            {
                MemBlockType memBlock;
                Node* prevNode;
            };

            using NodeArray = std::array<Node, NodeArrayCnt>;
            using HyperNodePtrArray = std::vector<HyperMemBlockAlignedPtr>;

        public:
            DependentMemBlockPool()
            {
            }

            ~DependentMemBlockPool()
            {
                for (auto& hyperNode : hyperNodes)
                {
                    delete hyperNode.ptr;
                }
            }

            MemBlockAlignedPtr allocChunk()
            {
                if (!stack && !grow())
                    return MemBlockAlignedPtr{ .ptr = nullptr };

                MemBlockAlignedPtr top{ .ptr = &stack->memBlock };
                stack = stack->prevNode;

                return top;
            }

            void freeChunk(MemBlockAlignedPtr alignedPtr)
            {
                MemBlockAlignedPtr top = alignedPtr;
                top.ptr = &stack->memBlock;
                stack = force_ptr_cast<Node>(top.ptr);
            }

        private:
            bool grow()
            {
                HyperMemBlockAlignedPtr newHyperMemBlock{ .ptr = new HyperMemBlockType };
                if (!newHyperMemBlock.ptr)
                    return false;

                initNodes(newHyperMemBlock);
                hyperNodes.push_back(newHyperMemBlock);

                return false;
            }

            void initNodes(HyperMemBlockAlignedPtr hyperMemBlock)
            {
                if (hyperMemBlock.ptr == nullptr)
                {
                    assert(hyperMemBlock.ptr != nullptr);

                    return;
                }

                NodeArray& nodes = *force_ptr_cast<NodeArray>(hyperMemBlock.ptr);

                Node* prevNode = nullptr;

                for (size_t i = 0; i < NodeArrayCnt; ++i)
                {
                    Node& newStack = nodes[i];
                    newStack.prevNode = prevNode;
                    prevNode = &newStack;
                }

                stack = prevNode;
            }

            Node* stack = nullptr;
            HyperNodePtrArray hyperNodes;
        };

        inline namespace instanced
        {
            using PagePool = DependentMemBlockPool<Page, AllocUnit>;
            using CacheLinePool = DependentMemBlockPool<CacheLine, Page>;
        }
    }

    void init(size_t nArena = 4);
}
