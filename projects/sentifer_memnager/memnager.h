// inspired from https://www.qt.io/blog/a-fast-and-thread-safe-pool-allocator-for-qt-part-1

#pragma once

#include <cassert>

#define assert_msg(exp, msg) assert(((void)msg, exp))

#include <cstddef>
#include <array>
#include <variant>

namespace memnager
{
    template<size_t power>
    struct AlignedPtr final
    {
        void* ptr = nullptr;

        bool isValid()
        {
            return reinterpret_cast<size_t>(ptr) % (1 << power) == 0;
        }
    };

    template<size_t BlockSize>
    struct MemBlock
    {
        std::array<std::byte, BlockSize> memory;
    };

    namespace
    {
        static constexpr size_t PageSize = 4096;
        static constexpr size_t CacheLineSize = 64;

        using Page = MemBlock<PageSize>;
        using CacheLine = MemBlock<CacheLineSize>;
    }

    class CacheLinePool
    {
        static constexpr size_t NodeArrayCnt = PageSize / CacheLineSize;

        using CacheAlignedPtr = AlignedPtr<CacheLineSize>;

        using Node = std::variant<CacheLine, CacheAlignedPtr>;
        using NodeArray = std::array<Node, NodeArrayCnt>;
        using NodeList = std::variant<Page, NodeArray>;

    public:
        CacheLinePool(Page* page)
        {
            if (page == nullptr)
            {
                assert(page != nullptr);

                return;
            }

            nodes = static_cast<NodeList*>(static_cast<void*>(page));

            Node* prevNode = nullptr;

            for (size_t i = 0; i < NodeArrayCnt; ++i)
            {
                Node& newStack = std::get<NodeArray>(*nodes)[i];
                CacheAlignedPtr& link = std::get<CacheAlignedPtr>(newStack);
                link.ptr = static_cast<void*>(prevNode);
                prevNode = static_cast<Node*>(link.ptr);
            }

            stack = prevNode;
        }

        CacheAlignedPtr allocChunk()
        {
            if (!stack)
                return CacheAlignedPtr{};

            Node* top = stack;
            CacheAlignedPtr& link = std::get<CacheAlignedPtr>(*top);
            stack = static_cast<Node*>(link.ptr);

            CacheAlignedPtr chunk;
            chunk.ptr = static_cast<void*>(top);

            return chunk;
        }

        void freeChunk(CacheAlignedPtr ptr)
        {
            Node* top = static_cast<Node*>(ptr.ptr);
            CacheAlignedPtr& link = std::get<CacheAlignedPtr>(*top);
            link.ptr = static_cast<void*>(stack);
            stack = top;
        }

    private:
        Node* stack = nullptr;
        NodeList *nodes = nullptr;
    };

    void init(size_t nArena = 4);
}
