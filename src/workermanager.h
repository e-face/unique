#ifndef WORKERMANAGER_H
#define WORKERMANAGER_H

#include "workertypes.h"
//----------------------------------------------
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

class ChatSession;
class ProjectWorkspace;
class QTimer;
class Worker;

//----------------------------------------------

/**
 * @brief One tool run requested by a worker (a queue item).
 */
struct ToolRun
{
    QString          id;
    QPointer<Worker> caller;
    QString          message;
};

//----------------------------------------------

/**
 * @brief Waiting for a peer's reply to talk_to (parent or child only).
 */
struct TalkWait
{
    TalkWait()
        : armed(false)
    {
    }

    QString          id;
    QPointer<Worker> caller;
    QPointer<Worker> peer;
    bool             armed;
};

//----------------------------------------------

enum GroupDialoguePhase
{
    GroupDialogueIdle = 0,
    GroupDialogueSpeaking,
    GroupDialogueVoting,
    GroupDialogueDone
};

//----------------------------------------------

struct GroupUtterance
{
    GroupUtterance()
        : seq(0)
    {
    }

    int     seq;
    QString kind; // speak | proposal | reject | vote | done | turn
    QString workerId;
    QString name;
    QString text;
};

//----------------------------------------------

/**
 * @brief A turn-based group dialogue: one speaker, a line to everyone, then a vote.
 *
 * Events are monotonic (seq). Each member gets exactly one action prompt
 * per turn or vote, and one terminal DONE. Other members' lines do not wake the model on their own.
 */
struct GroupDialogue
{
    GroupDialogue()
        : speakerIndex(0)
        , speakCount(0)
        , phase(GroupDialogueIdle)
        , nextSeq(1)
        , roundId(0)
    {
    }

    QString             groupId;
    QStringList         order;
    int                 speakerIndex;
    int                 speakCount; // a counter only, with no cap
    GroupDialoguePhase  phase;
    QList<GroupUtterance> transcript;
    QString             proposal;
    QString             proposalBy;
    QSet<QString>       voted;
    QSet<QString>       approvals;
    QHash<QString, QString> voteComments;
    QString             outcome;
    QString             lastOutcome; // outcome of the previous round (after a restart)
    int                 nextSeq;
    int                 roundId;           // round counter (full-transcript dedup)
    QHash<QString, int> roundSeen;         // workerId -> last roundId seen by him
    QHash<QString, int> cursor;           // workerId -> last acked seq
    QSet<QString>       terminalDelivered; // DONE was already delivered to this member
    /** How many times this member was already nudged on the current turn or vote (anti-deadlock). */
    QHash<QString, int> actionNudges;
};

//----------------------------------------------

/**
 * @brief Creates workers, attaches isolated tools, and waits for the result.
 *
 * Enforces limits: tree depth, and how many workers run at once.
 * Extra workers are queued and start on their own.
 */
class WorkerManager : public QObject
{
    Q_OBJECT
public:
    explicit WorkerManager(ProjectWorkspace *workspace, QObject *parent = 0);

    Worker *createRoot(ChatSession *session);
    Worker *createWorker(WorkerType type, QString id, ChatSession *session,
                         Worker *parent = 0, QString task = QString(),
                         QString llmProfileId = QString());
    Worker *spawn(Worker *parent, WorkerType type, QString task, QString *error,
                  QString llmProfileId = QString(), bool checkOnly = false);
    /**
     * @brief Add a peer to the chief's group (creates the group if needed).
     */
    Worker *addGroupMember(Worker *chief, QString presetId, QString *error);
    QList<Worker *> groupMembers(QString groupId) const;
    QString allocatePersonName() const;
    /**
     * @brief A message to the parent or a child. Blocks until the reply.
     *
     * Not used for members of the same group. There it is group_speak or group_await.
     */
    QString talkTo(Worker *from, QString peerId, QString message, QString *error);

    bool isGroupDialogueActive(QString groupId) const;
    bool isGroupDialogueDone(QString groupId) const;
    QString groupDialogueOutcome(QString groupId) const;
    /**
     * @brief Status of the dialogue. afterSeq>0 - only events newer than this seq.
     */
    QString groupDialogueStatus(QString groupId, int afterSeq = 0) const;
    QString groupSpeak(Worker *from, QString message, QString *error);
    QString groupEndTurn(Worker *from, QString *error);
    QString groupPropose(Worker *from, QString result, QString *error);
    QString groupVote(Worker *from, bool approve, QString comment, QString *error);
    QString groupAwait(Worker *from, QString *error);
    /**
     * @brief The member must act now (speaker, vote, or DONE).
     */
    bool groupParticipantShouldAct(Worker *worker) const;
    /**
     * @brief If the member must act and the LLM went idle, wake them again.
     */
    void ensureGroupParticipantActive(Worker *worker);
    /**
     * @brief Reinstall tools and raise saved group dialogues.
     */
    void reinstallToolsAndResumeGroups();

    bool dismissChild(Worker *parent, QString childId, QString *error);
    /** Delete a worker that has already finished (Completed or Failed), together with its children. */
    bool forceRemoveFinished(Worker *worker, QString *error);
    /**
     * @brief Delete tools and non-root workers that use this profile.
     *        On roots, clear llmProfileId so the role profile is substituted.
     */
    int removeWorkersUsingProfile(QString profileId);
    int countWorkersUsingProfile(QString profileId) const;
    QString runAndWait(Worker *worker);

    void installTools(Worker *worker);
    /**
     * @brief Modality of the worker's model profile (llm, image, speech, and so on).
     */
    WorkerTaskKind modalityOf(Worker *worker) const;
    /**
     * @brief A direct media or embedding tool run, with no chat cycle.
     * @return the report for complete(); *ok is true when the tool succeeded.
     */
    QString runMediaExecutor(Worker *worker, bool *ok);
    void refreshPrompt(Worker *worker);
    void refreshAllPrompts();
    void stopAll();
    void clear();
    Worker *workerById(QString id) const;
    QList<Worker *> workers() const;
    QList<Worker *> roots() const;
    QList<Worker *> tools() const;
    void linkParent(Worker *child, QString parentId);

    /**
     * @brief Tools: shared reusable executors with no parent.
     *
     * The manager creates them. Any worker except a tool may start them.
     * One tool runs one request at a time; the rest wait in a queue.
     * They are deleted only when the user asks.
     */
    /**
     * @param llmProfileId model profile from preset_id. Empty means the Tool role profile.
     */
    Worker *createTool(Worker *creator, QString name, QString purpose, QString *error,
                       QString llmProfileId = QString());
    Worker *toolByIdOrName(QString key) const;
    QString toolsSummary() const;
    int toolQueueLength(Worker *tool) const;
    bool toolBusy(Worker *tool) const;
    QString runTool(Worker *caller, QString toolKey, QString message, QString *error);
    bool destroyTool(QString id);

    int  maxDepth() const;
    int  maxConcurrent() const;
    int  activeCount() const;
    static int maxSubtreeToolBatches();
    static qint64 maxSubtreeMs();
    int  queuedCount() const;
    bool canStartNow() const;
    bool canSpawnManagerUnder(Worker *parent) const;
    bool canSpawnLeafUnder(Worker *parent) const;
    QString limitsSummaryFor(Worker *worker) const;
    QString loadSummary() const;
    /**
     * @brief The variable part of the startup context (limits and the tool list).
     *
     * It goes into the worker's first message, not the system prompt, so
     * the request prefix stays stable for the provider cache.
     */
    QString startupContextFor(Worker *worker) const;
    /**
     * @brief The team schema and the worker's place in it.
     *
     * Empty for the root: the schema arrives as an attached file.
     * Children receive it once when the assignment starts.
     */
    QString teamSchemaNoteFor(Worker *worker) const;
    QString changedFilesForChildren(Worker *parent) const;
    /**
     * @brief One active writer per file (a relative path). Fail closed.
     */
    bool claimFilePath(Worker *worker, QString relativePath, QString *error);
    void releaseFilePaths(Worker *worker);
    QString fileOwnershipSummary() const;
    QString workspaceCardText() const;
    void noteTaskCompleted();

    /**
     * @brief Start a worker, or queue it to start.
     */
    void requestStart(Worker *worker);

signals:
    void workerCreated(Worker *worker);
    void workerAboutToBeRemoved(Worker *worker);
    void treeChanged();
    void toolRunFinished(const QString &runId);
    void talkFinished(const QString &waitId);
    void groupAwaitFinished(const QString &workerId);

private slots:
    void sltPurgeWorker(QString id);
    void sltScheduleQueuePump();
    void sltPumpQueue();
    void sltLimitsChanged();
    void sltApplyLimitsChanged();
    void sltToolStateChanged();
    void sltToolFinished();
    void sltPumpTools();

private:
    QString promptFor(Worker *worker) const;
    bool canSpawn(WorkerType parentType, WorkerType childType) const;
    void destroyWorkerNow(Worker *worker);
    void pumpTool(Worker *tool);
    void finishToolRun(QString runId, QString result);
    void failToolRuns(Worker *tool, QString reason);
    void finishTalk(QString waitId, QString reply);
    void failTalksForWorker(Worker *worker, QString reason);
    QString findTalkWaitId(Worker *caller, Worker *peer) const;
    bool peerReadyForTalkReply(Worker *peer) const;
    bool talkWaitReachable(Worker *from, Worker *to) const;
    bool wouldCreateTalkCycle(Worker *from, Worker *peer) const;
    void armTalkHead(Worker *peer);
    void maybeFinishTalkHead(Worker *peer);

    void startGroupDialogue(QString groupId);
    /**
     * @brief A new discussion round after DONE, with the same members.
     * @return false if there is no group or too few members.
     */
    bool reopenGroupDialogue(QString groupId, Worker *starter, QString *error);
    /**
     * @brief Delta of new events for one participant (cursor + roundId dedup).
     *
     * @return false when there is nothing new to send.
     */
    bool deltaFor(GroupDialogue *dialogue, Worker *worker,
                      QString *delta, bool *fullTranscript);
    void syncDialogueOrder(GroupDialogue *dialogue) const;
    Worker *currentSpeaker(const GroupDialogue &dialogue) const;
    int appendGroupEvent(GroupDialogue *dialogue, QString kind, Worker *from, QString text);
    QString formatTranscript(const GroupDialogue &dialogue, int afterSeq = 0) const;
    int latestSeq(const GroupDialogue &dialogue) const;
    void ackGroupCursor(GroupDialogue *dialogue, Worker *worker, int seq);
    QString resumeMessageFor(Worker *worker, GroupDialogue *dialogue, bool ackCursor);
    bool workerShouldAct(Worker *worker, const GroupDialogue &dialogue) const;
    void wakeGroupAwaiter(Worker *worker, QString message);
    void failGroupAwaitsForWorker(Worker *worker, QString reason);
    void deliverGroupAction(Worker *worker, GroupDialogue *dialogue);
    void advanceDialogueAfterChange(QString groupId);
    void finishGroupDialogue(QString groupId, QString outcome);
    QString groupDialoguesPath() const;
    void saveGroupDialogues() const;
    void loadGroupDialogues();
    void resumeGroupDialogues();

    ProjectWorkspace *_workspace;
    QList<Worker *>   _workers;
    QList<Worker *>   _startQueue;
    bool              _pumpScheduled;
    QHash<QString, QList<ToolRun> > _toolQueues;
    QHash<QString, ToolRun>         _toolCurrent;
    QHash<QString, QString>         _finishedRuns;
    int                             _runCounter;
    QHash<QString, TalkWait>        _talkWaits;
    QHash<QString, QList<QString> > _talkWaitQueue;
    QHash<QString, QString>         _finishedTalks;
    int                             _talkCounter;
    QHash<QString, GroupDialogue>   _groupDialogues;
    QSet<QString>                   _groupAwaiting;
    QHash<QString, QString>         _groupAwaitResults;
    QHash<QString, QString>         _fileOwners;
    int                             _spawnCount;
    int                             _completeCount;
    QTimer                         *_limitsApplyTimer;
};

#endif // WORKERMANAGER_H
