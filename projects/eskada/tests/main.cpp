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

        should("Success-Already") = [&] {
            raw.storeTask(idx, desc.newTask);
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

    test("create") = [] {
        struct empty_res :
            std::pmr::memory_resource
        {
        private:
            void* do_allocate(size_t, size_t)
                override
            {
                throw std::bad_alloc{};
            }

            void do_deallocate(void*, size_t, size_t)
                override
            {

            }

            [[nodiscard]]
            bool do_is_equal(const std::pmr::memory_resource&)
                const noexcept override
            {
                return false;
            }
        };

        should("Other-Type") = [] {
            DeqType::RawType raw;
            DeqType deq(&raw);
            int x = 0;
            auto* result = deq.create(x);

            expect(result == nullptr);
        };

        should("Index-Success") = [] {
            DeqType::RawType raw;
            DeqType deq(&raw);
            DeqType::IndexType x;
            auto* result = deq.create(x);

            expect(result != nullptr);
        };

        should("Index-Throw") = [] {
            empty_res r;
            DeqType::RawType raw;
            DeqType deq(&raw, &r);
            DeqType::IndexType x;
            auto* result = deq.create(x);

            expect(result == nullptr);
        };

        should("Desc-Success") = [] {
            DeqType::RawType raw;
            DeqType deq(&raw);
            DeqType::DescType x;
            auto* result = deq.create(x);

            expect(result != nullptr);
        };

        should("Desc-Throw") = [] {
            empty_res r;
            DeqType::RawType raw;
            DeqType deq(&raw, &r);
            DeqType::DescType x;
            auto* result = deq.create(x);

            expect(result == nullptr);
        };
    };

    test("destroy") = [] {
        should("Other-Type") = [] {
            DeqType::RawType raw;
            DeqType deq(&raw);
            int x = 0;
            int* result = &x;
            deq.destroy(result);

            expect(result != nullptr);
        };

        should("Index-Success") = [] {
            DeqType::RawType raw;
            DeqType deq(&raw);
            DeqType::IndexType x;
            auto* result = deq.create(x);
            deq.destroy(result);

            expect(result == nullptr);
        };

        should("Desc-Success") = [] {
            DeqType::RawType raw;
            DeqType deq(&raw);
            DeqType::DescType x;
            auto* result = deq.create(x);
            deq.destroy(result);

            expect(result == nullptr);
        };
    };

    return 0;
}
