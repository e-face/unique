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
class Worker;

//----------------------------------------------

/**
 * @brief Один запуск инструмента по запросу воркера (элемент очереди).
 */
struct ToolRun
{
    QString          id;
    QPointer<Worker> caller;
    QString          message;
};

//----------------------------------------------

/**
 * @brief Ожидание ответа собеседника на talk_to (только parent/child).
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
 * @brief Поочерёдный диалог группы: один оратор, реплика всем, затем голос.
 *
 * События монотонны (seq). Каждому участнику доставляется ровно один action-prompt
 * на ход/голос и один терминальный DONE; чужие реплики не будят модель отдельно.
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
    int                 speakCount; // только счётчик, без потолка
    GroupDialoguePhase  phase;
    QList<GroupUtterance> transcript;
    QString             proposal;
    QString             proposalBy;
    QSet<QString>       voted;
    QSet<QString>       approvals;
    QHash<QString, QString> voteComments;
    QString             outcome;
    QString             lastOutcome; // итог прошлого раунда (после рестарта)
    int                 nextSeq;
    int                 roundId;           // round counter (full-transcript dedup)
    QHash<QString, int> roundSeen;         // workerId -> last roundId seen by him
    QHash<QString, int> cursor;           // workerId -> last acked seq
    QSet<QString>       terminalDelivered; // DONE уже отдан участнику
};

//----------------------------------------------

/**
 * @brief Создаёт воркеров, вешает изолированные инструменты, ждёт результат.
 *
 * Следит за лимитами: глубина дерева и число одновременно работающих
 * воркеров. Лишние воркеры ставятся в очередь и стартуют сами.
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
                  QString llmProfileId = QString());
    /**
     * @brief Добавить собеседника в группу главного (создаёт группу при необходимости).
     */
    Worker *addGroupMember(Worker *chief, QString presetId, QString *error);
    QList<Worker *> groupMembers(QString groupId) const;
    QString allocatePersonName() const;
    /**
     * @brief Сообщение родителю/ребёнку; блокирует до ответа.
     *
     * Для участников одной группы не используется — там group_speak / group_await.
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
     * @brief Переустановить инструменты и поднять сохранённые групповые диалоги.
     */
    void reinstallToolsAndResumeGroups();

    bool dismissChild(Worker *parent, QString childId, QString *error);
    QString runAndWait(Worker *worker);

    void installTools(Worker *worker);
    void refreshPrompt(Worker *worker);
    void stopAll();
    void clear();
    Worker *workerById(QString id) const;
    QList<Worker *> workers() const;
    QList<Worker *> roots() const;
    QList<Worker *> tools() const;
    void linkParent(Worker *child, QString parentId);

    /**
     * @brief Инструменты: общие многоразовые исполнители без родителя.
     *
     * Создаёт менеджер, запускать может любой воркер (кроме инструментов).
     * Один инструмент выполняет один запрос за раз, остальные ждут в очереди.
     * Удаляются только по требованию пользователя.
     */
    Worker *createTool(Worker *creator, QString name, QString purpose, QString *error);
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
     * @brief Переменная часть стартового контекста (лимиты, список инструментов).
     *
     * Идёт в первое сообщение воркера, а не в системный промпт, чтобы
     * префикс запроса оставался стабильным для кеша провайдера.
     */
    QString startupContextFor(Worker *worker) const;
    QString changedFilesForChildren(Worker *parent) const;
    /**
     * @brief Один активный писатель на файл (относительный путь). Fail-closed.
     */
    bool claimFilePath(Worker *worker, QString relativePath, QString *error);
    void releaseFilePaths(Worker *worker);
    QString fileOwnershipSummary() const;
    QString workspaceCardText() const;
    void noteTaskCompleted();

    /**
     * @brief Запустить воркера или поставить его в очередь на запуск.
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
     * @brief Новый раунд обсуждения после DONE (те же участники).
     * @return false, если группы нет или мало участников.
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
};

#endif // WORKERMANAGER_H
