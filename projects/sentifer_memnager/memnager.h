// inspired from https://www.qt.io/blog/a-fast-and-thread-safe-pool-allocator-for-qt-part-1

#pragma once

#include <cassert>

#define assert_msg(exp, msg) assert(((void)msg, exp))

#include <cstddef>
#include <array>
#include <memory_resource>

#include "wil/resource.h"

namespace memnager
{
    template<size_t Alignment>
    struct AlignedPtr final
    {
        void* ptr = nullptr;

        bool isValid()
        {
            return reinterpret_cast<size_t>(ptr) % Alignment == 0;
        }
    };

    namespace
    {
        constexpr size_t MaxAlign = 8192;
    }

    template<size_t BlockSize>
    struct alignas(BlockSize < MaxAlign ? BlockSize : MaxAlign) MemBlock
    {
        std::array<std::byte, BlockSize> memory;
    };

    namespace
    {
        constexpr size_t PageSize = 4096;
        constexpr size_t CacheLineSize = 64;
        constexpr size_t AllocUnitSize = PageSize * 1024;

        using Page = MemBlock<PageSize>;
        using CacheLine = MemBlock<CacheLineSize>;
        using AllocUnit = MemBlock<AllocUnitSize>;

        using PageAlignedPtr = AlignedPtr<PageSize>;
        using CacheAlignedPtr = AlignedPtr<CacheLineSize>;
        using UnitAlignedPtr = AlignedPtr<AllocUnitSize>;
    }

    struct memblock_resource_pool :
        public std::pmr::memory_resource
    {
        virtual void release() = 0;
        virtual memblock_resource_pool* upstream_resource() const = 0;
    };

    template<class MemBlockType, class DependentMemBlockType>
    struct DependentMemBlockPool
    {
        using MemBlockAlignedPtr = AlignedPtr<alignof(MemBlockType)>;

    private:
        static constexpr size_t NodeArrayCnt = sizeof(DependentMemBlockType) / sizeof(MemBlockType);

        union alignas(alignof(MemBlockType)) Node
        {
            MemBlockType cacheLine;
            MemBlockAlignedPtr alignedPtr;
        };
        using NodeArray = std::array<Node, NodeArrayCnt>;
        union alignas(alignof(DependentMemBlockType)) NodeList
        {
            DependentMemBlockType page;
            NodeArray nodeArray;
        };

    public:
        DependentMemBlockPool(DependentMemBlockType* memBlock)
        {
            initNodes(memBlock);
        }

        MemBlockAlignedPtr allocChunk()
        {
            if (!stack)
                return MemBlockAlignedPtr{ .ptr = nullptr };

            Node* top = stack;
            stack = static_cast<Node*>(top->alignedPtr.ptr);

            return MemBlockAlignedPtr{ .ptr = static_cast<void*>(top) };
        }

        void freeChunk(MemBlockAlignedPtr alignedPtr)
        {
            Node* top = static_cast<Node*>(alignedPtr.ptr);
            top->alignedPtr.ptr = static_cast<void*>(stack);
            stack = top;
        }

    private:
        void initNodes(DependentMemBlockType* memBlock)
        {
            if (memBlock == nullptr)
            {
                assert(memBlock != nullptr);

                return;
            }

            nodes = static_cast<NodeList*>(static_cast<void*>(memBlock));

            Node* prevNode = nullptr;

            for (size_t i = 0; i < NodeArrayCnt; ++i)
            {
                Node& newStack = nodes->nodeArray[i];
                newStack.alignedPtr.ptr = static_cast<void*>(prevNode);
                prevNode = static_cast<Node*>(newStack.alignedPtr.ptr);
            }

            stack = prevNode;
        }

        Node* stack = nullptr;
        NodeList* nodes = nullptr;
    };

    namespace
    {
        using PagePool = DependentMemBlockPool<Page, AllocUnit>;
        using CacheLinePool = DependentMemBlockPool<CacheLine, Page>;
    }

    void init(size_t nArena = 4);
}
