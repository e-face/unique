#include "journalwidget.h"
//----------------------------------------------
#include "agent.h"
#include "appsettings.h"
#include "chatsession.h"
#include "markdownformatter.h"
//----------------------------------------------
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegExp>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtGlobal>
#include <QtMath>

//----------------------------------------------

static const int kJournalTextClip       = 16000;
static const int kMaxPersistedEntries   = 100;
static const int kJournalPersistMs      = 500;
static const int kLiveRotateMs          = 3000;
static const int kLiveFadeMs            = 40;
/** Полный цикл яркости «чёткий → почти невидимый → чёткий», мс. */
static const int kLiveFadeCycleMs       = 2200;

//----------------------------------------------

JournalWidget::JournalWidget(QWidget *parent)
    : ConversationWidget(parent)
    , _lastPermanentFromUser(false)
    , _loading(false)
    , _persistDirty(false)
    , _liveRotateTimer(0)
    , _liveFadeTimer(0)
    , _persistTimer(0)
    , _liveActionIndex(0)
    , _liveFadePhase(0.0)
    , _publishedFadeAlpha(-1)
    , _transientBody()
{
    setUserInputEnabled(false);
    setContextRowVisible(false);
    setChatLiveActionVisible(false);

    _liveRotateTimer = new QTimer(this);
    _liveRotateTimer->setInterval(kLiveRotateMs);
    connect(_liveRotateTimer, SIGNAL(timeout()), this, SLOT(sltLiveActionRotate()));

    _liveFadeTimer = new QTimer(this);
    _liveFadeTimer->setInterval(kLiveFadeMs);
    connect(_liveFadeTimer, SIGNAL(timeout()), this, SLOT(sltLiveActionFade()));

    connect(this, SIGNAL(messageLinkActivated(QUrl)),
            this, SLOT(sltMessageLinkActivated(QUrl)));
}

//----------------------------------------------

void JournalWidget::resetUi()
{
    ConversationWidget::clearMessages();
    _transient = 0;
    _transientBody.clear();
    _lastPermanent = 0;
    _lastPermanentAuthor.clear();
    _lastPermanentFromUser = false;
    _agentActions.clear();
    _liveActions.clear();
    _liveActionIndex = 0;
    _liveFadePhase = 0.0;
    refreshLiveActionText();
}

//----------------------------------------------

void JournalWidget::clearMessages()
{
    _entries.clear();
    resetUi();
    schedulePersist();
}

//----------------------------------------------

bool JournalWidget::canPersist()
{
    return AppSettings::instance()->hasProjectSettings()
           && !ChatStore::storeDir().trimmed().isEmpty();
}

//----------------------------------------------

QString JournalWidget::persistPath()
{
    return ChatStore::storeDir() + QString::fromUtf8("/journal.json");
}

//----------------------------------------------

void JournalWidget::schedulePersist()
{
    if (_loading || !canPersist())
        return;
    _persistDirty = true;
    if (!_persistTimer)
    {
        _persistTimer = new QTimer(this);
        _persistTimer->setSingleShot(true);
        connect(_persistTimer, SIGNAL(timeout()), this, SLOT(sltFlushPersist()));
    }
    _persistTimer->start(kJournalPersistMs);
}

//----------------------------------------------

void JournalWidget::flushPersist()
{
    if (_persistTimer)
        _persistTimer->stop();
    sltFlushPersist();
}

//----------------------------------------------

void JournalWidget::sltFlushPersist()
{
    if (!_persistDirty)
        return;
    _persistDirty = false;
    if (!canPersist())
        return;

    QJsonArray items;
    const int start = qMax(0, _entries.size() - kMaxPersistedEntries);
    for (int i = start; i < _entries.size(); ++i)
    {
        const JournalEntry &entry = _entries.at(i);
        QJsonObject object;
        object.insert(QString::fromUtf8("author"), entry.author);
        object.insert(QString::fromUtf8("text"), entry.text);
        object.insert(QString::fromUtf8("fromUser"), entry.fromUser);
        if (!entry.workerId.trimmed().isEmpty())
            object.insert(QString::fromUtf8("workerId"), entry.workerId);
        items.append(object);
    }
    QJsonObject root;
    root.insert(QString::fromUtf8("entries"), items);

    const QString path = persistPath();
    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir))
        return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.commit();
}

//----------------------------------------------

void JournalWidget::loadPersisted()
{
    _entries.clear();
    resetUi();
    _persistDirty = false;
    if (_persistTimer)
        _persistTimer->stop();
    if (!canPersist())
        return;

    QFile file(persistPath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return;
    const QJsonArray items = document.object().value(QString::fromUtf8("entries")).toArray();
    const int start = qMax(0, items.size() - kMaxPersistedEntries);
    for (int i = start; i < items.size(); ++i)
    {
        const QJsonObject object = items.at(i).toObject();
        JournalEntry entry;
        entry.author = object.value(QString::fromUtf8("author")).toString();
        entry.text = object.value(QString::fromUtf8("text")).toString();
        entry.fromUser = object.value(QString::fromUtf8("fromUser")).toBool();
        entry.workerId = object.value(QString::fromUtf8("workerId")).toString();
        if (entry.text.trimmed().isEmpty() && entry.author.trimmed().isEmpty())
            continue;
        _entries.append(entry);
    }
    rebuildFromEntries();
}

//----------------------------------------------

void JournalWidget::rememberEntry(const QString &author, const QString &text, bool fromUser,
                                  const QString &workerId)
{
    if (_loading)
        return;
    JournalEntry entry;
    entry.author = author;
    entry.text = text;
    entry.fromUser = fromUser;
    entry.workerId = workerId.trimmed();
    _entries.append(entry);
    bool trimmed = false;
    while (_entries.size() > kMaxPersistedEntries)
    {
        _entries.removeFirst();
        trimmed = true;
    }
    if (trimmed)
        rebuildFromEntries();
    schedulePersist();
}

//----------------------------------------------

void JournalWidget::rebuildFromEntries()
{
    const QList<JournalEntry> keep = _entries;
    _loading = true;
    resetUi();
    foreach (const JournalEntry &entry, keep)
        addPermanent(entry.author, entry.text, entry.fromUser, entry.workerId);
    _entries = keep;
    _loading = false;
}

//----------------------------------------------

void JournalWidget::attachAgent(Agent *agent, QString label, QString workerId)
{
    if (!agent)
        return;
    if (_labels.contains(agent))
    {
        _labels[agent] = label;
        if (!workerId.trimmed().isEmpty())
            _workerIds[agent] = workerId.trimmed();
        return;
    }
    _labels.insert(agent, label);
    if (!workerId.trimmed().isEmpty())
        _workerIds.insert(agent, workerId.trimmed());
    connect(agent, SIGNAL(userMessage(QString)),
            this, SLOT(sltJournalUser(QString)));
    connect(agent, SIGNAL(assistantMessage(QString)),
            this, SLOT(sltJournalAssistant(QString)));
    connect(agent, SIGNAL(toolCallStarted(QString,QString)),
            this, SLOT(sltToolCallStarted(QString,QString)));
    connect(agent, SIGNAL(toolCallFinished(QString,QString,bool)),
            this, SLOT(sltToolCallFinished(QString,QString,bool)));
    connect(agent, SIGNAL(busyChanged(bool)),
            this, SLOT(sltJournalBusyChanged(bool)));
    connect(agent, SIGNAL(compactingChanged(bool)),
            this, SLOT(sltJournalCompactingChanged(bool)));
    connect(agent, SIGNAL(notice(QString)),
            this, SLOT(sltJournalNotice(QString)));
    connect(agent, SIGNAL(forwardedContext(QString)),
            this, SLOT(sltJournalForwarded(QString)));
    connect(agent, SIGNAL(failed(QString)),
            this, SLOT(sltJournalFailed(QString)));
    connect(agent, SIGNAL(destroyed(QObject*)),
            this, SLOT(sltAgentDestroyed(QObject*)));

    if (agent->isBusy())
        setAgentLiveAction(agent, tr("Думаю . . ."));
}

//----------------------------------------------

void JournalWidget::detachAgent(Agent *agent)
{
    if (!agent)
        return;
    if (!_labels.contains(agent))
        return;
    disconnect(agent, 0, this, 0);
    _labels.remove(agent);
    _workerIds.remove(agent);
    clearAgentLiveAction(agent);
}

//----------------------------------------------

void JournalWidget::setAgentLabel(Agent *agent, QString label)
{
    if (agent && _labels.contains(agent))
        _labels[agent] = label;
}

//----------------------------------------------

void JournalWidget::sltAgentDestroyed(QObject *object)
{
    _labels.remove(object);
    _workerIds.remove(object);
    clearAgentLiveAction(object);
}

//----------------------------------------------

QString JournalWidget::labelFor(QObject *sender) const
{
    const QString label = _labels.value(sender);
    if (label.isEmpty())
        return tr("Чат");
    return label;
}

//----------------------------------------------

QString JournalWidget::workerIdFor(QObject *sender) const
{
    return _workerIds.value(sender);
}

//----------------------------------------------

QString JournalWidget::workerChatUrl(const QString &workerId)
{
    const QString id = workerId.trimmed();
    if (id.isEmpty())
        return QString();
    return QString::fromUtf8("unique-worker:%1").arg(id);
}

//----------------------------------------------

QString JournalWidget::workerIdFromUrl(const QUrl &url)
{
    if (url.scheme() != QString::fromUtf8("unique-worker"))
        return QString();
    QString id = url.path();
    if (id.startsWith(QChar('/')))
        id = id.mid(1);
    if (id.isEmpty())
    {
        const QString s = url.toString();
        const int colon = s.indexOf(QChar(':'));
        if (colon >= 0)
            id = s.mid(colon + 1);
    }
    if (id.startsWith(QString::fromUtf8("//")))
        id = id.mid(2);
    return id.trimmed();
}

//----------------------------------------------

void JournalWidget::bindTitleLink(QWidget *bubble, const QString &workerId)
{
    ChatMessageFrame *frame = qobject_cast<ChatMessageFrame *>(bubble);
    if (!frame)
        return;
    const QString url = workerChatUrl(workerId);
    if (!url.isEmpty())
        frame->setTitleLink(url);
}

//----------------------------------------------

void JournalWidget::sltMessageLinkActivated(const QUrl &url)
{
    const QString id = workerIdFromUrl(url);
    if (!id.isEmpty())
        emit workerChatRequested(id);
}

//----------------------------------------------

QString JournalWidget::caption(const QString &author, const QString &title)
{
    const QString who = author.trimmed();
    const QString what = title.trimmed();
    if (who.isEmpty())
        return what;
    if (what.isEmpty())
        return who;
    return QString::fromUtf8("%1 \xC2\xB7 %2").arg(who, what);
}

//----------------------------------------------

QString JournalWidget::actionTitleForTool(const QString &name)
{
    const QString n = name.trimmed();
    if (n == QString::fromUtf8("read_file"))
        return JournalWidget::tr("чтение файла");
    if (n == QString::fromUtf8("write_file") || n == QString::fromUtf8("create_file"))
        return JournalWidget::tr("запись файла");
    if (n == QString::fromUtf8("delete_file") || n == QString::fromUtf8("rename_file"))
        return JournalWidget::tr("файл");
    if (n == QString::fromUtf8("list_dir") || n == QString::fromUtf8("list_directory"))
        return JournalWidget::tr("обзор каталога");
    if (n == QString::fromUtf8("run_command"))
        return JournalWidget::tr("команда");
    if (n == QString::fromUtf8("complete_task"))
        return JournalWidget::tr("завершение задачи");
    if (n == QString::fromUtf8("plan_work"))
        return JournalWidget::tr("план");
    if (n == QString::fromUtf8("list_tools"))
        return JournalWidget::tr("список инструментов");
    if (n == QString::fromUtf8("list_worker_presets"))
        return JournalWidget::tr("список воркеров");
    if (n == QString::fromUtf8("spawn_worker") || n == QString::fromUtf8("create_worker")
        || n == QString::fromUtf8("delegate_task"))
        return JournalWidget::tr("поручение");
    if (n == QString::fromUtf8("group_speak"))
        return JournalWidget::tr("реплика в группе");
    if (n.isEmpty())
        return JournalWidget::tr("функция");
    return n;
}

//----------------------------------------------

QString JournalWidget::toolArgsExcerpt(const QString &name, const QString &arguments)
{
    const QJsonDocument doc = QJsonDocument::fromJson(arguments.toUtf8());
    if (!doc.isObject())
    {
        QString raw = arguments.trimmed();
        raw.replace(QChar('\n'), QChar(' '));
        raw.replace(QChar('\r'), QChar(' '));
        return raw;
    }
    const QJsonObject o = doc.object();
    const QString n = name.trimmed();
    QString excerpt;
    if (n == QString::fromUtf8("run_command"))
    {
        const QJsonArray cmds = o.value(QString::fromUtf8("commands")).toArray();
        if (!cmds.isEmpty())
            excerpt = cmds.at(0).toString();
        if (excerpt.isEmpty())
            excerpt = o.value(QString::fromUtf8("command")).toString();
    }
    else if (n == QString::fromUtf8("read_file") || n == QString::fromUtf8("write_file")
             || n == QString::fromUtf8("create_file") || n == QString::fromUtf8("delete_file")
             || n == QString::fromUtf8("rename_file")
             || n == QString::fromUtf8("list_directory") || n == QString::fromUtf8("list_dir"))
    {
        const QJsonArray paths = o.value(QString::fromUtf8("paths")).toArray();
        if (!paths.isEmpty())
            excerpt = paths.at(0).toString();
        if (excerpt.isEmpty())
        {
            const QJsonArray files = o.value(QString::fromUtf8("files")).toArray();
            if (!files.isEmpty() && files.at(0).isObject())
                excerpt = files.at(0).toObject().value(QString::fromUtf8("path")).toString();
        }
        if (excerpt.isEmpty())
            excerpt = o.value(QString::fromUtf8("path")).toString();
    }
    else if (n == QString::fromUtf8("delegate_task") || n == QString::fromUtf8("spawn_worker"))
    {
        excerpt = o.value(QString::fromUtf8("task")).toString();
        if (excerpt.isEmpty())
            excerpt = o.value(QString::fromUtf8("instruction")).toString();
    }
    else if (n == QString::fromUtf8("group_speak"))
    {
        excerpt = o.value(QString::fromUtf8("message")).toString();
    }
    else if (n == QString::fromUtf8("complete_task"))
    {
        excerpt = o.value(QString::fromUtf8("result")).toString();
    }
    excerpt = excerpt.trimmed();
    excerpt.replace(QChar('\n'), QChar(' '));
    excerpt.replace(QChar('\r'), QChar(' '));
    while (excerpt.contains(QString::fromUtf8("  ")))
        excerpt.replace(QString::fromUtf8("  "), QString::fromUtf8(" "));
    return excerpt;
}

//----------------------------------------------

QString JournalWidget::friendlyIngressBody(const QString &text, int ingress)
{
    QString t = text.trimmed();
    if (ingress == AgentUserIngressGroup)
    {
        if (t.startsWith(QString::fromUtf8("[Your turn as narrator]")))
            return JournalWidget::tr("Ход в обсуждении группы");
        if (t.startsWith(QString::fromUtf8("[Group VOTE required]")))
            return JournalWidget::tr("Голосование в группе");
        if (t.startsWith(QString::fromUtf8("[Group dialogue DONE]")))
            return JournalWidget::tr("Группа пришла к решению");
        if (t.startsWith(QString::fromUtf8("[Waiting]"))
            || t.startsWith(QString::fromUtf8("[Waiting for other votes]")))
            return JournalWidget::tr("Ожидание хода в группе");
    }
    if (t.startsWith(QString::fromUtf8("Follow-up from your parent worker.")))
    {
        const int sep = t.indexOf(QString::fromUtf8("\n\n"));
        if (sep >= 0)
            t = t.mid(sep + 2).trimmed();
    }
    if (t.contains(QString::fromUtf8("CONTEXT HANDOFF")))
    {
        const QString marker = QString::fromUtf8("--- your assignment ---");
        const int at = t.indexOf(marker);
        if (at >= 0)
            t = t.mid(at + marker.size()).trimmed();
    }
    if (t.size() > 600)
        t = t.left(597) + QString::fromUtf8("...");
    return t;
}

//----------------------------------------------

static QString shortNameFromLabel(const QString &label)
{
    QString name = label.trimmed();
    const int dot = name.indexOf(QString::fromUtf8(" \xC2\xB7 "));
    if (dot > 0)
        return name.left(dot).trimmed();
    const QString toolPrefix = QString::fromUtf8("Инструмент: ");
    if (name.startsWith(toolPrefix))
        name = name.mid(toolPrefix.size()).trimmed();
    const int dash = name.indexOf(QString::fromUtf8(" \xE2\x80\x94 "));
    if (dash > 0)
        name = name.left(dash).trimmed();
    return name;
}

//----------------------------------------------

static QString firstJournalLine(const QString &text)
{
    QString t = text.trimmed();
    t.replace(QChar('\r'), QChar('\n'));
    const int nl = t.indexOf(QChar('\n'));
    if (nl > 0)
        t = t.left(nl);
    t.replace(QRegExp(QString::fromUtf8("\\s+")), QString::fromUtf8(" "));
    t = t.trimmed();
    if (t.size() > 88)
        t = t.left(85) + QString::fromUtf8("…");
    return t;
}

//----------------------------------------------

static QString exchangeTitle(const QString &from, const QString &to, const QString &heading)
{
    const QString author = from.trimmed().isEmpty()
                           ? JournalWidget::tr("воркер")
                           : from.trimmed();
    const QString dest = to.trimmed().isEmpty()
                         ? JournalWidget::tr("воркер")
                         : to.trimmed();
    const QString title = heading.trimmed();
    if (title.isEmpty())
        return QString::fromUtf8("%1 \xE2\x86\x92 %2").arg(author, dest);
    return QString::fromUtf8("%1 \xE2\x86\x92 %2: %3").arg(author, dest, title);
}

//----------------------------------------------

void JournalWidget::setAgentLiveAction(QObject *agent, const QString &action)
{
    if (!agent)
        return;
    const QString text = action.trimmed();
    if (text.isEmpty())
    {
        clearAgentLiveAction(agent);
        return;
    }
    _agentActions.insert(agent, text);
    rebuildLiveActionList();
    refreshLiveActionText();
}

//----------------------------------------------

void JournalWidget::clearAgentLiveAction(QObject *agent)
{
    if (!agent)
        return;
    if (!_agentActions.contains(agent))
        return;
    _agentActions.remove(agent);
    rebuildLiveActionList();
    refreshLiveActionText();
}

//----------------------------------------------

void JournalWidget::rebuildLiveActionList()
{
    _liveActions.clear();
    QHashIterator<QObject *, QString> it(_agentActions);
    while (it.hasNext())
    {
        it.next();
        if (!it.key() || it.value().trimmed().isEmpty())
            continue;
        LiveAction item;
        item.agent = it.key();
        item.text = caption(labelFor(it.key()), it.value());
        _liveActions.append(item);
    }
    if (_liveActionIndex >= _liveActions.size())
        _liveActionIndex = 0;
}

//----------------------------------------------

void JournalWidget::applyLiveActionFadeStyle()
{
    double alpha = 0.0;
    if (!_liveActions.isEmpty())
    {
        // 1 → ~0.12 → 1 по синусу.
        const double wave = 0.5 * (1.0 + qSin(_liveFadePhase * 2.0 * 3.14159265358979323846));
        alpha = 0.12 + wave * 0.88;
    }
    const int fade = qBound(0, int(alpha * 255.0 + 0.5), 255);
    if (fade != _publishedFadeAlpha)
    {
        _publishedFadeAlpha = fade;
        emit liveFadeChanged(_publishedFadeAlpha);
    }
}

//----------------------------------------------

void JournalWidget::refreshLiveActionText()
{
    if (_liveActions.isEmpty())
    {
        _liveFadePhase = 0.0;
        applyLiveActionFadeStyle();
        publishLiveStatus();
        if (_liveRotateTimer)
            _liveRotateTimer->stop();
        if (_liveFadeTimer)
            _liveFadeTimer->stop();
        return;
    }

    if (_liveActionIndex < 0 || _liveActionIndex >= _liveActions.size())
        _liveActionIndex = 0;
    applyLiveActionFadeStyle();
    publishLiveStatus();

    if (_liveRotateTimer && !_liveRotateTimer->isActive())
        _liveRotateTimer->start();
    if (_liveFadeTimer && !_liveFadeTimer->isActive())
        _liveFadeTimer->start();
}

//----------------------------------------------

void JournalWidget::sltLiveActionRotate()
{
    if (_liveActions.isEmpty())
    {
        refreshLiveActionText();
        return;
    }
    _liveActionIndex = (_liveActionIndex + 1) % _liveActions.size();
    applyLiveActionFadeStyle();
    publishLiveStatus();
}

//----------------------------------------------

QString JournalWidget::currentLiveActionText() const
{
    if (_liveActions.isEmpty())
        return QString();
    int index = _liveActionIndex;
    if (index < 0 || index >= _liveActions.size())
        index = 0;
    return _liveActions.at(index).text.trimmed();
}

//----------------------------------------------

void JournalWidget::publishLiveStatus()
{
    const QString text = currentLiveActionText();
    if (text == _publishedLiveStatus)
        return;
    _publishedLiveStatus = text;
    emit liveStatusChanged(text);
}

//----------------------------------------------

void JournalWidget::sltLiveActionFade()
{
    if (_liveActions.isEmpty())
        return;
    const double step = double(kLiveFadeMs) / double(kLiveFadeCycleMs);
    _liveFadePhase += step;
    if (_liveFadePhase >= 1.0)
        _liveFadePhase -= 1.0;
    applyLiveActionFadeStyle();
}

//----------------------------------------------

void JournalWidget::addPermanent(const QString &author, const QString &text, bool fromUser,
                                 const QString &workerId)
{
    // Постоянная запись: предыдущая временная остаётся в ленте как есть.
    _transient = 0;
    const QString who = author.trimmed().isEmpty() ? tr("Чат") : author.trimmed();
    QString colorKey = who;
    const int arrowAt = who.indexOf(QString::fromUtf8(" \xE2\x86\x92 "));
    if (arrowAt > 0)
        colorKey = who.left(arrowAt).trimmed();
    if (fromUser)
        colorKey = QString::fromUtf8("Пользователь");

    if (_lastPermanent
        && _lastPermanentAuthor == who
        && _lastPermanentFromUser == fromUser)
    {
        if (!isRestoring())
            setPinBottom(isNearBottom());
        if (fromUser)
            _lastPermanent->appendEscapedParagraph(text);
        else
        {
            const QString html = MarkdownFormatter::toHtml(text.left(kJournalTextClip));
            _lastPermanent->appendHtmlParagraph(html, text);
        }
        if (!workerId.trimmed().isEmpty())
            bindTitleLink(_lastPermanent, workerId);
        if (!isRestoring() && pinBottom())
            scheduleScrollToBottom();
        rememberEntry(who, text, fromUser, workerId);
        return;
    }

    QWidget *bubble = 0;
    if (fromUser)
    {
        bubble = addPlainBubble(who, text, QString::fromUtf8("userMsg"), true, false, true,
                                colorKey);
    }
    else
    {
        const QString html = MarkdownFormatter::toHtml(text.left(kJournalTextClip));
        bubble = addBubble(who, html, text, QString::fromUtf8("assistantMsg"), false, false,
                           colorKey);
    }
    bindTitleLink(bubble, workerId);
    _lastPermanent = qobject_cast<ChatMessageFrame *>(bubble);
    if (_lastPermanent)
        _lastPermanent->setPreview(QString());
    _lastPermanentAuthor = who;
    _lastPermanentFromUser = fromUser;
    rememberEntry(who, text, fromUser, workerId);
}

//----------------------------------------------

void JournalWidget::addTransient(const QString &author, const QString &title,
                                 const QString &text, const QString &kind,
                                 const QString &workerId)
{
    if (_transient && _transientBody == text)
        return;
    _transientBody = text;
    if (_transient)
        removeBubble(_transient);
    QString objectName = kind;
    if (objectName.isEmpty())
        objectName = QString::fromUtf8("noticeMsg");
    const QString who = author.trimmed().isEmpty() ? tr("Чат") : author.trimmed();
    _transient = addPlainBubble(caption(who, title), text, objectName, false, true, false, who);
    bindTitleLink(_transient, workerId);
}

//----------------------------------------------

void JournalWidget::sltJournalUser(const QString &text)
{
    Agent *agent = qobject_cast<Agent *>(sender());
    const int ingress = agent ? agent->lastUserIngress() : AgentUserIngressHuman;
    if (ingress == AgentUserIngressSystem)
        return;
    QString from = agent ? agent->turnAuthor() : QString();
    if (from.isEmpty())
    {
        if (ingress == AgentUserIngressHuman)
            from = tr("Пользователь");
        else if (ingress == AgentUserIngressGroup)
            from = tr("Группа");
        else
            from = tr("Родитель");
    }
    const QString to = shortNameFromLabel(labelFor(sender()));
    const QString body = friendlyIngressBody(text, ingress);
    if (body.trimmed().isEmpty())
        return;
    addPermanent(exchangeTitle(from, to, firstJournalLine(body)), body,
                 ingress == AgentUserIngressHuman, workerIdFor(sender()));
}

//----------------------------------------------

void JournalWidget::sltJournalAssistant(const QString &text)
{
    Agent *agent = qobject_cast<Agent *>(sender());
    const QString from = shortNameFromLabel(labelFor(sender()));
    QString to = agent ? agent->journalReplyTo() : QString();
    if (to.isEmpty())
        to = tr("Пользователь");
    const QString body = text.trimmed();
    if (body.isEmpty())
        return;
    addPermanent(exchangeTitle(from, to, firstJournalLine(body)), text, false,
                 workerIdFor(sender()));
}

//----------------------------------------------

void JournalWidget::sltJournalNotice(const QString &text)
{
    addTransient(labelFor(sender()), tr("Заметка"), text,
                 QString::fromUtf8("noticeMsg"), workerIdFor(sender()));
}

//----------------------------------------------

void JournalWidget::sltJournalForwarded(const QString &text)
{
    Q_UNUSED(text);
    // Служебные контексты (handoff, отчёты) в журнал не показываем —
    // только понятные человеку события.
}

//----------------------------------------------

void JournalWidget::sltJournalFailed(const QString &error)
{
    addTransient(labelFor(sender()), tr("Ошибка"), error,
                 QString::fromUtf8("errorMsg"), workerIdFor(sender()));
}

//----------------------------------------------

void JournalWidget::sltToolCallStarted(const QString &name, const QString &arguments)
{
    QString action = actionTitleForTool(name);
    const QString excerpt = toolArgsExcerpt(name, arguments);
    if (!excerpt.isEmpty())
        action += QString::fromUtf8(": ") + excerpt;
    setAgentLiveAction(sender(), action);
}

//----------------------------------------------

void JournalWidget::sltToolCallFinished(const QString &name, const QString &result, bool ok)
{
    Q_UNUSED(name);
    Q_UNUSED(result);
    Q_UNUSED(ok);
    Agent *agent = qobject_cast<Agent *>(sender());
    if (agent && agent->isBusy())
        setAgentLiveAction(agent, tr("Думаю . . ."));
    else
        clearAgentLiveAction(sender());
}

//----------------------------------------------

void JournalWidget::sltJournalBusyChanged(bool busy)
{
    Agent *agent = qobject_cast<Agent *>(sender());
    if (!agent)
        return;
    if (busy)
    {
        if (!_agentActions.contains(agent)
            || _agentActions.value(agent) == tr("Думаю . . .")
            || _agentActions.value(agent).isEmpty())
            setAgentLiveAction(agent, tr("Думаю . . ."));
        return;
    }
    clearAgentLiveAction(agent);
}

//----------------------------------------------

void JournalWidget::sltJournalCompactingChanged(bool compacting)
{
    if (compacting)
        setAgentLiveAction(sender(), tr("сжатие контекста"));
    else
    {
        Agent *agent = qobject_cast<Agent *>(sender());
        if (agent && agent->isBusy())
            setAgentLiveAction(agent, tr("Думаю . . ."));
        else
            clearAgentLiveAction(sender());
    }
}
