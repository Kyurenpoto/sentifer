#include "../include/sentifer_mtbase/details/base_structures.hpp"

using namespace mtbase;

#pragma region task_storage__descriptor

[[nodiscard]]
task_storage::descriptor task_storage::descriptor::rollbacked(
    index_t* const oldIndexLoad,
    index_t* const newIndexLoad)
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
task_storage::descriptor task_storage::descriptor::completed()
    const noexcept
{
    return descriptor
    {
        .phase = descriptor::PHASE::COMPLETE,
        .op = op,
        .target = target
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
        .target = target
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
    if (!isValidIndex(&oldIndex, op))
        return nullptr;

    index_t* const newIndex = new_index(moveIndex(&oldIndex, op));
    std::atomic<task_t*>& target = getTask(getTargetIndex(&oldIndex, op));

    descriptor* desc = new_desc(descriptor
        {
            .phase = descriptor::PHASE::RESERVE,
            .op = op,
            .target = target,
            .oldTask = target.load(std::memory_order_acquire),
            .newTask = task,
            .oldIndex = new_index(std::move(oldIndex)),
            .newIndex = newIndex
        });

    applyDesc(desc);

    releaseProgress(desc);

    return desc;
}

void task_storage::destroyDesc(descriptor* const desc)
{
    if (desc == nullptr)
        return;

    descriptor* oldDesc = desc;
    tryRegister(oldDesc, nullptr);

    delete_index(desc->oldIndex);
    delete_index(desc->newIndex);

    delete_desc(desc);
}

[[nodiscard]]
task_storage::index_t* task_storage::new_index(index_t&& idx)
{
    return alloc.new_object<index_t>(idx);
}

void task_storage::delete_index(index_t* const idx)
{
    if (idx != nullptr)
        alloc.delete_object(idx);
}

[[nodiscard]]
task_storage::descriptor* task_storage::new_desc(descriptor&& desc)
{
    return alloc.new_object<descriptor>(desc);
}

void task_storage::delete_desc(descriptor* const desc)
{
    if (desc != nullptr)
        alloc.delete_object(desc);
}

void task_storage::applyDesc(descriptor*& desc)
{
    descriptor* helpDesc = registered.load(std::memory_order_acquire);
    if (helpDesc != nullptr)
        help_registered(helpDesc);

    while (!trySetProgress(desc));

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

        descriptor* trouble = new_desc(std::move(*oldDesc));
        help_registered(trouble);
    }

    descriptor* trouble = new_desc(std::move(*desc));
    help_registered(trouble);
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

        if (tryCommitWithRegistered(desc))
            return;
    }
}

void task_storage::help_registered_complete(descriptor*& desc)
{
    if (desc->phase == descriptor::PHASE::FAIL)
        return;

    while (desc->phase != descriptor::PHASE::COMPLETE)
    {
        descriptor* oldDesc = desc;
        desc = new_desc(oldDesc->completed());
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
    desc->target.store(desc->oldTask, std::memory_order_release);

    return refreshIndex(desc);
}

[[nodiscard]]
task_storage::descriptor* task_storage::refreshIndex(descriptor*& desc)
{
    descriptor* oldDesc = new_desc(std::move(*desc));
    delete_desc(desc);

    index_t* const oldIndex =
        new_index(std::move(*(index.load(std::memory_order_acquire))));
    if (isValidIndex(oldIndex, oldDesc->op))
    {
        index_t* const newIndex =
            new_index(moveIndex(oldIndex, oldDesc->op));
        desc = new_desc(oldDesc->rollbacked(oldIndex, newIndex));
    }
    else
        desc = new_desc(oldDesc->failed());
    return oldDesc;
}

void task_storage::completeDesc(descriptor*& desc)
{
    descriptor* const oldDesc = desc;
    delete_index(oldDesc->oldIndex);

    desc = new_desc(oldDesc->completed());
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

        desc = new_desc(std::move(*curDesc));
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

    return tryEfficientCAS(desc->target, oldTask, desc->newTask);
}

[[nodiscard]]
bool task_storage::tryCommitIndex(descriptor* const desc)
    noexcept
{
    index_t* oldIndex = desc->oldIndex;
    index_t* newIndex = new_index(std::move(*(desc->newIndex)));

    index_t* origin = index.load(std::memory_order_acquire);
    if (origin->front != oldIndex->front || origin->back != oldIndex->back)
    {
        oldIndex->~index_t();
        new(oldIndex) index_t{ *origin };
        delete_index(newIndex);

        return false;
    }

    if (tryEfficientCAS(index, origin, newIndex))
        return true;

    delete_index(newIndex);

    return false;
}

bool task_storage::tryRegister(
    descriptor*& expected,
    descriptor* const desired)
    noexcept
{
    descriptor* const copied =
        desired == nullptr ? nullptr : new_desc(std::move(*desired));
    if (tryEfficientCAS(registered, expected, copied))
        return true;

    delete_desc(copied);

    return false;
}

#pragma endregion task_storage
