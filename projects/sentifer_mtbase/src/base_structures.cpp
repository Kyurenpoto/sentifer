#include "../include/sentifer_mtbase/details/base_structures.hpp"

using namespace mtbase;

#pragma region task_storage__index_base_t

[[nodiscard]]
task_storage::index_base_t task_storage::index_base_t::move(OP op)
    const noexcept
{
    switch (op)
    {
    case OP::PUSH_FRONT:
        return pushed_front();
    case OP::PUSH_BACK:
        return pushed_back();
    case OP::POP_FRONT:
        return poped_front();
    case OP::POP_BACK:
        return poped_back();
    default:
        return index_base_t{};
    }
}

[[nodiscard]]
bool task_storage::index_base_t::isValid(OP op)
    const noexcept
{
    if (front == back)
        return false;

    switch (op)
    {
    case OP::PUSH_FRONT:
    case OP::PUSH_BACK:
        return isFull();
    case OP::POP_FRONT:
    case OP::POP_BACK:
        return isEmpty();
    default:
        return true;
    }
}

#pragma endregion task_storage__index_base_t

#pragma region task_storage__descriptor

[[nodiscard]]
task_storage::descriptor task_storage::descriptor::rollbacked(
    index_base_t* const oldIndexLoad,
    index_base_t* const newIndexLoad)
    const noexcept
{
    return descriptor
    {
        .op = op,
        .target = target,
        .oldTask = oldTask,
        .newTask = newTask,
        .oldIndex = oldIndexLoad,
        .newIndex = newIndexLoad
    };
}

[[nodiscard]]
task_storage::descriptor task_storage::descriptor::completed(
    index_base_t* const oldIndexLoad)
    const noexcept
{
    return descriptor
    {
        .phase = descriptor::PHASE::COMPLETE,
        .op = op,
        .target = target,
        .oldTask = oldTask,
        .newTask = newTask,
        .oldIndex = oldIndexLoad
    };
}

[[nodiscard]]
task_storage::descriptor task_storage::descriptor::failed(
    index_base_t* const oldIndexLoad)
    const noexcept
{
    return descriptor
    {
        .phase = descriptor::PHASE::FAIL,
        .op = op,
        .target = target,
        .oldTask = oldTask,
        .newTask = newTask,
        .oldIndex = oldIndexLoad
    };
}

#pragma endregion task_storage__descriptor

template<class T>
bool tryEfficientCAS(
    std::atomic<T*>& target,
    T*& expected,
    T* const desired)
    noexcept
{
    return target.compare_exchange_strong(expected, desired,
        std::memory_order_acq_rel, std::memory_order_acquire);
}

bool tryEfficientCAS(
    std::atomic_bool& target,
    bool& expected,
    bool const desired)
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

    bool result = (desc->phase == descriptor::PHASE::COMPLETE);
    destroyDesc(desc);

    return result;
}

[[nodiscard]]
bool task_storage::push_back(task_t* task)
{
    descriptor* const desc = createDesc(task, OP::PUSH_BACK);

    bool result = (desc->phase == descriptor::PHASE::COMPLETE);
    destroyDesc(desc);

    return result;
}

[[nodiscard]]
task_t* task_storage::pop_front()
{
    descriptor* const desc = createDesc(nullptr, OP::POP_FRONT);

    task_t* result = (desc->phase == descriptor::PHASE::COMPLETE ?
        desc->oldTask : nullptr);
    destroyDesc(desc);

    return result;
}

[[nodiscard]]
task_t* task_storage::pop_back()
{
    descriptor* const desc = createDesc(nullptr, OP::POP_BACK);

    task_t* result = (desc->phase == descriptor::PHASE::COMPLETE ?
        desc->oldTask : nullptr);
    destroyDesc(desc);

    return result;
}

[[nodiscard]]
task_storage::descriptor* task_storage::createDesc(task_t* task, OP op)
{
    index_base_t* const oldIndex = index.load(std::memory_order_acquire);
    if (!oldIndex->isValid(op))
        return nullptr;

    index_base_t* const newIndex = new_index(oldIndex->move(op));
    std::atomic<task_t*>& target = getTask(oldIndex->getTargetIndex(op));

    descriptor* desc = new_desc(descriptor
        {
            .phase = descriptor::PHASE::RESERVE,
            .op = op,
            .target = target,
            .oldTask = target.load(std::memory_order_acquire),
            .newTask = task,
            .oldIndex = oldIndex,
            .newIndex = newIndex
        });

    applyDesc(desc);

    releaseProgress(desc);

    return desc;
}

void task_storage::destroyDesc(descriptor* const desc)
{
    descriptor* oldDesc = desc;
    while (!tryRegister(oldDesc, nullptr))
        if (oldDesc == desc)
            break;

    index_base_t* const curIndex = index.load(std::memory_order_acquire);
    if (desc->oldIndex == curIndex)
        delete_index(desc->oldIndex);
    if (desc->newIndex == curIndex)
        delete_index(desc->newIndex);

    delete_desc(desc);
}

[[nodiscard]]
task_storage::index_base_t* task_storage::new_index(index_base_t&& idx)
{
    return alloc.new_object<index_base_t>(idx);
}

void task_storage::delete_index(index_base_t* const idx)
{
    alloc.delete_object(idx);
}

[[nodiscard]]
task_storage::descriptor* task_storage::new_desc(descriptor&& desc)
{
    return alloc.new_object<descriptor>(desc);
}

void task_storage::delete_desc(descriptor* const desc)
{
    alloc.delete_object(desc);
}

void task_storage::applyDesc(descriptor*& desc)
{
    descriptor* helpDesc = registered.load(std::memory_order_acquire);
    if (helpDesc != nullptr)
        help_registered(helpDesc);

    for (size_t i = 0; i < MAX_RETRY; ++i)
        if (fast_path(desc))
            return;

    slow_path(desc);
}

bool task_storage::fast_path(descriptor*& desc)
{
    if (!trySetProgress(desc))
        return false;

    if (!tryCommitTask(desc))
        return false;

    if (!tryCommitIndex(desc))
    {
        descriptor* oldDesc = rollbackTask(desc);
        destroyDesc(oldDesc);

        return false;
    }

    completeDesc(desc);

    return true;
}

void task_storage::slow_path(descriptor*& desc)
{
    descriptor* oldDesc = nullptr;
    bool progress = false;
    while (true)
    {
        if (!progress)
            progress = trySetProgress(desc);

        if (progress && tryRegister(oldDesc, desc))
            break;

        if (oldDesc == nullptr)
            continue;

        help_registered(oldDesc);
    }

    help_registered(desc);
}

void task_storage::help_registered(descriptor*& desc)
{
    help_registered_progress(desc);
    help_registered_complete(desc);
}

void task_storage::help_registered_progress(descriptor*& desc)
{
    while (true)
    {
        if (desc->phase != descriptor::PHASE::RESERVE)
            return;

        if (!tryCommitTask(desc))
            continue;

        if (tryCommitIndex(desc))
            break;

        descriptor* oldDesc = rollbackTask(desc);
        renewRegistered(oldDesc, desc);
    }
}

void task_storage::help_registered_complete(descriptor*& desc)
{
    if (desc->phase == descriptor::PHASE::FAIL)
        return;

    while (desc->phase != descriptor::PHASE::COMPLETE)
    {
        descriptor* oldDesc = desc;
        desc = new_desc(
            oldDesc->completed(index.load(std::memory_order_acquire)));
        renewRegistered(oldDesc, desc);
    }
}

[[nodiscard]]
task_storage::descriptor* task_storage::rollbackTask(descriptor*& desc)
{
    desc->target.store(desc->oldTask, std::memory_order_release);

    descriptor* oldDesc = desc;
    index_base_t* const oldIndex = index.load(std::memory_order_acquire);
    if (oldIndex->isValid(oldDesc->op))
    {
        index_base_t* const newIndex =
            new_index(oldIndex->move(oldDesc->op));
        desc = new_desc(oldDesc->rollbacked(oldIndex, newIndex));
    }
    else
        desc = new_desc(oldDesc->failed(oldIndex));
    return oldDesc;
}

void task_storage::completeDesc(descriptor*& desc)
{
    descriptor* const oldDesc = desc;
    delete_index(oldDesc->oldIndex);

    desc = new_desc(
        oldDesc->completed(index.load(std::memory_order_acquire)));
    delete_desc(oldDesc);
}

void task_storage::renewRegistered(
    descriptor* const oldDesc,
    descriptor*& desc)
{
    descriptor* curDesc = oldDesc;
    descriptor* const newDesc = desc;
    if (tryRegister(curDesc, newDesc))
    {
        destroyDesc(oldDesc);

        desc = newDesc;
    }
    else
    {
        destroyDesc(oldDesc);
        destroyDesc(newDesc);

        desc = curDesc;
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

    if (tryEfficientCAS(desc->target, oldTask, desc->newTask))
        return true;

    return oldTask == desc->newTask;
}

[[nodiscard]]
bool task_storage::tryCommitIndex(descriptor* const desc)
    noexcept
{
    index_base_t* oldIndex = desc->oldIndex;

    if (tryEfficientCAS(index, oldIndex, desc->newIndex))
        return true;

    return oldIndex == desc->newIndex;
}

[[nodiscard]]
bool task_storage::tryRegister(
    descriptor*& expected,
    descriptor* const desired)
    noexcept
{
    return tryEfficientCAS(registered, expected, desired);
}

#pragma endregion task_storage
