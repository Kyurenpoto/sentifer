// inspired from https://www.qt.io/blog/a-fast-and-thread-safe-pool-allocator-for-qt-part-1

#pragma once

#include <cassert>

#define assert_msg(exp, msg) assert(((void)msg, exp))

#include <cstddef>
#include <array>
#include <vector>
#include <memory_resource>

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
    }

    inline namespace aligned_ptr
    {
        template<class MemBlockType>
        struct AlignedPtr final
        {
            MemBlockType* ptr = nullptr;

            bool isValid()
            {
                return reinterpret_cast<size_t>(ptr) % alignof(MemBlockType) == 0;
            }
        };

        inline namespace instanced_aligned_ptr
        {
            using PageAlignedPtr = AlignedPtr<Page>;
            using CacheAlignedPtr = AlignedPtr<CacheLine>;
            using UnitAlignedPtr = AlignedPtr<AllocUnit>;
        }
    }

    struct memblock_resource_pool :
        public std::pmr::memory_resource
    {
        virtual void release() = 0;
        virtual memblock_resource_pool* upstream_resource() const = 0;
    };

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

    inline namespace
    {
        using PagePool = DependentMemBlockPool<Page, AllocUnit>;
        using CacheLinePool = DependentMemBlockPool<CacheLine, Page>;
    }

    void init(size_t nArena = 4);
}
