#include "../include/sentifer_mtbase/details/mtbase_assert.h"

#include <cstdlib>

void mtbase::do_assert(bool condition)
{
    if (!condition)
        std::abort();
}
