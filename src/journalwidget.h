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
 * @brief Запись постоянной ленты журнала (для сохранения в проект).
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
 * @brief Журнал: сводная лента чатов проекта.
 *
 * Сообщения пользователя и ассистента — с подписью автора.
 * Вызовы функций в постоянную ленту не попадают. Текущее действие
 * в журнал не выводится: оно мерцает в строке состояния.
 * Заметки/ошибки — временные свёрнутые записи (новая замещает предыдущую).
 * Постоянные записи сохраняются в ___project___/journal.json
 * (не больше последних 100).
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
    /** Загрузить постоянную ленту из каталога текущего проекта. */
    void loadPersisted();
    /** Сразу записать отложенные изменения на диск. */
    void flushPersist();

signals:
    void workerChatRequested(const QString &workerId);
    /** Текущее действие для строки состояния; пусто, когда запрос не выполняется. */
    void liveStatusChanged(const QString &text);
    /** Яркость той же строки, 0…255. */
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
