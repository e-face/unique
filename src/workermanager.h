#ifndef WORKERMANAGER_H
#define WORKERMANAGER_H

#include "workertypes.h"
//----------------------------------------------
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

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
                         Worker *parent = 0, QString task = QString());
    Worker *spawn(Worker *parent, WorkerType type, QString task, QString *error);
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
     * @brief Запустить воркера или поставить его в очередь на запуск.
     */
    void requestStart(Worker *worker);

signals:
    void workerCreated(Worker *worker);
    void workerAboutToBeRemoved(Worker *worker);
    void treeChanged();
    void toolRunFinished(const QString &runId);

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

    ProjectWorkspace *_workspace;
    QList<Worker *>   _workers;
    QList<Worker *>   _startQueue;
    bool              _pumpScheduled;
    QHash<QString, QList<ToolRun> > _toolQueues;
    QHash<QString, ToolRun>         _toolCurrent;
    QHash<QString, QString>         _finishedRuns;
    int                             _runCounter;
};

#endif // WORKERMANAGER_H
