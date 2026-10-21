#include "worker.h"
//----------------------------------------------
#include "agent.h"
#include "appsettings.h"
#include "chatsession.h"
#include "knowledgestore.h"
#include "llmtypes.h"
#include "projectbrief.h"
#include "workermanager.h"
//----------------------------------------------
#include <QDateTime>
#include <QEventLoop>
#include <QMetaObject>
#include <QRegExp>
#include <QtGlobal>
#include <QStringList>
#include <QUuid>

//----------------------------------------------

/** Сколько символов отчёта ребёнка попадает в контекст родителя автоматически. */
static const int kChildResultReportChars = 2500;
/** Длина метки задачи ребёнка в отчёте (полный текст уже есть в истории). */
static const int kChildTaskLabelChars = 120;

//----------------------------------------------

Worker::Worker(QObject *parent)
    : QObject(parent)
    , _type(WorkerTypeManager)
    , _state(WorkerStateIdle)
    , _parentWorker(0)
    , _session(0)
    , _groupRole(GroupRoleNone)
    , _completed(false)
    , _skipIdleComplete(false)
    , _needsParentReport(false)
    , _parentReportNudged(false)
    , _continueRetry(false)
    , _permissionLoopActive(false)
    , _consoleInputLoopActive(false)
    , _consoleInputSent(false)
    , _resumeContinueChat(false)
    , _userStopped(false)
    , _unusedParentTurns(0)
    , _permissionOfferAlways(false)
    , _toolStarted(false)
    , _waitingForTool(0)
    , _parkForGroupDialogue(false)
    , _hasVerification(false)
    , _verificationPassed(false)
    , _toolBatchCount(0)
    , _subtreeBatchAllowance(0)
    , _startedAtMs(0)
    , _subtreeMsAllowance(0)
    , _subtreeBudgetGranted(false)
    , _subtreeBudgetLoopActive(false)
    , _pendingSubtreeGrantBatches(0)
    , _pendingSubtreeGrantMs(0)
{
    QString id = QUuid::createUuid().toString();
    if (id.startsWith(QChar('{')))
        id.remove(0, 1);
    if (id.endsWith(QChar('}')))
        id.chop(1);
    _id = id;
}

//----------------------------------------------

Worker::~Worker()
{
}

//----------------------------------------------

QString Worker::id() const
{
    return _id;
}

//----------------------------------------------

void Worker::setId(QString id)
{
    const QString trimmed = id.trimmed();
    if (!trimmed.isEmpty())
        _id = trimmed;
}

//----------------------------------------------

WorkerType Worker::type() const
{
    return _type;
}

//----------------------------------------------

void Worker::setType(WorkerType type)
{
    _type = type;
}

//----------------------------------------------

WorkerState Worker::state() const
{
    return _state;
}

//----------------------------------------------

void Worker::setState(WorkerState state)
{
    if (_state == state)
        return;
    _state = state;
    emit stateChanged();
}

//----------------------------------------------

Worker *Worker::parentWorker() const
{
    return _parentWorker;
}

//----------------------------------------------

void Worker::setParentWorker(Worker *parent)
{
    _parentWorker = parent;
}

//----------------------------------------------

QList<Worker *> Worker::children() const
{
    return _children;
}

//----------------------------------------------

bool Worker::answeredState(WorkerState state) const
{
    return state == WorkerStateCompleted || state == WorkerStateFailed;
}

//----------------------------------------------

QList<Worker *> Worker::unfinishedChildren() const
{
    QList<Worker *> list;
    foreach (Worker *child, _children)
    {
        // Собеседники группы не «задачи»: главный не ждёт их complete_task.
        if (child && !child->isGroupInterlocutor() && !child->hasAnswered())
            list.append(child);
    }
    return list;
}

//----------------------------------------------

bool Worker::hasUnfinishedChildren() const
{
    return !unfinishedChildren().isEmpty();
}

//----------------------------------------------

bool Worker::hasAnswered() const
{
    return _completed || answeredState(_state);
}

//----------------------------------------------

void Worker::addChild(Worker *child)
{
    if (!child)
        return;
    if (_children.contains(child))
        return;
    _children.append(child);
    child->setParentWorker(this);
    connect(child, SIGNAL(finished()), this, SLOT(sltChildFinished()));
    // stateChanged детей намеренно не пробрасывается вверх: главное окно
    // подписано на каждого воркера напрямую, а каскад по цепочке родителей
    // давал O(глубина) полных перестроек интерфейса на одно изменение.
}

//----------------------------------------------

void Worker::detachChild(Worker *child)
{
    if (!child)
        return;
    disconnect(child, SIGNAL(finished()), this, SLOT(sltChildFinished()));
    _children.removeAll(child);
    _delegatedThisBatch.removeAll(child);
    if (child->parentWorker() == this)
        child->setParentWorker(0);
}

//----------------------------------------------

Worker *Worker::childById(QString id) const
{
    const QString trimmed = id.trimmed();
    if (trimmed.isEmpty())
        return 0;
    foreach (Worker *child, _children)
    {
        if (child && child->id() == trimmed)
            return child;
    }
    return 0;
}

//----------------------------------------------

static QString normalizeAssignment(QString text)
{
    QString t = text.trimmed().toLower();
    t.replace(QRegExp(QString::fromUtf8("\\s+")), QString::fromUtf8(" "));
    return t;
}

//----------------------------------------------

static QSet<QString> assignmentTokens(QString text)
{
    QSet<QString> tokens;
    const QString normalized = normalizeAssignment(text);
    QRegExp re(QString::fromUtf8("[a-z0-9_./\\\\+-]+"));
    int pos = 0;
    while ((pos = re.indexIn(normalized, pos)) != -1)
    {
        const QString word = re.cap(0);
        if (word.size() >= 3)
            tokens.insert(word);
        pos += re.matchedLength();
    }
    return tokens;
}

//----------------------------------------------

static double assignmentScore(QString left, QString right)
{
    const QString a = normalizeAssignment(left);
    const QString b = normalizeAssignment(right);
    if (a.isEmpty() || b.isEmpty())
        return 0;
    if (a == b)
        return 1;
    const int minLen = qMin(a.size(), b.size());
    if (minLen >= 16 && (a.contains(b) || b.contains(a)))
        return 0.86;

    const QSet<QString> ta = assignmentTokens(left);
    const QSet<QString> tb = assignmentTokens(right);
    if (ta.isEmpty() || tb.isEmpty())
        return 0;
    int overlap = 0;
    foreach (const QString &token, ta)
    {
        if (tb.contains(token))
            ++overlap;
    }
    if (overlap < 2)
        return 0;
    const int denom = ta.size() + tb.size() - overlap;
    if (denom <= 0)
        return 0;
    return double(overlap) / double(denom);
}

//----------------------------------------------

Worker *Worker::findChildForAssignment(WorkerType type, QString workerId,
                                       QString task) const
{
    const QString id = workerId.trimmed();
    if (!id.isEmpty())
        return childById(id);

    Worker *best = 0;
    double bestScore = 0;
    int ties = 0;
    foreach (Worker *child, _children)
    {
        if (!child || child->type() != type)
            continue;
        const double score = assignmentScore(child->task(), task);
        if (score < 0.48)
            continue;
        if (score > bestScore + 0.02)
        {
            best = child;
            bestScore = score;
            ties = 0;
        }
        else if (best && qAbs(score - bestScore) <= 0.02)
            ++ties;
    }
    if (ties > 0)
        return 0;
    return best;
}

//----------------------------------------------

QString Worker::childrenSummary() const
{
    if (_children.isEmpty())
        return QString::fromUtf8("No child workers.");

    QString text = QString::fromUtf8("Child workers (%1):\n").arg(_children.size());
    foreach (Worker *child, _children)
    {
        if (!child)
            continue;
        text += QString::fromUtf8("\n- id=%1 type=%2 state=%3\n  task: %4\n")
                    .arg(child->id(),
                         workerTypeToString(child->type()),
                         workerStateToString(child->state()),
                         child->task().trimmed());
        const QString result = child->result().trimmed();
        if (!result.isEmpty())
        {
            QString preview = result;
            if (preview.size() > 240)
                preview = preview.left(237) + QString::fromUtf8("...");
            text += QString::fromUtf8("  last result: %1\n").arg(preview);
        }
    }
    return text;
}

//----------------------------------------------

bool Worker::canAcceptFollowUp() const
{
    if (!_session || !_session->agent())
        return false;
    if (_session->agent()->isBusy())
        return false;
    if (_state == WorkerStateWaitingApproval
        || _state == WorkerStateWaitingChildren
        || _state == WorkerStateWaitingContinuation)
        return false;
    return true;
}

//----------------------------------------------

void Worker::sendFollowUp(QString message)
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty() || !_session || !_session->agent())
        return;
    stampTurnAuthor();
    _parkForGroupDialogue = false;
    _completed = false;
    _skipIdleComplete = false;
    _needsParentReport = false;
    _parentReportNudged = false;
    _userStopped = false;
    _resumeContinueChat = false;
    _lastError.clear();
    _result.clear();
    clearWorkPlan();
    setState(WorkerStateRunning);

    WorkerManager *manager = qobject_cast<WorkerManager *>(parent());
    if (manager && workerTaskKindIsMediaExecutor(manager->modalityOf(this)))
    {
        _task = trimmed;
        pushTaskAnchor();
        _session->agent()->recordLocalExchange(
            trimmed, QString(), AgentUserIngressFollowUp);
        bool ok = false;
        const QString report = manager->runMediaExecutor(this, &ok);
        _lastAssistant = report;
        _session->agent()->recordLocalExchange(
            QString(), report, AgentUserIngressFollowUp);
        complete(report, ok);
        return;
    }

    // Follow-up от родителя: историю не сворачиваем заранее — сжатие
    // сработает само, когда контекст подойдёт к лимиту.
    if (isTool())
    {
        // Для инструмента каждое сообщение — самостоятельный запуск.
        _session->agent()->sendUserMessageAfterCompact(trimmed,
                                                       AgentUserIngressAssignment);
        return;
    }
    // Групповые action-prompt не маскируем под «Follow-up from parent»
    // (иначе модель думает, что задание переоткрыли).
    const bool groupAction =
        trimmed.startsWith(QString::fromUtf8("[Your turn as narrator]"))
        || trimmed.startsWith(QString::fromUtf8("[Group VOTE required]"))
        || trimmed.startsWith(QString::fromUtf8("[Group dialogue DONE]"))
        || trimmed.startsWith(QString::fromUtf8("[Waiting]"))
        || trimmed.startsWith(QString::fromUtf8("[Waiting for other votes]"));
    if (groupAction)
    {
        _session->agent()->sendUserMessage(trimmed, AgentUserIngressGroup);
        return;
    }
    _session->agent()->sendUserMessage(
        QString::fromUtf8(
            "Follow-up from your parent worker. Continue the SAME assignment; "
            "do not start from scratch unless the parent explicitly asks.\n\n%1")
            .arg(trimmed),
        AgentUserIngressFollowUp);
}

//----------------------------------------------

void Worker::beginFollowUp(QString message)
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty())
        return;
    _pendingFollowUps.append(trimmed);
    _pendingAuthors.append(_incomingAuthor.trimmed());
    _incomingAuthor.clear();
    _userStopped = false;
    if (canAcceptFollowUp())
        sltFlushFollowUps();
}

//----------------------------------------------

void Worker::clearPendingFollowUps()
{
    _pendingFollowUps.clear();
    _pendingAuthors.clear();
}

//----------------------------------------------

bool Worker::hasPendingFollowUps() const
{
    return !_pendingFollowUps.isEmpty();
}

//----------------------------------------------

void Worker::sltFlushFollowUps()
{
    if (_userStopped)
        return;
    if (_pendingFollowUps.isEmpty())
        return;
    if (!canAcceptFollowUp())
        return;
    WorkerManager *manager = qobject_cast<WorkerManager *>(parent());
    if (manager && !manager->canStartNow())
    {
        // Мест нет: встаём в очередь менеджера (не только state=Queued).
        _completed = false;
        _skipIdleComplete = true;
        _result.clear();
        manager->requestStart(this);
        return;
    }
    const QString next = _pendingFollowUps.takeFirst();
    const QString author = _pendingAuthors.isEmpty() ? QString() : _pendingAuthors.takeFirst();
    _incomingAuthor = author;
    sendFollowUp(next);
}

//----------------------------------------------

ChatSession *Worker::session() const
{
    return _session;
}

//----------------------------------------------

void Worker::setSession(ChatSession *session)
{
    if (_session == session)
        return;
    if (_session && _session->agent())
        disconnect(_session->agent(), 0, this, 0);
    _session = session;
    bindAgent();
}

//----------------------------------------------

void Worker::bindAgent()
{
    if (!_session || !_session->agent())
        return;
    connect(_session->agent(), SIGNAL(busyChanged(bool)),
            this, SLOT(sltBusyChanged(bool)));
    connect(_session->agent(), SIGNAL(assistantMessage(QString)),
            this, SLOT(sltAssistantMessage(QString)));
    connect(_session->agent(), SIGNAL(failed(QString)),
            this, SLOT(sltFailed(QString)));
    connect(_session->agent(), SIGNAL(toolBatchFinished()),
            this, SLOT(sltToolBatchFinished()));
    pushTaskAnchor();
}

//----------------------------------------------

QString Worker::task() const
{
    return _task;
}

//----------------------------------------------

void Worker::setTask(QString task)
{
    _task = task;
    pushTaskAnchor();
}

//----------------------------------------------

void Worker::pushTaskAnchor()
{
    if (!_session || !_session->agent())
        return;
    // Формулировка задачи от родителя (или запрос пользователя для главного
    // чата) при сжатии контекста остаётся дословно. У инструмента «задача» —
    // очередной запрос на запуск, его закреплять не нужно: назначение
    // инструмента уже в системном промпте.
    _session->agent()->setTaskAnchor(isTool() ? QString() : _task);
}

//----------------------------------------------

QString Worker::title() const
{
    return _title;
}

//----------------------------------------------

void Worker::setTitle(QString title)
{
    QString clean = title.simplified();
    if (clean.size() > 80)
        clean = clean.left(77) + QString::fromUtf8("…");
    if (_title == clean)
        return;
    _title = clean;
    emit stateChanged();
}

//----------------------------------------------

QString Worker::personName() const
{
    return _personName;
}

//----------------------------------------------

void Worker::setPersonName(QString name)
{
    const QString clean = name.simplified();
    if (_personName == clean)
        return;
    _personName = clean;
    emit stateChanged();
}

//----------------------------------------------

QString Worker::shortName() const
{
    const QString person = _personName.trimmed();
    if (!person.isEmpty())
        return person;
    if (isTool())
    {
        const QString tool = toolName().trimmed();
        if (!tool.isEmpty())
            return tool;
        return tr("Инструмент");
    }
    if (isRoot())
        return tr("Главный менеджер");
    switch (_type)
    {
    case WorkerTypeExecutor:
        return tr("Исполнитель");
    case WorkerTypeTester:
        return tr("Тестировщик");
    case WorkerTypeTool:
        return tr("Инструмент");
    case WorkerTypeManager:
    default:
        return tr("Менеджер");
    }
}

//----------------------------------------------

void Worker::setIncomingAuthor(const QString &name)
{
    _incomingAuthor = name.trimmed();
}

//----------------------------------------------

void Worker::stampTurnAuthor()
{
    if (!_session || !_session->agent())
        return;
    QString author = _incomingAuthor.trimmed();
    _incomingAuthor.clear();
    if (author.isEmpty() && _parentWorker)
        author = _parentWorker->shortName();
    if (author.isEmpty())
        author = tr("Пользователь");
    _session->agent()->setTurnAuthor(author);
}

//----------------------------------------------

QString Worker::llmProfileId() const
{
    return _llmProfileId;
}

//----------------------------------------------

void Worker::setLlmProfileId(QString id)
{
    const QString clean = id.trimmed();
    if (_llmProfileId == clean)
        return;
    _llmProfileId = clean;
}

//----------------------------------------------

QString Worker::groupId() const
{
    return _groupId;
}

//----------------------------------------------

void Worker::setGroupId(QString id)
{
    _groupId = id.trimmed();
}

//----------------------------------------------

GroupRole Worker::groupRole() const
{
    return _groupRole;
}

//----------------------------------------------

void Worker::setGroupRole(GroupRole role)
{
    _groupRole = role;
}

//----------------------------------------------

bool Worker::isGroupChief() const
{
    return _groupRole == GroupRoleChief && !_groupId.isEmpty();
}

//----------------------------------------------

bool Worker::isGroupInterlocutor() const
{
    return _groupRole == GroupRoleInterlocutor && !_groupId.isEmpty();
}

//----------------------------------------------

bool Worker::canSpawnChildren() const
{
    if (isGroupInterlocutor())
        return false;
    if (isTool())
        return false;
    return type() == WorkerTypeManager || type() == WorkerTypeExecutor;
}

//----------------------------------------------

QString Worker::displayLabel() const
{
    QString taskPart = _title;
    if (taskPart.isEmpty())
        taskPart = shortLabelFromTask(_task);
    if (!_personName.isEmpty() && !taskPart.isEmpty())
        return _personName + QString::fromUtf8(" — ") + taskPart;
    if (!_personName.isEmpty())
        return _personName;
    return taskPart;
}

//----------------------------------------------

QString Worker::shortLabelFromTask(QString task)
{
    // Тексты задач у соседних воркеров начинаются с одинаковой преамбулы
    // (проект, правила, контекст), поэтому «первые 40 символов» не различают
    // их. Ищем строку с явным заголовком задачи, иначе берём первую
    // содержательную строку без markdown-мусора.
    const QStringList rawLines = task.split(QChar('\n'));
    QStringList lines;
    foreach (const QString &raw, rawLines)
    {
        const QString line = raw.simplified();
        if (!line.isEmpty())
            lines.append(line);
    }
    if (lines.isEmpty())
        return QString();

    static const QRegExp marker(
        QString::fromUtf8("^[#*\\-\\s]*(?:\\*\\*)?\\s*(?:задача|подзадача|цель|название|заголовок|"
                          "task|subtask|goal|title|objective)\\s*(?:\\*\\*)?\\s*[:：—\\-]\\s*(.+)$"),
        Qt::CaseInsensitive);
    QString picked;
    foreach (const QString &line, lines)
    {
        if (marker.indexIn(line) == 0)
        {
            picked = marker.cap(1);
            break;
        }
    }
    if (picked.isEmpty())
    {
        // Заголовок markdown в начале — лучший кандидат.
        foreach (const QString &line, lines)
        {
            if (line.startsWith(QChar('#')))
            {
                picked = line;
                break;
            }
        }
    }
    if (picked.isEmpty())
        picked = lines.first();

    picked.remove(QRegExp(QString::fromUtf8("^[#*\\-\\d.)\\s]+")));
    picked.remove(QString::fromUtf8("**"));
    picked = picked.simplified();
    // Обрезаем по границе предложения, если суть уместилась в первое.
    const int stop = picked.indexOf(QRegExp(QString::fromUtf8("[.!?;]\\s")));
    if (stop >= 12)
        picked = picked.left(stop);
    if (picked.size() > 60)
        picked = picked.left(57) + QString::fromUtf8("…");
    return picked;
}

//----------------------------------------------

QString Worker::result() const
{
    return _result;
}

//----------------------------------------------

void Worker::setResult(QString result)
{
    _result = result;
}

//----------------------------------------------

QString Worker::lastError() const
{
    return _lastError;
}

//----------------------------------------------

QString Worker::lastAssistant() const
{
    return _lastAssistant;
}

//----------------------------------------------

QString Worker::permissionAction() const
{
    return _permissionAction;
}

//----------------------------------------------

int Worker::depth() const
{
    int d = 0;
    const Worker *walk = _parentWorker;
    while (walk)
    {
        ++d;
        walk = walk->parentWorker();
    }
    return d;
}

//----------------------------------------------

bool Worker::isRoot() const
{
    return _parentWorker == 0 && _type != WorkerTypeTool;
}

//----------------------------------------------

bool Worker::isTool() const
{
    return _type == WorkerTypeTool;
}

//----------------------------------------------

bool Worker::isUserStopped() const
{
    return _userStopped;
}

//----------------------------------------------

QString Worker::toolName() const
{
    return _toolName;
}

//----------------------------------------------

void Worker::setToolName(QString name)
{
    _toolName = name.simplified();
}

//----------------------------------------------

QString Worker::toolPurpose() const
{
    return _toolPurpose;
}

//----------------------------------------------

void Worker::setToolPurpose(QString purpose)
{
    _toolPurpose = purpose.trimmed();
}

//----------------------------------------------

void Worker::noteFileChanged(QString path)
{
    QString rel = path.trimmed();
    rel.replace(QChar('\\'), QChar('/'));
    while (rel.startsWith(QString::fromUtf8("./")))
        rel.remove(0, 2);
    if (rel.isEmpty())
        return;
    if (!_changedFiles.contains(rel))
        _changedFiles.append(rel);
    ProjectBrief::instance()->invalidate();
}

//----------------------------------------------

QStringList Worker::changedFiles() const
{
    return _changedFiles;
}

//----------------------------------------------

void Worker::setChangedFiles(const QStringList &files)
{
    _changedFiles.clear();
    foreach (const QString &item, files)
    {
        QString rel = item.trimmed();
        rel.replace(QChar('\\'), QChar('/'));
        while (rel.startsWith(QString::fromUtf8("./")))
            rel.remove(0, 2);
        if (!rel.isEmpty() && !_changedFiles.contains(rel))
            _changedFiles.append(rel);
    }
}

//----------------------------------------------

QString Worker::changedFilesText() const
{
    if (_changedFiles.isEmpty())
        return QString();
    QString text = QString::fromUtf8("Files changed:");
    foreach (const QString &item, _changedFiles)
        text += QString::fromUtf8("\n- ") + item;
    return text;
}

//----------------------------------------------

bool Worker::toolIsFree() const
{
    if (!isTool())
        return false;
    if (!_session || !_session->agent())
        return false;
    if (_session->agent()->isBusy())
        return false;
    if (_state == WorkerStateRunning || _state == WorkerStateQueued
        || _state == WorkerStateWaitingApproval
        || _state == WorkerStateWaitingContinuation
        || _state == WorkerStateWaitingChildren)
        return false;
    return true;
}

//----------------------------------------------

void Worker::beginToolRun(QString message)
{
    // Инструмент: первый запуск — как обычная задача (через очередь
    // менеджера), дальнейшие — как продолжение той же беседы, чтобы
    // накопленный опыт (команды, пути, ошибки) не терялся.
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty())
        return;
    _changedFiles.clear();
    WorkerManager *manager = qobject_cast<WorkerManager *>(parent());
    if (!_toolStarted)
    {
        _toolStarted = true;
        _task = trimmed;
        // После восстановления инструмент может числиться «ответившим» —
        // иначе requestStart() молча откажется его запускать.
        _completed = false;
        if (answeredState(_state))
            setState(WorkerStateIdle);
        if (manager)
            manager->requestStart(this);
        else
            startTask();
        return;
    }
    beginFollowUp(trimmed);
}

//----------------------------------------------

bool Worker::waitingForTool() const
{
    return _waitingForTool > 0;
}

//----------------------------------------------

void Worker::setWaitingForTool(bool waiting)
{
    if (waiting)
        ++_waitingForTool;
    else if (_waitingForTool > 0)
        --_waitingForTool;
}

//----------------------------------------------

void Worker::parkForGroupDialogue()
{
    _parkForGroupDialogue = true;
    _skipIdleComplete = true;
}

//----------------------------------------------

void Worker::setVerification(bool present, bool passed, QString command, QString output,
                             QString artifact, QString digest)
{
    _hasVerification = present;
    _verificationPassed = present && passed;
    _verificationCommand = command.trimmed();
    _verificationOutput = output.trimmed();
    _verificationArtifact = artifact.trimmed();
    _verificationDigest = digest.trimmed();
}

//----------------------------------------------

bool Worker::hasVerification() const
{
    return _hasVerification;
}

//----------------------------------------------

bool Worker::verificationPassed() const
{
    return _hasVerification && _verificationPassed;
}

//----------------------------------------------

QString Worker::verificationCommand() const
{
    return _verificationCommand;
}

//----------------------------------------------

QString Worker::verificationOutput() const
{
    return _verificationOutput;
}

//----------------------------------------------

QString Worker::verificationArtifact() const
{
    return _verificationArtifact;
}

//----------------------------------------------

QString Worker::verificationDigest() const
{
    return _verificationDigest;
}

//----------------------------------------------

void Worker::noteToolBatch()
{
    ++_toolBatchCount;
}

//----------------------------------------------

int Worker::toolBatchCount() const
{
    return _toolBatchCount;
}

//----------------------------------------------

int Worker::subtreeToolBatches() const
{
    int total = _toolBatchCount;
    foreach (Worker *child, _children)
    {
        if (child)
            total += child->subtreeToolBatches();
    }
    return total;
}

//----------------------------------------------

qint64 Worker::startedAtMs() const
{
    return _startedAtMs;
}

//----------------------------------------------

qint64 Worker::subtreeElapsedMs() const
{
    qint64 start = _startedAtMs;
    foreach (Worker *child, _children)
    {
        if (!child || child->startedAtMs() <= 0)
            continue;
        if (start <= 0 || child->startedAtMs() < start)
            start = child->startedAtMs();
    }
    if (start <= 0)
        return 0;
    return QDateTime::currentMSecsSinceEpoch() - start;
}

//----------------------------------------------

int Worker::subtreeBatchLimit() const
{
    return WorkerManager::maxSubtreeToolBatches() + _subtreeBatchAllowance;
}

//----------------------------------------------

qint64 Worker::subtreeMsLimit() const
{
    return WorkerManager::maxSubtreeMs() + _subtreeMsAllowance;
}

//----------------------------------------------

bool Worker::requestSubtreeBudgetGrant(int grantBatches, qint64 grantMs)
{
    if (_userStopped)
        return false;
    _subtreeBudgetGranted = false;
    _pendingSubtreeGrantBatches = qMax(0, grantBatches);
    _pendingSubtreeGrantMs = qMax(qint64(0), grantMs);

    const int usedBatches = subtreeToolBatches();
    const int limitBatches = subtreeBatchLimit();
    const int usedMin = int(subtreeElapsedMs() / 60000);
    const int limitMin = int(subtreeMsLimit() / 60000);
    const QString text = tr(
        "Бюджет поддерева исчерпан.\n"
        "Tool-пакеты: %1 / %2. Время: ≈%3 / %4 мин.\n"
        "Сбросить лимиты (+%5 пакетов, +%6 мин) и продолжить создание детей?")
                             .arg(usedBatches)
                             .arg(limitBatches)
                             .arg(usedMin)
                             .arg(limitMin)
                             .arg(_pendingSubtreeGrantBatches)
                             .arg(int(_pendingSubtreeGrantMs / 60000));

    setState(WorkerStateWaitingApproval);
    _subtreeBudgetPromptText = text;
    emit subtreeBudgetRequested(text);

    _subtreeBudgetLoopActive = true;
    QEventLoop loop;
    connect(this, SIGNAL(subtreeBudgetResolved()), &loop, SLOT(quit()));
    loop.exec();
    _subtreeBudgetLoopActive = false;
    _subtreeBudgetPromptText.clear();

    if (_state == WorkerStateWaitingApproval)
        setState(WorkerStateRunning);
    return _subtreeBudgetGranted;
}

//----------------------------------------------

void Worker::resolveSubtreeBudget(bool granted)
{
    _subtreeBudgetGranted = granted;
    if (granted)
    {
        _subtreeBatchAllowance += _pendingSubtreeGrantBatches;
        _subtreeMsAllowance += _pendingSubtreeGrantMs;
    }
    _pendingSubtreeGrantBatches = 0;
    _pendingSubtreeGrantMs = 0;
    emit subtreeBudgetResolved();
}

//----------------------------------------------

bool Worker::isWaitingSubtreeBudget() const
{
    return _subtreeBudgetLoopActive;
}

//----------------------------------------------

QString Worker::subtreeBudgetPromptText() const
{
    return _subtreeBudgetPromptText;
}

//----------------------------------------------

void Worker::applyPersistedState(WorkerState state, QString result,
                                 QString lastError, QString lastAssistant,
                                 QString permissionAction)
{
    _result = result;
    _lastError = lastError;
    _lastAssistant = lastAssistant;
    _permissionAction = permissionAction;
    _skipIdleComplete = false;
    _resumeContinueChat = false;
    if (isTool())
        _toolStarted = !_task.trimmed().isEmpty();

    if (answeredState(state))
    {
        _completed = true;
        _state = state;
        return;
    }

    if (state == WorkerStateIdle && !isRoot() && !result.trimmed().isEmpty())
    {
        _completed = true;
        _state = WorkerStateCompleted;
        return;
    }

    _completed = false;
    if (state == WorkerStateWaitingContinuation
        || state == WorkerStateWaitingApproval
        || state == WorkerStateWaitingChildren)
        _skipIdleComplete = true;
    _state = state;
}

//----------------------------------------------

void Worker::resumeAfterRestore()
{
    if (_completed || hasAnswered())
        return;

    if (isTool())
    {
        // Запросивший воркер после перезапуска уже не ждёт результата:
        // просто освобождаем инструмент для новых запусков.
        _completed = true;
        _skipIdleComplete = false;
        _pendingFollowUps.clear();
        _pendingAuthors.clear();
        if (!_result.trimmed().isEmpty() || _toolStarted)
            _state = WorkerStateCompleted;
        else
            _state = WorkerStateIdle;
        return;
    }

    if (_state == WorkerStateQueued)
    {
        WorkerManager *manager = qobject_cast<WorkerManager *>(parent());
        _state = WorkerStateIdle;
        if (manager)
            manager->requestStart(this);
        else
            startTask();
        return;
    }

    // История чата грузится только там, где она нужна сразу: инструментам —
    // нет, а воркер в очереди прочитает её сам в startTask(). Раньше при
    // открытии проекта читались чаты всех незавершённых воркеров подряд.
    if (_session)
        _session->ensureContentLoaded();

    if (_state == WorkerStateWaitingChildren)
    {
        if (!hasUnfinishedChildren())
            QMetaObject::invokeMethod(this, "sltFinishWaitingChildren",
                                      Qt::QueuedConnection);
        return;
    }

    if (_state == WorkerStateWaitingApproval)
    {
        QString action = _permissionAction.trimmed();
        if (action.isEmpty())
            action = QString::fromUtf8("Pending action");
        emit permissionRequested(action);
        return;
    }

    if (_state == WorkerStateWaitingContinuation)
    {
        QString error = _lastError.trimmed();
        if (error.isEmpty())
            error = QString::fromUtf8("Application was restarted. Continue the task.");
        emit continuationRequested(error);
        return;
    }

    if (isRoot() && _state == WorkerStateIdle)
        return;

    _skipIdleComplete = true;
    _resumeContinueChat = (_state == WorkerStateRunning);
    if (_lastError.trimmed().isEmpty())
        _lastError = QString::fromUtf8(
            "Application was restarted. Continue the assigned task from the current history.");
    setState(WorkerStateWaitingContinuation);
    emit continuationRequested(_lastError);
}

//----------------------------------------------

void Worker::quitBlockingLoops()
{
    _pendingFollowUps.clear();
    _pendingAuthors.clear();
    _completed = true;
    _skipIdleComplete = true;
    _permissionLoopActive = false;
    _consoleInputSent = false;
    _consoleInputLoopActive = false;
    _consoleInputTranscript.clear();
    emit permissionResolved();
    emit consoleInputResolved();
    emit continuationResolved();
    emit subtreeBudgetResolved();
    emit finished();
}

//----------------------------------------------

void Worker::beginUserStop()
{
    _userStopped = true;
    _skipIdleComplete = true;
    _parkForGroupDialogue = false;
    _pendingFollowUps.clear();
    _pendingAuthors.clear();
    _delegatedThisBatch.clear();
    if (_session && _session->agent())
        _session->agent()->abort();
    _permissionLoopActive = false;
    _consoleInputSent = false;
    _consoleInputLoopActive = false;
    _consoleInputTranscript.clear();
    emit permissionResolved();
    emit consoleInputResolved();
    emit continuationResolved();
    emit subtreeBudgetResolved();
}

//----------------------------------------------

void Worker::finishUserStop()
{
    if (!_userStopped)
        return;
    if (isRoot())
    {
        _completed = false;
        _skipIdleComplete = false;
        _userStopped = false;
        setState(WorkerStateIdle);
        return;
    }
    if (!hasAnswered())
    {
        complete(QString::fromUtf8("STOPPED: the user stopped all workers."), false);
    }
}

//----------------------------------------------

void Worker::forceStop()
{
    beginUserStop();
    finishUserStop();
}

//----------------------------------------------

void Worker::forceReportToParent()
{
    if (isRoot())
        return;
    if (_session && _session->agent())
        _session->agent()->abort();
    _userStopped = true;
    _skipIdleComplete = true;
    _pendingFollowUps.clear();
    _pendingAuthors.clear();
    QString report = composeParentReport();
    if (report.trimmed().isEmpty())
        report = _lastAssistant.trimmed();
    if (report.trimmed().isEmpty())
        report = QString::fromUtf8(
            "FORCED REPORT: the user interrupted the worker and sent this report.");
    else
        report = QString::fromUtf8("FORCED REPORT (user interrupted the worker):\n")
                 + report;
    _completed = false;
    complete(report, true);
}

//----------------------------------------------

QString Worker::composeStartPrefix() const
{
    QStringList parts;
    WorkerManager *manager = qobject_cast<WorkerManager *>(parent());
    if (manager)
    {
        const QString startup = manager->startupContextFor(const_cast<Worker *>(this));
        if (!startup.trimmed().isEmpty())
            parts.append(startup);
    }
    if (_type != WorkerTypeTool)
    {
        const QString brief = ProjectBrief::instance()->text();
        if (!brief.trimmed().isEmpty())
            parts.append(brief);
    }
    if (_parentWorker)
    {
        QString arch = _parentWorker->architecture().trimmed();
        if (!arch.isEmpty())
        {
            if (arch.size() > 1500)
                arch = arch.left(1497) + QString::fromUtf8("...");
            parts.append(QString::fromUtf8("Architecture (from your manager):\n") + arch);
        }
        // Handoff — в injectContext, не в тексте задания (журнал показывает чистое задание).
        QString handoff = QString::fromUtf8(
            "CONTEXT HANDOFF (use this; do not ignore):\n"
            "--- parent brief ---\n%1\n")
                              .arg(_parentWorker->task().trimmed().left(2000));
        const QString siblings = _parentWorker->childResultsReport().trimmed();
        if (!siblings.isEmpty())
            handoff += QString::fromUtf8("--- sibling / prior child outcomes ---\n%1\n")
                           .arg(siblings.left(2500));
        handoff += QString::fromUtf8(
            "COMMUNICATION: Do not discuss with sibling workers. Receive work from your "
            "parent and report via complete_task; managers assign children via "
            "delegate_task. Group dialogue uses group_speak / group_await / "
            "group_propose / group_vote only. Keep your work as isolated as possible "
            "from siblings so contexts barely overlap. Name the files/interfaces you own.\n");
        parts.append(handoff);
    }
    const QString hint = KnowledgeStore::instance()->hintFor(_type, _task);
    if (!hint.isEmpty())
        parts.append(hint);
    return parts.join(QString::fromUtf8("\n\n"));
}

//----------------------------------------------

QString Worker::composeStartMessage() const
{
    const QString prefix = composeStartPrefix();
    if (prefix.isEmpty())
        return _task;
    return prefix + QString::fromUtf8("\n\n") + _task;
}

//----------------------------------------------

void Worker::startTask()
{
    if (_task.trimmed().isEmpty())
        return;
    if (!_session || !_session->agent())
        return;
    stampTurnAuthor();
    _session->ensureContentLoaded();
    _completed = false;
    _skipIdleComplete = false;
    _needsParentReport = false;
    _parentReportNudged = false;
    _userStopped = false;
    _resumeContinueChat = false;
    _lastAssistant.clear();
    _lastError.clear();
    _result.clear();
    if (!isTool())
        _changedFiles.clear();
    clearWorkPlan();
    _hasVerification = false;
    _verificationPassed = false;
    _verificationCommand.clear();
    _verificationOutput.clear();
    _verificationArtifact.clear();
    _verificationDigest.clear();
    _toolBatchCount = 0;
    _startedAtMs = QDateTime::currentMSecsSinceEpoch();
    setState(WorkerStateRunning);

    WorkerManager *manager = qobject_cast<WorkerManager *>(parent());
    if (manager && workerTaskKindIsMediaExecutor(manager->modalityOf(this)))
    {
        // Image/speech/audio/video/embedding — без chat tool_calls.
        pushTaskAnchor();
        _session->agent()->recordLocalExchange(
            _task, QString(), AgentUserIngressAssignment);
        bool ok = false;
        const QString report = manager->runMediaExecutor(this, &ok);
        _lastAssistant = report;
        _session->agent()->recordLocalExchange(
            QString(), report, AgentUserIngressAssignment);
        complete(report, ok);
        return;
    }

    const QString prefix = composeStartPrefix();
    if (!prefix.isEmpty())
        _session->agent()->injectContext(prefix);
    _session->agent()->sendUserMessage(_task, AgentUserIngressAssignment);
}

//----------------------------------------------

void Worker::complete(QString result, bool ok)
{
    if (!_pendingFollowUps.isEmpty())
    {
        _result = result;
        _completed = false;
        _skipIdleComplete = true;
        QMetaObject::invokeMethod(this, "sltFlushFollowUps", Qt::QueuedConnection);
        return;
    }
    if (_completed)
        return;
    // Наверх уходит только отчёт модели: она уже пересказала детей, а их
    // полные результаты остаются доступны через get_child_result. Сводку по
    // детям добавляем лишь когда отчёта нет вовсе.
    QString text = result.trimmed();
    if (text.isEmpty() || text == QString::fromUtf8("(no output)"))
        text = composeParentReport();
    const QString files = changedFilesText();
    if (!files.isEmpty() && !text.contains(QString::fromUtf8("Files changed:")))
        text += QString::fromUtf8("\n\n") + files;
    _completed = true;
    _skipIdleComplete = false;
    _needsParentReport = false;
    _parentReportNudged = false;
    _result = text;
    WorkerManager *manager = qobject_cast<WorkerManager *>(parent());
    if (manager)
        manager->releaseFilePaths(this);
    setState(ok ? WorkerStateCompleted : WorkerStateFailed);
    emit finished();
}

//----------------------------------------------

void Worker::registerDelegated(Worker *child)
{
    if (!child)
        return;
    if (!_delegatedThisBatch.contains(child))
        _delegatedThisBatch.append(child);
    if (!_touchedThisTurn.contains(child))
        _touchedThisTurn.append(child);
    child->markUsed();
}

//----------------------------------------------

QString Worker::childResultsReport() const
{
    QString text;
    if (isRoot())
    {
        text = QString::fromUtf8(
            "All child workers have answered. Summarize the outcome for the user. "
            "Do not spawn more workers unless something failed and still needs work.\n");
    }
    else
    {
        text = QString::fromUtf8(
            "All child workers have answered. You MUST now call complete_task with YOUR "
            "report for the parent worker. Do not address the end user. "
            "Summarize the assignment, what each child delivered, and the overall outcome. "
            "That complete_task result is how the parent learns you finished.\n");
    }
    const QList<Worker *> list = _delegatedThisBatch.isEmpty() ? _children : _delegatedThisBatch;
    if (list.isEmpty())
    {
        text += QString::fromUtf8("(no child workers)\n");
        return text;
    }
    bool clipped = false;
    foreach (Worker *child, list)
    {
        if (!child)
            continue;
        text += QString::fromUtf8("\n---\n");
        text += QString::fromUtf8("id=%1 type=%2 state=%3\n")
                    .arg(child->id(),
                         workerTypeToString(child->type()),
                         workerStateToString(child->state()));
        // Полный текст задачи уже есть в истории родителя (аргументы
        // delegate_task) — даём лишь короткую метку для ориентировки.
        QString task = child->task().simplified();
        if (task.size() > kChildTaskLabelChars)
            task = task.left(kChildTaskLabelChars - 3) + QString::fromUtf8("...");
        if (!task.isEmpty())
            text += QString::fromUtf8("task: %1\n").arg(task);
        text += QString::fromUtf8("result:\n");
        QString result = child->result().trimmed();
        if (result.isEmpty())
            result = QString::fromUtf8("(empty)");
        else if (result.size() > kChildResultReportChars)
        {
            result = result.left(kChildResultReportChars)
                     + QString::fromUtf8("\n...[clipped: %1 of %2 chars shown; "
                                           "call get_child_result(worker_id=%3) for the full report]")
                           .arg(kChildResultReportChars)
                           .arg(result.size())
                           .arg(child->id());
            clipped = true;
        }
        text += result;
        text += QChar('\n');
    }
    if (clipped)
        text += QString::fromUtf8(
            "\nLong reports above are clipped; fetch a full one with get_child_result "
            "only if the clipped part is really needed.\n");
    return text;
}

//----------------------------------------------

QString Worker::childOutcomesSummary() const
{
    if (_children.isEmpty())
        return QString();

    // Свежие делегирования в этом батче — первыми (приоритет, не отсечение).
    QList<Worker *> ordered;
    QList<Worker *> seen;
    foreach (Worker *child, _delegatedThisBatch)
    {
        if (!child || seen.contains(child))
            continue;
        seen.append(child);
        ordered.append(child);
    }
    foreach (Worker *child, _children)
    {
        if (!child || seen.contains(child))
            continue;
        seen.append(child);
        ordered.append(child);
    }

    static const int kPerChildChars = 120;
    static const int kBlockMaxChars = 750;

    QString text = QString::fromUtf8("Child outcomes:");
    int omitted = 0;
    for (int i = 0; i < ordered.size(); ++i)
    {
        Worker *child = ordered.at(i);
        QString snippet = child->result().trimmed();
        if (snippet.isEmpty())
            snippet = QString::fromUtf8("(empty)");
        if (snippet.size() > kPerChildChars)
            snippet = snippet.left(kPerChildChars - 3) + QString::fromUtf8("...");
        const QString row = QString::fromUtf8("\n- [%1 %2] %3")
                                .arg(workerTypeToString(child->type()),
                                     workerStateToString(child->state()),
                                     snippet);
        if (text.size() + row.size() > kBlockMaxChars)
        {
            omitted = ordered.size() - i;
            break;
        }
        text += row;
    }
    if (omitted > 0)
    {
        text += QString::fromUtf8(
                    "\n... (%1 more; use get_child_result for full reports)")
                    .arg(omitted);
    }
    return text;
}

//----------------------------------------------

QString Worker::composeParentReport() const
{
    QString text;
    const QString assignment = _task.trimmed();
    if (!assignment.isEmpty())
    {
        text += QString::fromUtf8("Assignment: ");
        text += assignment;
        text += QChar('\n');
    }
    QString body = _lastAssistant.trimmed();
    if (body == QString::fromUtf8("(no output)"))
        body.clear();
    if (!body.isEmpty())
    {
        text += body;
        text += QChar('\n');
    }
    const QString kids = childOutcomesSummary();
    if (!kids.isEmpty())
    {
        text += QChar('\n');
        text += kids;
    }
    text = text.trimmed();
    if (text.isEmpty())
        return QString::fromUtf8("(no output)");
    return text;
}

//----------------------------------------------

void Worker::onChildrenAnswered()
{
    if (!isRoot())
    {
        _needsParentReport = true;
        _parentReportNudged = false;
        _skipIdleComplete = true;
    }
    if (_session && _session->agent())
        _session->agent()->appendContextNote(childResultsReport());
}

//----------------------------------------------

QString Worker::askPermission(QString action, bool offerAlways, QString alwaysKey)
{
    Q_UNUSED(offerAlways);
    QString key = alwaysKey.trimmed();
    if (key.isEmpty())
        key = action.trimmed();
    if (!key.isEmpty() && AppSettings::instance()->isCommandAlwaysAllowed(key))
        return QString::fromUtf8("approved: always");

    _permissionResult.clear();
    _permissionAction = action;
    _permissionOfferAlways = true;
    _pendingAlwaysKey = key;
    setState(WorkerStateWaitingApproval);
    emit permissionRequested(action);

    _permissionLoopActive = true;
    QEventLoop loop;
    connect(this, SIGNAL(permissionResolved()), &loop, SLOT(quit()));
    loop.exec();
    _permissionLoopActive = false;

    if (_state == WorkerStateWaitingApproval)
        setState(WorkerStateRunning);
    return _permissionResult;
}

//----------------------------------------------

bool Worker::askConsoleInput(QString transcript, QString *line)
{
    _consoleInputLine.clear();
    _consoleInputSent = false;
    _consoleInputTranscript = transcript;
    _consoleInputLoopActive = true;
    emit consoleInputRequested(transcript);

    QEventLoop loop;
    connect(this, SIGNAL(consoleInputResolved()), &loop, SLOT(quit()));
    loop.exec();
    _consoleInputLoopActive = false;
    if (line)
        *line = _consoleInputLine;
    return _consoleInputSent;
}

//----------------------------------------------

void Worker::resolveConsoleInput(bool sent, QString line)
{
    _consoleInputSent = sent;
    _consoleInputLine = line;
    _consoleInputLoopActive = false;
    _consoleInputTranscript.clear();
    emit consoleInputResolved();
}

//----------------------------------------------

bool Worker::isWaitingConsoleInput() const
{
    return _consoleInputLoopActive;
}

//----------------------------------------------

QString Worker::consoleInputTranscript() const
{
    return _consoleInputTranscript;
}

//----------------------------------------------

void Worker::resolvePermission(bool approved, bool always, QString comment)
{
    if (approved)
        _permissionResult = QString::fromUtf8("approved");
    else
        _permissionResult = QString::fromUtf8("rejected");
    if (approved && always && !_pendingAlwaysKey.trimmed().isEmpty())
    {
        AppSettings::instance()->addAlwaysAllowedCommand(_pendingAlwaysKey, _permissionAction);
        _permissionResult = QString::fromUtf8("approved: always");
    }
    if (!comment.trimmed().isEmpty())
    {
        _permissionResult += QString::fromUtf8(": ");
        _permissionResult += comment.trimmed();
    }
    emit permissionResolved();

    if (_permissionLoopActive || _completed)
        return;
    if (!_session || !_session->agent() || _session->agent()->isBusy())
        return;

    _skipIdleComplete = false;
    setState(WorkerStateRunning);
    QString message = QString::fromUtf8(
        "The user answered request_permission: %1")
                          .arg(_permissionResult);
    if (!_permissionAction.trimmed().isEmpty())
    {
        message += QString::fromUtf8("\nAction: ");
        message += _permissionAction.trimmed();
    }
    message += QString::fromUtf8("\nContinue the assigned task.");
    _session->agent()->sendUserMessage(message, AgentUserIngressSystem);
}

//----------------------------------------------

bool Worker::permissionOfferAlways() const
{
    return _permissionOfferAlways;
}

//----------------------------------------------

QStringList Worker::alwaysAllowedCommands() const
{
    return QStringList();
}

//----------------------------------------------

void Worker::setAlwaysAllowedCommands(const QStringList &commands)
{
    AppSettings::instance()->addAlwaysAllowedCommands(commands);
}

//----------------------------------------------

void Worker::markUsed()
{
    _unusedParentTurns = 0;
}

//----------------------------------------------

static bool assignmentLooksComposite(QString text)
{
    const QString t = text.trimmed();
    if (t.size() >= 400)
        return true;
    QRegExp numbered(QString::fromUtf8("(?:^|\\n)\\s*\\d+[.)]\\s+\\S"));
    int pos = 0;
    int n = 0;
    while ((pos = numbered.indexIn(t, pos)) != -1)
    {
        ++n;
        pos += numbered.matchedLength();
    }
    if (n >= 2)
        return true;
    QRegExp bullets(QString::fromUtf8("(?:^|\\n)\\s*[-*]\\s+\\S"));
    pos = 0;
    n = 0;
    while ((pos = bullets.indexIn(t, pos)) != -1)
    {
        ++n;
        pos += bullets.matchedLength();
    }
    if (n >= 2)
        return true;
    const int paragraphs = t.count(QString::fromUtf8("\n\n"));
    if (paragraphs >= 2 && t.size() >= 220)
        return true;
    return false;
}

//----------------------------------------------

QString Worker::currentAssignment() const
{
    if (!_task.trimmed().isEmpty())
        return _task;
    if (!_session || !_session->agent())
        return QString();
    const QList<LlmMessage> history = _session->agent()->history();
    for (int i = history.size() - 1; i >= 0; --i)
    {
        if (history.at(i).role != LlmRoleUser)
            continue;
        const QString text = history.at(i).content.trimmed();
        if (text.isEmpty())
            continue;
        if (text.startsWith(QString::fromUtf8("All child workers have answered")))
            continue;
        if (text.startsWith(QString::fromUtf8("Follow-up from your parent")))
            continue;
        if (text.startsWith(QString::fromUtf8("The user answered")))
            continue;
        if (text.startsWith(QString::fromUtf8("Previous model request failed")))
            continue;
        return text;
    }
    return QString();
}

//----------------------------------------------

bool Worker::hasWorkPlan() const
{
    return !_architecture.trimmed().isEmpty() && !_workPlan.isEmpty();
}

//----------------------------------------------

QString Worker::architecture() const
{
    return _architecture;
}

//----------------------------------------------

QList<WorkSubtask> Worker::workPlan() const
{
    return _workPlan;
}

//----------------------------------------------

QString Worker::applyWorkPlan(QString architecture, const QList<WorkSubtask> &items)
{
    const QString arch = architecture.trimmed();
    if (arch.size() < 40)
        return QString::fromUtf8(
            "Architecture is too short. Describe components, boundaries and data flow.");
    if (items.isEmpty())
        return QString::fromUtf8("The plan must list at least one subtask.");

    QSet<QString> names;
    foreach (const WorkSubtask &item, items)
    {
        const QString name = item.name.trimmed().toLower();
        const QString task = item.task.trimmed();
        const QString complexity = item.complexity.trimmed().toLower();
        if (name.isEmpty())
            return QString::fromUtf8("Each subtask needs a name.");
        if (names.contains(name))
            return QString::fromUtf8("Duplicate subtask name: %1").arg(item.name.trimmed());
        names.insert(name);
        if (task.size() < 8)
            return QString::fromUtf8("Subtask '%1' needs a concrete task description.")
                .arg(item.name.trimmed());
        if (complexity != QString::fromUtf8("simple") && complexity != QString::fromUtf8("complex"))
            return QString::fromUtf8(
                "Subtask '%1' complexity must be 'simple' or 'complex'.")
                .arg(item.name.trimmed());
        if (complexity == QString::fromUtf8("complex") && item.cannotSplit)
            return QString::fromUtf8(
                "Subtask '%1': if it cannot be split further, mark complexity=simple, not complex.")
                .arg(item.name.trimmed());
        if (complexity == QString::fromUtf8("complex"))
        {
            WorkerManager *manager = qobject_cast<WorkerManager *>(parent());
            if (manager && !manager->canSpawnManagerUnder(this))
                return QString::fromUtf8(
                    "Subtask '%1' is marked complex, but type=manager is not allowed at your "
                    "depth. Split it in THIS plan into simple executor-sized subtasks.")
                    .arg(item.name.trimmed());
            if (item.expectedParts < 3)
                return QString::fromUtf8(
                    "Subtask '%1' is complex but would give a child manager only %2 part(s). "
                    "An intermediate manager for 1-2 parts is wasteful: list those parts here "
                    "as simple subtasks and delegate them to executors yourself, or set "
                    "expected_parts>=3 if the subtask really needs its own decomposition.")
                    .arg(item.name.trimmed())
                    .arg(item.expectedParts);
        }
    }

    if (assignmentLooksComposite(currentAssignment()) && items.size() < 2)
        return QString::fromUtf8(
            "This assignment is composite. Split it into at least two subtasks. "
            "Still-complex subtasks become child managers; simple/atomic ones become executors. "
            "Do not give the whole job to one executor.");

    _architecture = arch;
    _workPlan = items;
    for (int i = 0; i < _workPlan.size(); ++i)
        _workPlan[i].delegated = false;
    return QString();
}

//----------------------------------------------

WorkSubtask *Worker::findWorkSubtask(QString name, QString task)
{
    const QString wantName = name.trimmed().toLower();
    if (!wantName.isEmpty())
    {
        for (int i = 0; i < _workPlan.size(); ++i)
        {
            if (_workPlan.at(i).name.trimmed().toLower() == wantName)
                return &_workPlan[i];
        }
        return 0;
    }
    const QString wantTask = normalizeAssignment(task);
    if (wantTask.isEmpty())
        return 0;
    WorkSubtask *best = 0;
    int hits = 0;
    for (int i = 0; i < _workPlan.size(); ++i)
    {
        if (normalizeAssignment(_workPlan.at(i).task) == wantTask)
        {
            best = &_workPlan[i];
            ++hits;
        }
    }
    if (hits == 1)
        return best;
    return 0;
}

//----------------------------------------------

void Worker::markSubtaskDelegated(QString name)
{
    const QString want = name.trimmed().toLower();
    for (int i = 0; i < _workPlan.size(); ++i)
    {
        if (_workPlan.at(i).name.trimmed().toLower() == want)
        {
            _workPlan[i].delegated = true;
            return;
        }
    }
}

//----------------------------------------------

void Worker::clearWorkPlan()
{
    _architecture.clear();
    _workPlan.clear();
}

//----------------------------------------------

void Worker::resolveContinuation(bool retry)
{
    _continueRetry = retry;
    emit continuationResolved();
    if (retry)
        QMetaObject::invokeMethod(this, "sltResumeAfterError", Qt::QueuedConnection);
    else if (isRoot())
    {
        _skipIdleComplete = false;
        setState(WorkerStateIdle);
    }
    else
    {
        QString report = _lastAssistant.trimmed();
        const QString error = _lastError.trimmed();
        if (report.isEmpty())
            report = error.isEmpty()
                     ? QString::fromUtf8("FAILED: worker stopped after an error.")
                     : QString::fromUtf8("FAILED: %1").arg(error);
        else if (!error.isEmpty())
            report = QString::fromUtf8("FAILED: %1\n\nPartial output:\n%2")
                         .arg(error, report);
        complete(report, false);
    }
}

//----------------------------------------------

void Worker::sltResumeAfterError()
{
    if (_completed || !_session || !_session->agent())
        return;
    _skipIdleComplete = false;
    setState(WorkerStateRunning);
    if (_resumeContinueChat)
    {
        _resumeContinueChat = false;
        _session->agent()->continueChat();
        return;
    }
    QString message = QString::fromUtf8(
        "Previous model request failed");
    if (!_lastError.trimmed().isEmpty())
    {
        message += QString::fromUtf8(": ");
        message += _lastError.trimmed();
    }
    message += QString::fromUtf8(
        ".\nContinue the assigned task. "
        "If you cannot finish, call complete_task with what was done and what failed. "
        "The parent is still waiting for your answer.");
    _session->agent()->sendUserMessage(message, AgentUserIngressSystem);
}

//----------------------------------------------

void Worker::sltBusyChanged(bool busy)
{
    if (busy)
    {
        _userStopped = false;
        if (_state == WorkerStateIdle || _state == WorkerStateQueued
            || _state == WorkerStateWaitingChildren)
            setState(WorkerStateRunning);
        return;
    }
    // Инструмент освободился: менеджер запустит следующий запрос из очереди.
    if (isTool())
        emit stateChanged();
    if (_userStopped)
        return;
    if (!_pendingFollowUps.isEmpty())
    {
        QMetaObject::invokeMethod(this, "sltFlushFollowUps", Qt::QueuedConnection);
        return;
    }
    if (_completed)
        return;
    if (_state == WorkerStateWaitingApproval
        || _state == WorkerStateWaitingContinuation
        || _state == WorkerStateWaitingChildren)
        return;
    // Пауза на решение по бюджету функций — не конец хода.
    if (_session && _session->agent() && _session->agent()->isWaitingToolBudget())
        return;
    QMetaObject::invokeMethod(this, "sltIdleAfterBusy", Qt::QueuedConnection);
}

//----------------------------------------------

void Worker::sltIdleAfterBusy()
{
    if (_completed || _userStopped)
        return;
    if (_state == WorkerStateWaitingApproval
        || _state == WorkerStateWaitingContinuation
        || _state == WorkerStateWaitingChildren)
        return;
    if (_session && _session->agent()
        && (_session->agent()->isBusy() || _session->agent()->isWaitingToolBudget()))
        return;
    if (isRoot())
    {
        if (_state == WorkerStateRunning
            && !(_session && _session->agent() && _session->agent()->hasPendingContinue()))
        {
            setState(WorkerStateIdle);
            noteParentTurnEnded();
            // Главный менеджер ответил пользователю: сообщаем окну,
            // чтобы оно открыло его чат.
            emit finished();
        }
        return;
    }
    if (_needsParentReport)
    {
        if (!_parentReportNudged && _session && _session->agent())
        {
            _parentReportNudged = true;
            _skipIdleComplete = true;
            _session->agent()->sendUserMessage(
                QString::fromUtf8(
                    "You still have not called complete_task. "
                    "Call complete_task now with a concise report for your parent: "
                    "the assignment, what the children delivered, and the overall outcome. "
                    "Do not address the end user."),
                AgentUserIngressSystem);
            return;
        }
        complete(composeParentReport(), true);
        return;
    }
    if (_skipIdleComplete)
        return;
    if (_state != WorkerStateRunning)
        return;
    // Участники активного группового диалога не завершают задачу между репликами.
    if (!_groupId.isEmpty())
    {
        WorkerManager *manager = qobject_cast<WorkerManager *>(parent());
        if (manager && manager->isGroupDialogueActive(_groupId))
        {
            // Оратор/голосующий ушёл в idle без действия — снова разбудить,
            // иначе остальные навсегда остаются в group_await.
            if (manager->groupParticipantShouldAct(this))
            {
                setState(WorkerStateIdle);
                manager->ensureGroupParticipantActive(this);
                return;
            }
            setState(WorkerStateIdle);
            return;
        }
        // После единогласного DONE: собеседники автозавершаются с outcome,
        // chief (не root) должен вызвать complete_task — иначе один авто-отчёт.
        if (manager && manager->isGroupDialogueDone(_groupId))
        {
            const QString outcome = manager->groupDialogueOutcome(_groupId);
            if (isGroupInterlocutor())
            {
                complete(QString::fromUtf8("Group dialogue DONE:\n%1").arg(outcome), true);
                return;
            }
            if (!isRoot())
            {
                if (!_parentReportNudged && _session && _session->agent())
                {
                    _parentReportNudged = true;
                    _skipIdleComplete = true;
                    _session->agent()->sendUserMessage(
                        QString::fromUtf8(
                            "[Group dialogue DONE]\nApproved result:\n%1\n\n"
                            "Call complete_task with this result if you must report to a parent, "
                            "OR start another round with the same members via group_speak / "
                            "group_propose.")
                            .arg(outcome),
                        AgentUserIngressSystem);
                    return;
                }
                complete(QString::fromUtf8("Group dialogue DONE:\n%1").arg(outcome), true);
                return;
            }
            // Root chief: диалог закончен — idle без авто-complete в «parent».
            setState(WorkerStateIdle);
            return;
        }
    }
    noteParentTurnEnded();
    QString text = _lastAssistant.trimmed();
    if (text.isEmpty())
        text = QString::fromUtf8("(no output)");
    complete(text, true);
}

//----------------------------------------------

void Worker::sltAssistantMessage(const QString &text)
{
    _lastAssistant = text;
}

//----------------------------------------------

void Worker::sltFailed(const QString &error)
{
    if (_completed || _userStopped || isRoot())
        return;
    _skipIdleComplete = true;
    _resumeContinueChat = false;
    _lastError = error;
    setState(WorkerStateWaitingContinuation);
    emit continuationRequested(error);
}

//----------------------------------------------

void Worker::sltToolBatchFinished()
{
    if (_userStopped)
        return;
    noteToolBatch();
    if (_parkForGroupDialogue)
    {
        _parkForGroupDialogue = false;
        if (_session && _session->agent())
            _session->agent()->holdForExternal();
        if (_state == WorkerStateRunning)
            setState(WorkerStateIdle);
        return;
    }
    if (_delegatedThisBatch.isEmpty() && !hasUnfinishedChildren())
        return;

    if (!hasUnfinishedChildren())
    {
        onChildrenAnswered();
        _delegatedThisBatch.clear();
        return;
    }

    setState(WorkerStateWaitingChildren);
    if (_session && _session->agent())
        _session->agent()->holdForExternal();
    if (!hasUnfinishedChildren())
        QMetaObject::invokeMethod(this, "sltFinishWaitingChildren",
                                  Qt::QueuedConnection);
}

//----------------------------------------------

void Worker::sltFinishWaitingChildren()
{
    if (_completed || _userStopped)
        return;
    if (hasUnfinishedChildren())
        return;
    if (_state != WorkerStateWaitingChildren)
        return;
    onChildrenAnswered();
    _delegatedThisBatch.clear();
    setState(WorkerStateRunning);
    if (_session && _session->agent())
        _session->agent()->continueChat();
}

//----------------------------------------------

void Worker::sltChildFinished()
{
    if (_userStopped)
        return;
    emit stateChanged();
    if (hasUnfinishedChildren())
        return;
    if (_state != WorkerStateWaitingChildren)
        return;
    QMetaObject::invokeMethod(this, "sltFinishWaitingChildren",
                              Qt::QueuedConnection);
}

//----------------------------------------------

void Worker::noteParentTurnEnded()
{
    QMetaObject::invokeMethod(this, "sltPruneUnusedChildren", Qt::QueuedConnection);
}

//----------------------------------------------

void Worker::sltPruneUnusedChildren()
{
    if (_completed)
        return;
    if (_state == WorkerStateWaitingChildren
        || _state == WorkerStateWaitingApproval
        || _state == WorkerStateWaitingContinuation
        || _state == WorkerStateRunning)
        return;

    WorkerManager *manager = qobject_cast<WorkerManager *>(parent());
    Q_UNUSED(manager);
    const QList<Worker *> kids = _children;
    foreach (Worker *child, kids)
    {
        if (!child)
            continue;
        if (_touchedThisTurn.contains(child))
            child->markUsed();
        else
            ++child->_unusedParentTurns;
    }
    // Завершённых детей не удаляем автоматически: родитель может многократно
    // перезапустить их через delegate_task(worker_id=...) с новыми указаниями.
    _touchedThisTurn.clear();
}
