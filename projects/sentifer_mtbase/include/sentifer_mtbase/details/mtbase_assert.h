#pragma once

#include <cstdlib>

namespace mtbase
{
    void do_assert(bool condition)
    {
        if (!condition)
            std::abort();
    }

#define MTBASE_ASSERT(condition) do_assert(condition)
}
