#ifndef WORKER_H
#define WORKER_H

#include "workertypes.h"
//----------------------------------------------
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class ChatSession;

//----------------------------------------------

struct WorkSubtask
{
    WorkSubtask()
        : cannotSplit(false)
        , delegated(false)
        , expectedParts(0)
    {
    }

    QString name;
    QString task;
    QString complexity;
    QString how;
    QString reason;
    bool    cannotSplit;
    bool    delegated;
    int     expectedParts;
};

//----------------------------------------------

/**
 * @brief A unit of work: a chat, a task, a parent, and children.
 *
 * Input is a task from the parent, or from the user for the root.
 * Output is a result to the parent. The root has no parent.
 * The parent continues only after every child has answered.
 */
class Worker : public QObject
{
    Q_OBJECT
public:
    explicit Worker(QObject *parent = 0);
    virtual ~Worker();

    QString id() const;
    void setId(QString id);

    WorkerType type() const;
    void setType(WorkerType type);

    WorkerState state() const;
    void setState(WorkerState state);

    Worker *parentWorker() const;
    void setParentWorker(Worker *parent);
    QList<Worker *> children() const;
    QList<Worker *> unfinishedChildren() const;
    bool hasUnfinishedChildren() const;
    void addChild(Worker *child);
    void detachChild(Worker *child);
    Worker *childById(QString id) const;
    Worker *findChildForAssignment(WorkerType type, QString workerId,
                                   QString task) const;
    QString childrenSummary() const;
    bool canAcceptFollowUp() const;
    void beginFollowUp(QString message);
    void clearPendingFollowUps();
    bool hasPendingFollowUps() const;

    ChatSession *session() const;
    void setSession(ChatSession *session);

    QString task() const;
    void setTask(QString task);
    /**
     * @brief Short task title (2 to 6 words), set by the model
     *        through delegate_task or plan_work. Empty means it is taken from the task text.
     */
    QString title() const;
    void setTitle(QString title);
    /**
     * @brief A unique human name of the worker.
     */
    QString personName() const;
    void setPersonName(QString name);
    /**
     * @brief Short name for the journal: a person's name, a tool name, or a role.
     */
    QString shortName() const;
    /** Author of the next incoming message, when it is not the parent. */
    void setIncomingAuthor(const QString &name);
    QString groupId() const;
    void setGroupId(QString id);
    GroupRole groupRole() const;
    void setGroupRole(GroupRole role);
    /**
     * @brief Model profile from the preset. Empty means the role's default profile.
     */
    QString llmProfileId() const;
    void setLlmProfileId(QString id);
    /**
     * @brief Preset this worker was created from. Empty falls back to the first
     *        preset with the same role and profile.
     */
    QString presetId() const;
    void setPresetId(QString id);
    bool isGroupChief() const;
    bool isGroupInterlocutor() const;
    bool canSpawnChildren() const;
    /**
     * @brief Caption for the tab and the schema: name, role, and task.
     */
    QString displayLabel() const;
    /**
     * @brief Pull one line with the gist out of a long task text.
     */
    static QString shortLabelFromTask(QString task);
    /**
     * @brief True when a distinctive token of @p specialization appears in @p text.
     *        Generic words are ignored.
     */
    static bool specializationHits(QString specialization, QString text);
    QString result() const;
    void setResult(QString result);
    QString lastError() const;
    QString lastAssistant() const;
    QString permissionAction() const;

    int depth() const;
    bool isRoot() const;
    bool isTool() const;
    /** Independent check: read and run commands, with no writes and no children. */
    bool isCheckOnly() const;
    void setCheckOnly(bool on);
    bool hasAnswered() const;
    bool isUserStopped() const;

    QString toolName() const;
    void setToolName(QString name);
    QString toolPurpose() const;
    void setToolPurpose(QString purpose);
    bool toolIsFree() const;
    /** True after a run was stopped before the tool finished. Cleared on the next run. */
    bool toolInterrupted() const;
    void setToolInterrupted(bool on);
    void beginToolRun(QString message);
    bool waitingForTool() const;
    void setWaitingForTool(bool waiting);
    /**
     * @brief After the current tool batch, stop (holdForExternal)
     *        until the group dialogue sends a follow-up for your turn or vote.
     */
    void parkForGroupDialogue();
    void setVerification(bool present, bool passed, QString command, QString output,
                         QString artifact, QString digest);
    bool hasVerification() const;
    bool verificationPassed() const;
    QString verificationCommand() const;
    QString verificationOutput() const;
    QString verificationArtifact() const;
    QString verificationDigest() const;
    void noteToolBatch();
    int toolBatchCount() const;
    int subtreeToolBatches() const;
    qint64 startedAtMs() const;
    qint64 subtreeElapsedMs() const;
    int subtreeBatchLimit() const;
    qint64 subtreeMsLimit() const;
    /**
     * @brief Ask the user to reset the subtree budget or to forbid spawning.
     *        Blocks on QEventLoop, like askPermission.
     */
    bool requestSubtreeBudgetGrant(int grantBatches, qint64 grantMs);
    void resolveSubtreeBudget(bool granted);
    bool isWaitingSubtreeBudget() const;
    QString subtreeBudgetPromptText() const;

    void applyPersistedState(WorkerState state, QString result,
                             QString lastError, QString lastAssistant,
                             QString permissionAction);
    void resumeAfterRestore();
    void quitBlockingLoops();
    void beginUserStop();
    void finishUserStop();
    void forceStop();
    void forceReportToParent();

    void startTask();
    QString composeStartPrefix() const;
    QString composeStartMessage() const;
    void complete(QString result, bool ok);
    void noteFileChanged(QString path);
    QStringList changedFiles() const;
    void setChangedFiles(const QStringList &files);
    QString changedFilesText() const;
    void registerDelegated(Worker *child);
    QString childResultsReport() const;
    QString askPermission(QString action, bool offerAlways = false,
                          QString alwaysKey = QString(), bool forceDialog = false);
    /**
     * @brief Ask the user and wait for the answer.
     * @return false if the question was cancelled or the worker was stopped.
     */
    bool askUserQuestion(QString question, QString *answer);
    bool isAskingUser() const;
    void resolvePermission(bool approved, bool always, QString comment);
    /**
     * @brief Show the tail of command output and wait for a line for stdin.
     * @return false if the user skipped the question or the worker was stopped.
     */
    bool askConsoleInput(QString transcript, QString *line);
    void resolveConsoleInput(bool sent, QString line);
    bool isWaitingConsoleInput() const;
    QString consoleInputTranscript() const;
    void resolveContinuation(bool retry);
    bool permissionOfferAlways() const;
    QStringList alwaysAllowedCommands() const;
    void setAlwaysAllowedCommands(const QStringList &commands);
    void markUsed();

    QString currentAssignment() const;
    bool hasWorkPlan() const;
    QString architecture() const;
    QList<WorkSubtask> workPlan() const;
    QString applyWorkPlan(QString architecture, const QList<WorkSubtask> &items);
    WorkSubtask *findWorkSubtask(QString name, QString task);
    void markSubtaskDelegated(QString name);
    /** A successful run_tool covers the plan parts marked how=tool. */
    void noteToolRan();
    void clearWorkPlan();
    void noteWorkAction();
    bool needsWorkOpening() const;
    QString openingBlockReason() const;

signals:
    void stateChanged();
    void finished();
    void permissionRequested(const QString &action);
    void permissionResolved();
    void consoleInputRequested(const QString &transcript);
    void consoleInputResolved();
    void continuationRequested(const QString &error);
    void continuationResolved();
    void subtreeBudgetRequested(const QString &text);
    void subtreeBudgetResolved();

private slots:
    void sltBusyChanged(bool busy);
    void sltAssistantMessage(const QString &text);
    void sltFailed(const QString &error);
    void sltToolBatchFinished();
    void sltChildFinished();
    void sltResumeAfterError();
    void sltFinishWaitingChildren();
    void sltFlushFollowUps();
    void sltPruneUnusedChildren();
    void sltIdleAfterBusy();

private:
    void bindAgent();
    void pushTaskAnchor();
    bool answeredState(WorkerState state) const;
    void sendFollowUp(QString message);
    void stampTurnAuthor();
    void noteParentTurnEnded();
    void onChildrenAnswered();
    QString composeParentReport() const;
    QString childOutcomesSummary() const;
    bool shouldNudgeToContinue() const;
    QString continueNudgeText() const;

    QString       _id;
    WorkerType    _type;
    bool          _checkOnly;
    WorkerState   _state;
    Worker       *_parentWorker;
    QList<Worker *> _children;
    QList<Worker *> _delegatedThisBatch;
    ChatSession  *_session;
    QString       _task;
    QString       _title;
    QString       _personName;
    QString       _incomingAuthor;
    QString       _groupId;
    GroupRole     _groupRole;
    QString       _llmProfileId;
    QString       _presetId;
    QString       _result;
    QString       _lastAssistant;
    QString       _lastError;
    QString       _permissionAction;
    QString       _permissionResult;
    bool          _completed;
    bool          _skipIdleComplete;
    bool          _needsParentReport;
    bool          _parentReportNudged;
    bool          _continueRetry;
    bool          _permissionLoopActive;
    bool          _askingUser;
    bool          _consoleInputLoopActive;
    bool          _consoleInputSent;
    QString       _consoleInputLine;
    QString       _consoleInputTranscript;
    bool          _resumeContinueChat;
    bool          _userStopped;
    int           _unusedParentTurns;
    bool          _permissionOfferAlways;
    QString       _pendingAlwaysKey;
    QList<Worker *> _touchedThisTurn;
    QStringList   _pendingFollowUps;
    QStringList   _pendingAuthors;
    QString       _architecture;
    QList<WorkSubtask> _workPlan;
    QString       _toolName;
    QString       _toolPurpose;
    bool          _toolStarted;
    bool          _toolInterrupted;
    int           _waitingForTool;
    bool          _parkForGroupDialogue;
    QStringList   _changedFiles;
    bool          _hasVerification;
    bool          _verificationPassed;
    QString       _verificationCommand;
    QString       _verificationOutput;
    QString       _verificationArtifact;
    QString       _verificationDigest;
    int           _toolBatchCount;
    int           _subtreeBatchAllowance;
    qint64        _startedAtMs;
    qint64        _subtreeMsAllowance;
    bool          _subtreeBudgetGranted;
    bool          _subtreeBudgetLoopActive;
    int           _pendingSubtreeGrantBatches;
    qint64        _pendingSubtreeGrantMs;
    QString       _subtreeBudgetPromptText;
    bool          _actedAfterPlan;
    int           _idleNudges;
};

#endif // WORKER_H
