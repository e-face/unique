#include "journalwidget.h"
//----------------------------------------------
#include "agent.h"
#include "markdownformatter.h"
//----------------------------------------------
#include <QtGlobal>

//----------------------------------------------

static const int kJournalTextClip = 16000;

//----------------------------------------------

JournalWidget::JournalWidget(QWidget *parent)
    : ConversationWidget(parent)
{
    setUserInputEnabled(false);
    setContextRowVisible(false);
}

//----------------------------------------------

void JournalWidget::attachAgent(Agent *agent, QString label)
{
    if (!agent)
        return;
    if (_labels.contains(agent))
    {
        _labels[agent] = label;
        return;
    }
    _labels.insert(agent, label);
    connect(agent, SIGNAL(userMessage(QString)),
            this, SLOT(sltJournalUser(QString)));
    connect(agent, SIGNAL(assistantMessage(QString)),
            this, SLOT(sltJournalAssistant(QString)));
    connect(agent, SIGNAL(toolCallStarted(QString,QString)),
            this, SLOT(sltJournalToolStarted(QString,QString)));
    connect(agent, SIGNAL(toolCallFinished(QString,QString,bool)),
            this, SLOT(sltJournalToolFinished(QString,QString,bool)));
    connect(agent, SIGNAL(notice(QString)),
            this, SLOT(sltJournalNotice(QString)));
    connect(agent, SIGNAL(failed(QString)),
            this, SLOT(sltJournalFailed(QString)));
    connect(agent, SIGNAL(destroyed(QObject*)),
            this, SLOT(sltAgentDestroyed(QObject*)));
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
    _pendingArgs.remove(agent);
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
    _pendingArgs.remove(object);
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

QString JournalWidget::shortArgs(QString arguments)
{
    QString text = arguments.simplified();
    if (text.size() > 160)
        text = text.left(157) + QString::fromUtf8("…");
    return text;
}

//----------------------------------------------

void JournalWidget::addPermanent(const QString &source, const QString &title,
                                 const QString &text, bool fromUser)
{
    // Постоянная запись: предыдущая временная остаётся в ленте как есть.
    _transient = 0;
    const QString caption = QString::fromUtf8("%1 \xC2\xB7 %2").arg(source, title);
    if (fromUser)
    {
        addPlainBubble(caption, text, QString::fromUtf8("userMsg"), true, false, true);
        return;
    }
    const QString html = MarkdownFormatter::toHtml(text.left(kJournalTextClip));
    addBubble(caption, html, text, QString::fromUtf8("assistantMsg"), false, false);
}

//----------------------------------------------

void JournalWidget::addTransient(const QString &source, const QString &title,
                                 const QString &text, const QString &kind)
{
    if (_transient)
        removeBubble(_transient);
    QString objectName = kind;
    if (objectName.isEmpty())
        objectName = QString::fromUtf8("toolMsg");
    const QString caption = QString::fromUtf8("%1 \xC2\xB7 %2").arg(source, title);
    _transient = addPlainBubble(caption, text, objectName, false, true, false);
}

//----------------------------------------------

void JournalWidget::sltJournalUser(const QString &text)
{
    addPermanent(labelFor(sender()), tr("Задача"), text, true);
}

//----------------------------------------------

void JournalWidget::sltJournalAssistant(const QString &text)
{
    addPermanent(labelFor(sender()), tr("Ответ"), text, false);
}

//----------------------------------------------

void JournalWidget::sltJournalToolStarted(const QString &name, const QString &arguments)
{
    QObject *agent = sender();
    _pendingArgs.insert(agent, arguments);
    addTransient(labelFor(agent), name,
                 tr("Выполняется: %1(%2)").arg(name, shortArgs(arguments)));
}

//----------------------------------------------

void JournalWidget::sltJournalToolFinished(const QString &name, const QString &result, bool ok)
{
    QObject *agent = sender();
    const QString args = _pendingArgs.take(agent);
    QString body = QString::fromUtf8("%1(%2)\n%3").arg(name, shortArgs(args), result);
    addTransient(labelFor(agent),
                 ok ? name : tr("%1 (ошибка)").arg(name),
                 body,
                 ok ? QString::fromUtf8("toolMsg") : QString::fromUtf8("errorMsg"));
}

//----------------------------------------------

void JournalWidget::sltJournalNotice(const QString &text)
{
    addTransient(labelFor(sender()), tr("Контекст"), text,
                 QString::fromUtf8("noticeMsg"));
}

//----------------------------------------------

void JournalWidget::sltJournalFailed(const QString &error)
{
    addTransient(labelFor(sender()), tr("Ошибка"), error,
                 QString::fromUtf8("errorMsg"));
}
