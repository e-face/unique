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
     * @brief Подпись для вкладки и схемы: title, иначе суть из task.
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
                          QString alwaysKey = QString());
    void resolvePermission(bool approved, bool always, QString comment);
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

signals:
    void stateChanged();
    void finished();
    void permissionRequested(const QString &action);
    void permissionResolved();
    void continuationRequested(const QString &error);
    void continuationResolved();

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
    void noteParentTurnEnded();
    void onChildrenAnswered();
    QString composeParentReport() const;
    QString childOutcomesSummary() const;

    QString       _id;
    WorkerType    _type;
    WorkerState   _state;
    Worker       *_parentWorker;
    QList<Worker *> _children;
    QList<Worker *> _delegatedThisBatch;
    ChatSession  *_session;
    QString       _task;
    QString       _title;
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
    bool          _resumeContinueChat;
    bool          _userStopped;
    int           _unusedParentTurns;
    bool          _permissionOfferAlways;
    QString       _pendingAlwaysKey;
    QList<Worker *> _touchedThisTurn;
    QStringList   _pendingFollowUps;
    QString       _architecture;
    QList<WorkSubtask> _workPlan;
    QString       _toolName;
    QString       _toolPurpose;
    bool          _toolStarted;
    int           _waitingForTool;
    QStringList   _changedFiles;
};

#endif // WORKER_H
