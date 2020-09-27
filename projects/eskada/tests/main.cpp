#include "eskada.h"

#include "boost/ut.hpp"

int main()
{
    using namespace boost::ut;
    using namespace eskada;

    using DeqType = EventDeqBase<int, 10>;

    test("rollback") = [] {
        should("Success-Always") = [] {
            DeqType::RawType raw;
            DeqType deq(&raw);
            int task = 1;
            int idx = 0;
            DeqType::DescType desc;
            desc.oldTask = nullptr;
            desc.newTask = &task;

            deq.storeTask(idx, desc.newTask);
            deq.rollbackTask(&desc);
            int* result = deq.loadTask(idx);

            expect(result == nullptr);
        };
    };

    test("tryCommitTask") = [] {
        DeqType::RawType raw;
        DeqType deq(&raw);
        int task = 1;
        int idx = 0;
        DeqType::DescType desc;
        desc.oldTask = nullptr;
        desc.newTask = &task;

        should("Success-CAS") = [&] {
            deq.storeTask(idx, desc.oldTask);
            bool result = deq.tryCommitTask(&desc);

            expect(result == true);

            int* target = deq.loadTask(idx);

            expect(target == desc.newTask);
        };

        should("Success-Already") = [&] {
            deq.storeTask(idx, desc.newTask);
            bool result = deq.tryCommitTask(&desc);

            expect(result == true);

            int* target = deq.loadTask(idx);

            expect(target == desc.newTask);
        };

        should("Fail") = [&] {
            int tmp = 2;

            deq.storeTask(idx, &tmp);
            bool result = deq.tryCommitTask(&desc);

            expect(result == false);

            int* target = deq.loadTask(idx);

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

    test("tryCommitIndex") = [] {
        should("Success-Already") = [] {
            DeqType::RawType raw;
            DeqType deq(&raw);
            DeqType::DescType desc;
            desc.oldIndex.front = 0;
            desc.oldIndex.back = 2;
            desc.newIndex.front = 0;
            desc.newIndex.back = 1;

            bool result = deq.tryCommitIndex(&desc);
            expect(result == true);

            DeqType::IndexType* target = deq.loadIndex();
            expect(target != nullptr && *target == desc.newIndex);
        };

        should("Fail-Different") = [] {
            DeqType::RawType raw;
            DeqType deq(&raw);
            DeqType::DescType desc;
            desc.oldIndex.front = 0;
            desc.oldIndex.back = 2;
            desc.newIndex.front = 0;
            desc.newIndex.back = 3;

            bool result = deq.tryCommitIndex(&desc);
            expect(result == false);

            DeqType::IndexType* target = deq.loadIndex();
            expect(target != nullptr && *target != desc.newIndex);
        };

        should("Success-CAS") = [] {
            DeqType::RawType raw;
            DeqType deq(&raw);
            DeqType::DescType desc;
            desc.oldIndex.front = 0;
            desc.oldIndex.back = 1;
            desc.newIndex.front = 0;
            desc.newIndex.back = 2;

            bool result = deq.tryCommitIndex(&desc);
            expect(result == true);

            DeqType::IndexType* target = deq.loadIndex();
            expect(target != nullptr && *target == desc.newIndex);
        };

        should("Fail-CAS") = [] {
            struct tmp :
                DeqType
            {
                tmp(RawType* base) :
                    DeqType(base)
                {}

                bool casIndex(IndexType*& oldIndex, IndexType* const newIndex)
                    override
                {
                    IndexType currIndex = *DeqType::loadIndex();
                    currIndex.front = 9;
                    IndexType* updateIndex = create(currIndex);
                    DeqType::storeIndex(updateIndex);

                    return DeqType::casIndex(oldIndex, newIndex);
                }
            };

            DeqType::RawType raw;
            tmp deq(&raw);
            DeqType::DescType desc;
            desc.oldIndex.front = 0;
            desc.oldIndex.back = 1;
            desc.newIndex.front = 0;
            desc.newIndex.back = 2;

            bool result = deq.tryCommitIndex(&desc);
            expect(result == false);

            DeqType::IndexType* target = deq.loadIndex();
            expect(target != nullptr && *target != desc.newIndex);
        };
    };

    return 0;
}
