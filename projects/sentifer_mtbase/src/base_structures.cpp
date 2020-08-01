#include "../include/sentifer_mtbase/details/base_structures.hpp"

using namespace mtbase;

#pragma region task_storage__descriptor

[[nodiscard]]
task_storage::descriptor task_storage::descriptor::copied(
    const index_t& oldIndexLoad,
    const index_t& newIndexLoad,
    task_t* const oldTaskLoad)
    const noexcept
{
    return descriptor
    {
        .phase = phase,
        .op = op,
        .oldTask = oldTaskLoad,
        .newTask = newTask,
        .oldIndex = oldIndexLoad,
        .newIndex = newIndexLoad
    };
}

[[nodiscard]]
task_storage::descriptor task_storage::descriptor::rollbacked(
    const index_t& oldIndexLoad,
    const index_t& newIndexLoad,
    task_t* const oldTaskLoad)
    const noexcept
{
    return descriptor
    {
        .phase = descriptor::PHASE::RESERVE,
        .op = op,
        .oldTask = oldTaskLoad,
        .newTask = newTask,
        .oldIndex = oldIndexLoad,
        .newIndex = newIndexLoad
    };
}

[[nodiscard]]
task_storage::descriptor task_storage::descriptor::completed()
    const noexcept
{
    return descriptor
    {
        .phase = descriptor::PHASE::COMPLETE,
        .op = op,
        .oldTask = oldTask
    };
}

[[nodiscard]]
task_storage::descriptor task_storage::descriptor::failed()
    const noexcept
{
    return descriptor
    {
        .phase = descriptor::PHASE::FAIL,
        .op = op,
        .oldTask = oldTask
    };
}

#pragma endregion task_storage__descriptor

template<class T>
bool tryEfficientCAS(
    std::atomic<T>& target,
    T& expected,
    T const desired)
    noexcept
{
    return target.compare_exchange_strong(expected, desired,
        std::memory_order_acq_rel, std::memory_order_acquire);
}

#pragma region task_storage

[[nodiscard]]
bool task_storage::push_front(task_t* task)
{
    descriptor* const desc = createDesc(task, OP::PUSH_FRONT);

    if (desc == nullptr)
        return false;

    bool result = (desc->phase == descriptor::PHASE::COMPLETE);
    destroyDesc(desc);

    return result;
}

[[nodiscard]]
bool task_storage::push_back(task_t* task)
{
    descriptor* const desc = createDesc(task, OP::PUSH_BACK);

    if (desc == nullptr)
        return false;

    bool result = (desc->phase == descriptor::PHASE::COMPLETE);
    destroyDesc(desc);

    return result;
}

[[nodiscard]]
task_t* task_storage::pop_front()
{
    descriptor* const desc = createDesc(nullptr, OP::POP_FRONT);

    if (desc == nullptr)
        return nullptr;

    task_t* result = (desc->phase == descriptor::PHASE::COMPLETE ?
        desc->oldTask : nullptr);
    destroyDesc(desc);

    return result;
}

[[nodiscard]]
task_t* task_storage::pop_back()
{
    descriptor* const desc = createDesc(nullptr, OP::POP_BACK);

    if (desc == nullptr)
        return nullptr;

    task_t* result = (desc->phase == descriptor::PHASE::COMPLETE ?
        desc->oldTask : nullptr);
    destroyDesc(desc);

    return result;
}

[[nodiscard]]
task_storage::descriptor* task_storage::createDesc(task_t* task, OP op)
{
    index_t oldIndex = *(index.load(std::memory_order_acquire));
    if (!isValidIndex(oldIndex, op))
        return nullptr;

    index_t newIndex = moveIndex(oldIndex, op);
    std::atomic<task_t*>& target = getElementRef(oldIndex, op);

    descriptor descVal
    {
        .phase = descriptor::PHASE::RESERVE,
        .op = op,
        .oldTask = target.load(std::memory_order_acquire),
        .newTask = task,
        .oldIndex = oldIndex,
        .newIndex = newIndex
    };
    descriptor* desc = new_desc(descVal);
    applyDesc(desc);
    
    if (desc != nullptr)
        releaseProgress(desc);

    return desc;
}

void task_storage::destroyDesc(descriptor* const desc)
{
    if (desc == nullptr)
        return;

    descriptor* oldDesc = desc;
    tryRegister(oldDesc, nullptr);

    delete_desc(desc);
}

[[nodiscard]]
task_storage::index_t* task_storage::new_index(const index_t& idx)
{
    return alloc.new_object<index_t>(idx);
}

void task_storage::delete_index(index_t* const idx)
{
    if (idx != nullptr)
        alloc.delete_object(idx);
}

[[nodiscard]]
task_storage::descriptor* task_storage::new_desc(const descriptor& desc)
{
    return alloc.new_object<descriptor>(desc);
}

[[nodiscard]]
task_storage::descriptor* task_storage::copy_desc(descriptor* const desc)
{
    if (desc == nullptr)
        return nullptr;

    descriptor copied =
        desc->copied(desc->oldIndex, desc->newIndex,
        getElementRef(desc->oldIndex, desc->op));

    return new_desc(copied);
}

void task_storage::delete_desc(descriptor* const desc)
{
    if (desc != nullptr)
        alloc.delete_object(desc);
}

void task_storage::applyDesc(descriptor*& desc)
{
    do
    {
        descriptor* helpDesc = nullptr;
        tryRegister(helpDesc, nullptr);
        helpRegistered(helpDesc);
        destroyDesc(helpDesc);
    } while (!trySetProgress(desc));

    for (size_t i = 0; i < MAX_RETRY; ++i)
        if (fast_path(desc))
            return;

    slow_path(desc);
}

bool task_storage::fast_path(descriptor*& desc)
{
    if (!tryCommit(desc))
        return false;

    completeDesc(desc);

    return true;
}

void task_storage::slow_path(descriptor*& desc)
{
    while (true)
    {
        descriptor* oldDesc = nullptr;
        if (tryRegister(oldDesc, desc))
            break;

        helpRegistered(oldDesc);
    }

    helpRegistered(desc);
}

void task_storage::helpRegistered(descriptor*& desc)
{
    while (desc != nullptr &&
        desc->phase == descriptor::PHASE::RESERVE &&
        !tryCommitWithRegistered(desc));

    while (desc != nullptr &&
        desc->phase == descriptor::PHASE::RESERVE)
    {
        descriptor* oldDesc = desc;
        descriptor completed = desc->completed();
        desc = new_desc(completed);
        renewRegistered(oldDesc, desc);
    }
}

[[nodiscard]]
bool task_storage::tryCommit(descriptor*& desc)
{
    if (!tryCommitTask(desc))
    {
        descriptor* oldDesc = refreshIndex(desc);
        destroyDesc(oldDesc);

        return false;
    }

    if (!tryCommitIndex(desc))
    {
        descriptor* oldDesc = rollbackTask(desc);
        destroyDesc(oldDesc);

        return false;
    }

    return true;
}

[[nodiscard]]
bool task_storage::tryCommitWithRegistered(descriptor*& desc)
{
    if (!tryCommitTask(desc))
    {
        descriptor* oldDesc = refreshIndex(desc);
        renewRegistered(oldDesc, desc);

        return false;
    }

    if (!tryCommitIndex(desc))
    {
        descriptor* oldDesc = rollbackTask(desc);
        renewRegistered(oldDesc, desc);

        return false;
    }

    return true;
}

[[nodiscard]]
task_storage::descriptor* task_storage::rollbackTask(descriptor*& desc)
{
    std::atomic<task_t*>& target = getElementRef(desc->oldIndex, desc->op);
    target.store(desc->oldTask, std::memory_order_release);

    return refreshIndex(desc);
}

[[nodiscard]]
task_storage::descriptor* task_storage::refreshIndex(descriptor*& desc)
{
    descriptor* oldDesc = desc;
    index_t oldIndex = *(index.load(std::memory_order_acquire));
    if (isValidIndex(oldIndex, oldDesc->op))
    {
        index_t newIndex = moveIndex(oldIndex, oldDesc->op);
        descriptor rollbacked =
            oldDesc->rollbacked(oldIndex, newIndex,
            getElementRef(oldIndex, oldDesc->op));
        desc = new_desc(rollbacked);
    }
    else
    {
        descriptor failed = oldDesc->failed();
        desc = new_desc(failed);
    }

    return oldDesc;
}

void task_storage::completeDesc(descriptor*& desc)
{
    descriptor* const oldDesc = desc;

    descriptor completed = oldDesc->completed();
    desc = new_desc(completed);
    delete_desc(oldDesc);
}

void task_storage::renewRegistered(
    descriptor* const oldDesc,
    descriptor*& desc)
{
    descriptor* curDesc = oldDesc;
    if (tryRegister(curDesc, desc))
        destroyDesc(oldDesc);
    else
    {
        destroyDesc(oldDesc);
        destroyDesc(desc);

        desc = copy_desc(curDesc);
    }
}

[[nodiscard]]
bool task_storage::trySetProgress(descriptor* const desc)
    noexcept
{
    std::atomic_bool& target = getTargetProgress(desc->op);
    bool oldTarget = false;

    return tryEfficientCAS(target, oldTarget, true);
}

void task_storage::releaseProgress(descriptor* const desc)
    noexcept
{
    getTargetProgress(desc->op)
        .store(false, std::memory_order_release);
}

[[nodiscard]]
std::atomic_bool& task_storage::getTargetProgress(const OP op)
    noexcept
{
    switch (op)
    {
    case OP::PUSH_FRONT:
    case OP::POP_FRONT:
        return progressFront;
    case OP::PUSH_BACK:
    case OP::POP_BACK:
    default:
        return progressBack;
    }
}

[[nodiscard]]
bool task_storage::tryCommitTask(descriptor* const desc)
    noexcept
{
    task_t* oldTask = desc->oldTask;
    std::atomic<task_t*>& target = getElementRef(desc->oldIndex, desc->op);

    return tryEfficientCAS(target, oldTask, desc->newTask);
}

[[nodiscard]]
bool task_storage::tryCommitIndex(descriptor* const desc)
    noexcept
{
    index_t* origin = index.load(std::memory_order_acquire);
    index_t originCopied = *origin;
    if (originCopied != desc->oldIndex)
        return false;

    index_t* newIndex = new_index(desc->newIndex);

    if (tryEfficientCAS(index, origin, newIndex))
    {
        delete_index(origin);

        return true;
    }

    delete_index(newIndex);

    return false;
}

bool task_storage::tryRegister(
    descriptor*& expected,
    descriptor* const desired)
    noexcept
{
    descriptor* origin = nullptr;
    descriptor* originCopied = nullptr;
    do
    {
        origin = registered.load(std::memory_order_acquire);
        originCopied = copy_desc(origin);
    } while (origin != nullptr && originCopied == nullptr);

    if (origin == nullptr && expected != nullptr)
    {
        expected = nullptr;

        delete_desc(originCopied);

        return false;
    }

    if (origin != nullptr &&
        (expected == nullptr || *expected != *originCopied))
    {
        expected = originCopied;

        return false;
    }

    delete_desc(originCopied);

    descriptor* const copied =
        desired == nullptr ? nullptr : copy_desc(desired);
    if (tryEfficientCAS(registered, origin, copied))
        return true;

    delete_desc(copied);

    return false;
}

#pragma endregion task_storage
