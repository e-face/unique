#ifndef WORKERTYPES_H
#define WORKERTYPES_H

#include <QString>
#include <QStringList>

//----------------------------------------------

/**
 * @brief Kind of worker.
 *
 * In the tree, only an executor. A tool is a shared specialist outside the tree.
 * Manager and Tester stay in the enum so old files can be read,
 * and are immediately treated as an executor.
 */
enum WorkerType
{
    WorkerTypeManager,
    WorkerTypeExecutor,
    WorkerTypeTester,
    /** A tool: an executor with no parent, shared by everyone, and reusable. */
    WorkerTypeTool
};

//----------------------------------------------

/**
 * @brief Worker state on the schema and in the task loop.
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
 * @brief A worker's role in a discussion group.
 */
enum GroupRole
{
    GroupRoleNone = 0,
    /** Accepts tasks from the parent or the user, and may assign tasks to children. */
    GroupRoleChief,
    /** A peer: discusses and acts, but does not assign tasks to children. */
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
 * @brief Task class solved by a preset or a model profile.
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
 * @brief Media and vector modalities: no chat tool calls, a direct executor.
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
 * @brief An ordinary LLM chat cycle (tool calls) is required.
 */
inline bool workerTaskKindUsesAgentChat(WorkerTaskKind kind)
{
    return !workerTaskKindIsMediaExecutor(kind);
}

//----------------------------------------------

/**
 * @brief A predefined worker template: a role and a model profile.
 *
 * The task class (taskKind) is inherited from the chosen profile's modality.
 * Several presets may share one role and one model profile.
 * The preset id and specialization tell them apart.
 */
struct WorkerPreset
{
    WorkerPreset()
        : type(WorkerTypeExecutor)
        , taskKind(WorkerTaskKindLlm)
        , toolIdsSet(false)
    {
    }

    QString         id;
    WorkerType      type;
    QString         profileId;
    /** A copy of the profile modality, kept in sync on save and load. */
    WorkerTaskKind  taskKind;
    /**
     * Short note for text workers: what this worker is for.
     */
    QString         specialization;
    /**
     * Tool ids present in a text worker's context.
     * toolIdsSet is false for an old preset: it keeps every built-in tool.
     */
    QStringList     toolIds;
    bool            toolIdsSet;
};

//----------------------------------------------

/** A manager and a tester from old projects are an executor. */
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
