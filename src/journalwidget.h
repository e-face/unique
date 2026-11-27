#ifndef JOURNALWIDGET_H
#define JOURNALWIDGET_H

#include "conversationwidget.h"
//----------------------------------------------
#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>
#include <QUrl>

class Agent;
class QTimer;

//----------------------------------------------

/**
 * @brief A permanent journal-feed record, saved into the project.
 */
struct JournalEntry
{
    JournalEntry()
        : fromUser(false)
    {
    }

    QString author;
    QString text;
    QString workerId;
    bool    fromUser;
};

//----------------------------------------------

/**
 * @brief Journal: the combined feed of the project's chats.
 *
 * User and assistant messages carry an author caption.
 * Function calls do not enter the permanent feed. A finished work plan
 * does: it is the executor's message, the same text as in that chat.
 * The current action is not printed in the journal: it flickers in the status line.
 * Notes and errors are temporary collapsed records; a new one replaces the previous.
 * Permanent records are saved in ___project___/journal.json
 * (no more than the last 100).
 */
class JournalWidget : public ConversationWidget
{
    Q_OBJECT
public:
    explicit JournalWidget(QWidget *parent = 0);

    void attachAgent(Agent *agent, QString label, QString workerId = QString());
    void detachAgent(Agent *agent);
    void setAgentLabel(Agent *agent, QString label);
    void clearMessages();
    /** Load the permanent feed from the current project's directory. */
    void loadPersisted();
    /** Write deferred changes to disk immediately. */
    void flushPersist();

signals:
    void workerChatRequested(const QString &workerId);
    /** Current action for the status line; empty when no request is running. */
    void liveStatusChanged(const QString &text);
    /** Brightness of that line, from 0 to 255. */
    void liveFadeChanged(int alpha);

public slots:
    void addPermanent(const QString &author, const QString &text, bool fromUser,
                      const QString &workerId = QString());
    void addTransient(const QString &author, const QString &title,
                      const QString &text, const QString &kind = QString(),
                      const QString &workerId = QString());

private slots:
    void sltJournalUser(const QString &text);
    void sltJournalAssistant(const QString &text);
    void sltJournalRetracted(const QString &text);
    void sltJournalNotice(const QString &text);
    void sltJournalForwarded(const QString &text);
    void sltJournalFailed(const QString &error);
    void sltAgentDestroyed(QObject *object);
    void sltToolCallStarted(const QString &name, const QString &arguments);
    void sltToolCallFinished(const QString &name, const QString &result, bool ok);
    void sltJournalBusyChanged(bool busy);
    void sltJournalCompactingChanged(bool compacting);
    void sltLiveActionRotate();
    void sltLiveActionFade();
    void sltMessageLinkActivated(const QUrl &url);
    void sltFlushPersist();

private:
    QString labelFor(QObject *sender) const;
    QString workerIdFor(QObject *sender) const;
    static QString workerChatUrl(const QString &workerId);
    static QString workerIdFromUrl(const QUrl &url);
    static QString caption(const QString &author, const QString &title);
    static QString actionTitleForTool(const QString &name);
    static QString toolArgsExcerpt(const QString &name, const QString &arguments);
    static QString friendlyIngressBody(const QString &text, int ingress);
    void setAgentLiveAction(QObject *agent, const QString &action);
    void clearAgentLiveAction(QObject *agent);
    void rebuildLiveActionList();
    void refreshLiveActionText();
    void applyLiveActionFadeStyle();
    QString currentLiveActionText() const;
    void publishLiveStatus();
    void bindTitleLink(QWidget *bubble, const QString &workerId);
    void resetUi();
    void rememberEntry(const QString &author, const QString &text, bool fromUser,
                       const QString &workerId);
    void rebuildFromEntries();
    void schedulePersist();
    static QString persistPath();
    static bool canPersist();

    struct LiveAction
    {
        QPointer<QObject> agent;
        QString           text;
    };

    QHash<QObject *, QString>  _labels;
    QHash<QObject *, QString>  _workerIds;
    QHash<QObject *, QString>  _agentActions;
    QHash<QObject *, QString>  _pendingToolArgs;
    QList<LiveAction>          _liveActions;
    QList<JournalEntry>        _entries;
    QPointer<QWidget>          _transient;
    QPointer<ChatMessageFrame> _lastPermanent;
    QString                    _lastPermanentAuthor;
    bool                       _lastPermanentFromUser;
    bool                       _loading;
    bool                       _persistDirty;
    QTimer                    *_liveRotateTimer;
    QTimer                    *_liveFadeTimer;
    QTimer                    *_persistTimer;
    int                        _liveActionIndex;
    double                     _liveFadePhase;
    QString                    _publishedLiveStatus;
    int                        _publishedFadeAlpha;
    QString                    _transientBody;
};

#endif // JOURNALWIDGET_H
