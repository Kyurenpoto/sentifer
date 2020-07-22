#include <fmt/core.h>

#include "sentifer_mtbase/mtbase.h"

void warmup()
{
    int x = 0x12345678;
    for (int i = 0; i < 1'000'000'000; ++i)
        x ^= (x * x);
}

int main()
{
    fmt::print("Warm up process...\n");
    warmup();
    fmt::print("Warm up complete\n");

    return 0;
}
