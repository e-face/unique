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

/**
 * @brief Роль воркера в группе обсуждения.
 */
enum GroupRole
{
    GroupRoleNone = 0,
    /** Принимает задачи от родителя/пользователя, может ставить задачи детям. */
    GroupRoleChief,
    /** Собеседник: обсуждает и действует, но не ставит задачи детям. */
    GroupRoleInterlocutor
};

//----------------------------------------------

inline QString groupRoleToString(GroupRole role)
{
    switch (role)
    {
    case GroupRoleChief:
        return QString::fromUtf8("chief");
    case GroupRoleInterlocutor:
        return QString::fromUtf8("interlocutor");
    case GroupRoleNone:
    default:
        return QString::fromUtf8("none");
    }
}

//----------------------------------------------

inline GroupRole groupRoleFromString(QString value)
{
    if (value == QString::fromUtf8("chief"))
        return GroupRoleChief;
    if (value == QString::fromUtf8("interlocutor"))
        return GroupRoleInterlocutor;
    return GroupRoleNone;
}

//----------------------------------------------

/**
 * @brief Класс задач, которые решает пресет / профиль модели.
 */
enum WorkerTaskKind
{
    WorkerTaskKindLlm = 0,
    WorkerTaskKindImageGeneration,
    WorkerTaskKindSpeech,
    WorkerTaskKindEmbedding,
    WorkerTaskKindOther
};

//----------------------------------------------

inline QString workerTaskKindToString(WorkerTaskKind kind)
{
    switch (kind)
    {
    case WorkerTaskKindImageGeneration:
        return QString::fromUtf8("image_generation");
    case WorkerTaskKindSpeech:
        return QString::fromUtf8("speech");
    case WorkerTaskKindEmbedding:
        return QString::fromUtf8("embedding");
    case WorkerTaskKindOther:
        return QString::fromUtf8("other");
    case WorkerTaskKindLlm:
    default:
        return QString::fromUtf8("llm");
    }
}

//----------------------------------------------

inline WorkerTaskKind workerTaskKindFromString(QString value)
{
    const QString v = value.trimmed().toLower();
    if (v == QString::fromUtf8("image_generation")
        || v == QString::fromUtf8("image")
        || v == QString::fromUtf8("img"))
        return WorkerTaskKindImageGeneration;
    if (v == QString::fromUtf8("speech")
        || v == QString::fromUtf8("audio")
        || v == QString::fromUtf8("tts")
        || v == QString::fromUtf8("stt"))
        return WorkerTaskKindSpeech;
    if (v == QString::fromUtf8("embedding")
        || v == QString::fromUtf8("embeddings"))
        return WorkerTaskKindEmbedding;
    if (v == QString::fromUtf8("other"))
        return WorkerTaskKindOther;
    return WorkerTaskKindLlm;
}

//----------------------------------------------

/**
 * @brief Заранее заданный шаблон воркера: роль + профиль + класс задач.
 *
 * Уникальна комбинация type + profileId + taskKind.
 */
struct WorkerPreset
{
    WorkerPreset()
        : type(WorkerTypeManager)
        , taskKind(WorkerTaskKindLlm)
    {
    }

    QString         id;
    WorkerType      type;
    QString         profileId;
    WorkerTaskKind  taskKind;
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
