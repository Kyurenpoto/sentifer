#pragma once

namespace mtbase
{
    void do_assert(bool condition);

#define MTBASE_ASSERT(condition) do_assert(condition)
}
