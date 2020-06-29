#pragma once

#include "../scheduler.hpp"

namespace mtbase
{
    struct control_block;

    struct transaction_scheduler final :
        public scheduler
    {
    };
}
