#pragma once

namespace mtbase
{
    namespace details
    {
        void do_assert(bool condition);
    }

#define MTBASE_ASSERT(condition) details::do_assert(condition)
}
