#include "workermanager.h"
//----------------------------------------------
#include "agent.h"
#include "chatsession.h"
#include "projectfiletools.h"
#include "projectworkspace.h"
#include "worker.h"
#include "workertools.h"
#include "appsettings.h"
//----------------------------------------------
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QSet>
#include <QTextStream>
#include <QtGlobal>
#include <QUuid>

//----------------------------------------------

/** Бюджет: не больше стольких незавершённых детей у одного родителя. */
static const int kMaxChildrenPerParent = 5;
/** Бюджет поддерева: суммарные tool-batch'и (вызовы модели с инструментами). */
static const int kMaxSubtreeToolBatches = 120;
/** Бюджет поддерева по времени (45 минут). */
static const qint64 kMaxSubtreeMs = 45LL * 60 * 1000;

//----------------------------------------------

WorkerManager::WorkerManager(ProjectWorkspace *workspace, QObject *parent)
    : QObject(parent)
    , _workspace(workspace)
    , _pumpScheduled(false)
    , _runCounter(0)
    , _talkCounter(0)
    , _spawnCount(0)
    , _completeCount(0)
{
    connect(AppSettings::instance(), SIGNAL(workerLimitsChanged()),
            this, SLOT(sltLimitsChanged()));
}

//----------------------------------------------

int WorkerManager::maxDepth() const
{
    return AppSettings::instance()->maxWorkerDepth();
}

//----------------------------------------------

int WorkerManager::maxConcurrent() const
{
    return AppSettings::instance()->maxConcurrentWorkers();
}

//----------------------------------------------

int WorkerManager::activeCount() const
{
    int count = 0;
    foreach (Worker *worker, _workers)
    {
        if (!worker)
            continue;
        // Воркер, заблокированный в ожидании инструмента, слот не занимает —
        // иначе инструмент мог бы никогда не получить слот (взаимная блокировка).
        if (worker->waitingForTool())
            continue;
        const WorkerState state = worker->state();
        if (state == WorkerStateRunning
            || state == WorkerStateWaitingApproval
            || state == WorkerStateWaitingContinuation)
            ++count;
        else if (worker->session() && worker->session()->agent()
                 && worker->session()->agent()->isBusy())
            ++count;
    }
    return count;
}

//----------------------------------------------

int WorkerManager::maxSubtreeToolBatches()
{
    return kMaxSubtreeToolBatches;
}

//----------------------------------------------

qint64 WorkerManager::maxSubtreeMs()
{
    return kMaxSubtreeMs;
}

//----------------------------------------------

int WorkerManager::queuedCount() const
{
    int count = 0;
    foreach (Worker *worker, _workers)
    {
        if (worker && worker->state() == WorkerStateQueued)
            ++count;
    }
    return count;
}

//----------------------------------------------

bool WorkerManager::canStartNow() const
{
    return activeCount() < maxConcurrent();
}

//----------------------------------------------

bool WorkerManager::canSpawnManagerUnder(Worker *parent) const
{
    if (!parent)
        return false;
    return parent->depth() + 1 < maxDepth();
}

//----------------------------------------------

bool WorkerManager::canSpawnLeafUnder(Worker *parent) const
{
    if (!parent)
        return false;
    return parent->depth() + 1 <= maxDepth();
}

//----------------------------------------------

QString WorkerManager::limitsSummaryFor(Worker *worker) const
{
    if (!worker)
        return QString();
    if (worker->isTool())
        return QString::fromUtf8(
            "You are a shared tool outside the worker tree: depth limits do not apply, "
            "you cannot create workers or tools. At most %1 workers run at the same time; "
            "you may briefly wait in a queue before a run starts.")
            .arg(maxConcurrent());
    const int depth = worker->depth();
    const int limit = maxDepth();
    QString text = QString::fromUtf8(
        "Worker tree limits: your depth is %1 of max %2 (root = 0). ")
                       .arg(depth)
                       .arg(limit);
    if (worker->type() == WorkerTypeManager)
    {
        if (!canSpawnLeafUnder(worker))
        {
            text += QString::fromUtf8(
                "You are at the maximum depth and CANNOT spawn any child workers; "
                "answer with what you can determine yourself. ");
        }
        else if (!canSpawnManagerUnder(worker))
        {
            text += QString::fromUtf8(
                "You are at the LAST manager level: type=manager is forbidden here. "
                "Give every subtask, including complex ones, to type=executor "
                "(split it yourself into executor-sized pieces) and verify with type=tester. ");
        }
        else
        {
            text += QString::fromUtf8(
                "Child managers are allowed (they would be at depth %1); "
                "managers deeper than %2 are impossible, so plan the split accordingly. ")
                        .arg(depth + 1)
                        .arg(limit - 1);
        }
    }
    text += QString::fromUtf8(
        "At most %1 workers talk to the model at the same time in the whole tree. "
        "Extra children wait in a queue and start automatically. "
        "Keep unfinished children ≤%2 per parent (economical parallelism). "
        "Subtree budgets: ≤%3 tool-batches and ≤%4 min wall time. "
        "User Stop is the kill-switch for all workers.")
                .arg(maxConcurrent())
                .arg(kMaxChildrenPerParent)
                .arg(kMaxSubtreeToolBatches)
                .arg(int(kMaxSubtreeMs / 60000));
    return text;
}

//----------------------------------------------

QString WorkerManager::loadSummary() const
{
    return QString::fromUtf8(
        "Workers active: %1 of %2 allowed, queued: %3. "
        "Session telemetry: spawned=%4 completed=%5. "
        "Max children/parent: %6. Subtree budgets: ≤%7 tool-batches, ≤%8 min.")
        .arg(activeCount())
        .arg(maxConcurrent())
        .arg(queuedCount())
        .arg(_spawnCount)
        .arg(_completeCount)
        .arg(kMaxChildrenPerParent)
        .arg(kMaxSubtreeToolBatches)
        .arg(int(kMaxSubtreeMs / 60000));
}

//----------------------------------------------

void WorkerManager::requestStart(Worker *worker)
{
    if (!worker || !_workers.contains(worker))
        return;
    // Уже ответивших не стартуем, пока родитель не сбросит complete
    // через beginFollowUp/sendFollowUp (там _completed=false).
    if (worker->hasAnswered())
        return;
    if (canStartNow())
    {
        _startQueue.removeAll(worker);
        if (worker->hasPendingFollowUps())
        {
            QMetaObject::invokeMethod(worker, "sltFlushFollowUps", Qt::QueuedConnection);
            return;
        }
        if (worker->state() == WorkerStateRunning
            && worker->session() && worker->session()->agent()
            && worker->session()->agent()->isBusy())
            return;
        worker->startTask();
        return;
    }
    if (!_startQueue.contains(worker))
        _startQueue.append(worker);
    worker->setState(WorkerStateQueued);
}

//----------------------------------------------

void WorkerManager::sltScheduleQueuePump()
{
    if (_pumpScheduled)
        return;
    _pumpScheduled = true;
    QMetaObject::invokeMethod(this, "sltPumpQueue", Qt::QueuedConnection);
}

//----------------------------------------------

void WorkerManager::sltPumpQueue()
{
    _pumpScheduled = false;
    while (!_startQueue.isEmpty() && canStartNow())
    {
        Worker *worker = _startQueue.takeFirst();
        if (!worker || !_workers.contains(worker))
            continue;
        if (worker->hasAnswered())
            continue;
        if (worker->state() != WorkerStateQueued)
            continue;
        if (worker->hasPendingFollowUps())
        {
            QMetaObject::invokeMethod(worker, "sltFlushFollowUps", Qt::QueuedConnection);
            continue;
        }
        worker->startTask();
    }
    if (!canStartNow())
        return;
    foreach (Worker *worker, _workers)
    {
        if (!canStartNow())
            break;
        if (!worker || worker->state() != WorkerStateQueued)
            continue;
        if (!worker->hasPendingFollowUps())
            continue;
        QMetaObject::invokeMethod(worker, "sltFlushFollowUps", Qt::QueuedConnection);
    }
}

//----------------------------------------------

void WorkerManager::sltLimitsChanged()
{
    foreach (Worker *worker, _workers)
    {
        refreshPrompt(worker);
        if (!worker || !worker->session() || !worker->session()->agent())
            continue;
        Agent *agent = worker->session()->agent();
        agent->applyRoleSettings(worker->type());
        agent->refreshContextUsage();
    }
    sltScheduleQueuePump();
}

//----------------------------------------------

void WorkerManager::refreshPrompt(Worker *worker)
{
    if (!worker || !worker->session() || !worker->session()->agent())
        return;
    worker->session()->agent()->setSystemPrompt(promptFor(worker));
}

//----------------------------------------------

Worker *WorkerManager::createRoot(ChatSession *session)
{
    const WorkerPreset preset = AppSettings::instance()->rootWorkerPreset();
    QString profileId;
    // Главный менеджер — только роль Manager.
    if (!preset.id.isEmpty() && preset.type == WorkerTypeManager)
        profileId = preset.profileId.trimmed();
    Worker *worker = createWorker(WorkerTypeManager,
                                  session ? session->id() : QString(), session,
                                  0, QString(), profileId);
    emit treeChanged();
    return worker;
}

//----------------------------------------------

Worker *WorkerManager::createWorker(WorkerType type, QString id, ChatSession *session,
                                    Worker *parent, QString task, QString llmProfileId)
{
    Worker *worker = new Worker(this);
    worker->setType(type);
    if (!id.trimmed().isEmpty())
        worker->setId(id);
    else if (session)
        worker->setId(session->id());
    worker->setTask(task);
    if (!llmProfileId.trimmed().isEmpty())
        worker->setLlmProfileId(llmProfileId);
    worker->setSession(session);
    if (session)
        session->setWorker(worker);
    if (parent)
        parent->addChild(worker);
    if (type != WorkerTypeTool)
        worker->setPersonName(allocatePersonName());
    _workers.append(worker);
    connect(worker, SIGNAL(stateChanged()), this, SLOT(sltScheduleQueuePump()));
    connect(worker, SIGNAL(finished()), this, SLOT(sltScheduleQueuePump()));
    if (type == WorkerTypeTool)
    {
        connect(worker, SIGNAL(stateChanged()), this, SLOT(sltToolStateChanged()));
        connect(worker, SIGNAL(finished()), this, SLOT(sltToolFinished()));
    }
    emit workerCreated(worker);
    return worker;
}

//----------------------------------------------

Worker *WorkerManager::createTool(Worker *creator, QString name, QString purpose, QString *error)
{
    if (!creator || creator->type() != WorkerTypeManager)
    {
        if (error)
            *error = QString::fromUtf8("Only managers can create tools.");
        return 0;
    }
    const QString cleanName = name.simplified();
    const QString cleanPurpose = purpose.trimmed();
    if (cleanName.isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("Tool name is empty.");
        return 0;
    }
    if (cleanPurpose.isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("Tool purpose is empty.");
        return 0;
    }
    Worker *existing = toolByIdOrName(cleanName);
    if (existing)
    {
        if (error)
            *error = QString::fromUtf8(
                "A tool named '%1' already exists (tool_id=%2). Reuse it with run_tool "
                "instead of creating a duplicate.\n%3")
                         .arg(cleanName, existing->id(), toolsSummary());
        return 0;
    }

    Worker *tool = createWorker(WorkerTypeTool, QString(), 0, 0, QString());
    tool->setToolName(cleanName);
    tool->setToolPurpose(cleanPurpose);
    refreshPrompt(tool);
    emit treeChanged();
    return tool;
}

//----------------------------------------------

Worker *WorkerManager::toolByIdOrName(QString key) const
{
    const QString clean = key.trimmed();
    if (clean.isEmpty())
        return 0;
    foreach (Worker *worker, _workers)
    {
        if (worker && worker->isTool() && worker->id() == clean)
            return worker;
    }
    foreach (Worker *worker, _workers)
    {
        if (worker && worker->isTool()
            && worker->toolName().compare(clean, Qt::CaseInsensitive) == 0)
            return worker;
    }
    return 0;
}

//----------------------------------------------

int WorkerManager::toolQueueLength(Worker *tool) const
{
    if (!tool)
        return 0;
    return _toolQueues.value(tool->id()).size();
}

//----------------------------------------------

bool WorkerManager::toolBusy(Worker *tool) const
{
    if (!tool)
        return false;
    if (_toolCurrent.contains(tool->id()))
        return true;
    return !tool->toolIsFree();
}

//----------------------------------------------

QString WorkerManager::toolsSummary() const
{
    const QList<Worker *> list = tools();
    if (list.isEmpty())
        return QString::fromUtf8("No tools exist yet.");
    QString text = QString::fromUtf8("Available tools (%1):").arg(list.size());
    foreach (Worker *tool, list)
    {
        QString status = toolBusy(tool) ? QString::fromUtf8("busy") : QString::fromUtf8("free");
        const int queued = toolQueueLength(tool);
        if (queued > 0)
            status += QString::fromUtf8(", %1 waiting").arg(queued);
        QString purpose = tool->toolPurpose().simplified();
        if (purpose.size() > 300)
            purpose = purpose.left(297) + QString::fromUtf8("...");
        text += QString::fromUtf8("\n- tool_id=%1 name=\"%2\" [%3]: %4")
                    .arg(tool->id(), tool->toolName(), status, purpose);
    }
    return text;
}

//----------------------------------------------

QString WorkerManager::runTool(Worker *caller, QString toolKey, QString message, QString *error)
{
    if (!caller)
    {
        if (error)
            *error = QString::fromUtf8("Calling worker is missing.");
        return QString();
    }
    if (caller->isTool())
    {
        if (error)
            *error = QString::fromUtf8("A tool cannot run other tools.");
        return QString();
    }
    Worker *tool = toolByIdOrName(toolKey);
    if (!tool)
    {
        if (error)
            *error = QString::fromUtf8("No tool with tool_id or name '%1'.\n%2")
                         .arg(toolKey.trimmed(), toolsSummary());
        return QString();
    }
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("Run request text is empty.");
        return QString();
    }

    ToolRun run;
    run.id = QString::fromUtf8("run-%1").arg(++_runCounter);
    run.caller = caller;
    run.message = QString::fromUtf8(
        "Run request from %1 worker (worker_id=%2). Do it within your purpose and "
        "finish with complete_task; the result goes back to that worker.\n\n%3")
                      .arg(workerTypeToString(caller->type()), caller->id(), trimmed);
    _toolQueues[tool->id()].append(run);
    pumpTool(tool);

    // Ждём результат, не занимая слот параллельности: инструмент должен
    // иметь возможность стартовать, даже если все слоты держат ожидающие.
    QPointer<Worker> callerPtr(caller);
    QPointer<Worker> toolPtr(tool);
    caller->setWaitingForTool(true);
    sltScheduleQueuePump();

    QEventLoop loop;
    connect(this, SIGNAL(toolRunFinished(QString)), &loop, SLOT(quit()));
    connect(caller, SIGNAL(finished()), &loop, SLOT(quit()));
    connect(caller, SIGNAL(permissionResolved()), &loop, SLOT(quit()));
    connect(caller, SIGNAL(destroyed()), &loop, SLOT(quit()));
    while (!_finishedRuns.contains(run.id))
    {
        if (callerPtr.isNull() || callerPtr->isUserStopped() || callerPtr->hasAnswered())
            break;
        if (toolPtr.isNull())
            break;
        loop.exec();
    }
    if (callerPtr)
        callerPtr->setWaitingForTool(false);

    if (_finishedRuns.contains(run.id))
    {
        if (toolPtr && callerPtr)
        {
            foreach (const QString &item, toolPtr->changedFiles())
                callerPtr->noteFileChanged(item);
        }
        return _finishedRuns.take(run.id);
    }

    // Прервано: снимаем запрос из очереди, текущий запуск инструмента
    // доработает сам, результат никому не нужен.
    if (toolPtr)
    {
        QList<ToolRun> &queue = _toolQueues[toolPtr->id()];
        for (int i = 0; i < queue.size(); ++i)
        {
            if (queue.at(i).id == run.id)
            {
                queue.removeAt(i);
                break;
            }
        }
    }
    if (error)
        *error = QString::fromUtf8("Tool run was interrupted before the tool answered.");
    return QString();
}

//----------------------------------------------

void WorkerManager::pumpTool(Worker *tool)
{
    if (!tool || !_workers.contains(tool))
        return;
    const QString toolId = tool->id();
    if (_toolCurrent.contains(toolId))
        return;
    QList<ToolRun> &queue = _toolQueues[toolId];
    while (!queue.isEmpty() && queue.first().caller.isNull())
        queue.removeFirst();
    if (queue.isEmpty())
        return;
    if (!tool->toolIsFree())
        return;
    const ToolRun run = queue.takeFirst();
    _toolCurrent.insert(toolId, run);
    tool->beginToolRun(run.message);
}

//----------------------------------------------

void WorkerManager::finishToolRun(QString runId, QString result)
{
    if (runId.isEmpty())
        return;
    _finishedRuns.insert(runId, result);
    emit toolRunFinished(runId);
}

//----------------------------------------------

void WorkerManager::failToolRuns(Worker *tool, QString reason)
{
    if (!tool)
        return;
    const QString toolId = tool->id();
    if (_toolCurrent.contains(toolId))
    {
        const ToolRun current = _toolCurrent.take(toolId);
        finishToolRun(current.id, reason);
    }
    const QList<ToolRun> queue = _toolQueues.take(toolId);
    foreach (const ToolRun &run, queue)
        finishToolRun(run.id, reason);
}

//----------------------------------------------

void WorkerManager::sltToolStateChanged()
{
    Worker *tool = qobject_cast<Worker *>(sender());
    if (!tool || !tool->isTool())
        return;
    if (!_toolCurrent.contains(tool->id()))
        pumpTool(tool);
}

//----------------------------------------------

void WorkerManager::sltToolFinished()
{
    Worker *tool = qobject_cast<Worker *>(sender());
    if (!tool || !tool->isTool())
        return;
    const QString toolId = tool->id();
    if (_toolCurrent.contains(toolId))
    {
        const ToolRun current = _toolCurrent.take(toolId);
        QString result = tool->result().trimmed();
        if (result.isEmpty())
            result = QString::fromUtf8("(tool produced no output)");
        if (tool->state() == WorkerStateFailed)
            result = QString::fromUtf8("TOOL FAILED: ") + result;
        finishToolRun(current.id, result);
    }
    // Следующий в очереди — после того, как инструмент действительно
    // освободится (агент ещё занят, состояние обновится асинхронно).
    QMetaObject::invokeMethod(this, "sltPumpTools", Qt::QueuedConnection);
}

//----------------------------------------------

void WorkerManager::sltPumpTools()
{
    foreach (Worker *tool, tools())
    {
        if (tool && !_toolCurrent.contains(tool->id()))
            pumpTool(tool);
    }
}

//----------------------------------------------

bool WorkerManager::destroyTool(QString id)
{
    Worker *tool = workerById(id);
    if (!tool || !tool->isTool())
        return false;
    destroyWorkerNow(tool);
    emit treeChanged();
    return true;
}

//----------------------------------------------

bool WorkerManager::canSpawn(WorkerType parentType, WorkerType childType) const
{
    Q_UNUSED(childType);
    if (parentType == WorkerTypeManager)
        return true;
    return false;
}

//----------------------------------------------

Worker *WorkerManager::spawn(Worker *parent, WorkerType type, QString task, QString *error,
                             QString llmProfileId)
{
    if (!parent)
    {
        if (error)
            *error = QString::fromUtf8("Parent worker is missing.");
        return 0;
    }
    if (!parent->canSpawnChildren())
    {
        if (error)
            *error = QString::fromUtf8(
                "This worker cannot create children (group interlocutors may discuss and "
                "act, but only the group chief / a manager may delegate tasks).");
        return 0;
    }
    if (type == WorkerTypeTool)
    {
        if (error)
            *error = QString::fromUtf8(
                "Tools are not children: use create_tool to make a shared tool and run_tool "
                "to run it. delegate_task accepts only manager, executor or tester.");
        return 0;
    }
    if (!canSpawn(parent->type(), type))
    {
        if (error)
            *error = QString::fromUtf8(
                "This worker type is not allowed to delegate to '%1'.")
                         .arg(workerTypeToString(type));
        return 0;
    }
    if (!canSpawnLeafUnder(parent))
    {
        if (error)
            *error = QString::fromUtf8(
                "Worker tree depth limit reached (max depth %1, you are at depth %2). "
                "No child workers can be created here; finish the task yourself.")
                         .arg(maxDepth())
                         .arg(parent->depth());
        return 0;
    }
    if (parent->unfinishedChildren().size() >= kMaxChildrenPerParent)
    {
        if (error)
            *error = QString::fromUtf8(
                "Child budget exhausted: at most %1 unfinished children per parent "
                "(economical parallelism). Wait for results or dismiss finished work "
                "before spawning more.")
                         .arg(kMaxChildrenPerParent);
        return 0;
    }
    if (parent->subtreeToolBatches() >= parent->subtreeBatchLimit()
        || parent->subtreeElapsedMs() >= parent->subtreeMsLimit())
    {
        if (!parent->requestSubtreeBudgetGrant(kMaxSubtreeToolBatches, kMaxSubtreeMs))
        {
            if (error)
            {
                if (parent->subtreeToolBatches() >= parent->subtreeBatchLimit())
                {
                    *error = QString::fromUtf8(
                        "Subtree tool-batch budget exhausted (%1 / %2). "
                        "User denied reset. Finish with complete_task or stop — "
                        "do not spawn more children.")
                                 .arg(parent->subtreeToolBatches())
                                 .arg(parent->subtreeBatchLimit());
                }
                else
                {
                    *error = QString::fromUtf8(
                        "Subtree time budget exhausted (%1 min). User denied reset.")
                                 .arg(int(parent->subtreeMsLimit() / 60000));
                }
            }
            return 0;
        }
    }
    if (parent->subtreeToolBatches() >= parent->subtreeBatchLimit()
        || parent->subtreeElapsedMs() >= parent->subtreeMsLimit())
    {
        if (error)
            *error = QString::fromUtf8(
                "Subtree budget still exhausted after grant "
                "(batches %1/%2, time ≈%3/%4 min).")
                         .arg(parent->subtreeToolBatches())
                         .arg(parent->subtreeBatchLimit())
                         .arg(int(parent->subtreeElapsedMs() / 60000))
                         .arg(int(parent->subtreeMsLimit() / 60000));
        return 0;
    }
    if (type == WorkerTypeManager && !canSpawnManagerUnder(parent))
    {
        if (error)
            *error = QString::fromUtf8(
                "Depth limit: a manager at depth %1 would exceed the maximum manager depth "
                "(%2). Use type=executor for this subtask instead and split it yourself.")
                         .arg(parent->depth() + 1)
                         .arg(maxDepth() - 1);
        return 0;
    }
    QString trimmed = task.trimmed();
    if (trimmed.isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("Task text is empty.");
        return 0;
    }
    // CONTEXT HANDOFF уходит в injectContext при startTask (не в текст задания).
    if (type == WorkerTypeTester)
    {
        const QString files = changedFilesForChildren(parent);
        if (!files.isEmpty() && !trimmed.contains(QString::fromUtf8("Files changed")))
            trimmed += QString::fromUtf8("\n\n") + files;
    }

    Worker *child = createWorker(type, QString(), 0, parent, trimmed, llmProfileId);
    ++_spawnCount;
    emit treeChanged();
    return child;
}

//----------------------------------------------

QString WorkerManager::allocatePersonName() const
{
    static const char *kNames[] = {
        "Алексей", "Мария", "Иван", "Елена", "Дмитрий", "Анна", "Сергей", "Ольга",
        "Андрей", "Наталья", "Павел", "Екатерина", "Николай", "Татьяна", "Михаил",
        "Юлия", "Артём", "Ирина", "Владимир", "Светлана", "Роман", "Виктория",
        "Кирилл", "Дарья", "Максим", "Полина", "Игорь", "Алина", "Егор", "София",
        "Глеб", "Валерия", "Лев", "Ксения", "Тимур", "Вера", "Марк", "Людмила", 0
    };
    QSet<QString> used;
    foreach (Worker *worker, _workers)
    {
        if (worker && !worker->personName().isEmpty())
            used.insert(worker->personName());
    }
    for (int i = 0; kNames[i]; ++i)
    {
        const QString name = QString::fromUtf8(kNames[i]);
        if (!used.contains(name))
            return name;
    }
    return QString::fromUtf8("Воркер-%1").arg(used.size() + 1);
}

//----------------------------------------------

QList<Worker *> WorkerManager::groupMembers(QString groupId) const
{
    QList<Worker *> list;
    const QString id = groupId.trimmed();
    if (id.isEmpty())
        return list;
    foreach (Worker *worker, _workers)
    {
        if (worker && worker->groupId() == id)
            list.append(worker);
    }
    return list;
}

//----------------------------------------------

Worker *WorkerManager::addGroupMember(Worker *chief, QString presetId, QString *error)
{
    if (!chief)
    {
        if (error)
            *error = QString::fromUtf8("Chief worker is missing.");
        return 0;
    }
    if (!chief->canSpawnChildren() && !chief->isRoot())
    {
        if (error)
            *error = QString::fromUtf8(
                "Only the group chief (or root manager) can add group members.");
        return 0;
    }
    if (chief->isGroupInterlocutor())
    {
        if (error)
            *error = QString::fromUtf8("Group interlocutors cannot add members.");
        return 0;
    }

    const WorkerPreset preset = AppSettings::instance()->workerPresetById(presetId);
    if (preset.id.isEmpty())
    {
        if (error)
            *error = QString::fromUtf8(
                "Unknown preset_id. Available presets:\n%1")
                         .arg(AppSettings::instance()->workerPresetsSummary());
        return 0;
    }
    if (preset.type == WorkerTypeTool)
    {
        if (error)
            *error = QString::fromUtf8(
                "Tools are not group members; use create_tool / run_tool.");
        return 0;
    }

    // В группе у каждого участника — свой профиль модели.
    const QString wantProfile = preset.profileId.trimmed();
    if (!wantProfile.isEmpty())
    {
        QString chiefProfile = chief->llmProfileId().trimmed();
        if (chiefProfile.isEmpty())
            chiefProfile = AppSettings::instance()->profileForWorkerType(chief->type()).id;
        if (chiefProfile == wantProfile)
        {
            if (error)
                *error = QString::fromUtf8(
                    "Group members must use different model profiles. "
                    "The chief already uses this profile; pick another preset.\n%1")
                             .arg(AppSettings::instance()->workerPresetsSummary());
            return 0;
        }
        foreach (Worker *member, groupMembers(chief->groupId()))
        {
            if (!member || member == chief)
                continue;
            QString mid = member->llmProfileId().trimmed();
            if (mid.isEmpty())
                mid = AppSettings::instance()->profileForWorkerType(member->type()).id;
            if (mid == wantProfile)
            {
                if (error)
                    *error = QString::fromUtf8(
                        "Group already has a member with this model profile (%1). "
                        "Choose a preset with a different profile.\n%2")
                                 .arg(member->personName().isEmpty() ? member->id()
                                                                    : member->personName(),
                                      AppSettings::instance()->workerPresetsSummary());
                return 0;
            }
        }
    }

    if (chief->groupId().isEmpty())
    {
        QString gid = QUuid::createUuid().toString();
        gid.remove(QChar('{')).remove(QChar('}'));
        chief->setGroupId(gid);
        chief->setGroupRole(GroupRoleChief);
    }
    else if (chief->groupRole() == GroupRoleNone)
        chief->setGroupRole(GroupRoleChief);

    Worker *peer = createWorker(preset.type, QString(), 0, chief, QString(),
                                preset.profileId);
    peer->setGroupId(chief->groupId());
    peer->setGroupRole(GroupRoleInterlocutor);
    peer->setTask(QString::fromUtf8(
        "You are %1, an interlocutor in a discussion group led by %2 (%3). "
        "The group speaks in turns: only the current narrator may group_speak; "
        "everyone else must group_await. After discussion, group_propose a result and "
        "all members group_vote until unanimous approval. "
        "You MUST NOT create child workers or delegate tasks — only the chief does that.")
                      .arg(peer->personName(),
                           chief->personName().isEmpty() ? chief->id() : chief->personName(),
                           workerTypeToString(chief->type())));
    refreshPrompt(peer);
    installTools(peer);
    refreshPrompt(chief);
    installTools(chief);
    emit treeChanged();
    requestStart(peer);
    startGroupDialogue(chief->groupId());
    return peer;
}

//----------------------------------------------

bool WorkerManager::isGroupDialogueActive(QString groupId) const
{
    const GroupDialogue d = _groupDialogues.value(groupId.trimmed());
    return d.phase == GroupDialogueSpeaking || d.phase == GroupDialogueVoting;
}

//----------------------------------------------

bool WorkerManager::isGroupDialogueDone(QString groupId) const
{
    const GroupDialogue d = _groupDialogues.value(groupId.trimmed());
    return d.phase == GroupDialogueDone;
}

//----------------------------------------------

QString WorkerManager::groupDialogueOutcome(QString groupId) const
{
    return _groupDialogues.value(groupId.trimmed()).outcome;
}

//----------------------------------------------

void WorkerManager::syncDialogueOrder(GroupDialogue *dialogue) const
{
    if (!dialogue)
        return;
    const QList<Worker *> members = groupMembers(dialogue->groupId);
    QStringList order;
    foreach (Worker *worker, members)
    {
        if (worker && worker->isGroupChief())
            order.append(worker->id());
    }
    foreach (Worker *worker, members)
    {
        if (worker && !worker->isGroupChief())
            order.append(worker->id());
    }
    // Сохраняем известный порядок, добавляем новых в конец.
    QStringList merged;
    foreach (const QString &id, dialogue->order)
    {
        if (order.contains(id) && !merged.contains(id))
            merged.append(id);
    }
    foreach (const QString &id, order)
    {
        if (!merged.contains(id))
            merged.append(id);
    }
    dialogue->order = merged;
    if (dialogue->speakerIndex < 0 || dialogue->speakerIndex >= dialogue->order.size())
        dialogue->speakerIndex = 0;
}

//----------------------------------------------

Worker *WorkerManager::currentSpeaker(const GroupDialogue &dialogue) const
{
    if (dialogue.order.isEmpty())
        return 0;
    if (dialogue.speakerIndex < 0 || dialogue.speakerIndex >= dialogue.order.size())
        return 0;
    return workerById(dialogue.order.at(dialogue.speakerIndex));
}

//----------------------------------------------

int WorkerManager::appendGroupEvent(GroupDialogue *dialogue, QString kind, Worker *from,
                                    QString text)
{
    if (!dialogue)
        return 0;
    if (dialogue->nextSeq < 1)
        dialogue->nextSeq = 1;
    GroupUtterance u;
    u.seq = dialogue->nextSeq++;
    u.kind = kind;
    u.workerId = from ? from->id() : QString();
    u.name = from
        ? (from->personName().isEmpty() ? from->id() : from->personName())
        : QString::fromUtf8("system");
    u.text = text;
    dialogue->transcript.append(u);
    return u.seq;
}

//----------------------------------------------

int WorkerManager::latestSeq(const GroupDialogue &dialogue) const
{
    if (dialogue.transcript.isEmpty())
        return qMax(0, dialogue.nextSeq - 1);
    return dialogue.transcript.last().seq;
}

//----------------------------------------------

void WorkerManager::ackGroupCursor(GroupDialogue *dialogue, Worker *worker, int seq)
{
    if (!dialogue || !worker || seq <= 0)
        return;
    const int prev = dialogue->cursor.value(worker->id(), 0);
    if (seq > prev)
        dialogue->cursor.insert(worker->id(), seq);
}

//----------------------------------------------

QString WorkerManager::formatTranscript(const GroupDialogue &dialogue, int afterSeq) const
{
    QStringList lines;
    for (int i = 0; i < dialogue.transcript.size(); ++i)
    {
        const GroupUtterance &u = dialogue.transcript.at(i);
        if (u.seq <= afterSeq)
            continue;
        const QString kind = u.kind.isEmpty() ? QString::fromUtf8("speak") : u.kind;
        lines << QString::fromUtf8("#%1 [%2] %3: %4")
                     .arg(u.seq)
                     .arg(kind, u.name, u.text);
    }
    if (lines.isEmpty())
        return afterSeq > 0 ? QString::fromUtf8("(no new events)")
                            : QString::fromUtf8("(empty)");
    return lines.join(QString::fromUtf8("\n"));
}

//----------------------------------------------

QString WorkerManager::groupDialogueStatus(QString groupId, int afterSeq) const
{
    const QString id = groupId.trimmed();
    if (!_groupDialogues.contains(id))
        return QString::fromUtf8("No active group dialogue.");
    const GroupDialogue d = _groupDialogues.value(id);
    QString phase = QString::fromUtf8("idle");
    if (d.phase == GroupDialogueSpeaking)
        phase = QString::fromUtf8("speaking");
    else if (d.phase == GroupDialogueVoting)
        phase = QString::fromUtf8("voting");
    else if (d.phase == GroupDialogueDone)
        phase = QString::fromUtf8("done");
    Worker *speaker = currentSpeaker(d);
    const QString speakerName = speaker
        ? (speaker->personName().isEmpty() ? speaker->id() : speaker->personName())
        : QString::fromUtf8("-");
    return QString::fromUtf8(
        "phase=%1; narrator=%2 (worker_id=%3); turns_passed=%4; members=%5; seq=%6\n"
        "Transcript:\n%7")
        .arg(phase, speakerName,
             speaker ? speaker->id() : QString::fromUtf8("-"),
             QString::number(d.speakCount),
             QString::number(d.order.size()), QString::number(latestSeq(d)),
             formatTranscript(d, afterSeq));
}

//----------------------------------------------

/**
 * @brief Delta of new events for one participant (cursor + roundId dedup).
 *
 * @return false when there is nothing new to send.
 */
bool WorkerManager::deltaFor(GroupDialogue *dialogue, Worker *worker,
                             QString *delta, bool *fullTranscript)
{
    if (!dialogue || !worker || !delta || !fullTranscript)
        return false;
    const int after = dialogue->cursor.value(worker->id(), 0);
    // A round the participant has not seen yet is shown as a full transcript
    // (his own history may have been compacted), but exactly once per round.
    *fullTranscript = (dialogue->roundSeen.value(worker->id(), -1)
        != dialogue->roundId);
    ackGroupCursor(dialogue, worker, latestSeq(*dialogue));
    dialogue->roundSeen.insert(worker->id(), dialogue->roundId);
    const QString text = formatTranscript(*dialogue,
        *fullTranscript ? 0 : after);
    *delta = text;
    return !(text.isEmpty()
        || text.startsWith(QString::fromUtf8("(no new"))
        || text.startsWith(QString::fromUtf8("(empty")));
}

//----------------------------------------------

QString WorkerManager::resumeMessageFor(Worker *worker, GroupDialogue *dialogue, bool ackCursor)
{
    if (!worker || !dialogue)
        return QString();
    const int after = dialogue->cursor.value(worker->id(), 0);
    const QString delta = formatTranscript(*dialogue, after);
    const int tipSeq = latestSeq(*dialogue);
    if (ackCursor)
        ackGroupCursor(dialogue, worker, tipSeq);

    if (dialogue->phase == GroupDialogueDone)
    {
        return QString::fromUtf8(
            "[Group dialogue DONE] seq=%1\nApproved result:\n%2\n\n"
            "New events since your cursor:\n%3\n\n"
            "This ROUND is finished. To discuss again with the same members, "
            "call group_speak or group_propose (opens a new round). "
            "Do not group_vote on the old proposal.")
            .arg(tipSeq)
            .arg(dialogue->outcome, delta);
    }
    if (dialogue->phase == GroupDialogueVoting)
    {
        const bool voted = dialogue->voted.contains(worker->id());
        if (voted)
        {
            return QString::fromUtf8(
                "[Waiting for other votes] seq=%1\nProposal by %2:\n%3\n\n"
                "New events:\n%4\n\n"
                "You already voted. Call group_await ONCE to park until the vote ends.")
                .arg(tipSeq)
                .arg(dialogue->proposalBy, dialogue->proposal, delta);
        }
        return QString::fromUtf8(
            "[Group VOTE required] seq=%1\nProposal by %2:\n%3\n\n"
            "New events:\n%4\n\n"
            "ACTION: call group_vote(approve=true|false, comment=...) NOW. "
            "Unanimous approval required. This is not a broadcast of others' speech.")
            .arg(tipSeq)
            .arg(dialogue->proposalBy, dialogue->proposal, delta);
    }
    Worker *speaker = currentSpeaker(*dialogue);
    if (speaker == worker)
    {
        const QString tip = QString::fromUtf8(
            "You are the NARRATOR now. Call group_speak(message), then group_end_turn(). "
            "When the group has a concrete shared answer, call group_propose(result). "
            "Do NOT call group_await while you are narrator. No hard limit on turns.");
        return QString::fromUtf8(
            "[Your turn as narrator] seq=%1\n%2\n\nNew events:\n%3")
            .arg(tipSeq)
            .arg(tip, delta);
    }
    const QString speakerName = speaker
        ? (speaker->personName().isEmpty() ? speaker->id() : speaker->personName())
        : QString::fromUtf8("?");
    return QString::fromUtf8(
        "[Waiting] seq=%1 Narrator is %2. Call group_await ONCE to park until YOUR turn "
        "or vote (exactly-once wake). Do not poll.\nNew events so far:\n%3")
        .arg(tipSeq)
        .arg(speakerName, delta);
}

//----------------------------------------------

bool WorkerManager::workerShouldAct(Worker *worker, const GroupDialogue &dialogue) const
{
    if (!worker)
        return false;
    if (dialogue.phase == GroupDialogueDone)
        return !dialogue.terminalDelivered.contains(worker->id());
    if (dialogue.phase == GroupDialogueVoting)
        return !dialogue.voted.contains(worker->id());
    if (dialogue.phase == GroupDialogueSpeaking)
        return currentSpeaker(dialogue) == worker;
    return false;
}

//----------------------------------------------

void WorkerManager::wakeGroupAwaiter(Worker *worker, QString message)
{
    if (!worker)
        return;
    const QString id = worker->id();
    if (!_groupAwaiting.contains(id))
        return;
    _groupAwaitResults.insert(id, message);
    emit groupAwaitFinished(id);
}

//----------------------------------------------

void WorkerManager::failGroupAwaitsForWorker(Worker *worker, QString reason)
{
    if (!worker)
        return;
    if (_groupAwaiting.contains(worker->id()))
        wakeGroupAwaiter(worker, reason);
    const QString gid = worker->groupId();
    if (gid.isEmpty() || !_groupDialogues.contains(gid))
        return;
    foreach (Worker *member, groupMembers(gid))
    {
        if (member && member != worker && _groupAwaiting.contains(member->id()))
            wakeGroupAwaiter(member, reason);
    }
}

//----------------------------------------------

void WorkerManager::deliverGroupAction(Worker *worker, GroupDialogue *dialogue)
{
    if (!worker || !dialogue)
        return;
    if (!workerShouldAct(worker, *dialogue))
        return;
    if (dialogue->phase == GroupDialogueDone
        && dialogue->terminalDelivered.contains(worker->id()))
        return;

    // Сбрасываем устаревшие «чужие» follow-up, чтобы не копить narrator/vote/DONE.
    worker->clearPendingFollowUps();

    // Incremental delivery: no repeat of the whole transcript each turn.
    QString delta;
    bool fullTranscript = false;
    const bool hasDelta = deltaFor(dialogue, worker, &delta, &fullTranscript);
    const QString msg = resumeMessageFor(worker, dialogue, false);
    QString out;
    if (!hasDelta && !fullTranscript)
    {
        out = msg;
    }
    else
    {
        const QStringList parts = msg.split(QChar('\n'));
        QStringList head;
        for (int i = 0; i < parts.size(); ++i)
        {
            if (parts.at(i).startsWith(QString::fromUtf8("New events")))
            break;
            head.append(parts.at(i));
        }
        out = head.join(QChar('\n')).trimmed();
        if (out.isEmpty())
            out = msg;
        else if (fullTranscript)
            out += QString::fromUtf8("\n\nTranscript:\n") + delta;
        else
            out += QString::fromUtf8("\n\nNew events:\n") + delta;
    }
    if (dialogue->phase == GroupDialogueDone)
        dialogue->terminalDelivered.insert(worker->id());

    if (_groupAwaiting.contains(worker->id()))
        wakeGroupAwaiter(worker, msg);
    worker->beginFollowUp(out);
}

//----------------------------------------------

void WorkerManager::advanceDialogueAfterChange(QString groupId)
{
    if (!_groupDialogues.contains(groupId))
        return;
    GroupDialogue &d = _groupDialogues[groupId];
    syncDialogueOrder(&d);
    foreach (Worker *member, groupMembers(groupId))
    {
        if (!member)
            continue;
        deliverGroupAction(member, &d);
    }
    saveGroupDialogues();
}

//----------------------------------------------

void WorkerManager::finishGroupDialogue(QString groupId, QString outcome)
{
    if (!_groupDialogues.contains(groupId))
        return;
    GroupDialogue &d = _groupDialogues[groupId];
    d.phase = GroupDialogueDone;
    d.outcome = outcome.trimmed();
    d.lastOutcome = d.outcome;
    appendGroupEvent(&d, QString::fromUtf8("done"), 0, d.outcome);

    foreach (Worker *member, groupMembers(groupId))
    {
        if (!member)
            continue;
        member->clearPendingFollowUps();
    }

    saveGroupDialogues();

    foreach (Worker *member, groupMembers(groupId))
    {
        if (!member)
            continue;
        if (d.terminalDelivered.contains(member->id()))
            continue;
        // Собеседников завершаем без лишнего LLM-хода; chief получает один DONE.
        if (member->isGroupInterlocutor())
        {
            d.terminalDelivered.insert(member->id());
            ackGroupCursor(&d, member, latestSeq(d));
            if (_groupAwaiting.contains(member->id()))
            {
                wakeGroupAwaiter(member,
                                 QString::fromUtf8(
                                     "[Group dialogue DONE]\n%1").arg(d.outcome));
            }
            member->complete(QString::fromUtf8("Group dialogue DONE:\n%1").arg(d.outcome),
                             true);
            continue;
        }
        deliverGroupAction(member, &d);
    }
    saveGroupDialogues();
}

//----------------------------------------------

void WorkerManager::startGroupDialogue(QString groupId)
{
    const QString id = groupId.trimmed();
    if (id.isEmpty())
        return;
    const QList<Worker *> members = groupMembers(id);
    if (members.size() < 2)
        return;

    GroupDialogue d = _groupDialogues.value(id);
    const bool fresh = (d.phase == GroupDialogueIdle || d.phase == GroupDialogueDone
                        || !_groupDialogues.contains(id));
    d.groupId = id;
    syncDialogueOrder(&d);
    if (fresh)
    {
        if (d.phase == GroupDialogueDone && !d.outcome.isEmpty())
            d.lastOutcome = d.outcome;
        d.speakerIndex = 0;
        d.speakCount = 0;
        d.phase = GroupDialogueSpeaking;
        d.transcript.clear();
        d.proposal.clear();
        d.proposalBy.clear();
        d.voted.clear();
        d.approvals.clear();
        d.voteComments.clear();
        d.outcome.clear();
        d.nextSeq = 1;
        d.cursor.clear();
        // New round: the transcript restarts, so a full transcript goes only
        // to participants who have not seen this round yet.
        ++d.roundId;
        d.roundSeen.clear();
        d.terminalDelivered.clear();
        foreach (Worker *member, members)
        {
            if (member)
                member->clearPendingFollowUps();
        }
    }
    else
    {
        syncDialogueOrder(&d);
    }
    _groupDialogues.insert(id, d);
    saveGroupDialogues();

    // Только кто должен действовать (обычно chief-narrator) — без бродкаста всем.
    advanceDialogueAfterChange(id);
}

//----------------------------------------------

bool WorkerManager::reopenGroupDialogue(QString groupId, Worker *starter, QString *error)
{
    const QString id = groupId.trimmed();
    if (id.isEmpty() || !starter || starter->groupId() != id)
    {
        if (error)
            *error = QString::fromUtf8("Not in this group.");
        return false;
    }
    if (groupMembers(id).size() < 2)
    {
        if (error)
            *error = QString::fromUtf8("Group dialogue needs at least 2 members.");
        return false;
    }
    // Принудительно новый раунд: phase=Done → startGroupDialogue сбросит состояние.
    if (_groupDialogues.contains(id))
    {
        GroupDialogue &d = _groupDialogues[id];
        if (d.phase != GroupDialogueDone && d.phase != GroupDialogueIdle)
        {
            if (error)
                *error = QString::fromUtf8(
                    "A dialogue round is already in progress (phase not done).");
            return false;
        }
        d.phase = GroupDialogueDone;
        if (d.outcome.isEmpty() && !d.lastOutcome.isEmpty())
            d.outcome = d.lastOutcome;
    }
    startGroupDialogue(id);
    // Стартер (обычно chief) становится первым рассказчиком.
    if (_groupDialogues.contains(id) && starter)
    {
        GroupDialogue &d = _groupDialogues[id];
        const int idx = d.order.indexOf(starter->id());
        if (idx >= 0)
            d.speakerIndex = idx;
        saveGroupDialogues();
        advanceDialogueAfterChange(id);
    }
    return _groupDialogues.contains(id)
           && _groupDialogues.value(id).phase == GroupDialogueSpeaking;
}

//----------------------------------------------

QString WorkerManager::groupSpeak(Worker *from, QString message, QString *error)
{
    if (!from || from->groupId().isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("Not in a group.");
        return QString();
    }
    if (!_groupDialogues.contains(from->groupId()))
        startGroupDialogue(from->groupId());
    if (!_groupDialogues.contains(from->groupId()))
    {
        if (error)
            *error = QString::fromUtf8("Group dialogue needs at least 2 members.");
        return QString();
    }
    if (_groupDialogues.value(from->groupId()).phase == GroupDialogueDone)
    {
        QString reopenError;
        if (!reopenGroupDialogue(from->groupId(), from, &reopenError))
        {
            if (error)
                *error = reopenError;
            return QString();
        }
    }
    GroupDialogue &d = _groupDialogues[from->groupId()];
    syncDialogueOrder(&d);
    if (d.phase == GroupDialogueVoting)
    {
        if (error)
            *error = QString::fromUtf8(
                "Voting in progress. Call group_vote, not group_speak.");
        return QString();
    }
    if (currentSpeaker(d) != from)
    {
        if (error)
            *error = QString::fromUtf8(
                "Only the current narrator may group_speak. Call group_await.");
        return QString();
    }
    const QString body = message.trimmed();
    if (body.isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("Message is empty.");
        return QString();
    }
    appendGroupEvent(&d, QString::fromUtf8("speak"), from, body);
    ackGroupCursor(&d, from, latestSeq(d));
    saveGroupDialogues();
    QString prefix;
    if (!d.lastOutcome.isEmpty() && d.speakCount == 0 && d.transcript.size() == 1)
    {
        prefix = QString::fromUtf8(
            "New discussion round started (previous outcome kept for context:\n%1).\n")
                     .arg(d.lastOutcome);
    }
    return QString::fromUtf8(
        "%1Replica recorded (seq=%2). Your own words are already in this "
        "conversation, so they are not repeated back to you. Listeners are "
        "NOT woken separately. Call group_end_turn() when finished, or "
        "group_propose(result) for a vote.")
        .arg(prefix)
        .arg(latestSeq(d));
}

//----------------------------------------------

QString WorkerManager::groupEndTurn(Worker *from, QString *error)
{
    if (!from || from->groupId().isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("Not in a group.");
        return QString();
    }
    if (!_groupDialogues.contains(from->groupId()))
    {
        if (error)
            *error = QString::fromUtf8("No active group dialogue.");
        return QString();
    }
    GroupDialogue &d = _groupDialogues[from->groupId()];
    syncDialogueOrder(&d);
    if (d.phase == GroupDialogueDone)
    {
        QString reopenError;
        if (!reopenGroupDialogue(from->groupId(), from, &reopenError))
        {
            if (error)
                *error = reopenError.isEmpty()
                    ? QString::fromUtf8("Dialogue finished. Call group_speak to start a new round.")
                    : reopenError;
            return QString();
        }
        return QString::fromUtf8(
            "Previous round was finished. New round opened; you are narrator — "
            "call group_speak, then group_end_turn when ready.");
    }
    if (d.phase != GroupDialogueSpeaking)
    {
        if (error)
            *error = QString::fromUtf8("Not in speaking phase.");
        return QString();
    }
    if (currentSpeaker(d) != from)
    {
        if (error)
            *error = QString::fromUtf8("Only the current narrator may end the turn.");
        return QString();
    }
    if (d.order.isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("No members in dialogue order.");
        return QString();
    }
    ++d.speakCount;
    d.speakerIndex = (d.speakerIndex + 1) % d.order.size();
    Worker *next = currentSpeaker(d);
    const QString nextName = next
        ? (next->personName().isEmpty() ? next->id() : next->personName())
        : QString::fromUtf8("?");
    appendGroupEvent(&d, QString::fromUtf8("turn"), from,
                     QString::fromUtf8("Pass floor to %1").arg(nextName));
    ackGroupCursor(&d, from, latestSeq(d));
    saveGroupDialogues();
    advanceDialogueAfterChange(from->groupId());
    return QString::fromUtf8(
        "Turn passed. Next narrator: %1 (worker_id=%2). Turns passed: %3.\n"
        "If you are not the next narrator, call group_await once.\n"
        "New events since your last turn:\n%4")
        .arg(nextName, next ? next->id() : QString(),
             QString::number(d.speakCount),
             formatTranscript(d, d.cursor.value(from->id(), 0)));
}

//----------------------------------------------

QString WorkerManager::groupPropose(Worker *from, QString result, QString *error)
{
    if (!from || from->groupId().isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("Not in a group.");
        return QString();
    }
    if (!_groupDialogues.contains(from->groupId()))
        startGroupDialogue(from->groupId());
    if (!_groupDialogues.contains(from->groupId()))
    {
        if (error)
            *error = QString::fromUtf8("Group dialogue needs at least 2 members.");
        return QString();
    }
    if (_groupDialogues.value(from->groupId()).phase == GroupDialogueDone)
    {
        QString reopenError;
        if (!reopenGroupDialogue(from->groupId(), from, &reopenError))
        {
            if (error)
                *error = reopenError;
            return QString();
        }
    }
    GroupDialogue &d = _groupDialogues[from->groupId()];
    syncDialogueOrder(&d);
    if (d.phase == GroupDialogueSpeaking && currentSpeaker(d) != from
        && !from->isGroupChief())
    {
        if (error)
            *error = QString::fromUtf8(
                "Only the current narrator (or chief) may group_propose.");
        return QString();
    }
    const QString body = result.trimmed();
    if (body.isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("Proposal is empty.");
        return QString();
    }
    d.phase = GroupDialogueVoting;
    d.proposal = body;
    d.proposalBy = from->personName().isEmpty() ? from->id() : from->personName();
    d.voted.clear();
    d.approvals.clear();
    d.voteComments.clear();
    d.voted.insert(from->id());
    d.approvals.insert(from->id());
    d.voteComments.insert(from->id(), QString::fromUtf8("proposer"));
    appendGroupEvent(&d, QString::fromUtf8("proposal"), from, body);
    ackGroupCursor(&d, from, latestSeq(d));
    saveGroupDialogues();

    if (d.approvals.size() >= d.order.size() && !d.order.isEmpty())
    {
        finishGroupDialogue(from->groupId(), d.proposal);
        return QString::fromUtf8(
            "Proposal approved unanimously:\n%1\n"
            "You may start another round later with group_speak / group_propose.")
            .arg(_groupDialogues.value(from->groupId()).outcome);
    }
    advanceDialogueAfterChange(from->groupId());
    return QString::fromUtf8(
        "Proposal submitted (seq=%1). Waiting for unanimous group_vote from other members.\n%2")
        .arg(latestSeq(d))
        .arg(body);
}

//----------------------------------------------

QString WorkerManager::groupVote(Worker *from, bool approve, QString comment, QString *error)
{
    if (!from || from->groupId().isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("Not in a group.");
        return QString();
    }
    if (!_groupDialogues.contains(from->groupId()))
    {
        if (error)
            *error = QString::fromUtf8("No active group dialogue.");
        return QString();
    }
    GroupDialogue &d = _groupDialogues[from->groupId()];
    syncDialogueOrder(&d);
    if (d.phase == GroupDialogueDone)
    {
        ackGroupCursor(&d, from, latestSeq(d));
        d.terminalDelivered.insert(from->id());
        saveGroupDialogues();
        return QString::fromUtf8(
            "No proposal pending — this round is finished.\nOutcome:\n%1\n"
            "To discuss again with the same members, call group_speak or group_propose "
            "(starts a new round).")
            .arg(d.outcome);
    }
    if (d.phase != GroupDialogueVoting)
    {
        return QString::fromUtf8(
            "No proposal pending. Narrator should group_propose first "
            "(or dialogue not in voting phase).");
    }
    if (d.voted.contains(from->id()))
    {
        return QString::fromUtf8(
            "You already voted on this proposal (idempotent). "
            "Call group_await once if waiting for others.");
    }
    d.voted.insert(from->id());
    d.voteComments.insert(from->id(), comment.trimmed());
    if (approve)
        d.approvals.insert(from->id());
    appendGroupEvent(&d, QString::fromUtf8("vote"), from,
                     approve ? QString::fromUtf8("APPROVE")
                             : QString::fromUtf8("REJECT: %1")
                                   .arg(comment.trimmed().isEmpty()
                                            ? QString::fromUtf8("(no comment)")
                                            : comment.trimmed()));
    ackGroupCursor(&d, from, latestSeq(d));

    if (!approve)
    {
        d.phase = GroupDialogueSpeaking;
        d.proposal.clear();
        const int idx = d.order.indexOf(from->id());
        if (idx >= 0)
            d.speakerIndex = (idx + 1) % d.order.size();
        d.voted.clear();
        d.approvals.clear();
        d.voteComments.clear();
        saveGroupDialogues();
        advanceDialogueAfterChange(from->groupId());
        return QString::fromUtf8(
            "Rejection recorded. Dialogue continues with the next narrator.");
    }

    if (d.approvals.size() >= d.order.size() && d.voted.size() >= d.order.size())
    {
        finishGroupDialogue(from->groupId(), d.proposal);
        return QString::fromUtf8(
            "Unanimous approval. Final result:\n%1\n"
            "Same group can start another round with group_speak / group_propose.")
            .arg(_groupDialogues.value(from->groupId()).outcome);
    }
    saveGroupDialogues();
    return QString::fromUtf8("Approve recorded (%1/%2). Waiting for remaining votes.")
        .arg(d.approvals.size())
        .arg(d.order.size());
}

//----------------------------------------------

QString WorkerManager::groupAwait(Worker *from, QString *error)
{
    if (!from || from->groupId().isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("Not in a group.");
        return QString();
    }
    if (!_groupDialogues.contains(from->groupId()))
        startGroupDialogue(from->groupId());
    if (!_groupDialogues.contains(from->groupId()))
    {
        if (error)
            *error = QString::fromUtf8("Group dialogue needs at least 2 members.");
        return QString();
    }

    GroupDialogue &d = _groupDialogues[from->groupId()];
    syncDialogueOrder(&d);

    if (d.phase == GroupDialogueDone)
    {
        const QString status = resumeMessageFor(from, &d, true);
        d.terminalDelivered.insert(from->id());
        from->clearPendingFollowUps();
        saveGroupDialogues();
        return status + QString::fromUtf8(
            "\n\nTo start another discussion round with the same members, "
            "call group_speak or group_propose (chief/narrator). "
            "group_await alone does not reopen the round.");
    }

    const QString status = resumeMessageFor(from, &d, true);
    saveGroupDialogues();

    if (workerShouldAct(from, d))
        return status;

    from->parkForGroupDialogue();
    return status + QString::fromUtf8(
        "\n\nParked until a follow-up arrives for YOUR turn/vote only. "
        "Do not call any more tools in this turn.");
}

//----------------------------------------------

QString WorkerManager::groupDialoguesPath() const
{
    const QString dir = ChatStore::storeDir();
    if (dir.isEmpty())
        return QString();
    return dir + QString::fromUtf8("/group_dialogues.json");
}

//----------------------------------------------

void WorkerManager::saveGroupDialogues() const
{
    const QString path = groupDialoguesPath();
    if (path.isEmpty())
        return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonArray items;
    foreach (const QString &gid, _groupDialogues.keys())
    {
        const GroupDialogue &d = _groupDialogues[gid];
        QJsonObject obj;
        obj.insert(QString::fromUtf8("groupId"), d.groupId);
        obj.insert(QString::fromUtf8("order"), QJsonArray::fromStringList(d.order));
        obj.insert(QString::fromUtf8("speakerIndex"), d.speakerIndex);
        obj.insert(QString::fromUtf8("speakCount"), d.speakCount);
        obj.insert(QString::fromUtf8("nextSeq"), d.nextSeq);
        QString phase = QString::fromUtf8("idle");
        if (d.phase == GroupDialogueSpeaking)
            phase = QString::fromUtf8("speaking");
        else if (d.phase == GroupDialogueVoting)
            phase = QString::fromUtf8("voting");
        else if (d.phase == GroupDialogueDone)
            phase = QString::fromUtf8("done");
        obj.insert(QString::fromUtf8("phase"), phase);
        QJsonArray transcript;
        foreach (const GroupUtterance &u, d.transcript)
        {
            QJsonObject row;
            row.insert(QString::fromUtf8("seq"), u.seq);
            row.insert(QString::fromUtf8("kind"), u.kind);
            row.insert(QString::fromUtf8("workerId"), u.workerId);
            row.insert(QString::fromUtf8("name"), u.name);
            row.insert(QString::fromUtf8("text"), u.text);
            transcript.append(row);
        }
        obj.insert(QString::fromUtf8("transcript"), transcript);
        obj.insert(QString::fromUtf8("proposal"), d.proposal);
        obj.insert(QString::fromUtf8("proposalBy"), d.proposalBy);
        obj.insert(QString::fromUtf8("voted"), QJsonArray::fromStringList(d.voted.toList()));
        obj.insert(QString::fromUtf8("approvals"),
                   QJsonArray::fromStringList(d.approvals.toList()));
        QJsonObject comments;
        foreach (const QString &key, d.voteComments.keys())
            comments.insert(key, d.voteComments.value(key));
        obj.insert(QString::fromUtf8("voteComments"), comments);
        obj.insert(QString::fromUtf8("outcome"), d.outcome);
        obj.insert(QString::fromUtf8("lastOutcome"), d.lastOutcome);
        QJsonObject cursors;
        foreach (const QString &key, d.cursor.keys())
            cursors.insert(key, d.cursor.value(key));
        obj.insert(QString::fromUtf8("cursor"), cursors);
        obj.insert(QString::fromUtf8("roundId"), d.roundId);
        QJsonObject roundSeen;
        foreach (const QString &key, d.roundSeen.keys())
            roundSeen.insert(key, d.roundSeen.value(key));
        obj.insert(QString::fromUtf8("roundSeen"), roundSeen);
        obj.insert(QString::fromUtf8("terminalDelivered"),
                   QJsonArray::fromStringList(d.terminalDelivered.toList()));
        items.append(obj);
    }
    QJsonObject root;
    root.insert(QString::fromUtf8("dialogues"), items);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

//----------------------------------------------

void WorkerManager::loadGroupDialogues()
{
    _groupDialogues.clear();
    const QString path = groupDialoguesPath();
    if (path.isEmpty() || !QFile::exists(path))
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;
    const QJsonArray items = doc.object().value(QString::fromUtf8("dialogues")).toArray();
    for (int i = 0; i < items.size(); ++i)
    {
        const QJsonObject obj = items.at(i).toObject();
        GroupDialogue d;
        d.groupId = obj.value(QString::fromUtf8("groupId")).toString();
        if (d.groupId.isEmpty())
            continue;
        const QJsonArray order = obj.value(QString::fromUtf8("order")).toArray();
        for (int j = 0; j < order.size(); ++j)
            d.order.append(order.at(j).toString());
        d.speakerIndex = obj.value(QString::fromUtf8("speakerIndex")).toInt();
        d.speakCount = obj.value(QString::fromUtf8("speakCount")).toInt();
        d.nextSeq = obj.value(QString::fromUtf8("nextSeq")).toInt(1);
        const QString phase = obj.value(QString::fromUtf8("phase")).toString();
        if (phase == QString::fromUtf8("speaking"))
            d.phase = GroupDialogueSpeaking;
        else if (phase == QString::fromUtf8("voting"))
            d.phase = GroupDialogueVoting;
        else if (phase == QString::fromUtf8("done"))
            d.phase = GroupDialogueDone;
        else
            d.phase = GroupDialogueIdle;
        const QJsonArray transcript = obj.value(QString::fromUtf8("transcript")).toArray();
        int maxSeq = 0;
        for (int j = 0; j < transcript.size(); ++j)
        {
            const QJsonObject row = transcript.at(j).toObject();
            GroupUtterance u;
            u.seq = row.value(QString::fromUtf8("seq")).toInt(j + 1);
            u.kind = row.value(QString::fromUtf8("kind")).toString();
            if (u.kind.isEmpty())
                u.kind = QString::fromUtf8("speak");
            u.workerId = row.value(QString::fromUtf8("workerId")).toString();
            u.name = row.value(QString::fromUtf8("name")).toString();
            u.text = row.value(QString::fromUtf8("text")).toString();
            d.transcript.append(u);
            if (u.seq > maxSeq)
                maxSeq = u.seq;
        }
        if (d.nextSeq <= maxSeq)
            d.nextSeq = maxSeq + 1;
        d.proposal = obj.value(QString::fromUtf8("proposal")).toString();
        d.proposalBy = obj.value(QString::fromUtf8("proposalBy")).toString();
        const QJsonArray voted = obj.value(QString::fromUtf8("voted")).toArray();
        for (int j = 0; j < voted.size(); ++j)
            d.voted.insert(voted.at(j).toString());
        const QJsonArray approvals = obj.value(QString::fromUtf8("approvals")).toArray();
        for (int j = 0; j < approvals.size(); ++j)
            d.approvals.insert(approvals.at(j).toString());
        const QJsonObject comments = obj.value(QString::fromUtf8("voteComments")).toObject();
        foreach (const QString &key, comments.keys())
            d.voteComments.insert(key, comments.value(key).toString());
        d.outcome = obj.value(QString::fromUtf8("outcome")).toString();
        d.lastOutcome = obj.value(QString::fromUtf8("lastOutcome")).toString();
        if (d.lastOutcome.isEmpty() && d.phase == GroupDialogueDone && !d.outcome.isEmpty())
            d.lastOutcome = d.outcome;
        const QJsonObject cursors = obj.value(QString::fromUtf8("cursor")).toObject();
        foreach (const QString &key, cursors.keys())
            d.cursor.insert(key, cursors.value(key).toInt());
        d.roundId = obj.value(QString::fromUtf8("roundId")).toInt();
        const QJsonObject seen = obj.value(QString::fromUtf8("roundSeen")).toObject();
        foreach (const QString &key, seen.keys())
            d.roundSeen.insert(key, seen.value(key).toInt());
        const QJsonArray terminal = obj.value(QString::fromUtf8("terminalDelivered")).toArray();
        for (int j = 0; j < terminal.size(); ++j)
            d.terminalDelivered.insert(terminal.at(j).toString());
        syncDialogueOrder(&d);
        _groupDialogues.insert(d.groupId, d);
    }
}

//----------------------------------------------

void WorkerManager::resumeGroupDialogues()
{
    const QStringList ids = _groupDialogues.keys();
    foreach (const QString &gid, ids)
    {
        GroupDialogue &d = _groupDialogues[gid];
        if (d.phase != GroupDialogueSpeaking && d.phase != GroupDialogueVoting)
            continue;
        syncDialogueOrder(&d);
        if (d.order.size() < 2)
            continue;
        advanceDialogueAfterChange(gid);
    }
}

//----------------------------------------------

void WorkerManager::reinstallToolsAndResumeGroups()
{
    foreach (Worker *worker, _workers)
        installTools(worker);
    loadGroupDialogues();
    resumeGroupDialogues();
}

//----------------------------------------------

QString WorkerManager::findTalkWaitId(Worker *caller, Worker *peer) const
{
    if (!caller || !peer)
        return QString();
    const QList<QString> queue = _talkWaitQueue.value(peer->id());
    foreach (const QString &waitId, queue)
    {
        const TalkWait wait = _talkWaits.value(waitId);
        if (wait.caller == caller && wait.peer == peer)
            return waitId;
    }
    return QString();
}

//----------------------------------------------

bool WorkerManager::peerReadyForTalkReply(Worker *peer) const
{
    if (!peer || !peer->session() || !peer->session()->agent())
        return false;
    if (peer->session()->agent()->isBusy())
        return false;
    if (peer->hasPendingFollowUps())
        return false;
    const WorkerState state = peer->state();
    if (state == WorkerStateWaitingApproval
        || state == WorkerStateWaitingContinuation
        || state == WorkerStateWaitingChildren)
        return false;
    return true;
}

//----------------------------------------------

bool WorkerManager::talkWaitReachable(Worker *from, Worker *to) const
{
    if (!from || !to)
        return false;
    if (from == to)
        return true;
    QSet<QString> seen;
    QList<Worker *> queue;
    queue.append(from);
    seen.insert(from->id());
    while (!queue.isEmpty())
    {
        Worker *node = queue.takeFirst();
        foreach (const TalkWait &wait, _talkWaits)
        {
            if (wait.caller != node || wait.peer.isNull())
                continue;
            Worker *next = wait.peer.data();
            if (next == to)
                return true;
            if (seen.contains(next->id()))
                continue;
            seen.insert(next->id());
            queue.append(next);
        }
    }
    return false;
}

//----------------------------------------------

bool WorkerManager::wouldCreateTalkCycle(Worker *from, Worker *peer) const
{
    // Ребро from→peer замыкает цикл, если peer уже может дойти до from.
    return talkWaitReachable(peer, from);
}

//----------------------------------------------

void WorkerManager::finishTalk(QString waitId, QString reply)
{
    if (waitId.isEmpty() || !_talkWaits.contains(waitId))
        return;
    const TalkWait wait = _talkWaits.take(waitId);
    if (wait.peer)
    {
        QList<QString> &queue = _talkWaitQueue[wait.peer->id()];
        queue.removeAll(waitId);
        if (queue.isEmpty())
            _talkWaitQueue.remove(wait.peer->id());
    }
    _finishedTalks.insert(waitId, reply);
    emit talkFinished(waitId);
}

//----------------------------------------------

void WorkerManager::failTalksForWorker(Worker *worker, QString reason)
{
    if (!worker)
        return;
    const QStringList waitIds = _talkWaits.keys();
    foreach (const QString &waitId, waitIds)
    {
        const TalkWait wait = _talkWaits.value(waitId);
        if (wait.caller != worker && wait.peer != worker)
            continue;
        finishTalk(waitId, reason);
    }
}

//----------------------------------------------

void WorkerManager::armTalkHead(Worker *peer)
{
    if (!peer)
        return;
    const QList<QString> queue = _talkWaitQueue.value(peer->id());
    if (queue.isEmpty())
        return;
    const QString waitId = queue.first();
    if (!_talkWaits.contains(waitId))
        return;
    _talkWaits[waitId].armed = true;
}

//----------------------------------------------

void WorkerManager::maybeFinishTalkHead(Worker *peer)
{
    if (!peer || !peerReadyForTalkReply(peer))
        return;
    const QList<QString> queue = _talkWaitQueue.value(peer->id());
    if (queue.isEmpty())
        return;
    const QString waitId = queue.first();
    if (!_talkWaits.contains(waitId))
        return;
    const TalkWait wait = _talkWaits.value(waitId);
    if (!wait.armed)
        return;
    QString reply = peer->lastAssistant().trimmed();
    if (reply.isEmpty() && peer->hasAnswered())
        reply = peer->result().trimmed();
    if (reply.isEmpty())
        reply = QString::fromUtf8("(no reply text)");
    const QString peerName = peer->personName().isEmpty() ? peer->id() : peer->personName();
    finishTalk(waitId,
               QString::fromUtf8("Reply from %1 (worker_id=%2):\n%3")
                   .arg(peerName, peer->id(), reply));
}

//----------------------------------------------

QString WorkerManager::talkTo(Worker *from, QString peerId, QString message, QString *error)
{
    Q_UNUSED(from);
    Q_UNUSED(peerId);
    Q_UNUSED(message);
    // Обсуждение только в группе (group_*). Вне группы — задание от родителя /
    // ответ complete_task и делегирование детям; talk_to запрещён.
    if (error)
    {
        *error = QString::fromUtf8(
            "talk_to is disabled. Workers outside a group do not discuss peers: "
            "receive the assignment from the parent and report via complete_task; "
            "managers assign children via delegate_task. "
            "Group members use group_speak / group_await / group_end_turn / "
            "group_propose / group_vote.");
    }
    return QString();
}

//----------------------------------------------

QString WorkerManager::changedFilesForChildren(Worker *parent) const
{
    if (!parent)
        return QString();
    QStringList files;
    foreach (Worker *child, parent->children())
    {
        if (!child)
            continue;
        foreach (const QString &item, child->changedFiles())
        {
            if (!files.contains(item))
                files.append(item);
        }
    }
    if (files.isEmpty())
        return QString();
    QString text = QString::fromUtf8("Files changed by previous workers:");
    foreach (const QString &item, files)
        text += QString::fromUtf8("\n- ") + item;
    return text;
}

//----------------------------------------------

void WorkerManager::noteTaskCompleted()
{
    ++_completeCount;
}

//----------------------------------------------

bool WorkerManager::claimFilePath(Worker *worker, QString relativePath, QString *error)
{
    if (!worker)
    {
        if (error)
            *error = QString::fromUtf8("Worker is missing.");
        return false;
    }
    QString path = QDir::cleanPath(relativePath.trimmed());
    if (path.startsWith(QString::fromUtf8("./")))
        path = path.mid(2);
    if (path.isEmpty() || path == QString::fromUtf8("."))
        return true;
    const QString ownerId = _fileOwners.value(path);
    if (!ownerId.isEmpty() && ownerId != worker->id())
    {
        Worker *owner = workerById(ownerId);
        const QString name = owner
            ? (owner->personName().isEmpty() ? owner->id() : owner->personName())
            : ownerId;
        if (error)
            *error = QString::fromUtf8(
                "REFUSED (file ownership): '%1' is claimed by %2 (worker_id=%3). "
                "One active writer per file — wait for them to complete_task or pick another path.")
                         .arg(path, name, ownerId);
        return false;
    }
    _fileOwners.insert(path, worker->id());
    return true;
}

//----------------------------------------------

void WorkerManager::releaseFilePaths(Worker *worker)
{
    if (!worker)
        return;
    const QString id = worker->id();
    const QStringList keys = _fileOwners.keys();
    foreach (const QString &path, keys)
    {
        if (_fileOwners.value(path) == id)
            _fileOwners.remove(path);
    }
}

//----------------------------------------------

QString WorkerManager::fileOwnershipSummary() const
{
    if (_fileOwners.isEmpty())
        return QString();
    QStringList lines;
    lines << QString::fromUtf8("Active file ownership (one writer):");
    foreach (const QString &path, _fileOwners.keys())
    {
        const QString id = _fileOwners.value(path);
        Worker *owner = workerById(id);
        const QString name = owner
            ? (owner->personName().isEmpty() ? id : owner->personName())
            : id;
        lines << QString::fromUtf8("- %1 → %2").arg(path, name);
    }
    return lines.join(QChar('\n'));
}

//----------------------------------------------

QString WorkerManager::workspaceCardText() const
{
    const QString dir = ChatStore::storeDir();
    if (dir.isEmpty())
        return QString();
    const QString path = dir + QString::fromUtf8("/workspace_card.md");
    QFile file(path);
    if (!file.exists())
        return QString::fromUtf8(
            "Workspace card: missing (optional ___project___/workspace_card.md with Build/Run/"
            "Lint/Src/Tmp conventions). Prefer creating it so workers stop rediscovering the env.");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    QString text = QString::fromUtf8(file.readAll()).trimmed();
    if (text.isEmpty())
        return QString();
    if (text.size() > 2500)
        text = text.left(2497) + QString::fromUtf8("…");
    return QString::fromUtf8("Workspace card:\n") + text;
}

//----------------------------------------------

void WorkerManager::destroyWorkerNow(Worker *worker)
{
    if (!worker)
        return;

    const QList<Worker *> kids = worker->children();
    foreach (Worker *child, kids)
        destroyWorkerNow(child);

    Worker *parent = worker->parentWorker();
    if (parent)
        parent->detachChild(worker);

    if (worker->isTool())
        failToolRuns(worker, QString::fromUtf8(
            "TOOL DELETED: the user removed this tool before it answered."));

    failTalksForWorker(worker, QString::fromUtf8(
        "talk_to aborted: peer or caller was removed."));
    const QString gid = worker->groupId();
    failGroupAwaitsForWorker(worker, QString::fromUtf8(
        "group_await aborted: member was removed."));
    releaseFilePaths(worker);

    if (worker->session() && worker->session()->agent())
        worker->session()->agent()->abort();
    worker->quitBlockingLoops();

    _workers.removeAll(worker);
    _startQueue.removeAll(worker);
    if (!gid.isEmpty() && _groupDialogues.contains(gid))
    {
        GroupDialogue &d = _groupDialogues[gid];
        syncDialogueOrder(&d);
        if (d.order.size() < 2)
            _groupDialogues.remove(gid);
        else
            advanceDialogueAfterChange(gid);
        saveGroupDialogues();
    }
    emit workerAboutToBeRemoved(worker);
    worker->setSession(0);
    worker->disconnect();
    worker->deleteLater();
    sltScheduleQueuePump();
}

//----------------------------------------------

void WorkerManager::sltPurgeWorker(QString id)
{
    Worker *worker = workerById(id);
    if (!worker)
        return;
    destroyWorkerNow(worker);
    emit treeChanged();
}

//----------------------------------------------

bool WorkerManager::dismissChild(Worker *parent, QString childId, QString *error)
{
    if (!parent)
    {
        if (error)
            *error = QString::fromUtf8("Parent worker is missing.");
        return false;
    }
    Worker *child = parent->childById(childId);
    if (!child)
    {
        if (error)
            *error = QString::fromUtf8(
                "No direct child with worker_id=%1.\n%2")
                         .arg(childId.trimmed(), parent->childrenSummary());
        return false;
    }
    if (child->isRoot())
    {
        if (error)
            *error = QString::fromUtf8("The root worker cannot be dismissed.");
        return false;
    }

    const QList<Worker *> kids = child->children();
    foreach (Worker *grand, kids)
        destroyWorkerNow(grand);

    child->clearPendingFollowUps();
    if (child->result().trimmed().isEmpty())
        child->setResult(QString::fromUtf8(
            "DISMISSED: parent removed this worker."));
    if (child->session() && child->session()->agent())
        child->session()->agent()->abort();
    if (!child->hasAnswered())
        child->quitBlockingLoops();

    QMetaObject::invokeMethod(this, "sltPurgeWorker", Qt::QueuedConnection,
                              Q_ARG(QString, child->id()));
    emit treeChanged();
    return true;
}

//----------------------------------------------

QString WorkerManager::runAndWait(Worker *worker)
{
    if (!worker)
        return QString::fromUtf8("Worker is missing.");
    if (worker->state() == WorkerStateCompleted || worker->state() == WorkerStateFailed)
        return worker->result();

    QEventLoop loop;
    connect(worker, SIGNAL(finished()), &loop, SLOT(quit()));
    if (worker->state() == WorkerStateIdle)
        requestStart(worker);
    if (worker->state() != WorkerStateCompleted && worker->state() != WorkerStateFailed)
        loop.exec();
    return worker->result();
}

//----------------------------------------------

static void addFileAndShellTools(Agent *agent, ProjectWorkspace *workspace, Worker *worker)
{
    if (!agent || !workspace)
        return;
    agent->addTool(new CreateFileAgentTool(workspace, worker, agent));
    agent->addTool(new WriteFileAgentTool(workspace, worker, agent));
    agent->addTool(new DeleteFileAgentTool(workspace, worker, agent));
    agent->addTool(new RenameFileAgentTool(workspace, worker, agent));
    agent->addTool(new ReadFileAgentTool(workspace, agent));
    agent->addTool(new ListDirectoryAgentTool(workspace, agent));
    agent->addTool(new RunCommandAgentTool(workspace, worker, agent));
    agent->addTool(new RequestPermissionTool(worker, agent));
}

//----------------------------------------------

void WorkerManager::installTools(Worker *worker)
{
    if (!worker || !worker->session() || !worker->session()->agent())
        return;

    Agent *agent = worker->session()->agent();
    agent->clearTools();
    agent->setSystemPrompt(promptFor(worker));
    addFileAndShellTools(agent, _workspace, worker);

    if (!worker->isTool())
    {
        agent->addTool(new ListGroupMembersTool(this, worker, agent));
        // Групповые инструменты всегда в реестре — иначе после add_group_member /
        // restore модель вызывает group_* и получает Unknown function.
        agent->addTool(new GroupAwaitTool(this, worker, agent));
        agent->addTool(new GroupSpeakTool(this, worker, agent));
        agent->addTool(new GroupEndTurnTool(this, worker, agent));
        agent->addTool(new GroupProposeTool(this, worker, agent));
        agent->addTool(new GroupVoteTool(this, worker, agent));
        // talk_to не ставим: вне группы обсуждений нет; в группе — только group_*.
    }

    if (worker->isGroupInterlocutor())
    {
        // Собеседник: действия без делегирования детям.
        if (worker->type() != WorkerTypeManager)
        {
            agent->addTool(new ListToolsTool(this, agent));
            agent->addTool(new RunToolTool(this, worker, agent));
        }
        agent->addTool(new CompleteTaskTool(worker, agent));
    }
    else
    {
        switch (worker->type())
        {
        case WorkerTypeManager:
            agent->addTool(new ListChildWorkersTool(worker, agent));
            agent->addTool(new GetChildResultTool(worker, agent));
            agent->addTool(new PlanWorkTool(worker, agent));
            agent->addTool(new DelegateTaskTool(this, worker, agent));
            agent->addTool(new DismissChildWorkerTool(this, worker, agent));
            agent->addTool(new AddGroupMemberTool(this, worker, agent));
            agent->addTool(new ListToolsTool(this, agent));
            agent->addTool(new CreateToolTool(this, worker, agent));
            agent->addTool(new RunToolTool(this, worker, agent));
            if (!worker->isRoot())
                agent->addTool(new CompleteTaskTool(worker, agent));
            break;
        case WorkerTypeExecutor:
            agent->addTool(new ListToolsTool(this, agent));
            agent->addTool(new RunToolTool(this, worker, agent));
            agent->addTool(new CompleteTaskTool(worker, agent));
            break;
        case WorkerTypeTester:
            agent->addTool(new ListToolsTool(this, agent));
            agent->addTool(new RunToolTool(this, worker, agent));
            agent->addTool(new CompleteTaskTool(worker, agent));
            break;
        case WorkerTypeTool:
            agent->addTool(new CompleteTaskTool(worker, agent));
            break;
        }
    }
    agent->applyRoleSettings(worker->type());
    agent->setProjectDirectory(_workspace ? _workspace->root() : QString());
}

//----------------------------------------------

void WorkerManager::stopAll()
{
    _startQueue.clear();
    const QList<Worker *> list = _workers;
    foreach (Worker *worker, list)
    {
        if (worker && worker->isTool())
            failToolRuns(worker, QString::fromUtf8("STOPPED: the user stopped all workers."));
    }
    foreach (Worker *worker, list)
    {
        if (worker)
            failTalksForWorker(worker, QString::fromUtf8(
                "talk_to aborted: workers were stopped."));
    }
    foreach (Worker *worker, list)
    {
        if (worker)
            failGroupAwaitsForWorker(worker, QString::fromUtf8(
                "group_await aborted: workers were stopped."));
    }
    saveGroupDialogues();
    _groupAwaiting.clear();
    _groupAwaitResults.clear();
    foreach (Worker *worker, list)
    {
        if (worker)
            worker->beginUserStop();
    }
    foreach (Worker *worker, list)
    {
        if (worker)
            worker->finishUserStop();
    }
    emit treeChanged();
}

//----------------------------------------------

void WorkerManager::clear()
{
    const QList<Worker *> list = _workers;
    _workers.clear();
    _startQueue.clear();
    blockSignals(true);
    foreach (Worker *worker, list)
    {
        if (worker && worker->isTool())
            failToolRuns(worker, QString::fromUtf8("CLEARED: all workers were removed."));
    }
    _toolQueues.clear();
    _toolCurrent.clear();
    _talkWaits.clear();
    _talkWaitQueue.clear();
    _finishedTalks.clear();
    saveGroupDialogues();
    _groupDialogues.clear();
    _groupAwaiting.clear();
    _groupAwaitResults.clear();
    _fileOwners.clear();
    foreach (Worker *worker, list)
    {
        if (!worker)
            continue;
        if (worker->session() && worker->session()->agent())
            worker->session()->agent()->abort();
        worker->quitBlockingLoops();
        if (worker->session())
            worker->session()->setWorker(0);
        worker->setSession(0);
        worker->disconnect();
        worker->deleteLater();
    }
    blockSignals(false);
    emit treeChanged();
}

//----------------------------------------------

Worker *WorkerManager::workerById(QString id) const
{
    foreach (Worker *worker, _workers)
    {
        if (worker && worker->id() == id)
            return worker;
    }
    return 0;
}

//----------------------------------------------

QList<Worker *> WorkerManager::workers() const
{
    return _workers;
}

//----------------------------------------------

QList<Worker *> WorkerManager::roots() const
{
    QList<Worker *> list;
    foreach (Worker *worker, _workers)
    {
        if (worker && worker->parentWorker() == 0 && !worker->isTool())
            list.append(worker);
    }
    return list;
}

//----------------------------------------------

QList<Worker *> WorkerManager::tools() const
{
    QList<Worker *> list;
    foreach (Worker *worker, _workers)
    {
        if (worker && worker->isTool())
            list.append(worker);
    }
    return list;
}

//----------------------------------------------

void WorkerManager::linkParent(Worker *child, QString parentId)
{
    if (!child || parentId.isEmpty())
        return;
    Worker *parent = workerById(parentId);
    if (!parent)
        return;
    parent->addChild(child);
    refreshPrompt(child);
}

//----------------------------------------------

QString WorkerManager::promptFor(Worker *worker) const
{
    if (!worker)
        return QString();
    // Системный промпт СТАТИЧЕН для воркера: никаких лимитов, глубины, загрузки —
    // всё переменное уходит в первое сообщение (startupContextFor). Так префикс
    // «system + tools + начало истории» не меняется между запросами и попадает
    // в кеш провайдера.
    const WorkerType type = worker->type();
    QString base;
    if (worker->isGroupInterlocutor())
    {
        base = QString::fromUtf8(
            "You are a GROUP INTERLOCUTOR named in your first message. The group solves "
            "the assignment with a TURN-BASED dialogue (one narrator at a time):\n"
            "- If you are NOT the narrator and not voting: call group_await ONCE (parks you; "
            "a follow-up will wake you). Do not poll it.\n"
            "- If you ARE the narrator: group_speak(message) to ALL, then group_end_turn(). "
            "Do not group_await while you are narrator.\n"
            "- When ready: group_propose(result); everyone group_vote until UNANIMOUS.\n"
            "- After [Group dialogue DONE]: the ROUND ends; you may start another round "
            "with the same members via group_speak / group_propose, or complete_task "
            "if reporting to a parent.\n"
            "Do not use talk_to. Be concise. "
            "FORBIDDEN: add_group_member, plan_work, delegate_task, create_tool, spawning "
            "children. Only the group chief delegates work.");
        base += QString::fromUtf8(
            "\nSEQUENCE: In ONE assistant turn you may emit several tool_calls in the order "
            "they should run (e.g. write_file A → write_file B → run_command → run_command → "
            "read_file). Prefer a clear sequence in one turn over many round-trips when you "
            "already know the steps. Also batch same-type ops: read_file paths=[...], "
            "create_file/write_file files=[{path,content},...], run_command commands=[...].\n"
            "KNOWLEDGE: 'Experience of previous workers' in your first message is verified - "
            "start from it. In complete_task fill `lessons` as CMD:/ERR:/FIX:/PATH:/TAGS: lines "
            "(working commands, fixed errors, quirks, file locations) whenever you learned "
            "something non-obvious.");
        return base;
    }
    switch (type)
    {
    case WorkerTypeExecutor:
        base = QString::fromUtf8(
            "You are an EXECUTOR worker. Do exactly the assigned task: implement, edit, "
            "wire up code and commands as asked. Obey the assignment; do not redefine scope "
            "or invent extra goals.\n"
            "VERIFICATION GATE (evidence over claims, fail-closed): before complete_task, "
            "if you changed files you MUST run a build/compile/syntax check and pass "
            "verified=true|false plus evidence (exact command + key output). "
            "FORBIDDEN: unit/integration/manual test suites and judging product pass/fail — "
            "that is the tester's job. You MAY compile/build to prove the code builds.\n"
            "FORBIDDEN: spawning children. Do not design overall architecture.\n"
            "ALLOWED: read/write project files you own, run_command, run_tool for build/run "
            "helpers. Read only what you need; read_file returns chunks, use offset for more. "
            "Before delete/rename/overwrite call request_permission. "
            "One active writer per file — do not edit paths owned by another unfinished worker.\n"
            "Finish with complete_task(result, verified, evidence{command,output,artifact?,digest?}, "
            "lessons with CMD:/ERR:/FIX:/PATH:/TAGS:). "
            "Never stop without complete_task.");
        break;
    case WorkerTypeTool:
        base = QString::fromUtf8(
            "You are a TOOL worker named \"%1\": a reusable specialist shared by all workers "
            "of this project, without parent or children. PURPOSE: %2\n"
            "You perform ONE concrete operation that needs MINIMAL context (build, run the "
            "app, run a fixed test command, lint, package, etc.). Each user message is a run "
            "request: do only that, within your purpose. Do not explore the whole project, "
            "do not redesign architecture, do not implement features, do not hold a long "
            "dialogue - short factual work and done.\n"
            "Finish with complete_task: concise FACTUAL result for the requester (what ran, "
            "exit status, errors with file:line, key output). You keep memory across runs - "
            "reuse known commands and paths. Before delete/rename/overwrite call "
            "request_permission. Never stop without complete_task.")
                   .arg(worker->toolName().isEmpty() ? QString::fromUtf8("tool")
                                                     : worker->toolName(),
                        worker->toolPurpose().isEmpty()
                            ? QString::fromUtf8("(not specified; follow each request).")
                            : worker->toolPurpose());
        break;
    case WorkerTypeTester:
        base = QString::fromUtf8(
            "You are a TESTER worker. You ONLY verify the assigned work independently "
            "(reproduce; do not trust executor claims).\n"
            "VERIFICATION GATE (fail-closed): complete_task MUST include verified=true|false "
            "and structured evidence {command, output, artifact?, digest?} "
            "(exact commands + key output; file:line on fail).\n"
            "Do not discuss with sibling workers; report only via complete_task.\n"
            "FORBIDDEN: editing the code under test (production/source files listed as "
            "'files changed' or the implementation you are checking). Do not rewrite "
            "features to make tests pass - report the failure; the executor fixes it.\n"
            "ALLOWED: create, edit, rename and delete TEST and harness files (unit/integration "
            "tests, fixtures, mocks, ___project___/tmp scratch). You may read production "
            "files. Prefer run_tool (list_tools) for build/run/test when a shared tool fits.\n"
            "STOP ON FIRST FAILURE: as soon as one test or check fails, call complete_task "
            "immediately with verified=false and evidence. Do not keep fixing production code.\n"
            "If everything you checked passed, complete_task with verified=true and evidence. "
            "lessons: CMD:/ERR:/FIX:/PATH:/TAGS: when useful. "
            "Call request_permission before overwriting existing non-scratch files. "
            "Never stop without complete_task.");
        break;
    case WorkerTypeManager:
    default:
        base = QString::fromUtf8(
            "You are a MANAGER worker. Your job is ARCHITECTURE and DISTRIBUTION: design "
            "structure (components, boundaries, data flow, file ownership), split the work "
            "into responsibilities and small tasks, and assign them to children. Without a "
            "parent you talk to the user; with a parent you report to it and never address "
            "the user.\n"
            "SMALL TASKS: if the request is tiny and needs no split (one short edit, one "
            "command, a quick lookup), you may do it yourself. Do not spawn an executor for "
            "such work.\n"
            "LARGER WORK (before the first delegate_task): call plan_work with architecture "
            "and subtasks; rate each: simple/cannot-split -> executor, still complex -> "
            "manager (only if it splits into >=3 executor-sized parts, expected_parts>=3; "
            "1-2 parts stay in YOUR plan as simple items); then delegate_task for EACH "
            "subtask in the same turn. Forbidden: skipping plan_work when you delegate; "
            "one executor for a large split job; calling a splittable job 'simple'. "
            "Prefer the MOST ISOLATED tasks possible so children's contexts barely overlap "
            "(separate files/modules/ownership; avoid two children editing the same area). "
            "Tightly coupled work is ONE subtask; weakly dependent streams are separate for "
            "parallelism. In each subtask text name the files/interfaces the child owns.\n"
            "MODEL PRESETS: your first message lists YOUR model modality and preset_id + "
            "profile + model + modality for each preset. When calling delegate_task / "
            "add_group_member pass preset_id so the child matches the job: coding/agents → "
            "task_kind=llm + modality=llm; images → image_generation; speech → speech; "
            "embeddings → embedding. Prefer cheapest/fastest adequate models for routine "
            "edits; stronger models for architecture, hard bugs, ambiguous design. Do not "
            "default every child to the same costly LLM when a lighter or specialized "
            "preset fits.\n"
            "CHILDREN: reuse existing children including COMPLETED ones "
            "(list_child_workers, worker_id) — a finished child can be restarted many times "
            "with NEW instructions from you; at most 5 unfinished children at once; AFTER "
            "executors change files you MUST run a tester "
            "with verified=true + structured evidence before your own complete_task "
            "(acceptance gate, fail-closed). Children get parent brief + sibling outcomes. "
            "Children do not discuss with peers — only parent assignment / complete_task "
            "and your delegate_task. Reject claims without evidence.\n"
            "TOOLS: a tool is a reusable specialist for a narrow, low-context job (build, "
            "run app, run tests, lint). Use list_tools/run_tool or create_tool once - never "
            "a child executor for that. Never create a tool for one-off implementation or a "
            "duplicate purpose. run_tool waits for the answer.\n"
            "GROUP: at your discretion, form a discussion group with add_group_member"
            "(preset_id) for thorough study of a hard or ambiguous question (design trade-"
            "offs, root-cause analysis, conflicting approaches) BEFORE or alongside "
            "delegation. Peers MUST use DIFFERENT model profiles — pick capable, diverse "
            "presets for deep debate. Dialogue is "
            "TURN-BASED: one narrator group_speak → all listen → group_end_turn → next; "
            "finish a ROUND with group_propose + unanimous group_vote. Others call group_await. "
            "After DONE you may open another round with the same members via group_speak / "
            "group_propose (no hard limit on turns). Do not use talk_to. Only YOU (the "
            "chief) may delegate_task to children; interlocutors cannot spawn children. "
            "Keep the group small; prefer diverse profiles over duplicate ones.\n"
            "LIMITS: your first message states tree depth and concurrency; the depth rule "
            "overrides simple/complex.\n"
            "REPORT: with a parent, after children answered (or after a small task you did "
            "yourself) call complete_task with YOUR report - that is how the parent learns "
            "you finished. Never claim files changed unless you or an executor reported it.");
        break;
    }
    base += QString::fromUtf8(
        "\nCOMMUNICATION: Outside a group you do not discuss with peers. Receive the "
        "assignment from your parent (first message / follow-ups) and finish with "
        "complete_task; managers assign children via delegate_task and read their reports. "
        "Group dialogue uses only group_speak / group_await / group_propose / group_vote.\n"
        "SEQUENCE: In ONE assistant turn you may emit several tool_calls in the order they "
        "should run (e.g. write_file A → write_file B → run_command → run_command → "
        "read_file). Prefer a clear sequence in one turn over many round-trips when you "
        "already know the steps. Also batch same-type ops: read_file paths=[...], "
        "create_file/write_file files=[{path,content},...], run_command commands=[...].\n"
        "KNOWLEDGE: 'Experience of previous workers' in your first message is verified - "
        "start from it. In complete_task fill `lessons` as CMD:/ERR:/FIX:/PATH:/TAGS: lines "
        "(working commands, fixed errors, quirks, file locations) whenever you learned "
        "something non-obvious.");
    return base;
}

//----------------------------------------------

QString WorkerManager::startupContextFor(Worker *worker) const
{
    if (!worker)
        return QString();
    QString text = limitsSummaryFor(worker);
    text += QString::fromUtf8("\n") + loadSummary();
    const QString card = workspaceCardText();
    if (!card.isEmpty())
        text += QString::fromUtf8("\n") + card;
    const QString owners = fileOwnershipSummary();
    if (!owners.isEmpty())
        text += QString::fromUtf8("\n") + owners;
    if (!worker->personName().isEmpty())
        text += QString::fromUtf8("\nYour name: %1").arg(worker->personName());
    if (!worker->groupId().isEmpty())
    {
        text += QString::fromUtf8("\nGroup id=%1 role=%2")
                    .arg(worker->groupId(), groupRoleToString(worker->groupRole()));
        QStringList peers;
        foreach (Worker *peer, groupMembers(worker->groupId()))
        {
            if (!peer || peer == worker)
                continue;
            QString peerProfile = peer->llmProfileId().trimmed();
            if (peerProfile.isEmpty())
                peerProfile = AppSettings::instance()->profileForWorkerType(peer->type()).name;
            else
            {
                const LlmProfile p = AppSettings::instance()->profileById(peerProfile);
                if (!p.name.trimmed().isEmpty())
                    peerProfile = p.name.trimmed();
            }
            if (peerProfile.isEmpty())
                peerProfile = QString::fromUtf8("?");
            peers.append(QString::fromUtf8("%1 (%2, worker_id=%3, profile=%4)")
                             .arg(peer->personName().isEmpty() ? peer->id()
                                                               : peer->personName(),
                                  workerTypeToString(peer->type()), peer->id(),
                                  peerProfile));
        }
        if (!peers.isEmpty())
            text += QString::fromUtf8("\nGroup peers: ") + peers.join(QString::fromUtf8("; "));
        text += QString::fromUtf8(
            "\nCollaborate: turn-based group_speak / group_await / group_propose / group_vote "
            "until unanimous result. Do not use talk_to.");
        if (isGroupDialogueActive(worker->groupId()))
            text += QString::fromUtf8("\n") + groupDialogueStatus(worker->groupId());
    }
    if (!worker->isTool())
    {
        QString profileId = worker->llmProfileId().trimmed();
        LlmProfile profile;
        if (!profileId.isEmpty())
            profile = AppSettings::instance()->profileById(profileId);
        if (profile.id.isEmpty())
            profile = AppSettings::instance()->profileForWorkerType(worker->type());
        QString profileName = profile.name.trimmed();
        if (profileName.isEmpty())
            profileName = profile.id;
        if (profileName.isEmpty())
            profileName = QString::fromUtf8("?");
        QString model = profile.model.trimmed();
        if (model.isEmpty())
            model = QString::fromUtf8("?");
        text += QString::fromUtf8(
                    "\nYour model: profile=\"%1\" model=%2 modality=%3 "
                    "(modality = API capability: llm | image_generation | speech | "
                    "embedding | other). When you spawn children, pick preset_id whose "
                    "task_kind and modality fit the subtask.")
                    .arg(profileName, model, workerTaskKindToString(profile.modality));
        text += QString::fromUtf8("\n");
        text += AppSettings::instance()->workerPresetsSummary();
    }
    const QList<Worker *> list = tools();
    if (!list.isEmpty() && !worker->isTool())
    {
        text += QString::fromUtf8("\n");
        text += toolsSummary();
    }
    return text;
}
