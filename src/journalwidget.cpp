#include "journalwidget.h"
//----------------------------------------------
#include "agent.h"
#include "appsettings.h"
#include "markdownformatter.h"
//----------------------------------------------
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QtGlobal>
#include <QtMath>

//----------------------------------------------

static const int kJournalTextClip   = 16000;
static const int kLiveActionHeight  = 24;
static const int kLiveRotateMs      = 3000;
static const int kLiveFadeMs        = 40;
/** Полный цикл яркости «чёткий → почти невидимый → чёткий», мс. */
static const int kLiveFadeCycleMs   = 2200;

//----------------------------------------------

JournalWidget::JournalWidget(QWidget *parent)
    : ConversationWidget(parent)
    , _lastPermanentFromUser(false)
    , _liveActionLine(0)
    , _liveRotateTimer(0)
    , _liveFadeTimer(0)
    , _liveActionIndex(0)
    , _liveFadePhase(0.0)
{
    setUserInputEnabled(false);
    setContextRowVisible(false);
    setChatLiveActionVisible(false);

    _liveActionLine = new QLabel(messagesHost());
    _liveActionLine->setObjectName(QString::fromUtf8("journalLiveAction"));
    _liveActionLine->setWordWrap(false);
    _liveActionLine->setTextInteractionFlags(Qt::NoTextInteraction);
    _liveActionLine->setFixedHeight(kLiveActionHeight);
    _liveActionLine->setMinimumHeight(kLiveActionHeight);
    _liveActionLine->setMaximumHeight(kLiveActionHeight);
    _liveActionLine->setContentsMargins(12, 0, 12, 0);
    _liveActionLine->setText(QString::fromUtf8(" "));
    _liveActionLine->show();
    if (messagesLayout())
        messagesLayout()->addWidget(_liveActionLine, 0, Qt::AlignTop);
    applyLiveActionFadeStyle();

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

void JournalWidget::clearMessages()
{
    ConversationWidget::clearMessages();
    _transient = 0;
    _lastPermanent = 0;
    _lastPermanentAuthor.clear();
    _lastPermanentFromUser = false;
    _agentActions.clear();
    _liveActions.clear();
    _liveActionIndex = 0;
    _liveFadePhase = 0.0;
    if (_liveActionLine && messagesLayout())
    {
        messagesLayout()->addWidget(_liveActionLine, 0, Qt::AlignTop);
        _liveActionLine->setFixedHeight(kLiveActionHeight);
        _liveActionLine->show();
        refreshLiveActionText();
    }
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

QString JournalWidget::elideLiveAction(const QString &text) const
{
    if (!_liveActionLine)
        return text;
    QFontMetrics fm(_liveActionLine->font());
    int width = _liveActionLine->contentsRect().width();
    if (width < 40)
        width = qMax(120, _liveActionLine->width() - 24);
    if (width < 40)
        width = 360;
    // elidedText сам ставит «…» в конце, когда строка не входит.
    return fm.elidedText(text, Qt::ElideRight, width);
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

QString JournalWidget::ingressAuthorTitle(const QString &toLabel, int ingress)
{
    const QString to = toLabel.trimmed().isEmpty() ? JournalWidget::tr("воркер") : toLabel;
    if (ingress == AgentUserIngressAssignment)
        return JournalWidget::tr("Задание → %1").arg(to);
    if (ingress == AgentUserIngressFollowUp)
        return JournalWidget::tr("Уточнение → %1").arg(to);
    if (ingress == AgentUserIngressGroup)
        return JournalWidget::tr("Группа → %1").arg(to);
    return JournalWidget::tr("Пользователь → %1").arg(to);
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

void JournalWidget::ensureLiveActionAtEnd()
{
    if (!_liveActionLine || !messagesLayout())
        return;
    messagesLayout()->removeWidget(_liveActionLine);
    messagesLayout()->addWidget(_liveActionLine, 0, Qt::AlignTop);
    _liveActionLine->setFixedHeight(kLiveActionHeight);
    _liveActionLine->show();
}

//----------------------------------------------

void JournalWidget::applyLiveActionFadeStyle()
{
    if (!_liveActionLine)
        return;
    AppSettings *s = AppSettings::instance();
    QColor c = s->mutedTextColor();
    double alpha = 0.0;
    if (!_liveActions.isEmpty())
    {
        // 1 → ~0.12 → 1 по синусу.
        const double wave = 0.5 * (1.0 + qSin(_liveFadePhase * 2.0 * 3.14159265358979323846));
        alpha = 0.12 + wave * 0.88;
    }
    c.setAlpha(qBound(0, int(alpha * 255.0 + 0.5), 255));
    _liveActionLine->setStyleSheet(
        QString::fromUtf8(
            "QLabel#journalLiveAction { color:rgba(%1,%2,%3,%4); font-style:italic; "
            "background:transparent; border:none; min-height:%5px; max-height:%5px; }")
            .arg(c.red())
            .arg(c.green())
            .arg(c.blue())
            .arg(c.alpha())
            .arg(kLiveActionHeight));
}

//----------------------------------------------

void JournalWidget::refreshLiveActionText()
{
    if (!_liveActionLine)
        return;

    if (_liveActions.isEmpty())
    {
        _liveActionLine->setText(QString::fromUtf8(" "));
        _liveFadePhase = 0.0;
        applyLiveActionFadeStyle();
        if (_liveRotateTimer)
            _liveRotateTimer->stop();
        if (_liveFadeTimer)
            _liveFadeTimer->stop();
        return;
    }

    if (_liveActionIndex < 0 || _liveActionIndex >= _liveActions.size())
        _liveActionIndex = 0;
    _liveActionLine->setText(elideLiveAction(_liveActions.at(_liveActionIndex).text));
    applyLiveActionFadeStyle();

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
    if (_liveActionLine)
        _liveActionLine->setText(elideLiveAction(_liveActions.at(_liveActionIndex).text));
    applyLiveActionFadeStyle();
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
    const QString colorKey = fromUser ? QString::fromUtf8("Пользователь") : who;

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
        ensureLiveActionAtEnd();
        if (!isRestoring() && pinBottom())
            scheduleScrollToBottom();
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
    _lastPermanentAuthor = who;
    _lastPermanentFromUser = fromUser;
    ensureLiveActionAtEnd();
}

//----------------------------------------------

void JournalWidget::addTransient(const QString &author, const QString &title,
                                 const QString &text, const QString &kind,
                                 const QString &workerId)
{
    if (_transient)
        removeBubble(_transient);
    QString objectName = kind;
    if (objectName.isEmpty())
        objectName = QString::fromUtf8("noticeMsg");
    const QString who = author.trimmed().isEmpty() ? tr("Чат") : author.trimmed();
    _transient = addPlainBubble(caption(who, title), text, objectName, false, true, false, who);
    bindTitleLink(_transient, workerId);
    ensureLiveActionAtEnd();
}

//----------------------------------------------

void JournalWidget::sltJournalUser(const QString &text)
{
    Agent *agent = qobject_cast<Agent *>(sender());
    const int ingress = agent ? agent->lastUserIngress() : AgentUserIngressHuman;
    if (ingress == AgentUserIngressSystem)
        return;
    const QString to = labelFor(sender());
    const QString author = ingressAuthorTitle(to, ingress);
    const QString body = friendlyIngressBody(text, ingress);
    if (body.trimmed().isEmpty())
        return;
    addPermanent(author, body, ingress == AgentUserIngressHuman, workerIdFor(sender()));
}

//----------------------------------------------

void JournalWidget::sltJournalAssistant(const QString &text)
{
    addPermanent(labelFor(sender()), text, false, workerIdFor(sender()));
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
