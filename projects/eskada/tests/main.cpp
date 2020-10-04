#include "eskada.h"

#include "boost/ut.hpp"

int main()
{
    using namespace boost::ut;
    using namespace eskada;

    using DeqType = EventDeqBase<int, 10>;

    test("rollbackTask") = [] {
        should("Success-Always") = [] {
            DeqType::RawType raw;
            DeqType deq(&raw);
            int task = 1;
            size_t idx = 0;
            DeqType::DescType desc;
            desc.oldTask = nullptr;
            desc.newTask = &task;

            raw.storeTask(idx, desc.newTask);
            deq.rollbackTask(&desc, idx);
            int* result = raw.loadTask(idx);

            expect(result == nullptr);
        };
    };

    test("tryCommitTask") = [] {
        DeqType::RawType raw;
        DeqType deq(&raw);
        int task = 1;
        size_t idx = 0;
        DeqType::DescType desc;
        desc.oldTask = nullptr;
        desc.newTask = &task;

        should("Success-CAS") = [&] {
            raw.storeTask(idx, desc.oldTask);
            bool result = deq.tryCommitTask(&desc, idx);

            expect(result == true);

            int* target = raw.loadTask(idx);

            expect(target == desc.newTask);
        };

        should("Fail") = [&] {
            int tmp = 2;

            raw.storeTask(idx, &tmp);
            bool result = deq.tryCommitTask(&desc, idx);

            expect(result == false);

            int* target = raw.loadTask(idx);

            expect(target == &tmp);
        };
    };

    return 0;
}
