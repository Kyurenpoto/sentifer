#include <string>
#include <tuple>
#include <functional>

#include "eskada.h"

#include "boost/ut.hpp"

void testEventDeqBase()
{
    using namespace boost::ut;
    using namespace eskada;

    using DeqType = EventDeqBase<int, 10>;

    test("isValidIndex") = [] {
        DeqType::RawType raw;
        DeqType deq(&raw);

        for (auto& args : std::vector{
            std::make_tuple("PushBack", EventDeqOp::PUSH_BACK,
            DeqType::IndexType{}, DeqType::IndexType{.front = 0, .back = 2}),
            std::make_tuple("PushFront", EventDeqOp::PUSH_FRONT,
            DeqType::IndexType{}, DeqType::IndexType{.front = 11, .back = 1}),
            std::make_tuple("PopBack", EventDeqOp::POP_BACK,
            DeqType::IndexType{.front = 0, .back = 2}, DeqType::IndexType{}),
            std::make_tuple("PopFront", EventDeqOp::POP_FRONT,
            DeqType::IndexType{.front = 11, .back = 1}, DeqType::IndexType{}) })
        {
            auto& [name, op, prev, next] = args;

            should(std::string("Success-") + name) =
                [&, op = op, prev = prev, next = next] {
                DeqType::DescType desc;
                desc.op = op;

                DeqType::IndexType index = prev;
                raw.storeIndex(&index);

                DeqType::IndexType* oldIndex = nullptr;
                DeqType::IndexType oldIndexVal;
                DeqType::IndexType newIndexVal;
                bool result =
                    deq.isValidIndex(desc, oldIndex, oldIndexVal, newIndexVal);

                expect(result);
                expect(oldIndex == &index);
                expect(oldIndexVal.front == prev.front &&
                    oldIndexVal.back == prev.back);
                expect(newIndexVal.front == next.front &&
                    newIndexVal.back == next.back);
            };
        }

        for (auto& args : std::vector{
            std::make_tuple("PushBack", EventDeqOp::PUSH_BACK),
            std::make_tuple("PushFront", EventDeqOp::PUSH_FRONT),
            std::make_tuple("PopBack", EventDeqOp::POP_BACK),
            std::make_tuple("PopFront", EventDeqOp::POP_FRONT) })
        {
            auto& [name, op] = args;

            should(std::string("Fail-Old-") + name) = [&, op = op] {
                DeqType::DescType desc;
                desc.op = op;

                DeqType::IndexType index{ .front = 0, .back = 0 };
                raw.storeIndex(&index);

                DeqType::IndexType* oldIndex = nullptr;
                DeqType::IndexType oldIndexVal;
                DeqType::IndexType newIndexVal;
                bool result =
                    deq.isValidIndex(desc, oldIndex, oldIndexVal, newIndexVal);

                expect(!result);
            };
        }

        for (auto& args : std::vector{
            std::make_tuple("PushBack", EventDeqOp::PUSH_BACK,
            DeqType::IndexType{.front = 1, .back = 0}),
            std::make_tuple("PushFront", EventDeqOp::PUSH_FRONT,
            DeqType::IndexType{.front = 1, .back = 0}),
            std::make_tuple("PopBack", EventDeqOp::POP_BACK,
            DeqType::IndexType{}),
            std::make_tuple("PopFront", EventDeqOp::POP_FRONT,
            DeqType::IndexType{}) })
        {
            auto& [name, op, prev] = args;

            should(std::string("Fail-New-") + name) =
                [&, op = op, prev = prev] {
                DeqType::DescType desc;
                desc.op = op;

                DeqType::IndexType index = prev;
                raw.storeIndex(&index);

                DeqType::IndexType* oldIndex = nullptr;
                DeqType::IndexType oldIndexVal;
                DeqType::IndexType newIndexVal;
                bool result =
                    deq.isValidIndex(desc, oldIndex, oldIndexVal, newIndexVal);

                expect(!result);
            };
        }
    };

    test("tryCommitTask") = [] {
        DeqType::RawType raw;
        DeqType deq(&raw);
        int task = 1;
        size_t idx = 0;
        DeqType::DescType desc;

        should("Success-CAS") = [&] {
            desc.newTask = &task;

            raw.storeTask(idx, desc.oldTask);
            bool result = deq.tryCommitTask(desc, idx);

            expect(result);

            int* target = raw.loadTask(idx);

            expect(target == desc.newTask);
        };

        should("Fail-CAS") = [&] {
            desc.newTask = &task;

            int tmp = 2;

            raw.storeTask(idx, &tmp);
            bool result = deq.tryCommitTask(desc, idx);

            expect(!result);

            int* target = raw.loadTask(idx);

            expect(target == &tmp);
        };

        should("Fail-Desc") = [&] {
            desc.newTask = nullptr;

            raw.storeTask(idx, nullptr);
            bool result = deq.tryCommitTask(desc, idx);

            expect(!result);
        };
    };

    test("tryCommitIndex") = [] {
        DeqType::RawType raw;
        DeqType deq(&raw);

        should("Success") = [&]() {
            DeqType::IndexType index;
            raw.storeIndex(&index);

            DeqType::IndexType tmp;
            DeqType::IndexType* ptr = &tmp;
            deq.updateTLS(ptr);

            DeqType::IndexType* oldIndex = raw.loadIndex();
            DeqType::IndexType newIndexVal{ .front = 0, .back = 2 };
            bool result = deq.tryCommitIndex(oldIndex, newIndexVal);

            ThreadLocalStorage::index = nullptr;

            expect(result);

            DeqType::IndexType* newIndex = raw.loadIndex();
            expect(newIndex->front == newIndexVal.front &&
                newIndex->back == newIndexVal.back);
        };

        should("Fail") = [&]() {
            DeqType::IndexType index;
            raw.storeIndex(&index);

            DeqType::IndexType tmp;
            DeqType::IndexType* ptr = &tmp;
            deq.updateTLS(ptr);

            DeqType::IndexType otherIndex;
            DeqType::IndexType* oldIndex = &otherIndex;
            DeqType::IndexType newIndexVal{ .front = 0, .back = 2 };
            bool result = deq.tryCommitIndex(oldIndex, newIndexVal);

            ThreadLocalStorage::index = nullptr;

            expect(!result);
        };
    };

    test("rollbackCommits") = [] {
        DeqType::RawType raw;
        DeqType deq(&raw);
        size_t idx = 0;
        DeqType::DescType desc;
        desc.oldTask = nullptr;

        should("Success") = [&] {
            DeqType::IndexType index;
            ThreadLocalStorage::index = &index;

            deq.rollbackCommits(desc, idx);

            void* addr = ThreadLocalStorage::index;
            ThreadLocalStorage::index = nullptr;

            expect(addr == &index);
        };
    };

    test("updateTLS") = [] {
        DeqType::RawType raw;
        DeqType deq(&raw);

        should("Success") = [&] {
            ThreadLocalStorage::index = nullptr;

            DeqType::IndexType index;
            DeqType::IndexType* ptr = &index;
            deq.updateTLS(ptr);

            void* addr = ThreadLocalStorage::index;
            ThreadLocalStorage::index = nullptr;

            expect(addr == &index);
        };
    };
}

void testEventDeqLF()
{
    using namespace boost::ut;
    using namespace eskada;

    using DeqType = EventDeqLF<int, 10>;

    test("doLFOp") = [] {
        DeqType::RawType raw;
        DeqType::BaseType base(&raw);
        DeqType deq(&base);
        int x = 2;

        for (auto& args : std::vector{
            std::make_tuple(
                "",
                std::function{[&](DeqType::DescType& desc)
                {
                    deq.doLFOp(desc);
                }
                }),
            std::make_tuple(
                "-Simulated",
                std::function{[&](DeqType::DescType& desc)
                {
                    if (desc.state != EventDeqState::PENDING)
                        return;

                    DeqType::IndexType* oldIndex = nullptr;
                    DeqType::IndexType oldIndexVal;
                    DeqType::IndexType newIndexVal;
                    if (!base.isValidIndex(desc, oldIndex, oldIndexVal, newIndexVal))
                    {
                        desc.state = EventDeqState::FAILED;

                        return;
                    }

                    size_t idx = oldIndexVal.targetIndex(desc.op);
                    if (!base.tryCommitTask(desc, idx))
                    {
                        desc.state = EventDeqState::FAILED;

                        return;
                    }

                    if (!base.tryCommitIndex(oldIndex, newIndexVal))
                    {
                        base.rollbackCommits(desc, idx);

                        desc.state = EventDeqState::FAILED;

                        return;
                    }

                    base.updateTLS(oldIndex);

                    desc.state = EventDeqState::SUCCESS;
                }
                }) })
        {
            auto& [name, f] = args;

            should(std::string{ "Success" } + name) = [&, f = f] {
                DeqType::IndexType indexInit;
                raw.storeIndex(&indexInit);

                DeqType::IndexType indexTmp;
                ThreadLocalStorage::index = &indexTmp;

                DeqType::DescType desc;
                desc.op = EventDeqOp::PUSH_BACK;
                desc.newTask = &x;
                f(desc);

                ThreadLocalStorage::index = nullptr;

                expect(desc.state == EventDeqState::SUCCESS);

                DeqType::IndexType* index = raw.loadIndex();
                raw.storeIndex(nullptr);

                expect(index->front == 0 && index->back == 2);

                size_t idx = indexInit.targetIndex(desc.op);
                int* task = raw.loadTask(idx);
                raw.storeTask(idx, nullptr);

                expect(task == &x);
            };
        }

        int y = 2;
        DeqType::IndexType idxOther;

        for (auto& args : std::vector{
            std::make_tuple(
                "CAS-Task",
                std::function{[&](DeqType::DescType& desc)
                {
                    if (desc.state != EventDeqState::PENDING)
                        return;

                    DeqType::IndexType* oldIndex = nullptr;
                    DeqType::IndexType oldIndexVal;
                    DeqType::IndexType newIndexVal;
                    if (!base.isValidIndex(desc, oldIndex, oldIndexVal, newIndexVal))
                    {
                        desc.state = EventDeqState::FAILED;

                        return;
                    }

                    size_t idx = oldIndexVal.targetIndex(desc.op);
                    raw.storeTask(idx, &y);
                    if (!base.tryCommitTask(desc, idx))
                    {
                        desc.state = EventDeqState::FAILED;

                        return;
                    }

                    desc.state = EventDeqState::SUCCESS;
                }
                }),
            std::make_tuple(
                "CAS-Index",
                std::function{[&](DeqType::DescType& desc)
                {
                    if (desc.state != EventDeqState::PENDING)
                        return;

                    DeqType::IndexType* oldIndex = nullptr;
                    DeqType::IndexType oldIndexVal;
                    DeqType::IndexType newIndexVal;
                    if (!base.isValidIndex(desc, oldIndex, oldIndexVal, newIndexVal))
                    {
                        desc.state = EventDeqState::FAILED;

                        return;
                    }

                    size_t idx = oldIndexVal.targetIndex(desc.op);
                    if (!base.tryCommitTask(desc, idx))
                    {
                        desc.state = EventDeqState::FAILED;

                        return;
                    }

                    raw.storeIndex(&idxOther);
                    if (!base.tryCommitIndex(oldIndex, newIndexVal))
                    {
                        base.rollbackCommits(desc, idx);

                        desc.state = EventDeqState::FAILED;

                        return;
                    }

                    base.updateTLS(oldIndex);

                    desc.state = EventDeqState::SUCCESS;
                }
                }) })
        {
            auto& [name, f] = args;

            should(std::string{ "Fail-" } + name) = [&, f = f] {
                DeqType::IndexType indexInit;
                raw.storeIndex(&indexInit);

                DeqType::IndexType indexTmp;
                ThreadLocalStorage::index = &indexTmp;

                DeqType::DescType desc;
                desc.op = EventDeqOp::PUSH_BACK;
                desc.newTask = &x;
                f(desc);

                ThreadLocalStorage::index = nullptr;

                expect(desc.state == EventDeqState::FAILED);

                DeqType::IndexType* index = raw.loadIndex();
                raw.storeIndex(nullptr);

                expect(index->front == 0 && index->back == 1);

                size_t idx = indexInit.targetIndex(desc.op);
                int* task = raw.loadTask(idx);
                raw.storeTask(idx, nullptr);

                expect(task != &x);
            };
        }
    };
}

int main()
{
    testEventDeqBase();
    testEventDeqLF();

    return 0;
}
