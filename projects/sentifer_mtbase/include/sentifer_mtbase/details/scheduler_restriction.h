#pragma once

#include "clocks.hpp"

namespace mtbase
{
    struct scheduler_restriction final
    {
        const steady_tick MAX_OCCUPY_TICK;
        const steady_tick MAX_OCCUPY_TICK_FLUSHING;
        const size_t MAX_FLUSH_COUNT;
        const size_t MAX_FLUSH_COUNT_AT_ONCE;
    };
}
