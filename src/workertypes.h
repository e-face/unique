#ifndef WORKERTYPES_H
#define WORKERTYPES_H

#include <QString>

//----------------------------------------------

/**
 * @brief Роль воркера: изолированный набор функций и промпта.
 */
enum WorkerType
{
    WorkerTypeManager,
    WorkerTypeExecutor,
    WorkerTypeTester,
    /** Инструмент: исполнитель без родителя, общий для всех, многоразовый. */
    WorkerTypeTool
};

//----------------------------------------------

/**
 * @brief Состояние воркера на схеме и в цикле задачи.
 */
enum WorkerState
{
    WorkerStateIdle,
    WorkerStateQueued,
    WorkerStateRunning,
    WorkerStateWaitingApproval,
    WorkerStateWaitingChildren,
    WorkerStateWaitingContinuation,
    WorkerStateCompleted,
    WorkerStateFailed
};

//----------------------------------------------

inline QString workerTypeToString(WorkerType type)
{
    switch (type)
    {
    case WorkerTypeExecutor:
        return QString::fromUtf8("executor");
    case WorkerTypeTester:
        return QString::fromUtf8("tester");
    case WorkerTypeTool:
        return QString::fromUtf8("tool");
    case WorkerTypeManager:
    default:
        return QString::fromUtf8("manager");
    }
}

//----------------------------------------------

inline bool isKnownWorkerTypeName(QString value)
{
    return value == QString::fromUtf8("manager")
           || value == QString::fromUtf8("executor")
           || value == QString::fromUtf8("tester")
           || value == QString::fromUtf8("tool")
           || value == QString::fromUtf8("root")
           || value == QString::fromUtf8("planner")
           || value == QString::fromUtf8("file")
           || value == QString::fromUtf8("code");
}

//----------------------------------------------

inline WorkerType workerTypeFromString(QString value)
{
    if (value == QString::fromUtf8("executor")
        || value == QString::fromUtf8("file")
        || value == QString::fromUtf8("code"))
        return WorkerTypeExecutor;
    if (value == QString::fromUtf8("tester"))
        return WorkerTypeTester;
    if (value == QString::fromUtf8("tool"))
        return WorkerTypeTool;
    return WorkerTypeManager;
}

//----------------------------------------------

inline QString workerStateToString(WorkerState state)
{
    switch (state)
    {
    case WorkerStateQueued:
        return QString::fromUtf8("queued");
    case WorkerStateRunning:
        return QString::fromUtf8("running");
    case WorkerStateWaitingApproval:
        return QString::fromUtf8("waiting_approval");
    case WorkerStateWaitingChildren:
        return QString::fromUtf8("waiting_children");
    case WorkerStateWaitingContinuation:
        return QString::fromUtf8("waiting_continuation");
    case WorkerStateCompleted:
        return QString::fromUtf8("completed");
    case WorkerStateFailed:
        return QString::fromUtf8("failed");
    case WorkerStateIdle:
    default:
        return QString::fromUtf8("idle");
    }
}

//----------------------------------------------

inline WorkerState workerStateFromString(QString value)
{
    if (value == QString::fromUtf8("queued"))
        return WorkerStateQueued;
    if (value == QString::fromUtf8("running"))
        return WorkerStateRunning;
    if (value == QString::fromUtf8("waiting_approval"))
        return WorkerStateWaitingApproval;
    if (value == QString::fromUtf8("waiting_children"))
        return WorkerStateWaitingChildren;
    if (value == QString::fromUtf8("waiting_continuation"))
        return WorkerStateWaitingContinuation;
    if (value == QString::fromUtf8("completed"))
        return WorkerStateCompleted;
    if (value == QString::fromUtf8("failed"))
        return WorkerStateFailed;
    return WorkerStateIdle;
}

#endif // WORKERTYPES_H
