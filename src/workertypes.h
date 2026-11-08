#ifndef WORKERTYPES_H
#define WORKERTYPES_H

#include <QString>

//----------------------------------------------

/**
 * @brief Вид воркера.
 *
 * В дереве только исполнитель. Инструмент — общий специалист вне дерева.
 * Manager и Tester оставлены в перечислении, чтобы старые файлы читались,
 * и сразу приводятся к исполнителю.
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
    WorkerTaskKindAudioGeneration,
    WorkerTaskKindVideoGeneration,
    WorkerTaskKindEmbedding,
    WorkerTaskKindDecisions,
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
    case WorkerTaskKindAudioGeneration:
        return QString::fromUtf8("audio_generation");
    case WorkerTaskKindVideoGeneration:
        return QString::fromUtf8("video_generation");
    case WorkerTaskKindEmbedding:
        return QString::fromUtf8("embedding");
    case WorkerTaskKindDecisions:
        return QString::fromUtf8("decisions");
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
        || v == QString::fromUtf8("tts")
        || v == QString::fromUtf8("stt")
        || v == QString::fromUtf8("asr"))
        return WorkerTaskKindSpeech;
    if (v == QString::fromUtf8("audio_generation")
        || v == QString::fromUtf8("audio")
        || v == QString::fromUtf8("music")
        || v == QString::fromUtf8("sound"))
        return WorkerTaskKindAudioGeneration;
    if (v == QString::fromUtf8("video_generation")
        || v == QString::fromUtf8("video"))
        return WorkerTaskKindVideoGeneration;
    if (v == QString::fromUtf8("embedding")
        || v == QString::fromUtf8("embeddings"))
        return WorkerTaskKindEmbedding;
    if (v == QString::fromUtf8("decisions")
        || v == QString::fromUtf8("decision"))
        return WorkerTaskKindDecisions;
    if (v == QString::fromUtf8("other"))
        return WorkerTaskKindOther;
    return WorkerTaskKindLlm;
}

//----------------------------------------------

/**
 * @brief Медиа/векторные modality: без chat tool_calls — прямой executor.
 */
inline bool workerTaskKindIsMediaExecutor(WorkerTaskKind kind)
{
    switch (kind)
    {
    case WorkerTaskKindImageGeneration:
    case WorkerTaskKindSpeech:
    case WorkerTaskKindAudioGeneration:
    case WorkerTaskKindVideoGeneration:
    case WorkerTaskKindEmbedding:
        return true;
    default:
        return false;
    }
}

//----------------------------------------------

/**
 * @brief Нужен обычный LLM chat-цикл (tool_calls).
 */
inline bool workerTaskKindUsesAgentChat(WorkerTaskKind kind)
{
    return !workerTaskKindIsMediaExecutor(kind);
}

//----------------------------------------------

/**
 * @brief Заранее заданный шаблон воркера: роль + профиль модели.
 *
 * Класс задач (taskKind) наследуется от modality выбранного профиля.
 * Уникальна комбинация type + profileId.
 */
struct WorkerPreset
{
    WorkerPreset()
        : type(WorkerTypeExecutor)
        , taskKind(WorkerTaskKindLlm)
    {
    }

    QString         id;
    WorkerType      type;
    QString         profileId;
    /** Копия modality профиля; при сохранении/загрузке синхронизируется. */
    WorkerTaskKind  taskKind;
};

//----------------------------------------------

/** Менеджер и тестировщик из старых проектов — это исполнитель. */
inline WorkerType normalizeWorkerType(WorkerType type)
{
    if (type == WorkerTypeTool)
        return WorkerTypeTool;
    return WorkerTypeExecutor;
}

//----------------------------------------------

inline QString workerTypeToString(WorkerType type)
{
    if (normalizeWorkerType(type) == WorkerTypeTool)
        return QString::fromUtf8("tool");
    return QString::fromUtf8("executor");
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
    if (value == QString::fromUtf8("tool"))
        return WorkerTypeTool;
    return WorkerTypeExecutor;
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
