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
#include <QEventLoop>
#include <QMetaObject>

//----------------------------------------------

WorkerManager::WorkerManager(ProjectWorkspace *workspace, QObject *parent)
    : QObject(parent)
    , _workspace(workspace)
    , _pumpScheduled(false)
    , _runCounter(0)
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
        "Extra children wait in a queue and start automatically, so more children means "
        "slower progress: keep the number of children small (usually 2-5), "
        "never one child per file.")
                .arg(maxConcurrent());
    return text;
}

//----------------------------------------------

QString WorkerManager::loadSummary() const
{
    return QString::fromUtf8("Workers active: %1 of %2 allowed, queued: %3.")
        .arg(activeCount())
        .arg(maxConcurrent())
        .arg(queuedCount());
}

//----------------------------------------------

void WorkerManager::requestStart(Worker *worker)
{
    if (!worker || !_workers.contains(worker))
        return;
    if (worker->hasAnswered())
        return;
    if (canStartNow())
    {
        _startQueue.removeAll(worker);
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
    Worker *worker = createWorker(WorkerTypeManager, session ? session->id() : QString(), session);
    emit treeChanged();
    return worker;
}

//----------------------------------------------

Worker *WorkerManager::createWorker(WorkerType type, QString id, ChatSession *session,
                                    Worker *parent, QString task)
{
    Worker *worker = new Worker(this);
    worker->setType(type);
    if (!id.trimmed().isEmpty())
        worker->setId(id);
    else if (session)
        worker->setId(session->id());
    worker->setTask(task);
    worker->setSession(session);
    if (session)
        session->setWorker(worker);
    if (parent)
        parent->addChild(worker);
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

Worker *WorkerManager::spawn(Worker *parent, WorkerType type, QString task, QString *error)
{
    if (!parent)
    {
        if (error)
            *error = QString::fromUtf8("Parent worker is missing.");
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
    if (type == WorkerTypeTester)
    {
        const QString files = changedFilesForChildren(parent);
        if (!files.isEmpty() && !trimmed.contains(QString::fromUtf8("Files changed")))
            trimmed += QString::fromUtf8("\n\n") + files;
    }

    Worker *child = createWorker(type, QString(), 0, parent, trimmed);
    emit treeChanged();
    return child;
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

    if (worker->session() && worker->session()->agent())
        worker->session()->agent()->abort();
    worker->quitBlockingLoops();

    _workers.removeAll(worker);
    _startQueue.removeAll(worker);
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

    switch (worker->type())
    {
    case WorkerTypeManager:
        agent->addTool(new ListChildWorkersTool(worker, agent));
        agent->addTool(new GetChildResultTool(worker, agent));
        agent->addTool(new PlanWorkTool(worker, agent));
        agent->addTool(new DelegateTaskTool(this, worker, agent));
        agent->addTool(new DismissChildWorkerTool(this, worker, agent));
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
        // Без list_tools/run_tool: иначе инструменты могут ждать друг друга.
        agent->addTool(new CompleteTaskTool(worker, agent));
        break;
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
    switch (type)
    {
    case WorkerTypeExecutor:
        base = QString::fromUtf8(
            "You are an EXECUTOR worker. Do exactly the assigned task: implement, edit, "
            "wire up code and commands as asked. Obey the assignment; do not redefine scope "
            "or invent extra goals.\n"
            "FORBIDDEN: testing and verification (unit/integration/manual checks, writing "
            "or running test suites, judging pass/fail) - that is the tester's job. Do not "
            "spawn test workers. Do not design overall architecture or split the job into "
            "child workers - you have no children.\n"
            "ALLOWED: read/write project files, run_command, run_tool for build/run helpers. "
            "Read only what you need; read_file returns chunks, use offset for more. "
            "Before delete/rename/overwrite call request_permission.\n"
            "Finish with complete_task: what you changed (files), what works, what is left. "
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
            "You are a TESTER worker. You ONLY verify the assigned work.\n"
            "FORBIDDEN: editing the code under test (production/source files listed as "
            "'files changed' or the implementation you are checking). Do not rewrite "
            "features to make tests pass - report the failure; the executor fixes it.\n"
            "ALLOWED: create, edit, rename and delete TEST and harness files (unit/integration "
            "tests, fixtures, mocks, ___project___/tmp scratch). You may read production "
            "files. Prefer run_tool (list_tools) for build/run/test when a shared tool fits.\n"
            "STOP ON FIRST FAILURE: as soon as one test or check fails, call complete_task "
            "immediately with what you ran, the exact failure (error text, file:line), and "
            "what was not checked yet. Do not keep fixing production code or opening more "
            "suites after the first fail - the parent/executor will fix and re-test.\n"
            "If everything you checked passed, complete_task with what passed and remaining "
            "risks. Call request_permission before overwriting existing non-scratch files. "
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
            "one executor for a large split job; calling a splittable job 'simple'. Tightly "
            "coupled work is ONE subtask; weakly dependent streams are separate for "
            "parallelism. In each subtask text name the files/interfaces the child owns.\n"
            "CHILDREN: reuse existing children (list_child_workers, worker_id); testers only "
            "AFTER implementation; do not dismiss children yourself - unused ones are "
            "removed automatically. You continue only after ALL children of the batch "
            "answered; get_child_result gives the full report.\n"
            "TOOLS: a tool is a reusable specialist for a narrow, low-context job (build, "
            "run app, run tests, lint). Use list_tools/run_tool or create_tool once - never "
            "a child executor for that. Never create a tool for one-off implementation or a "
            "duplicate purpose. run_tool waits for the answer.\n"
            "LIMITS: your first message states tree depth and concurrency; the depth rule "
            "overrides simple/complex.\n"
            "REPORT: with a parent, after children answered (or after a small task you did "
            "yourself) call complete_task with YOUR report - that is how the parent learns "
            "you finished. Never claim files changed unless you or an executor reported it.");
        break;
    }
    base += QString::fromUtf8(
        "\nBATCHING: When you already know several independent files to read, prepare the "
        "list and call read_file once with paths=[...] (or emit several read_file tool_calls "
        "in the same assistant turn). Same for create/write: use create_file or write_file "
        "with files=[{path,content},...] for several independent files. Same for shell: "
        "run_command with commands=[...] or several tool_calls in one turn. Do not wait for "
        "the next round when the list is already clear.\n"
        "KNOWLEDGE: 'Experience of previous workers' in your first message is verified - "
        "start from it. In complete_task fill `lessons` (working commands, fixed errors, quirks, "
        "file locations) whenever you learned something non-obvious.");
    return base;
}

//----------------------------------------------

QString WorkerManager::startupContextFor(Worker *worker) const
{
    if (!worker)
        return QString();
    QString text = limitsSummaryFor(worker);
    const QList<Worker *> list = tools();
    if (!list.isEmpty() && !worker->isTool())
    {
        text += QString::fromUtf8("\n");
        text += toolsSummary();
    }
    return text;
}
