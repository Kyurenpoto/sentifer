// inspired from https://www.qt.io/blog/a-fast-and-thread-safe-pool-allocator-for-qt-part-1

#pragma once

namespace memnager
{
    void init(size_t nArena = 4);
}
