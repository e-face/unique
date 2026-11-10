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
 * @brief Единица работы: чат, задача, родитель и дети.
 *
 * Вход — задача от родителя (или пользователя для корня).
 * Выход — результат родителю. Корень родителя не имеет.
 * Родитель продолжает работу только после ответов всех детей.
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
     * @brief Короткое название задачи (2–6 слов) — задаёт модель
     *        через delegate_task/plan_work. Пусто — берётся из текста задачи.
     */
    QString title() const;
    void setTitle(QString title);
    /**
     * @brief Уникальное «человеческое» имя воркера (Алексей, Мария…).
     */
    QString personName() const;
    void setPersonName(QString name);
    /**
     * @brief Короткое имя для журнала: имя человека, имя инструмента или роль.
     */
    QString shortName() const;
    /** Автор следующего входящего сообщения, если это не родитель. */
    void setIncomingAuthor(const QString &name);
    QString groupId() const;
    void setGroupId(QString id);
    GroupRole groupRole() const;
    void setGroupRole(GroupRole role);
    /**
     * @brief Профиль модели из пресета; пусто — профиль роли по умолчанию.
     */
    QString llmProfileId() const;
    void setLlmProfileId(QString id);
    bool isGroupChief() const;
    bool isGroupInterlocutor() const;
    bool canSpawnChildren() const;
    /**
     * @brief Подпись для вкладки и схемы: имя · роль — задача.
     */
    QString displayLabel() const;
    /**
     * @brief Вытащить из длинного текста задачи одну строку с её сутью.
     */
    static QString shortLabelFromTask(QString task);
    QString result() const;
    void setResult(QString result);
    QString lastError() const;
    QString lastAssistant() const;
    QString permissionAction() const;

    int depth() const;
    bool isRoot() const;
    bool isTool() const;
    /** Независимая проверка: читать и запускать команды, без записи и без детей. */
    bool isCheckOnly() const;
    void setCheckOnly(bool on);
    bool hasAnswered() const;
    bool isUserStopped() const;

    QString toolName() const;
    void setToolName(QString name);
    QString toolPurpose() const;
    void setToolPurpose(QString purpose);
    bool toolIsFree() const;
    void beginToolRun(QString message);
    bool waitingForTool() const;
    void setWaitingForTool(bool waiting);
    /**
     * @brief После текущего batch инструментов остановиться (holdForExternal),
     *        пока group-диалог не пришлёт follow-up на ваш ход/голос.
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
     * @brief Спросить пользователя: сбросить бюджет поддерева или запретить спавн.
     *        Блокирует (QEventLoop), как askPermission.
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
     * @brief Спросить пользователя и дождаться ответа.
     * @return false, если вопрос отменён или воркер остановлен.
     */
    bool askUserQuestion(QString question, QString *answer);
    bool isAskingUser() const;
    void resolvePermission(bool approved, bool always, QString comment);
    /**
     * @brief Показать хвост вывода команды и дождаться строки для stdin.
     * @return false, если пользователь пропустил вопрос или воркер остановлен.
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
    void clearWorkPlan();
    void noteListedTools();
    void noteListedPresets();
    void noteWorkAction();
    bool listedTools() const;
    bool listedPresets() const;
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
    bool          _listedTools;
    bool          _listedPresets;
    bool          _actedAfterPlan;
    int           _idleNudges;
};

#endif // WORKER_H
