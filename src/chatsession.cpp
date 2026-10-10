#include "chatsession.h"
//----------------------------------------------
#include "agent.h"
#include "openaicompatibleclient.h"
#include "worker.h"
#include "workertypes.h"
//----------------------------------------------
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QUuid>
#include <QtGlobal>

//----------------------------------------------

ChatSession::ChatSession(QObject *parent)
    : QObject(parent)
    , _id(makeId())
    , _client(new OpenAiCompatibleClient(QString(), QString(), this))
    , _agent(new Agent(this))
    , _widget(0)
    , _worker(0)
    , _restoreMaximized(false)
    , _restoreMinimized(false)
    , _hasRestoreGeometry(false)
    , _autoTitle(true)
    , _contentLoaded(true)
    , _contentDirty(true)
{
    connect(_agent, SIGNAL(historyChanged()), this, SLOT(sltMarkDirty()));
    connect(_agent, SIGNAL(notice(QString)), this, SLOT(sltMarkDirty()));
}

//----------------------------------------------

bool ChatSession::contentDirty() const
{
    return _contentDirty;
}

//----------------------------------------------

void ChatSession::markContentDirty()
{
    _contentDirty = true;
}

//----------------------------------------------

void ChatSession::clearContentDirty()
{
    _contentDirty = false;
}

//----------------------------------------------

void ChatSession::sltMarkDirty()
{
    _contentDirty = true;
}

//----------------------------------------------

ChatSession::~ChatSession()
{
}

//----------------------------------------------

QString ChatSession::makeId()
{
    QString id = QUuid::createUuid().toString();
    if (id.startsWith(QChar('{')))
        id.remove(0, 1);
    if (id.endsWith(QChar('}')))
        id.chop(1);
    return id;
}

//----------------------------------------------

QString ChatSession::id() const
{
    return _id;
}

//----------------------------------------------

void ChatSession::setId(QString id)
{
    const QString trimmed = id.trimmed();
    if (!trimmed.isEmpty())
        _id = trimmed;
}

//----------------------------------------------

QString ChatSession::title() const
{
    return _title;
}

//----------------------------------------------

void ChatSession::setTitle(QString title)
{
    _title = title;
}

//----------------------------------------------

bool ChatSession::autoTitle() const
{
    return _autoTitle;
}

//----------------------------------------------

void ChatSession::setAutoTitle(bool enabled)
{
    _autoTitle = enabled;
}

//----------------------------------------------

Agent *ChatSession::agent() const
{
    return _agent;
}

//----------------------------------------------

ConversationWidget *ChatSession::widget() const
{
    return _widget;
}

//----------------------------------------------

void ChatSession::setWidget(ConversationWidget *widget)
{
    _widget = widget;
}

//----------------------------------------------

LlmClient *ChatSession::client() const
{
    return _client;
}

//----------------------------------------------

bool ChatSession::hasRestoreGeometry() const
{
    return _hasRestoreGeometry;
}

//----------------------------------------------

QRect ChatSession::restoreGeometry() const
{
    return _restoreGeometry;
}

//----------------------------------------------

bool ChatSession::restoreMaximized() const
{
    return _restoreMaximized;
}

//----------------------------------------------

bool ChatSession::restoreMinimized() const
{
    return _restoreMinimized;
}

//----------------------------------------------

bool ChatSession::contentLoaded() const
{
    return _contentLoaded;
}

//----------------------------------------------

void ChatSession::ensureContentLoaded()
{
    if (_contentLoaded)
        return;
    ChatSnapshot shot;
    if (ChatStore::loadChat(_id, &shot))
        applyContent(shot);
    else
        _contentLoaded = true;
}

//----------------------------------------------

void ChatSession::applyContent(const ChatSnapshot &shot)
{
    if (_agent)
        _agent->setHistory(shot.history);
    if (_widget)
    {
        if (!shot.display.isEmpty())
            _widget->restoreDisplayLog(shot.display);
        else
            _widget->restoreFromHistory(shot.history);
    }
    _contentLoaded = true;
    _contentDirty = false;
    if (_agent)
        _agent->refreshContextUsage();
}

//----------------------------------------------

ChatSnapshot ChatSession::snapshot(bool sealing) const
{
    ChatSnapshot shot;
    shot.id = _id;
    shot.title = _title;
    shot.autoTitle = _autoTitle;
    if (_contentLoaded && _agent)
    {
        if (sealing && _agent->isBusy())
            _agent->sealIncompleteToolCalls(
                QString::fromUtf8("Interrupted: application closed."));
        shot.history = _agent->history();
    }
    if (_contentLoaded && _widget)
        shot.display = _widget->displayLog();
    if (_worker)
    {
        shot.workerType = workerTypeToString(_worker->type());
        shot.parentId = _worker->parentWorker() ? _worker->parentWorker()->id() : QString();
        shot.toolName = _worker->toolName();
        shot.toolPurpose = _worker->toolPurpose();
        shot.task = _worker->task();
        shot.workerTitle = _worker->title();
        shot.personName = _worker->personName();
        shot.groupId = _worker->groupId();
        shot.groupRole = groupRoleToString(_worker->groupRole());
        shot.llmProfileId = _worker->llmProfileId();
        shot.result = _worker->result();
        shot.lastError = _worker->lastError();
        shot.lastAssistant = _worker->lastAssistant();
        shot.permissionAction = _worker->permissionAction();
        shot.workerState = workerStateToString(_worker->state());
        shot.alwaysCommands = _worker->alwaysAllowedCommands();
        shot.changedFiles = _worker->changedFiles();
    }
    else
        shot.workerType = QString::fromUtf8("root");
    return shot;
}

//----------------------------------------------

void ChatSession::applySnapshot(const ChatSnapshot &shot)
{
    setId(shot.id);
    setTitle(shot.title);
    _autoTitle = shot.autoTitle;
    _restoreGeometry = shot.geometry;
    _restoreMaximized = shot.maximized;
    _restoreMinimized = shot.minimized;
    _hasRestoreGeometry = !shot.geometry.isNull() || shot.maximized || shot.minimized;

    const bool hasContent = !shot.history.isEmpty() || !shot.display.isEmpty();
    if (hasContent)
        applyContent(shot);
    else
        _contentLoaded = false;
}

//----------------------------------------------

Worker *ChatSession::worker() const
{
    return _worker;
}

//----------------------------------------------

void ChatSession::setWorker(Worker *worker)
{
    _worker = worker;
}

//----------------------------------------------

//----------------------------------------------

static QString g_chatProjectRoot;

//----------------------------------------------

QString ChatStore::storeDir()
{
    if (!g_chatProjectRoot.isEmpty())
        return g_chatProjectRoot + QString::fromUtf8("/___project___");
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

//----------------------------------------------

QString ChatStore::indexPath()
{
    return storeDir() + QString::fromUtf8("/chats.json");
}

//----------------------------------------------

QString ChatStore::uiStatePath()
{
    return storeDir() + QString::fromUtf8("/ui.json");
}

//----------------------------------------------

QString ChatStore::chatsDir()
{
    return storeDir() + QString::fromUtf8("/chats");
}

//----------------------------------------------

QString ChatStore::safeId(QString id)
{
    QString out;
    foreach (const QChar c, id)
    {
        if (c.isLetterOrNumber() || c == QChar('-') || c == QChar('_'))
            out += c;
        else
            out += QChar('_');
    }
    if (out.isEmpty())
        out = QString::fromUtf8("chat");
    return out;
}

//----------------------------------------------

QString ChatStore::chatFilePath(QString id)
{
    return chatsDir() + QChar('/') + safeId(id) + QString::fromUtf8(".json");
}

//----------------------------------------------

void ChatStore::setProjectRoot(QString root)
{
    g_chatProjectRoot = QDir::cleanPath(root.trimmed());
}

//----------------------------------------------

QString ChatStore::filePath()
{
    return indexPath();
}

//----------------------------------------------

static QJsonArray contentPartsForStorage(const QJsonArray &parts)
{
    // На диск не пишем base64 media — иначе save-поток и JSON чата раздуваются
    // до десятков МБ и на 32-bit часто валят процесс.
    QJsonArray out;
    for (int i = 0; i < parts.size(); ++i)
    {
        QJsonObject part = parts.at(i).toObject();
        const QString type = part.value(QString::fromUtf8("type")).toString();
        if (type == QString::fromUtf8("image_url"))
        {
            QJsonObject imageUrl = part.value(QString::fromUtf8("image_url")).toObject();
            const QString url = imageUrl.value(QString::fromUtf8("url")).toString();
            imageUrl.insert(QString::fromUtf8("url"),
                            QString::fromUtf8("[omitted image, %1 chars]").arg(url.size()));
            part.insert(QString::fromUtf8("image_url"), imageUrl);
        }
        else if (type == QString::fromUtf8("input_audio"))
        {
            QJsonObject audio = part.value(QString::fromUtf8("input_audio")).toObject();
            const QString data = audio.value(QString::fromUtf8("data")).toString();
            audio.insert(QString::fromUtf8("data"),
                         QString::fromUtf8("[omitted audio, %1 chars]").arg(data.size()));
            part.insert(QString::fromUtf8("input_audio"), audio);
        }
        else if (type == QString::fromUtf8("file"))
        {
            QJsonObject file = part.value(QString::fromUtf8("file")).toObject();
            if (file.contains(QString::fromUtf8("file_data")))
            {
                const QString data = file.value(QString::fromUtf8("file_data")).toString();
                file.insert(QString::fromUtf8("file_data"),
                            QString::fromUtf8("[omitted file, %1 chars]").arg(data.size()));
                part.insert(QString::fromUtf8("file"), file);
            }
        }
        else if (type == QString::fromUtf8("text"))
        {
            QString text = part.value(QString::fromUtf8("text")).toString();
            if (text.size() > 200000)
            {
                part.insert(QString::fromUtf8("text"),
                            text.left(200000) + QString::fromUtf8("\n…[truncated]…"));
            }
        }
        out.append(part);
    }
    return out;
}

//----------------------------------------------

QJsonObject ChatStore::messageToJson(const LlmMessage &message)
{
    QJsonObject object;
    object.insert(QString::fromUtf8("role"), llmRoleToString(message.role));
    object.insert(QString::fromUtf8("content"), message.content);
    if (!message.contentParts.isEmpty())
        object.insert(QString::fromUtf8("content_parts"),
                      contentPartsForStorage(message.contentParts));
    if (!message.name.isEmpty())
        object.insert(QString::fromUtf8("name"), message.name);
    if (!message.toolCallId.isEmpty())
        object.insert(QString::fromUtf8("tool_call_id"), message.toolCallId);
    if (!message.reasoningContent.isEmpty())
        object.insert(QString::fromUtf8("reasoning_content"), message.reasoningContent);
    if (message.hideInUi)
        object.insert(QString::fromUtf8("hide_in_ui"), true);

    if (!message.toolCalls.isEmpty())
    {
        QJsonArray calls;
        foreach (const LlmToolCall &call, message.toolCalls)
        {
            QJsonObject item;
            item.insert(QString::fromUtf8("id"), call.id);
            item.insert(QString::fromUtf8("name"), call.name);
            item.insert(QString::fromUtf8("arguments"), call.arguments);
            calls.append(item);
        }
        object.insert(QString::fromUtf8("tool_calls"), calls);
    }
    return object;
}

//----------------------------------------------

LlmMessage ChatStore::messageFromJson(const QJsonObject &object)
{
    LlmMessage message;
    message.role = llmRoleFromString(object.value(QString::fromUtf8("role")).toString());
    const QJsonValue contentVal = object.value(QString::fromUtf8("content"));
    if (contentVal.isArray())
    {
        message.contentParts = contentVal.toArray();
        // Текст для UI: склеить text-части.
        QStringList texts;
        for (int i = 0; i < message.contentParts.size(); ++i)
        {
            const QJsonObject part = message.contentParts.at(i).toObject();
            if (part.value(QString::fromUtf8("type")).toString()
                == QString::fromUtf8("text"))
                texts.append(part.value(QString::fromUtf8("text")).toString());
        }
        message.content = texts.join(QChar('\n'));
    }
    else
        message.content = contentVal.toString();
    if (object.contains(QString::fromUtf8("content_parts")))
        message.contentParts = object.value(QString::fromUtf8("content_parts")).toArray();
    message.name = object.value(QString::fromUtf8("name")).toString();
    message.toolCallId = object.value(QString::fromUtf8("tool_call_id")).toString();
    message.reasoningContent = object.value(QString::fromUtf8("reasoning_content")).toString();
    message.hideInUi = object.value(QString::fromUtf8("hide_in_ui")).toBool();

    const QJsonArray calls = object.value(QString::fromUtf8("tool_calls")).toArray();
    for (int i = 0; i < calls.size(); ++i)
    {
        const QJsonObject item = calls.at(i).toObject();
        LlmToolCall call;
        call.id = item.value(QString::fromUtf8("id")).toString();
        call.name = item.value(QString::fromUtf8("name")).toString();
        call.arguments = item.value(QString::fromUtf8("arguments")).toString();
        if (call.name.isEmpty())
        {
            const QJsonObject function = item.value(QString::fromUtf8("function")).toObject();
            call.name = function.value(QString::fromUtf8("name")).toString();
            if (call.arguments.isEmpty())
                call.arguments = function.value(QString::fromUtf8("arguments")).toString();
        }
        message.toolCalls.append(call);
    }
    return message;
}

//----------------------------------------------

QJsonObject ChatStore::displayToJson(const ChatDisplayItem &item)
{
    QJsonObject object;
    object.insert(QString::fromUtf8("kind"), item.kind);
    object.insert(QString::fromUtf8("title"), item.title);
    object.insert(QString::fromUtf8("text"), item.text);
    if (!item.authorKey.isEmpty())
        object.insert(QString::fromUtf8("authorKey"), item.authorKey);
    return object;
}

//----------------------------------------------

ChatDisplayItem ChatStore::displayFromJson(const QJsonObject &object)
{
    ChatDisplayItem item;
    item.kind = object.value(QString::fromUtf8("kind")).toString();
    item.title = object.value(QString::fromUtf8("title")).toString();
    item.text = object.value(QString::fromUtf8("text")).toString();
    item.authorKey = object.value(QString::fromUtf8("authorKey")).toString();
    if (item.authorKey.isEmpty())
        item.authorKey = item.title;
    return item;
}

//----------------------------------------------

QJsonObject ChatStore::snapshotMetaToJson(const ChatSnapshot &shot)
{
    QJsonObject object;
    object.insert(QString::fromUtf8("id"), shot.id);
    object.insert(QString::fromUtf8("title"), shot.title);
    object.insert(QString::fromUtf8("maximized"), shot.maximized);
    object.insert(QString::fromUtf8("minimized"), shot.minimized);
    object.insert(QString::fromUtf8("autoTitle"), shot.autoTitle);
    object.insert(QString::fromUtf8("workerType"), shot.workerType);
    object.insert(QString::fromUtf8("parentId"), shot.parentId);
    if (!shot.toolName.isEmpty())
        object.insert(QString::fromUtf8("toolName"), shot.toolName);
    if (!shot.toolPurpose.isEmpty())
        object.insert(QString::fromUtf8("toolPurpose"), shot.toolPurpose);
    object.insert(QString::fromUtf8("task"), shot.task);
    if (!shot.workerTitle.isEmpty())
        object.insert(QString::fromUtf8("workerTitle"), shot.workerTitle);
    if (!shot.personName.isEmpty())
        object.insert(QString::fromUtf8("personName"), shot.personName);
    if (!shot.groupId.isEmpty())
        object.insert(QString::fromUtf8("groupId"), shot.groupId);
    if (!shot.groupRole.isEmpty() && shot.groupRole != QString::fromUtf8("none"))
        object.insert(QString::fromUtf8("groupRole"), shot.groupRole);
    if (!shot.llmProfileId.isEmpty())
        object.insert(QString::fromUtf8("llmProfileId"), shot.llmProfileId);
    object.insert(QString::fromUtf8("result"), shot.result);
    object.insert(QString::fromUtf8("workerState"), shot.workerState);
    object.insert(QString::fromUtf8("lastError"), shot.lastError);
    object.insert(QString::fromUtf8("lastAssistant"), shot.lastAssistant);
    object.insert(QString::fromUtf8("permissionAction"), shot.permissionAction);
    QJsonArray always;
    foreach (const QString &item, shot.alwaysCommands)
        always.append(item);
    object.insert(QString::fromUtf8("alwaysCommands"), always);
    if (!shot.changedFiles.isEmpty())
    {
        QJsonArray changed;
        foreach (const QString &item, shot.changedFiles)
            changed.append(item);
        object.insert(QString::fromUtf8("changedFiles"), changed);
    }
    object.insert(QString::fromUtf8("x"), shot.geometry.x());
    object.insert(QString::fromUtf8("y"), shot.geometry.y());
    object.insert(QString::fromUtf8("w"), shot.geometry.width());
    object.insert(QString::fromUtf8("h"), shot.geometry.height());
    return object;
}

//----------------------------------------------

ChatSnapshot ChatStore::snapshotMetaFromJson(const QJsonObject &object)
{
    ChatSnapshot shot;
    shot.id = object.value(QString::fromUtf8("id")).toString();
    shot.title = object.value(QString::fromUtf8("title")).toString();
    shot.maximized = object.value(QString::fromUtf8("maximized")).toBool();
    shot.minimized = object.value(QString::fromUtf8("minimized")).toBool();
    shot.autoTitle = object.value(QString::fromUtf8("autoTitle")).toBool(true);
    shot.workerType = object.value(QString::fromUtf8("workerType")).toString();
    shot.parentId = object.value(QString::fromUtf8("parentId")).toString();
    shot.toolName = object.value(QString::fromUtf8("toolName")).toString();
    shot.toolPurpose = object.value(QString::fromUtf8("toolPurpose")).toString();
    shot.task = object.value(QString::fromUtf8("task")).toString();
    shot.workerTitle = object.value(QString::fromUtf8("workerTitle")).toString();
    shot.personName = object.value(QString::fromUtf8("personName")).toString();
    shot.groupId = object.value(QString::fromUtf8("groupId")).toString();
    shot.groupRole = object.value(QString::fromUtf8("groupRole")).toString();
    shot.llmProfileId = object.value(QString::fromUtf8("llmProfileId")).toString();
    shot.result = object.value(QString::fromUtf8("result")).toString();
    shot.workerState = object.value(QString::fromUtf8("workerState")).toString();
    shot.lastError = object.value(QString::fromUtf8("lastError")).toString();
    shot.lastAssistant = object.value(QString::fromUtf8("lastAssistant")).toString();
    shot.permissionAction = object.value(QString::fromUtf8("permissionAction")).toString();
    const QJsonArray always = object.value(QString::fromUtf8("alwaysCommands")).toArray();
    for (int a = 0; a < always.size(); ++a)
        shot.alwaysCommands.append(always.at(a).toString());
    const QJsonArray changed = object.value(QString::fromUtf8("changedFiles")).toArray();
    for (int c = 0; c < changed.size(); ++c)
        shot.changedFiles.append(changed.at(c).toString());
    shot.geometry = QRect(object.value(QString::fromUtf8("x")).toInt(),
                          object.value(QString::fromUtf8("y")).toInt(),
                          object.value(QString::fromUtf8("w")).toInt(),
                          object.value(QString::fromUtf8("h")).toInt());
    return shot;
}

//----------------------------------------------

void ChatStore::fillContentFromJson(ChatSnapshot *shot, const QJsonObject &object)
{
    if (!shot)
        return;
    const QJsonArray history = object.value(QString::fromUtf8("history")).toArray();
    for (int h = 0; h < history.size(); ++h)
        shot->history.append(messageFromJson(history.at(h).toObject()));
    const QJsonArray display = object.value(QString::fromUtf8("display")).toArray();
    for (int d = 0; d < display.size(); ++d)
        shot->display.append(displayFromJson(display.at(d).toObject()));
}

//----------------------------------------------

bool ChatStore::writeJsonFile(QString path, const QJsonObject &root)
{
    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir))
    {
        qWarning("dsagent: не удалось создать каталог сессий %s", qPrintable(dir));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        qWarning("dsagent: не удалось записать %s", qPrintable(path));
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit())
    {
        qWarning("dsagent: не удалось сохранить %s", qPrintable(path));
        return false;
    }
    return true;
}

//----------------------------------------------

bool ChatStore::readJsonFile(QString path, QJsonObject *root)
{
    if (!root)
        return false;
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;
    *root = document.object();
    return true;
}

//----------------------------------------------

bool ChatStore::saveChat(const ChatSnapshot &shot)
{
    if (g_chatProjectRoot.isEmpty() || shot.id.isEmpty())
        return false;
    QJsonObject object = snapshotMetaToJson(shot);
    QJsonArray history;
    foreach (const LlmMessage &message, shot.history)
        history.append(messageToJson(message));
    object.insert(QString::fromUtf8("history"), history);
    QJsonArray display;
    foreach (const ChatDisplayItem &item, shot.display)
        display.append(displayToJson(item));
    object.insert(QString::fromUtf8("display"), display);
    return writeJsonFile(chatFilePath(shot.id), object);
}

//----------------------------------------------

bool ChatStore::loadChat(QString id, ChatSnapshot *shot)
{
    if (!shot || id.isEmpty())
        return false;
    QJsonObject object;
    if (!readJsonFile(chatFilePath(id), &object))
        return false;
    *shot = snapshotMetaFromJson(object);
    if (shot->id.isEmpty())
        shot->id = id;
    fillContentFromJson(shot, object);
    return true;
}

//----------------------------------------------

void ChatStore::removeChat(QString id)
{
    if (id.isEmpty())
        return;
    QFile::remove(chatFilePath(id));
}

//----------------------------------------------

bool ChatStore::migrateLegacyMonolith()
{
    QString legacy = storeDir() + QString::fromUtf8("/openchats.json");
    if (!QFileInfo(legacy).exists())
    {
        legacy = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                 + QString::fromUtf8("/openchats.json");
    }
    QJsonObject root;
    if (!readJsonFile(legacy, &root))
        return false;

    QList<ChatSnapshot> chats;
    const QJsonArray list = root.value(QString::fromUtf8("chats")).toArray();
    for (int i = 0; i < list.size(); ++i)
    {
        const QJsonObject object = list.at(i).toObject();
        ChatSnapshot shot = snapshotMetaFromJson(object);
        fillContentFromJson(&shot, object);
        if (shot.id.isEmpty())
            continue;
        saveChat(shot);
        shot.history.clear();
        shot.display.clear();
        chats.append(shot);
    }
    return save(chats, root.value(QString::fromUtf8("activeId")).toString());
}

//----------------------------------------------

bool ChatStore::save(const QList<ChatSnapshot> &chats, QString activeId)
{
    if (g_chatProjectRoot.isEmpty())
        return false;
    QSet<QString> keep;
    QJsonArray list;
    foreach (const ChatSnapshot &shot, chats)
    {
        if (shot.id.isEmpty())
            continue;
        keep.insert(safeId(shot.id));
        list.append(snapshotMetaToJson(shot));
    }

    QDir dir(chatsDir());
    const QStringList files = dir.entryList(QStringList() << QString::fromUtf8("*.json"),
                                            QDir::Files);
    foreach (const QString &name, files)
    {
        const QString stem = QFileInfo(name).completeBaseName();
        if (!keep.contains(stem))
            dir.remove(name);
    }

    QJsonObject root;
    root.insert(QString::fromUtf8("version"), 2);
    root.insert(QString::fromUtf8("activeId"), activeId);
    root.insert(QString::fromUtf8("chats"), list);

    // Индекс пишем только когда он реально изменился.
    static QString    s_lastIndexPath;
    static QByteArray s_lastIndexBytes;
    const QString path = indexPath();
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (path == s_lastIndexPath && bytes == s_lastIndexBytes && QFileInfo(path).exists())
        return true;
    if (!writeJsonFile(path, root))
        return false;
    s_lastIndexPath = path;
    s_lastIndexBytes = bytes;
    return true;
}

//----------------------------------------------

bool ChatStore::load(QList<ChatSnapshot> *chats, QString *activeId)
{
    if (!chats || g_chatProjectRoot.isEmpty())
        return false;
    chats->clear();
    if (activeId)
        activeId->clear();

    if (!QFileInfo(indexPath()).exists())
        migrateLegacyMonolith();

    QJsonObject root;
    if (!readJsonFile(indexPath(), &root))
        return false;
    if (activeId)
        *activeId = root.value(QString::fromUtf8("activeId")).toString();

    const QJsonArray list = root.value(QString::fromUtf8("chats")).toArray();
    for (int i = 0; i < list.size(); ++i)
    {
        ChatSnapshot shot = snapshotMetaFromJson(list.at(i).toObject());
        if (shot.id.isEmpty())
            continue;
        chats->append(shot);
    }
    return !chats->isEmpty();
}

//----------------------------------------------

bool ChatStore::saveUiState(const ProjectUiState &ui)
{
    if (g_chatProjectRoot.isEmpty())
        return false;
    QJsonObject root;
    root.insert(QString::fromUtf8("version"), 1);
    root.insert(QString::fromUtf8("activePage"), ui.activePage);
    QJsonArray tabs;
    foreach (const QString &key, ui.openTabs)
    {
        if (!key.trimmed().isEmpty())
            tabs.append(key);
    }
    root.insert(QString::fromUtf8("openTabs"), tabs);
    root.insert(QString::fromUtf8("geometry"),
                QString::fromLatin1(ui.geometry.toBase64()));
    root.insert(QString::fromUtf8("windowState"),
                QString::fromLatin1(ui.windowState.toBase64()));
    return writeJsonFile(uiStatePath(), root);
}

//----------------------------------------------

bool ChatStore::loadUiState(ProjectUiState *ui)
{
    if (!ui || g_chatProjectRoot.isEmpty())
        return false;
    *ui = ProjectUiState();
    QJsonObject root;
    if (!readJsonFile(uiStatePath(), &root))
        return false;
    ui->activePage = root.value(QString::fromUtf8("activePage")).toString();
    const QJsonArray tabs = root.value(QString::fromUtf8("openTabs")).toArray();
    for (int i = 0; i < tabs.size(); ++i)
    {
        const QString key = tabs.at(i).toString().trimmed();
        if (!key.isEmpty())
            ui->openTabs.append(key);
    }
    ui->geometry = QByteArray::fromBase64(
        root.value(QString::fromUtf8("geometry")).toString().toLatin1());
    ui->windowState = QByteArray::fromBase64(
        root.value(QString::fromUtf8("windowState")).toString().toLatin1());
    return !ui->activePage.isEmpty() || !ui->openTabs.isEmpty()
           || !ui->geometry.isEmpty();
}
