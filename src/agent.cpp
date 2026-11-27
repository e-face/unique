#include "agent.h"
//----------------------------------------------
#include "agenttool.h"
#include "appsettings.h"
#include "llmclient.h"
//----------------------------------------------
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QRegExp>
#include <QSet>

//----------------------------------------------

static const int kDefaultMaxToolIterations = 1000;

//----------------------------------------------

static QString takeForUser(QString *argumentsJson)
{
    if (!argumentsJson)
        return QString();
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(argumentsJson->toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return QString();
    QJsonObject object = doc.object();
    if (!object.contains(QString::fromUtf8("for_user")))
        return QString();
    const QString note = object.value(QString::fromUtf8("for_user")).toString().trimmed();
    object.remove(QString::fromUtf8("for_user"));
    *argumentsJson = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
    return note;
}
/** Window fill at which compression runs before a request. */
static const int kCompactRatioPermille     = 950;
/** Fill at which compression is harder (a shorter fresh tail). */
static const int kEmergencyRatioPermille   = 980;
/** Share of the window kept verbatim as the fresh tail during normal compression. */
static const int kTailKeepPermille         = 200;
/** The same share during emergency compression. */
static const int kEmergencyTailPermille    = 80;
/** Share of the window given to one history chunk sent to the model for a summary. */
static const int kSummaryChunkPermille     = 450;
/** Assumed window size in tokens when the model window is unknown. */
static const int kFallbackWindowTokens     = 32000;
/** Cap on the length of a summary reply. */
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
    , _useReasoning(true)
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
    // The client is a sibling child of ChatSession, created before the agent, so
    // it is destroyed first when the session is torn down. disconnect() on a dead
    // pointer read freed memory (a crash in QObject::disconnect);
    // Qt itself drops connections when either side is destroyed.
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

bool Agent::hasTool(QString name) const
{
    return findTool(name) != 0;
}

//----------------------------------------------

QStringList Agent::toolNames() const
{
    QStringList names;
    foreach (AgentTool *tool, _tools)
    {
        if (!tool || tool->name().isEmpty())
            continue;
        names.append(tool->name());
    }
    names.sort();
    return names;
}

//----------------------------------------------

void Agent::retainTools(const QStringList &names)
{
    QSet<QString> keep;
    foreach (const QString &name, names)
    {
        const QString id = name.trimmed();
        if (!id.isEmpty())
            keep.insert(id);
    }
    for (int i = _tools.size() - 1; i >= 0; --i)
    {
        AgentTool *tool = _tools.at(i);
        if (!tool || keep.contains(tool->name()))
            continue;
        _tools.removeAt(i);
        delete tool;
    }
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
    QString arguments = argumentsJson;
    const QString forUser = takeForUser(&arguments);
    emit toolCallStarted(name, arguments, forUser);
    bool localOk = false;
    const bool nested = (name == QString::fromUtf8("delegate_task")
                         || name == QString::fromUtf8("run_tool"));
    const int span = nested ? 0 : AppSettings::instance()->beginWork();
    const QString result = tool->invoke(arguments, &localOk);
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

void Agent::retractLastAssistant()
{
    if (_history.isEmpty())
        return;
    LlmMessage &last = _history.last();
    if (last.role != LlmRoleAssistant || !last.toolCalls.isEmpty())
        return;
    const QString text = last.content;
    if (text.trimmed().isEmpty() || last.hideInUi)
        return;
    last.hideInUi = true;
    emit assistantRetracted(text);
    emit historyChanged();
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
    // An in-progress dialogue is left alone: changing system breaks the provider
    // cache prefix. The new value enters history only on reset.
    if (!historyIsOnlySystem())
        return;
    if (!_busy)
        resetHistory();
}

//----------------------------------------------

void Agent::rewriteSystemPrompt()
{
    if (_history.isEmpty() || _history.first().role != LlmRoleSystem)
        return;
    const QString next = effectiveSystemPrompt();
    if (_history.first().content == next)
        return;
    _history[0].content = next;
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
            "Throwaway files are not part of the task. Do not create one to probe, "
            "measure, or retry. If a log or a download is required, put it in "
            "___project___/tmp (create it if missing). Do not put throwaway files in "
            "a project-root tmp/ or among source files. The rest of ___project___ "
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
    // Only a worker with a language model compresses and counts context.
    // A media executor calls the generation API and has no context window.
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

    // Otherwise context compression on this turn would finish the agent without
    // asking the model, and the worker would send the parent intermediate text instead of continuing.
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
    // An anchor from a previous follow-up (the task changed) is ordinary history:
    // it goes into the summary with everything else.
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

void Agent::setUseReasoning(bool enabled)
{
    _useReasoning = enabled;
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

static bool isContinuationNudge(const QString &text)
{
    return text.startsWith(QString::fromUtf8("Application was restarted"))
           || text.startsWith(QString::fromUtf8("Previous model request failed"))
           || text.startsWith(QString::fromUtf8("TOOL BUDGET EXHAUSTED"))
           || text.startsWith(QString::fromUtf8("Continue the assigned task"))
           || text.startsWith(QString::fromUtf8("Continue your part"));
}

//----------------------------------------------

static void dropStaleFollowUpPins(QList<LlmMessage> *history)
{
    if (!history)
        return;
    for (int i = history->size() - 1; i >= 0; --i)
    {
        const LlmMessage &message = history->at(i);
        if (message.role != LlmRoleUser || message.hideInUi)
            continue;
        const QString text = message.content.trimmed();
        if (!text.startsWith(kAnchorFollowUpPrefix))
            continue;
        const QString body = text.mid(kAnchorFollowUpPrefix.size()).trimmed();
        bool replaced = false;
        for (int j = i + 1; j < history->size(); ++j)
        {
            const LlmMessage &later = history->at(j);
            if (later.role != LlmRoleUser || later.hideInUi)
                continue;
            const QString laterText = later.content.trimmed();
            if (laterText.isEmpty() || isContinuationNudge(laterText))
                continue;
            if (laterText == text || laterText == body)
                continue;
            replaced = true;
            break;
        }
        if (replaced)
            history->removeAt(i);
    }
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
        // The prompt on disk may be stale: always substitute the current one.
        if (_history.first().role == LlmRoleSystem && !_systemPrompt.trimmed().isEmpty())
            _history[0].content = effectiveSystemPrompt();
        _optimizer.repairToolTurns(&_history);
        dropStaleFollowUpPins(&_history);
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
    // A service ping is visible only to the model. It does not enter the chat or the saved feed.
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

    // A media worker has no context window, so the model is not asked for a summary.
    if (!_tracksContext)
    {
        sendUserMessage(trimmed, ingress);
        return;
    }

    // The new request is the only pinned assignment. The previous one must be
    // summarized (or dropped), not left beside it as a second current task.
    _followUpAnchor = trimmed;

    int movable = 0;
    for (int i = (_history.first().role == LlmRoleSystem) ? 1 : 0; i < _history.size(); ++i)
    {
        if (!isAnchorMessage(_history.at(i)))
            ++movable;
    }
    if (movable == 0)
    {
        if (!_history.isEmpty() && _history.first().role == LlmRoleSystem)
        {
            _history = _optimizer.rebuildHistory(_history.first(), anchorMessages(),
                                                 QString(), QList<LlmMessage>());
        }
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
    // Compression was interrupted: history stayed intact; the intermediate summary
    // and the deferred message are not needed.
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
        emit historyChanged();
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
    // A late callback after Stop (or a cancel when the agent is already Idle).
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

    // Assumed window when the model did not report one: compression is still required.
    int window = contextLimit();
    if (window <= 0)
        window = kFallbackWindowTokens;

    const int oldBegin = (_history.first().role == LlmRoleSystem) ? 1 : 0;

    // The fresh tail kept verbatim. On a task change, nothing is kept:
    // all previous work is folded into the summary.
    int tailBudget = 0;
    if (mode == CompactRegular)
        tailBudget = window * kTailKeepPermille / 1000;
    else if (mode == CompactEmergency)
        tailBudget = window * kEmergencyTailPermille / 1000;

    int keepFrom = _optimizer.tailKeepIndex(_history, oldBegin, tailBudget);
    keepFrom = _optimizer.expandKeepIndexForTools(_history, keepFrom);
    if (keepFrom < oldBegin)
        keepFrom = oldBegin;

    // The tail ate the whole budget in one round (a huge function result):
    // keep only the last round; the rest goes into the summary.
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
        // The pinned task wording does not go into the summary: it returns
        // to history verbatim.
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
                                                         !_taskAnchor.isEmpty(),
                                                         !_pendingUserMessage.isEmpty());
    request.maxTokens = kSummaryMaxTokens;
    _lastEstimated = _optimizer.estimatePrompt(request.messages, QList<LlmTool>());
    // Do not call requestAsText() while Inspect is off: the argument is built
    // before the gateInspect check, and multimodal base64 blows memory up to OOM.
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
    // The model failed: so the worker does not stall, the remaining chunks go
    // into history as a text extract. This is an exception, not the normal mode.
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
        // A new task from the parent is pinned: the next compression keeps
        // it verbatim, and the previous follow-up goes into the summary.
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

static bool chatImageUrlIsSendable(const QString &url)
{
    const QString head = url.left(96).trimmed().toLower();
    if (head.startsWith(QString::fromUtf8("https://"))
        || head.startsWith(QString::fromUtf8("http://")))
    {
        return url.size() <= 8192
               && !url.contains(QChar(' '))
               && !url.contains(QChar('\n'));
    }
    if (!head.startsWith(QString::fromUtf8("data:image/")))
        return false;
    const int semi = head.indexOf(QChar(';'));
    const int comma = url.indexOf(QChar(','));
    if (semi < 0 || comma < 0 || comma > 128)
        return false;
    if (!url.left(comma).toLower().contains(QString::fromUtf8("base64")))
        return false;
    const QString mime = head.mid(5, semi - 5);
    return mime == QString::fromUtf8("image/jpeg")
           || mime == QString::fromUtf8("image/png")
           || mime == QString::fromUtf8("image/gif")
           || mime == QString::fromUtf8("image/webp");
}

//----------------------------------------------

static QStringList attachedImagePaths(const QString &content)
{
    const QString marker = QString::fromUtf8("Attached images (embedded for the model):");
    const int at = content.indexOf(marker);
    if (at < 0)
        return QStringList();
    QStringList paths;
    const QStringList lines = content.mid(at + marker.size()).split(QChar('\n'));
    foreach (QString line, lines)
    {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        if (!line.startsWith(QChar('-')))
            break;
        line = line.mid(1).trimmed();
        if (!line.isEmpty())
            paths.append(line);
    }
    return paths;
}

//----------------------------------------------

static QString absoluteImageInProject(const QString &projectDir, const QString &relative)
{
    if (projectDir.isEmpty() || relative.trimmed().isEmpty())
        return QString();
    QString rel = relative.trimmed();
    rel.replace(QChar('\\'), QChar('/'));
    if (rel.contains(QString::fromUtf8("..")) || rel.contains(QChar(0)))
        return QString();
    const QString rootClean = QDir::cleanPath(QDir(projectDir).absolutePath());
    const QString abs = QDir::cleanPath(QDir(projectDir).absoluteFilePath(rel));
    if (!abs.startsWith(rootClean, Qt::CaseInsensitive))
        return QString();
    if (abs.size() != rootClean.size())
    {
        const QChar sep = abs.at(rootClean.size());
        if (sep != QChar('/') && sep != QChar('\\'))
            return QString();
    }
    if (!QFileInfo(abs).isFile())
        return QString();
    return abs;
}

//----------------------------------------------

static QString sniffChatImageMime(const QByteArray &bytes)
{
    if (bytes.size() >= 3
        && quint8(bytes.at(0)) == 0xFF
        && quint8(bytes.at(1)) == 0xD8
        && quint8(bytes.at(2)) == 0xFF)
        return QString::fromUtf8("image/jpeg");
    if (bytes.size() >= 8 && bytes.startsWith(QByteArray("\x89PNG\r\n\x1a\n", 8)))
        return QString::fromUtf8("image/png");
    if (bytes.size() >= 6
        && (bytes.startsWith("GIF87a") || bytes.startsWith("GIF89a")))
        return QString::fromUtf8("image/gif");
    if (bytes.size() >= 12
        && bytes.startsWith("RIFF")
        && bytes.mid(8, 4) == QByteArray("WEBP"))
        return QString::fromUtf8("image/webp");
    return QString();
}

//----------------------------------------------

static QString encodeChatImageFile(const QString &absolutePath)
{
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QByteArray bytes = file.readAll();
    file.close();
    if (bytes.isEmpty())
        return QString();

    QString mime = QString::fromUtf8("image/jpeg");
    QImage image;
    if (!image.loadFromData(bytes))
    {
        mime = sniffChatImageMime(bytes);
        if (mime.isEmpty() || bytes.size() > 6 * 1024 * 1024)
            return QString();
        return QString::fromUtf8("data:%1;base64,%2")
            .arg(mime, QString::fromLatin1(bytes.toBase64()));
    }
    const int kMaxSide = 2048;
    if (image.width() > kMaxSide || image.height() > kMaxSide)
        image = image.scaled(kMaxSide, kMaxSide, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    if (image.isNull())
        return QString();
    QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    QByteArray encoded;
    QBuffer buffer(&encoded);
    buffer.open(QIODevice::WriteOnly);
    if (!rgb.save(&buffer, "JPEG", 85))
    {
        buffer.close();
        encoded.clear();
        buffer.open(QIODevice::WriteOnly);
        if (image.convertToFormat(QImage::Format_ARGB32).save(&buffer, "PNG"))
            mime = QString::fromUtf8("image/png");
    }
    buffer.close();
    if (encoded.isEmpty())
    {
        mime = sniffChatImageMime(bytes);
        if (mime.isEmpty() || bytes.size() > 6 * 1024 * 1024)
            return QString();
        return QString::fromUtf8("data:%1;base64,%2")
            .arg(mime, QString::fromLatin1(bytes.toBase64()));
    }
    return QString::fromUtf8("data:%1;base64,%2")
        .arg(mime, QString::fromLatin1(encoded.toBase64()));
}

//----------------------------------------------

static QJsonObject chatTextPart(const QString &text)
{
    QJsonObject part;
    part.insert(QString::fromUtf8("type"), QString::fromUtf8("text"));
    part.insert(QString::fromUtf8("text"), text);
    return part;
}

//----------------------------------------------

static QJsonObject chatImagePart(const QString &url)
{
    QJsonObject imageUrl;
    imageUrl.insert(QString::fromUtf8("url"), url);
    QJsonObject part;
    part.insert(QString::fromUtf8("type"), QString::fromUtf8("image_url"));
    part.insert(QString::fromUtf8("image_url"), imageUrl);
    return part;
}

//----------------------------------------------

static void repairMessageImages(LlmMessage *message, const QString &projectDir)
{
    if (!message || message->contentParts.isEmpty())
        return;
    const QStringList recovered = attachedImagePaths(message->content);
    int recoveredUsed = 0;
    QJsonArray out;
    for (int i = 0; i < message->contentParts.size(); ++i)
    {
        const QJsonObject part = message->contentParts.at(i).toObject();
        if (part.value(QString::fromUtf8("type")).toString()
            != QString::fromUtf8("image_url"))
        {
            out.append(part);
            continue;
        }
        const QJsonObject image = part.value(QString::fromUtf8("image_url")).toObject();
        QString url = image.value(QString::fromUtf8("url")).toString();
        QString source = image.value(QString::fromUtf8("source_path")).toString().trimmed();
        if (chatImageUrlIsSendable(url))
        {
            out.append(chatImagePart(url));
            continue;
        }
        if (source.isEmpty() && recoveredUsed < recovered.size())
            source = recovered.at(recoveredUsed++);
        const QString abs = absoluteImageInProject(projectDir, source);
        if (!abs.isEmpty())
            url = encodeChatImageFile(abs);
        if (chatImageUrlIsSendable(url))
        {
            out.append(chatImagePart(url));
            continue;
        }
        QString note = QString::fromUtf8(
            "[Image omitted: the provider does not accept this image_url.]");
        if (!source.isEmpty())
        {
            note = QString::fromUtf8(
                       "[Image omitted (%1): the provider does not accept this image_url.]")
                       .arg(source);
        }
        out.append(chatTextPart(note));
    }
    message->contentParts = out;
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
    request.reasoning = _useReasoning && AppSettings::instance()->reasoningEnabled();
    _optimizer.repairToolTurns(&_history);
    request.messages = _history;
    for (int i = 0; i < request.messages.size(); ++i)
        repairMessageImages(&request.messages[i], _projectDirectory);
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
    // See requestNextSummaryChunk: requestAsText only when Inspect is on.
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
    QJsonObject parameters = tool->parametersSchema();
    QJsonObject properties = parameters.value(QString::fromUtf8("properties")).toObject();
    if (!properties.contains(QString::fromUtf8("for_user")))
    {
        QJsonObject note;
        note.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
        note.insert(QString::fromUtf8("description"),
                    QString::fromUtf8(
                        "One short sentence for the user, in the user's language, "
                        "saying what this call does for them. Send it on every call. "
                        "The chat shows it instead of the function name."));
        properties.insert(QString::fromUtf8("for_user"), note);
        parameters.insert(QString::fromUtf8("properties"), properties);
        if (!parameters.contains(QString::fromUtf8("type")))
            parameters.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    }
    description.parameters = parameters;
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
        QString arguments = call.arguments;
        const QString forUser = takeForUser(&arguments);
        emit toolCallStarted(call.name, arguments, forUser);

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
            result = tool->invoke(arguments, &ok);
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
        if (tool)
        {
            const QJsonArray parts = tool->takeResultParts();
            if (!parts.isEmpty())
                toolMessage.contentParts = parts;
        }
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
    // Between function rounds context grows fastest: the same thresholds
    // as before a new user message.
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
