#include "agent.h"
//----------------------------------------------
#include "agenttool.h"
#include "appsettings.h"
#include "llmclient.h"
//----------------------------------------------
#include <QJsonDocument>
#include <QMetaObject>
#include <QPointer>
#include <QRegExp>
#include <QSet>

//----------------------------------------------

static const int kDefaultMaxToolIterations = 1000;
/** Заполнение окна, при котором перед запросом запускается сжатие. */
static const int kCompactRatioPermille     = 950;
/** Заполнение, при котором сжимаем жёстче (меньше свежего хвоста). */
static const int kEmergencyRatioPermille   = 980;
/** Доля окна, оставляемая дословно как свежий хвост при обычном сжатии. */
static const int kTailKeepPermille         = 200;
/** То же при аварийном сжатии. */
static const int kEmergencyTailPermille    = 80;
/** Доля окна под одну порцию истории, отдаваемую модели на сводку. */
static const int kSummaryChunkPermille     = 450;
/** Когда окно модели неизвестно: условный размер окна в токенах. */
static const int kFallbackWindowTokens     = 32000;
/** Потолок длины ответа-сводки. */
static const int kSummaryMaxTokens         = 3000;

static const QString kAnchorTaskPrefix = QString::fromUtf8(
    "ORIGINAL ASSIGNMENT (verbatim, preserved across context compaction):\n\n");
static const QString kAnchorFollowUpPrefix = QString::fromUtf8(
    "CURRENT FOLLOW-UP ASSIGNMENT (verbatim, preserved across context compaction):\n\n");

//----------------------------------------------

struct LiveFileContext
{
    QString id;
    QString meta;
    int     bytes;
};

static QList<LiveFileContext> liveFileContexts(const QList<LlmMessage> &history)
{
    QList<LiveFileContext> found;
    QRegExp rx(QString::fromUtf8("\\[\\[filectx id=(fc\\d+) ([^\\]]*)\\]\\]"));
    foreach (const LlmMessage &message, history)
    {
        int from = 0;
        while (from < message.content.size())
        {
            const int at = rx.indexIn(message.content, from);
            if (at < 0)
                break;
            LiveFileContext item;
            item.id = rx.cap(1);
            item.meta = rx.cap(2).trimmed();
            const int bodyStart = at + rx.matchedLength();
            const int closeAt = message.content.indexOf(QString::fromUtf8("[[/filectx]]"),
                                                        bodyStart);
            const int bodyEnd = closeAt < 0 ? message.content.size() : closeAt;
            item.bytes = message.content.mid(bodyStart, bodyEnd - bodyStart).toUtf8().size();
            from = bodyStart;
            if (item.meta.contains(QString::fromUtf8("dropped")))
                continue;
            bool seen = false;
            foreach (const LiveFileContext &have, found)
            {
                if (have.id == item.id)
                {
                    seen = true;
                    break;
                }
            }
            if (!seen)
                found.append(item);
        }
    }
    return found;
}

//----------------------------------------------

static QString fileRetentionRequest(const QList<LiveFileContext> &files)
{
    QString text = QString::fromUtf8(
        "FILE CONTEXT RETENTION (same reply as your real answer or tool calls; "
        "this is not a new task).\n"
        "Each block [[filectx id=...]] above is a file still stored in context.\n"
        "For every id, decide if that body is still needed on the NEXT iterations.\n"
        "Reply with these lines (omit a line if its list is empty):\n"
        "FILE_KEEP: id, id\n"
        "FILE_DROP: id, id\n"
        "FILE_DROP removes those bodies from later requests. "
        "Do not drop a file you still need to edit or quote. "
        "Unlisted ids stay.\n"
        "Cached files:");
    foreach (const LiveFileContext &file, files)
        text += QString::fromUtf8("\n- id=%1 %2").arg(file.id, file.meta);
    return text;
}

//----------------------------------------------

static QStringList parseDroppedFileIds(const QString &content)
{
    QStringList ids;
    const QStringList lines = content.split(QChar('\n'));
    QRegExp idRx(QString::fromUtf8("\\bfc\\d+\\b"));
    foreach (const QString &line, lines)
    {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(QString::fromUtf8("FILE_DROP:")))
            continue;
        const QString rest = trimmed.mid(QString::fromUtf8("FILE_DROP:").size());
        int from = 0;
        while (from < rest.size())
        {
            const int at = idRx.indexIn(rest, from);
            if (at < 0)
                break;
            const QString id = idRx.cap(0);
            if (!ids.contains(id))
                ids.append(id);
            from = at + idRx.matchedLength();
        }
    }
    return ids;
}

//----------------------------------------------

static QStringList eligibleDroppedFileIds(const QList<LlmMessage> &history,
                                          const QStringList &ids)
{
    if (ids.isEmpty() || !AppSettings::instance()->dropLargeContextFiles())
        return QStringList();
    const int minBytes = AppSettings::instance()->largeContextFileBytes();
    const QList<LiveFileContext> files = liveFileContexts(history);
    QStringList eligible;
    foreach (const QString &id, ids)
    {
        foreach (const LiveFileContext &file, files)
        {
            if (file.id == id && file.bytes >= minBytes)
            {
                eligible.append(id);
                break;
            }
        }
    }
    return eligible;
}

//----------------------------------------------

static QString stripRetentionDirectives(QString content)
{
    QStringList kept;
    const QStringList lines = content.split(QChar('\n'));
    foreach (const QString &line, lines)
    {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QString::fromUtf8("FILE_KEEP:"))
            || trimmed.startsWith(QString::fromUtf8("FILE_DROP:")))
            continue;
        kept.append(line);
    }
    return kept.join(QChar('\n')).trimmed();
}

//----------------------------------------------

static void dropFileContexts(QList<LlmMessage> *history, const QStringList &ids)
{
    if (!history || ids.isEmpty())
        return;
    for (int i = 0; i < history->size(); ++i)
    {
        QString content = history->at(i).content;
        bool changed = false;
        foreach (const QString &id, ids)
        {
            const QString open = QString::fromUtf8("[[filectx id=%1 ").arg(id);
            int from = 0;
            while (from < content.size())
            {
                const int at = content.indexOf(open, from);
                if (at < 0)
                    break;
                const int tagEnd = content.indexOf(QString::fromUtf8("]]"), at);
                if (tagEnd < 0)
                    break;
                const QString tag = content.mid(at, tagEnd - at);
                if (tag.contains(QString::fromUtf8("dropped")))
                {
                    from = tagEnd + 2;
                    continue;
                }
                const QString closeTag = QString::fromUtf8("[[/filectx]]");
                int blockEnd = content.indexOf(closeTag, tagEnd);
                if (blockEnd < 0)
                    blockEnd = tagEnd + 2;
                else
                    blockEnd += closeTag.size();
                const QString meta = tag.mid(open.size()).trimmed();
                const QString stub = QString::fromUtf8("[[filectx id=%1 %2 dropped]]")
                                         .arg(id, meta);
                content.replace(at, blockEnd - at, stub);
                changed = true;
                from = at + stub.size();
            }
        }
        if (changed)
            (*history)[i].content = content;
    }
}

//----------------------------------------------

Agent::Agent(QObject *parent)
    : QObject(parent)
    , _client(0)
    , _pendingUserIngress(AgentUserIngressHuman)
    , _lastUserIngress(AgentUserIngressHuman)
    , _summaryChunkTotal(0)
    , _phase(PhaseIdle)
    , _maxToolIterations(kDefaultMaxToolIterations)
    , _toolBudgetChunk(kDefaultMaxToolIterations)
    , _toolIterations(0)
    , _contextCap(0)
    , _tracksContext(true)
    , _lastEstimated(0)
    , _modelWorkId(0)
    , _busy(false)
    , _compactThenChat(false)
    , _overflowRetried(false)
    , _inspectIo(false)
    , _stopAfterTools(false)
    , _heldForExternal(false)
    , _waitingToolBudget(false)
    , _executingTools(false)
    , _toolCallDepth(0)
    , _repairedToolHistory(false)
    , _pendingContinueChat(false)
    , _finalAnswerRound(false)
    , _estimateDirty(true)
    , _estimateCache(0)
{
    _systemPrompt = QString::fromUtf8(
        "You are an assistant with access to tools (functions). "
        "Call them when you cannot give an accurate answer without them. "
        "After you receive tool results, answer the user. "
        "If the history contains a block titled "
        "\"Compressed summary of the previous conversation\", "
        "treat it as a continuation of the same chat. "
        "File tools create_file, write_file, read_file, delete_file, "
        "rename_file and list_directory work only inside the selected "
        "project directory. Use paths relative to that directory, "
        "without \"..\" to escape it.");
    resetHistory();
}

//----------------------------------------------

Agent::~Agent()
{
    endModelWork();
    // Клиент — соседний ребёнок ChatSession, созданный раньше агента, поэтому
    // при разрушении сессии он удаляется первым. disconnect() по мёртвому
    // указателю читал освобождённую память (падение в QObject::disconnect);
    // соединения Qt рвёт сама при удалении любой из сторон.
    _client = 0;
}

//----------------------------------------------

void Agent::setClient(LlmClient *client)
{
    if (_client == client)
        return;

    if (_client)
    {
        disconnect(_client, SIGNAL(responseReceived(LlmResponse)),
                   this, SLOT(sltResponseReceived(LlmResponse)));
        disconnect(_client, SIGNAL(failed(QString)),
                   this, SLOT(sltClientFailed(QString)));
        disconnect(_client, SIGNAL(contextWindowChanged()),
                   this, SLOT(refreshContextUsage()));
    }

    _client = client;
    if (_client)
    {
        connect(_client, SIGNAL(responseReceived(LlmResponse)),
                this, SLOT(sltResponseReceived(LlmResponse)));
        connect(_client, SIGNAL(failed(QString)),
                this, SLOT(sltClientFailed(QString)));
        connect(_client, SIGNAL(contextWindowChanged()),
                this, SLOT(refreshContextUsage()));
    }
    publishContextUsage();
}

//----------------------------------------------

LlmClient *Agent::client() const
{
    return _client;
}

//----------------------------------------------

void Agent::addTool(AgentTool *tool)
{
    if (!tool)
        return;
    if (tool->parent() == 0)
        tool->setParent(this);
    _tools.append(tool);
    _estimateDirty = true;
}

//----------------------------------------------

void Agent::clearTools()
{
    qDeleteAll(_tools);
    _tools.clear();
    _estimateDirty = true;
}

//----------------------------------------------

QString Agent::invokeTool(QString name, QString argumentsJson, bool *ok)
{
    AgentTool *tool = findTool(name);
    if (!tool)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Tool %1 is not installed.").arg(name);
    }
    emit toolCallStarted(name, argumentsJson);
    bool localOk = false;
    const bool nested = (name == QString::fromUtf8("delegate_task")
                         || name == QString::fromUtf8("run_tool"));
    const int span = nested ? 0 : AppSettings::instance()->beginWork();
    const QString result = tool->invoke(argumentsJson, &localOk);
    if (span)
        AppSettings::instance()->endWork(span);
    if (ok)
        *ok = localOk;
    emit toolCallFinished(name, result, localOk);
    return result;
}

//----------------------------------------------

void Agent::postNotice(QString text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;
    emit notice(trimmed);
}

//----------------------------------------------

void Agent::recordLocalExchange(QString userText, QString assistantText, int ingress)
{
    const QString user = userText.trimmed();
    const QString assistant = assistantText.trimmed();
    if (user.isEmpty() && assistant.isEmpty())
        return;
    if (_history.isEmpty())
        resetHistory();
    _lastUserIngress = ingress;
    if (!user.isEmpty())
    {
        LlmMessage msg;
        msg.role = LlmRoleUser;
        msg.content = user;
        msg.hideInUi = (ingress == AgentUserIngressSystem);
        _history.append(msg);
        if (!msg.hideInUi)
            emit userMessage(user);
    }
    if (!assistant.isEmpty())
    {
        LlmMessage msg;
        msg.role = LlmRoleAssistant;
        msg.content = assistant;
        _history.append(msg);
        emit assistantMessage(assistant);
    }
    emitContextUsage();
}

//----------------------------------------------

void Agent::requestStopAfterTools()
{
    _stopAfterTools = true;
}

//----------------------------------------------

void Agent::injectContext(QString text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;
    if (_history.isEmpty())
        resetHistory();
    LlmMessage note;
    note.role = LlmRoleUser;
    note.content = trimmed;
    note.hideInUi = true;
    _history.append(note);
    emitContextUsage();
}

//----------------------------------------------

void Agent::appendContextNote(QString text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;
    if (_executingTools)
    {
        _pendingNotes.append(trimmed);
        return;
    }

    LlmMessage note;
    note.role = LlmRoleUser;
    note.content = trimmed;
    _history.append(note);
    emit forwardedContext(trimmed);
    emitContextUsage();
}

//----------------------------------------------

void Agent::flushPendingNotes()
{
    const QStringList notes = _pendingNotes;
    _pendingNotes.clear();
    foreach (const QString &text, notes)
    {
        if (text.trimmed().isEmpty())
            continue;
        LlmMessage note;
        note.role = LlmRoleUser;
        note.content = text;
        _history.append(note);
        emit forwardedContext(text);
    }
    if (!notes.isEmpty())
        emitContextUsage();
}

//----------------------------------------------

void Agent::ensureToolCallIds(LlmMessage *message) const
{
    if (!message)
        return;
    for (int i = 0; i < message->toolCalls.size(); ++i)
    {
        if (!message->toolCalls.at(i).id.trimmed().isEmpty())
            continue;
        message->toolCalls[i].id = QString::fromUtf8("call_%1_%2")
                                       .arg(_history.size())
                                       .arg(i + 1);
    }
}

//----------------------------------------------

bool Agent::isBrokenToolHistoryError(QString error) const
{
    const QString e = error.toLower();
    if (!e.contains(QString::fromUtf8("tool")))
        return false;
    return e.contains(QString::fromUtf8("tool_calls"))
           || e.contains(QString::fromUtf8("role 'tool'"))
           || e.contains(QString::fromUtf8("role \"tool\""));
}

//----------------------------------------------

void Agent::holdForExternal()
{
    _heldForExternal = true;
}

//----------------------------------------------

void Agent::continueChat()
{
    if (_busy)
    {
        _pendingContinueChat = true;
        return;
    }
    if (!_client)
    {
        _pendingContinueChat = false;
        emit failed(tr("Клиент языковой модели не задан."));
        return;
    }
    _pendingContinueChat = false;
    _heldForExternal = false;
    _waitingToolBudget = false;
    _stopAfterTools = false;
    _finalAnswerRound = false;
    _compactThenChat = true;
    _overflowRetried = false;
    _repairedToolHistory = false;
    setBusy(true);
    setPhase(PhaseChat);
    maybeOptimizeThenComplete();
}

//----------------------------------------------

void Agent::sealIncompleteToolCalls(QString reason)
{
    QSet<QString> answered;
    foreach (const LlmMessage &message, _history)
    {
        if (message.role == LlmRoleTool && !message.toolCallId.isEmpty())
            answered.insert(message.toolCallId);
    }

    const QString text = reason.trimmed().isEmpty()
                         ? QString::fromUtf8("Interrupted.")
                         : reason.trimmed();
    for (int i = _history.size() - 1; i >= 0; --i)
    {
        if (_history.at(i).role != LlmRoleAssistant)
            continue;
        if (_history.at(i).toolCalls.isEmpty())
            break;
        int insertAt = i + 1;
        while (insertAt < _history.size() && _history.at(insertAt).role == LlmRoleTool)
            ++insertAt;
        foreach (const LlmToolCall &call, _history.at(i).toolCalls)
        {
            if (call.id.isEmpty() || answered.contains(call.id))
                continue;
            LlmMessage toolMessage;
            toolMessage.role = LlmRoleTool;
            toolMessage.toolCallId = call.id;
            toolMessage.name = call.name;
            toolMessage.content = text;
            _history.insert(insertAt, toolMessage);
            ++insertAt;
            answered.insert(call.id);
        }
        break;
    }
    _optimizer.repairToolTurns(&_history);
}

//----------------------------------------------

void Agent::setSystemPrompt(QString prompt)
{
    if (_systemPrompt == prompt)
        return;
    _systemPrompt = prompt;
    // Уже идущий диалог не трогаем: смена system ломает префикс кеша
    // провайдера. Новое значение попадёт в историю только при reset.
    if (!historyIsOnlySystem())
        return;
    if (!_busy)
        resetHistory();
}

//----------------------------------------------

QString Agent::systemPrompt() const
{
    return _systemPrompt;
}

//----------------------------------------------

void Agent::setProjectDirectory(QString path)
{
    _projectDirectory = path.trimmed();
    if (historyIsOnlySystem() && !_busy)
        resetHistory();
}

//----------------------------------------------

QString Agent::projectDirectory() const
{
    return _projectDirectory;
}

//----------------------------------------------

QString Agent::effectiveSystemPrompt() const
{
    QString text = _systemPrompt;
    if (_projectDirectory.isEmpty())
    {
        text += QString::fromUtf8(
            "\n\nProject directory is not selected yet. "
            "File operations are unavailable until the user picks a folder in the UI.");
    }
    else
    {
        text += QString::fromUtf8(
            "\n\nProject directory: %1\n"
            "All file operations must stay inside it. "
            "Use relative paths (for example src/main.cpp).\n"
            "Strongly preferred for scratch files (scripts, logs, test binaries, downloads, "
            "notes): always use ___project___/tmp (create it if missing, e.g. "
            "___project___/tmp/build.log). Do not put throwaway files in a project-root tmp/ "
            "or among source files. The rest of ___project___ "
            "is this tool's service storage: do not read, list or modify it.")
                .arg(_projectDirectory);
    }
    return text;
}

//----------------------------------------------

void Agent::setMaxToolIterations(int count)
{
    if (count > 0)
    {
        _maxToolIterations = count;
        if (!_waitingToolBudget)
            _toolBudgetChunk = count;
    }
}

//----------------------------------------------

void Agent::setContextCap(int tokens)
{
    const int value = tokens > 0 ? tokens : 0;
    if (_contextCap == value)
        return;
    _contextCap = value;
    publishContextUsage();
}

//----------------------------------------------

void Agent::applyProfileLimits(const LlmProfile &profile)
{
    int iterations = profile.maxToolIterations;
    if (iterations < 2)
        iterations = kDefaultMaxToolIterations;
    _toolBudgetChunk = iterations > 0 ? iterations : kDefaultMaxToolIterations;
    setMaxToolIterations(iterations);
    // Сжимает и считает контекст только воркер с языковой моделью.
    // Медиа-исполнитель ходит в API генерации и окна контекста не имеет.
    _tracksContext = !workerTaskKindIsMediaExecutor(profile.modality);
    setContextCap(profile.contextCap);
    if (_tracksContext)
        publishContextUsage();
}

//----------------------------------------------

bool Agent::isWaitingToolBudget() const
{
    return _waitingToolBudget;
}

//----------------------------------------------

int Agent::toolBudgetChunk() const
{
    return _toolBudgetChunk > 0 ? _toolBudgetChunk : kDefaultMaxToolIterations;
}

//----------------------------------------------

void Agent::resolveToolBudget(bool continueWork)
{
    if (!_waitingToolBudget)
        return;
    _waitingToolBudget = false;

    if (!_client)
    {
        emit failed(tr("Клиент языковой модели не задан."));
        setPhase(PhaseIdle);
        setBusy(false);
        return;
    }

    if (continueWork)
    {
        const int chunk = toolBudgetChunk();
        _toolIterations = 0;
        _maxToolIterations = chunk;
        _finalAnswerRound = false;
        emit notice(tr("Лимит вызовов функций сброшен (%1). Продолжаю работу.")
                    .arg(_maxToolIterations));
    }
    else
    {
        _finalAnswerRound = true;
        emit notice(tr("Бюджет вызовов функций исчерпан (%1). Завершаю работу.")
                    .arg(_maxToolIterations));
        LlmMessage nudge;
        nudge.role = LlmRoleUser;
        nudge.content = QString::fromUtf8(
            "TOOL BUDGET EXHAUSTED. If you are a child, the only remaining call is "
            "complete_task: report what you did in YOUR part. If you are the top "
            "executor, you have no complete_task: answer the user in text with the "
            "outcome. Do not tell the user or a parent to finish the work for you.");
        _history.append(nudge);
    }

    // Иначе сжатие контекста на этом ходе завершит агента, не запросив модель,
    // и воркер отправит родителю промежуточный текст вместо продолжения.
    _compactThenChat = true;
    setBusy(true);
    setPhase(PhaseChat);
    maybeOptimizeThenComplete();
}

//----------------------------------------------

void Agent::setTaskAnchor(QString text)
{
    _taskAnchor = text.trimmed();
}

//----------------------------------------------

QString Agent::taskAnchor() const
{
    return _taskAnchor;
}

//----------------------------------------------

QList<LlmMessage> Agent::anchorMessages() const
{
    QList<LlmMessage> anchors;
    if (!_taskAnchor.isEmpty())
    {
        LlmMessage task;
        task.role = LlmRoleUser;
        task.content = kAnchorTaskPrefix + _taskAnchor;
        anchors.append(task);
    }
    if (!_followUpAnchor.isEmpty())
    {
        LlmMessage followUp;
        followUp.role = LlmRoleUser;
        followUp.content = kAnchorFollowUpPrefix + _followUpAnchor;
        anchors.append(followUp);
    }
    return anchors;
}

//----------------------------------------------

bool Agent::isAnchorMessage(const LlmMessage &message) const
{
    if (message.role != LlmRoleUser)
        return false;
    const QString text = message.content.trimmed();
    if (text.isEmpty())
        return false;
    if (!_taskAnchor.isEmpty()
        && (text == _taskAnchor || text == (kAnchorTaskPrefix + _taskAnchor)))
        return true;
    if (!_followUpAnchor.isEmpty()
        && (text == _followUpAnchor || text == (kAnchorFollowUpPrefix + _followUpAnchor)))
        return true;
    // Якорь прошлого follow-up (задача сменилась) — обычная история:
    // уходит в сводку вместе со всем остальным.
    return false;
}

//----------------------------------------------

bool Agent::historyIsOnlySystem() const
{
    return _history.isEmpty()
           || (_history.size() == 1 && _history.first().role == LlmRoleSystem);
}

//----------------------------------------------

void Agent::setInspectIo(bool enabled)
{
    _inspectIo = enabled;
}

//----------------------------------------------

bool Agent::inspectIo() const
{
    return _inspectIo;
}

//----------------------------------------------

bool Agent::isBusy() const
{
    return _busy;
}

//----------------------------------------------

bool Agent::isInsideToolCall() const
{
    return _toolCallDepth > 0;
}

//----------------------------------------------

bool Agent::hasPendingContinue() const
{
    return _pendingContinueChat;
}

//----------------------------------------------

int Agent::estimatedPromptTokens() const
{
    if (_estimateDirty)
    {
        _estimateCache = _optimizer.estimatePrompt(_history, toolList());
        _estimateDirty = false;
    }
    return _estimateCache;
}

//----------------------------------------------

int Agent::contextLimit() const
{
    if (_contextCap > 0)
        return _contextCap;
    return _client ? _client->contextWindowTokens() : 0;
}

//----------------------------------------------

bool Agent::tracksContext() const
{
    return _tracksContext;
}

//----------------------------------------------

QList<LlmMessage> Agent::history() const
{
    return _history;
}

//----------------------------------------------

void Agent::setHistory(const QList<LlmMessage> &history)
{
    if (_busy)
        abort();

    clearCompactState();
    _history = history;
    if (_history.isEmpty())
        resetHistory();
    else
    {
        // Промпт на диске мог устареть: всегда подставляем актуальный.
        if (_history.first().role == LlmRoleSystem && !_systemPrompt.trimmed().isEmpty())
            _history[0].content = effectiveSystemPrompt();
        _optimizer.repairToolTurns(&_history);
    }
    emitContextUsage();
}

//----------------------------------------------

int Agent::lastUserIngress() const
{
    return _lastUserIngress;
}

//----------------------------------------------

QString Agent::turnAuthor() const
{
    return _turnAuthor;
}

//----------------------------------------------

void Agent::setTurnAuthor(const QString &name)
{
    _turnAuthor = name.trimmed();
}

//----------------------------------------------

QString Agent::journalReplyTo() const
{
    return _journalReplyTo;
}

//----------------------------------------------

void Agent::setJournalReplyTo(const QString &name)
{
    _journalReplyTo = name.trimmed();
}

//----------------------------------------------

void Agent::sendUserMessage(QString text, int ingress)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;
    if (_busy)
    {
        emit failed(tr("Агент занят предыдущим запросом."));
        return;
    }
    if (!_client)
    {
        emit failed(tr("Клиент языковой модели не задан."));
        return;
    }

    if (_history.isEmpty())
        resetHistory();

    beginUserTurn(trimmed, ingress);
    setBusy(true);
    setPhase(PhaseChat);
    maybeOptimizeThenComplete();
}

//----------------------------------------------

void Agent::sendUserMultimodal(QString text, QJsonArray contentParts, int ingress)
{
    QString trimmed = text.trimmed();
    if (trimmed.isEmpty() && contentParts.isEmpty())
        return;
    if (trimmed.isEmpty())
        trimmed = QString::fromUtf8("(multimodal attachment)");
    if (_busy)
    {
        emit failed(tr("Агент занят предыдущим запросом."));
        return;
    }
    if (!_client)
    {
        emit failed(tr("Клиент языковой модели не задан."));
        return;
    }

    if (_history.isEmpty())
        resetHistory();

    beginUserTurn(trimmed, ingress, contentParts);
    setBusy(true);
    setPhase(PhaseChat);
    maybeOptimizeThenComplete();
}

//----------------------------------------------

void Agent::beginUserTurn(QString text, int ingress, QJsonArray contentParts)
{
    _lastUserIngress = ingress;
    LlmMessage user;
    user.role = LlmRoleUser;
    user.content = text;
    user.contentParts = contentParts;
    // Служебный пинг видит только модель. В чат и в сохранённую ленту он не попадает.
    user.hideInUi = (ingress == AgentUserIngressSystem);
    _history.append(user);
    if (!user.hideInUi)
        emit userMessage(text);

    _toolIterations = 0;
    _finalAnswerRound = false;
    _overflowRetried = false;
    _repairedToolHistory = false;
    _compactThenChat = true;
    _stopAfterTools = false;
    _heldForExternal = false;
    _waitingToolBudget = false;
    _pendingContinueChat = false;
}

//----------------------------------------------

void Agent::sendUserMessageAfterCompact(QString text, int ingress)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;
    if (_busy)
    {
        emit failed(tr("Агент занят предыдущим запросом."));
        return;
    }
    if (!_client)
    {
        emit failed(tr("Клиент языковой модели не задан."));
        return;
    }
    if (_history.isEmpty())
        resetHistory();

    // У медиа-воркера нет окна контекста — сводку моделью не запрашиваем.
    if (!_tracksContext)
    {
        sendUserMessage(trimmed, ingress);
        return;
    }

    // Сжимать нечего — обычная отправка.
    int movable = 0;
    for (int i = (_history.first().role == LlmRoleSystem) ? 1 : 0; i < _history.size(); ++i)
    {
        if (!isAnchorMessage(_history.at(i)))
            ++movable;
    }
    if (movable == 0)
    {
        sendUserMessage(trimmed, ingress);
        return;
    }

    _pendingUserMessage = trimmed;
    _pendingUserIngress = ingress;
    _compactThenChat = true;
    _overflowRetried = false;
    _stopAfterTools = false;
    _heldForExternal = false;
    _pendingContinueChat = false;
    setBusy(true);
    emit notice(tr("Новая задача от родителя: сворачиваю предыдущую работу в сводку…"));
    runCompact(CompactFull);
}

//----------------------------------------------

void Agent::compactContext()
{
    if (!_tracksContext)
        return;
    if (_busy)
    {
        emit failed(tr("Агент занят предыдущим запросом."));
        return;
    }
    if (!_client)
    {
        emit failed(tr("Клиент языковой модели не задан."));
        return;
    }
    _compactThenChat = false;
    _overflowRetried = false;
    setBusy(true);
    runCompact(CompactRegular);
}

//----------------------------------------------

void Agent::abort()
{
    _heldForExternal = false;
    _waitingToolBudget = false;
    _stopAfterTools = true;
    _pendingContinueChat = false;
    _compactThenChat = false;
    _finalAnswerRound = false;
    foreach (AgentTool *tool, _tools)
    {
        if (tool)
            tool->interrupt();
    }
    _executingTools = false;
    flushPendingNotes();
    endModelWork();
    if (_client)
        _client->abort();
    // Сжатие прервано: история осталась нетронутой, промежуточная сводка
    // и отложенное сообщение не нужны.
    clearCompactState();
    setPhase(PhaseIdle);
    setBusy(false);
}

//----------------------------------------------

void Agent::clearCompactState()
{
    _pendingRecent.clear();
    _summaryChunks.clear();
    _runningSummary.clear();
    _pendingUserMessage.clear();
    _summaryChunkTotal = 0;
}

//----------------------------------------------

void Agent::clearHistory()
{
    if (_busy)
        abort();
    _optimizer.reset();
    _toolIterations = 0;
    _followUpAnchor.clear();
    resetHistory();
    emitContextUsage();
}

//----------------------------------------------

void Agent::refreshContextUsage()
{
    publishContextUsage();
}

//----------------------------------------------

void Agent::sltResponseReceived(const LlmResponse &response)
{
    endModelWork();
    if (!_busy)
        return;

    QPointer<Agent> self(this);

    if (response.usage.promptTokens > 0)
    {
        _optimizer.calibrate(response.usage.promptTokens, _lastEstimated);
        _estimateDirty = true;
    }

    emit usageUpdated(response.usage.promptTokens,
                      response.usage.completionTokens,
                      response.usage.totalTokens,
                      response.usage.cachedPromptTokens);

    if (_inspectIo)
        gateInspect(tr("Ответ модели"), responseAsText(response));
    if (!self)
        return;

    if (_phase == PhaseCompact)
    {
        const QString summary = response.message.content.trimmed();
        if (!summary.isEmpty())
            _runningSummary = summary;
        if (!_summaryChunks.isEmpty())
        {
            requestNextSummaryChunk();
            return;
        }
        if (_runningSummary.isEmpty())
        {
            finishCompactFallback(tr("модель вернула пустую сводку"));
            return;
        }
        applyCompactResult(_runningSummary);
        return;
    }

    LlmMessage stored = response.message;
    const QStringList droppedIds = eligibleDroppedFileIds(
        _history, parseDroppedFileIds(stored.content));
    stored.content = stripRetentionDirectives(stored.content);
    if (!droppedIds.isEmpty())
    {
        dropFileContexts(&_history, droppedIds);
        emit serviceNotice(tr("Убрал из контекста файлы: %1")
                           .arg(droppedIds.join(QString::fromUtf8(", "))));
    }
    _history.append(stored);
    emitContextUsage();

    if (!stored.content.trimmed().isEmpty())
        emit assistantMessage(stored.content);

    const bool wantsTools = (response.finishReason == QString::fromUtf8("tool_calls"))
                            || !stored.toolCalls.isEmpty();
    if (wantsTools)
    {
        if (!_history.isEmpty())
            ensureToolCallIds(&_history.last());
        handleToolCalls(_history.last());
        return;
    }

    setPhase(PhaseIdle);
    setBusy(false);
}

//----------------------------------------------

void Agent::sltClientFailed(const QString &error)
{
    endModelWork();
    // Поздний callback после Stop (или отмена, когда агент уже Idle).
    if (!_busy && _phase == PhaseIdle)
        return;

    if (_phase == PhaseCompact)
    {
        finishCompactFallback(error);
        return;
    }

    if (_phase == PhaseChat && !_repairedToolHistory
        && isBrokenToolHistoryError(error))
    {
        _repairedToolHistory = true;
        _optimizer.repairToolTurns(&_history);
        emit notice(tr("Исправляю историю вызовов функций и повторяю запрос."));
        setPhase(PhaseChat);
        setBusy(true);
        requestCompletion();
        return;
    }

    if (_tracksContext && _phase == PhaseChat && !_overflowRetried
        && ContextOptimizer::isContextOverflowError(error))
    {
        _overflowRetried = true;
        emit notice(tr("Окно контекста заполнено. Сжимаю историю и повторяю запрос, "
                       "не теряя суть беседы."));
        runCompact(CompactEmergency);
        return;
    }

    emit failed(error);
    setPhase(PhaseIdle);
    setBusy(false);
}

//----------------------------------------------

void Agent::resetHistory()
{
    _history.clear();
    clearCompactState();
    _pendingNotes.clear();
    _executingTools = false;
    _pendingContinueChat = false;
    if (_systemPrompt.trimmed().isEmpty())
        return;

    LlmMessage system;
    system.role = LlmRoleSystem;
    system.content = effectiveSystemPrompt();
    _history.append(system);
    emitContextUsage();
}

//----------------------------------------------

void Agent::setBusy(bool busy)
{
    if (_busy == busy)
        return;
    _busy = busy;
    emit busyChanged(_busy);
    if (!_busy && _pendingContinueChat)
        QMetaObject::invokeMethod(this, "continueChat", Qt::QueuedConnection);
}

//----------------------------------------------

void Agent::setPhase(Phase phase)
{
    const bool wasCompact = (_phase == PhaseCompact);
    _phase = phase;
    const bool nowCompact = (_phase == PhaseCompact);
    if (wasCompact != nowCompact)
        emit compactingChanged(nowCompact);
}

//----------------------------------------------

void Agent::maybeOptimizeThenComplete()
{
    if (!_tracksContext)
    {
        requestCompletion();
        return;
    }
    emitContextUsage();
    const int used = estimatedPromptTokens();
    const int limit = contextLimit();
    if (limit > 0)
    {
        if (used * 1000 >= limit * kEmergencyRatioPermille)
        {
            runCompact(CompactEmergency);
            return;
        }
        if (used * 1000 >= limit * kCompactRatioPermille)
        {
            runCompact(CompactRegular);
            return;
        }
    }
    requestCompletion();
}

//----------------------------------------------

void Agent::runCompact(CompactMode mode)
{
    if (!_tracksContext)
    {
        continueAfterCompact();
        return;
    }
    _optimizer.repairToolTurns(&_history);
    emitContextUsage();

    if (_history.isEmpty())
    {
        continueAfterCompact();
        return;
    }

    // Условное окно, когда модель его не сообщила: сжимать всё равно надо.
    int window = contextLimit();
    if (window <= 0)
        window = kFallbackWindowTokens;

    const int oldBegin = (_history.first().role == LlmRoleSystem) ? 1 : 0;

    // Свежий хвост, который остаётся дословно. При смене задачи — ничего:
    // вся прежняя работа сворачивается в сводку.
    int tailBudget = 0;
    if (mode == CompactRegular)
        tailBudget = window * kTailKeepPermille / 1000;
    else if (mode == CompactEmergency)
        tailBudget = window * kEmergencyTailPermille / 1000;

    int keepFrom = _optimizer.tailKeepIndex(_history, oldBegin, tailBudget);
    keepFrom = _optimizer.expandKeepIndexForTools(_history, keepFrom);
    if (keepFrom < oldBegin)
        keepFrom = oldBegin;

    // Хвост съел весь бюджет одним раундом (огромный результат функции):
    // оставляем только последний раунд, остальное — в сводку.
    if (keepFrom <= oldBegin && mode != CompactFull && _history.size() - oldBegin > 2)
    {
        keepFrom = _optimizer.expandKeepIndexForTools(_history, _history.size() - 1);
        if (keepFrom < oldBegin)
            keepFrom = oldBegin;
    }

    QList<LlmMessage> oldPart;
    QList<LlmMessage> recent;
    for (int i = oldBegin; i < _history.size(); ++i)
    {
        const LlmMessage &message = _history.at(i);
        // Закреплённая формулировка задачи в сводку не идёт: она вернётся
        // в историю дословно.
        if (isAnchorMessage(message))
            continue;
        if (i < keepFrom)
            oldPart.append(message);
        else
            recent.append(message);
    }

    if (oldPart.isEmpty())
    {
        if (!_compactThenChat)
            emit notice(tr("Сжимать нечего: история укладывается в окно контекста."));
        continueAfterCompact();
        return;
    }

    _pendingRecent = recent;
    _runningSummary.clear();
    _summaryChunks = _optimizer.splitForSummary(oldPart, window * kSummaryChunkPermille / 1000);
    _summaryChunkTotal = _summaryChunks.size();
    if (_summaryChunks.isEmpty())
    {
        continueAfterCompact();
        return;
    }

    setPhase(PhaseCompact);
    if (_summaryChunkTotal > 1)
    {
        emit notice(tr("Сжимаю предыдущую часть беседы силами модели (%1 порций)…")
                    .arg(_summaryChunkTotal));
    }
    else
        emit notice(tr("Сжимаю предыдущую часть беседы силами модели…"));
    requestNextSummaryChunk();
}

//----------------------------------------------

void Agent::requestNextSummaryChunk()
{
    if (!_client)
    {
        finishCompactFallback(tr("клиент модели не задан"));
        return;
    }
    if (_summaryChunks.isEmpty())
    {
        applyCompactResult(_runningSummary);
        return;
    }
    const QString chunk = _summaryChunks.takeFirst();
    const int index = _summaryChunkTotal - _summaryChunks.size();

    LlmRequest request;
    request.messages = _optimizer.buildSummarizeMessages(_runningSummary, chunk, index,
                                                         _summaryChunkTotal,
                                                         !_taskAnchor.isEmpty());
    request.maxTokens = kSummaryMaxTokens;
    _lastEstimated = _optimizer.estimatePrompt(request.messages, QList<LlmTool>());
    // Не вызывать requestAsText(), пока Inspect выключен: аргумент считается
    // до входа в gateInspect и на multimodal (base64) раздувает память до OOM.
    if (_inspectIo)
        gateInspect(tr("Запрос сводки"), requestAsText(request));
    if (!_client)
        return;
    beginModelWork();
    _client->send(request);
}

//----------------------------------------------

void Agent::finishCompactFallback(QString reason)
{
    // Модель не смогла: чтобы воркер не встал, оставшиеся порции уходят
    // в историю текстовой выжимкой. Это исключение, а не режим работы.
    QString summary = _runningSummary;
    if (!_summaryChunks.isEmpty())
    {
        QString rest = _summaryChunks.join(QString::fromUtf8("\n"));
        if (rest.size() > 6000)
            rest = rest.left(6000) + QString::fromUtf8("\n...");
        if (!summary.isEmpty())
            summary += QString::fromUtf8("\n\n");
        summary += QString::fromUtf8("Unsummarized tail of the history (model summary failed):\n");
        summary += rest;
    }
    emit notice(tr("Не удалось сжать историю через модель (%1); сохранена выжимка по тексту.")
                .arg(reason.trimmed()));
    applyCompactResult(summary);
}

//----------------------------------------------

void Agent::applyCompactResult(QString summary)
{
    LlmMessage systemPrompt;
    systemPrompt.role = LlmRoleSystem;
    systemPrompt.content = effectiveSystemPrompt();
    _history = _optimizer.rebuildHistory(systemPrompt, anchorMessages(), summary, _pendingRecent);
    _pendingRecent.clear();
    _summaryChunks.clear();
    _runningSummary.clear();
    _summaryChunkTotal = 0;

    emit notice(tr("Контекст сжат: в модель уходят формулировка задачи, сводка "
                   "предыдущей работы и последние сообщения."));
    emitContextUsage();
    continueAfterCompact();
}

//----------------------------------------------

void Agent::continueAfterCompact()
{
    if (!_pendingUserMessage.isEmpty())
    {
        const QString text = _pendingUserMessage;
        const int ingress = _pendingUserIngress;
        _pendingUserMessage.clear();
        _pendingUserIngress = AgentUserIngressHuman;
        // Новая задача от родителя закрепляется: следующее сжатие оставит
        // её дословно, а прежний follow-up уйдёт в сводку.
        _followUpAnchor = text;
        beginUserTurn(text, ingress);
    }
    if (_compactThenChat)
    {
        setPhase(PhaseChat);
        requestCompletion();
    }
    else
    {
        setPhase(PhaseIdle);
        setBusy(false);
        emitContextUsage();
    }
}

//----------------------------------------------

void Agent::beginModelWork()
{
    if (_modelWorkId != 0)
        return;
    _modelWorkId = AppSettings::instance()->beginWork();
}

//----------------------------------------------

void Agent::endModelWork()
{
    if (_modelWorkId == 0)
        return;
    const int id = _modelWorkId;
    _modelWorkId = 0;
    AppSettings::instance()->endWork(id);
}

//----------------------------------------------

void Agent::requestCompletion()
{
    if (!_client)
    {
        emit failed(tr("Клиент языковой модели не задан."));
        setPhase(PhaseIdle);
        setBusy(false);
        return;
    }

    LlmRequest request;
    request.reasoning = AppSettings::instance()->reasoningEnabled();
    _optimizer.repairToolTurns(&_history);
    request.messages = _history;
    QList<LiveFileContext> cachedFiles;
    if (AppSettings::instance()->dropLargeContextFiles())
    {
        const int minBytes = AppSettings::instance()->largeContextFileBytes();
        foreach (const LiveFileContext &file, liveFileContexts(_history))
        {
            if (file.bytes >= minBytes)
                cachedFiles.append(file);
        }
    }
    if (!cachedFiles.isEmpty())
    {
        const QString askText = fileRetentionRequest(cachedFiles);
        const bool canFold = !request.messages.isEmpty()
                             && request.messages.last().role == LlmRoleUser
                             && request.messages.last().toolCalls.isEmpty()
                             && request.messages.last().contentParts.isEmpty();
        if (canFold)
        {
            request.messages.last().content += QString::fromUtf8("\n\n") + askText;
        }
        else
        {
            LlmMessage ask;
            ask.role = LlmRoleUser;
            ask.content = askText;
            ask.hideInUi = true;
            request.messages.append(ask);
        }
    }
    request.tools = toolsForRequest();
    if (_tracksContext)
    {
        _lastEstimated = _optimizer.estimatePrompt(request.messages, request.tools);
        emitContextUsage();
    }
    // См. requestNextSummaryChunk: requestAsText только при Inspect.
    if (_inspectIo)
        gateInspect(tr("Запрос"), requestAsText(request));
    if (!_client)
        return;
    beginModelWork();
    _client->send(request);
}

//----------------------------------------------

void Agent::emitContextUsage()
{
    _estimateDirty = true;
    emit historyChanged();
    publishContextUsage();
}

//----------------------------------------------

void Agent::publishContextUsage()
{
    if (!_tracksContext)
        return;
    emit contextUsageChanged(estimatedPromptTokens(), contextLimit());
}

//----------------------------------------------

QList<LlmTool> Agent::toolList() const
{
    QList<LlmTool> tools;
    foreach (AgentTool *tool, _tools)
        tools.append(toolDescription(tool));
    return tools;
}

//----------------------------------------------

QList<LlmTool> Agent::toolsForRequest() const
{
    const QList<LlmTool> all = toolList();
    if (!_finalAnswerRound)
        return all;
    QList<LlmTool> filtered;
    foreach (const LlmTool &tool, all)
    {
        if (tool.name == QString::fromUtf8("complete_task"))
            filtered.append(tool);
    }
    return filtered;
}

//----------------------------------------------

LlmTool Agent::toolDescription(AgentTool *tool) const
{
    LlmTool description;
    if (!tool)
        return description;
    description.name = tool->name();
    description.description = tool->description();
    description.parameters = tool->parametersSchema();
    return description;
}

//----------------------------------------------

AgentTool *Agent::findTool(QString name) const
{
    foreach (AgentTool *tool, _tools)
    {
        if (tool && tool->name() == name)
            return tool;
    }
    return 0;
}

//----------------------------------------------

void Agent::handleToolCalls(LlmMessage message)
{
    if (message.toolCalls.isEmpty())
    {
        setPhase(PhaseIdle);
        setBusy(false);
        emit assistantMessage(message.content);
        return;
    }

    ++_toolIterations;
    const bool lastAllowedBatch = (_toolIterations >= _maxToolIterations);

    QPointer<Agent> self(this);
    _executingTools = true;
    foreach (const LlmToolCall &call, message.toolCalls)
    {
        emit toolCallStarted(call.name, call.arguments);

        bool ok = false;
        QString result;
        AgentTool *tool = findTool(call.name);
        if (_stopAfterTools)
        {
            ok = false;
            result = QString::fromUtf8("STOPPED: the user stopped all workers.");
        }
        else if (_finalAnswerRound && call.name != QString::fromUtf8("complete_task"))
        {
            ok = false;
            if (findTool(QString::fromUtf8("complete_task")))
            {
                result = QString::fromUtf8(
                    "TOOL BUDGET EXHAUSTED (%1 calls). Only complete_task is allowed. "
                    "Call it now with the report for your parent.")
                             .arg(_maxToolIterations);
            }
            else
            {
                result = QString::fromUtf8(
                    "TOOL BUDGET EXHAUSTED (%1 calls). You are the top executor and "
                    "have no complete_task. Stop calling functions and answer the user.")
                             .arg(_maxToolIterations);
            }
        }
        else if (!tool)
        {
            ok = false;
            result = QString::fromUtf8("Unknown function: %1").arg(call.name);
        }
        else
        {
            ++_toolCallDepth;
            const bool nested = (call.name == QString::fromUtf8("delegate_task")
                                 || call.name == QString::fromUtf8("run_tool"));
            const int span = nested ? 0 : AppSettings::instance()->beginWork();
            result = tool->invoke(call.arguments, &ok);
            if (span)
                AppSettings::instance()->endWork(span);
            if (!self)
                return;
            --_toolCallDepth;
        }
        if (!self)
            return;

        emit toolCallFinished(call.name, result, ok);

        LlmMessage toolMessage;
        toolMessage.role = LlmRoleTool;
        toolMessage.toolCallId = call.id;
        toolMessage.name = call.name;
        toolMessage.content = result;
        _history.append(toolMessage);
    }
    if (!self)
        return;
    _executingTools = false;
    flushPendingNotes();

    _heldForExternal = false;
    emit toolBatchFinished();
    if (_heldForExternal)
    {
        setPhase(PhaseIdle);
        setBusy(false);
        emitContextUsage();
        return;
    }

    if (_stopAfterTools)
    {
        _stopAfterTools = false;
        setPhase(PhaseIdle);
        setBusy(false);
        emitContextUsage();
        return;
    }

    emitContextUsage();
    if (lastAllowedBatch)
    {
        const int chunk = toolBudgetChunk();
        _waitingToolBudget = true;
        setPhase(PhaseIdle);
        setBusy(false);
        emit notice(tr("Лимит вызовов функций исчерпан (%1). Ожидаю решение.")
                    .arg(_maxToolIterations));
        emit toolBudgetExhausted(chunk);
        return;
    }
    // Между раундами функций контекст растёт быстрее всего: те же пороги,
    // что и перед новым сообщением пользователя.
    maybeOptimizeThenComplete();
}

//----------------------------------------------

void Agent::gateInspect(QString title, QString text)
{
    if (!_inspectIo)
        return;
    emit inspectRequested(title, text);
}

//----------------------------------------------

static QString formatMessageText(const LlmMessage &message)
{
    QString text;
    text += QChar('[');
    text += llmRoleToString(message.role);
    text += QChar(']');
    if (!message.name.isEmpty())
    {
        text += QChar(' ');
        text += message.name;
    }
    text += QChar('\n');
    if (!message.content.isEmpty())
    {
        text += message.content;
        text += QChar('\n');
    }
    if (!message.reasoningContent.isEmpty())
    {
        text += QString::fromUtf8("reasoning:\n");
        text += message.reasoningContent;
        text += QChar('\n');
    }
    foreach (const LlmToolCall &call, message.toolCalls)
    {
        text += QString::fromUtf8("tool_call %1 %2(%3)\n")
                    .arg(call.id, call.name, call.arguments);
    }
    if (!message.toolCallId.isEmpty())
        text += QString::fromUtf8("tool_call_id: %1\n").arg(message.toolCallId);
    return text;
}

//----------------------------------------------

QString Agent::requestAsText(const LlmRequest &request) const
{
    if (_client)
    {
        const QString wire = _client->encodeRequestText(request);
        if (!wire.isEmpty())
            return wire;
    }

    QString text;
    QString model = request.model;
    if (model.isEmpty() && _client)
        model = _client->model();
    if (!model.isEmpty())
        text += QString::fromUtf8("model: %1\n\n").arg(model);
    foreach (const LlmMessage &message, request.messages)
    {
        text += formatMessageText(message);
        text += QChar('\n');
    }
    if (!request.tools.isEmpty())
    {
        text += QString::fromUtf8("tools:\n");
        foreach (const LlmTool &tool, request.tools)
        {
            text += QString::fromUtf8("- %1: %2\n")
                    .arg(tool.name, tool.description);
            text += QString::fromUtf8(QJsonDocument(tool.parameters).toJson(QJsonDocument::Indented));
            text += QChar('\n');
        }
    }
    return text.trimmed();
}

//----------------------------------------------

QString Agent::responseAsText(const LlmResponse &response) const
{
    QString text;
    if (!response.finishReason.isEmpty())
    {
        text += QString::fromUtf8("finish_reason: %1\n\n")
                    .arg(response.finishReason);
    }
    text += formatMessageText(response.message);
    return text.trimmed();
}
