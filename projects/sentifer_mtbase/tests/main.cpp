#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include "sentifer_mtbase/mtbase.h"

int main(int argc, char** argv) {
    doctest::Context context;

    context.setOption("abort-after", 5);
    context.setOption("order-by", "file");

    context.applyCommandLine(argc, argv);

    context.setOption("no-breaks", true);

    int res = context.run();

    if (context.shouldExit())
        return res;

    context.clearFilters();

    int client_stuff_return_code = EXIT_SUCCESS;

    return res + client_stuff_return_code;
}
