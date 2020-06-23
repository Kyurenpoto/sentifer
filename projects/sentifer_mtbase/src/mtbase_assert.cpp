#include "../include/sentifer_mtbase/details/mtbase_assert.h"

#include <cstdlib>

using namespace mtbase;

void do_assert(bool condition)
{
    if (!condition)
        std::abort();
}
