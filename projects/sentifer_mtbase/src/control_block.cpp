#include "../include/sentifer_mtbase/details/control_block.h"

#include "../include/sentifer_mtbase/details/scheduler_restriction.h"

using namespace mtbase;

void control_block::reset()
    noexcept
{
    tickBeginOccupying = clock_t::getSteadyTick();
    tickFlushing = steady_tick{};
    cntFlushed = 0;
}

void control_block::release()
    noexcept
{
    sched = nullptr;
}

void control_block::recordTickFlushing(
    const steady_tick tickBegin,
    const steady_tick tickEnd)
    noexcept
{
    tickFlushing += (tickEnd - tickBegin);
}

void control_block::recordCountFlushing()
    noexcept
{
    ++cntFlushed;
}

void control_block::recordCountExpired(
    const scheduler_restriction& restriction)
    noexcept
{
    cntFlushed = restriction.MAX_FLUSH_COUNT;
}

[[nodiscard]]
bool control_block::checkExpiredTick(
    const scheduler_restriction& restriction,
    const steady_tick tickEnd)
    const noexcept
{
    return tickFlushing > restriction.MAX_OCCUPY_TICK_FLUSHING ||
        tickEnd - tickBeginOccupying > restriction.MAX_OCCUPY_TICK;
}

[[nodiscard]]
bool control_block::checkExpiredCount(
    const scheduler_restriction& restriction)
    const noexcept
{
    return cntFlushed >= restriction.MAX_FLUSH_COUNT;
}

[[nodiscard]]
bool control_block::checkExpired(
    const scheduler_restriction& restriction,
    const steady_tick tickEnd)
    const noexcept
{
    return checkExpiredCount(restriction) ||
        checkExpiredTick(restriction, tickEnd);
}
