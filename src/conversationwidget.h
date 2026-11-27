#ifndef CONVERSATIONWIDGET_H
#define CONVERSATIONWIDGET_H

#include "llmtypes.h"
//----------------------------------------------
#include <QColor>
#include <QElapsedTimer>
#include <QFrame>
#include <QList>
#include <QString>
#include <QUrl>
#include <QWidget>

class Agent;
class QCheckBox;
class QLabel;
class QLineEdit;
class QTimer;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QTextBrowser;
class QVBoxLayout;
class QWidget;

//----------------------------------------------

/**
 * @brief One message card: a single HTML document, with no per-line label.
 */
class ChatMessageFrame : public QFrame
{
    Q_OBJECT
public:
    ChatMessageFrame(QString title, QString html, QString preview,
                     QString objectName, bool alignRight, bool collapsed,
                     QWidget *parent = 0, QString authorKey = QString());
    void applyTheme();
    /**
     * @brief Link in the header (for example unique-worker:<id>).
     */
    void setTitleLink(QString url);
    /** Change the caption without recreating the widget (language change). */
    void setCaption(QString title, QString authorKey);
    /** Text after the header in a collapsed row. An empty string means the header only. */
    void setPreview(QString preview);
    /**
     * @brief Append a paragraph (escaped text) to an existing message.
     */
    void appendEscapedParagraph(QString text);
    /**
     * @brief Append a ready HTML paragraph to an existing message.
     */
    void appendHtmlParagraph(QString html, QString previewSource);

signals:
    void linkActivated(const QUrl &url);

protected:
    virtual void resizeEvent(QResizeEvent *event);

private slots:
    void sltToggle();
    void sltFitDocumentHeight();
    void sltDocumentSizeChanged();
    void sltAnchorClicked(const QUrl &url);

private:
    void applyCollapsed();
    void fitDocumentHeight();
    QString collapsedHtml() const;
    QString expandedHtml() const;
    QString titleBadgeHtml() const;

    QString      _title;
    QString      _authorKey;
    QString      _titleLink;
    QString      _bodyHtml;
    QString      _preview;
    QTextBrowser *_view;
    QPushButton  *_toggle;
    bool          _collapsed;
    bool          _fitting;
    bool          _fitScheduled;
    int           _lastFitWidth;
    int           _lastFitHeight;
};

//----------------------------------------------

/**
 * @brief A unique color for the author's name (stable by name, dark tones).
 */
namespace AuthorBadge
{
QString normalizeKey(QString author);
QColor colorFor(QString authorKey);
QString titleHtml(QString title, QString authorKey = QString(),
                  QString linkUrl = QString());
}

//----------------------------------------------

/**
 * @brief A visual chat-feed item (not necessarily the same as model history).
 */
struct ChatDisplayItem
{
    QString kind;
    QString title;
    QString text;
    QString authorKey;
};

//----------------------------------------------

/**
 * @brief Conversation widget: message feed, context meter, and input.
 *
 * The visual history is not erased when the model context is compressed.
 * The optimizer changes only what is sent to the API.
 */
class ConversationWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ConversationWidget(QWidget *parent = 0);

    void setAgent(Agent *agent);
    void clearMessages();
    QList<ChatDisplayItem> displayLog() const;
    void restoreDisplayLog(const QList<ChatDisplayItem> &items);
    void restoreFromHistory(const QList<LlmMessage> &history);
    void setUserInputEnabled(bool enabled);
    bool userInputEnabled() const;
    /**
     * @brief Author captions in the feed, as in the journal: the user and this worker.
     */
    void setAuthorLabels(QString userAuthor, QString assistantAuthor);
    void showPermissionRequest(QString action, bool offerAlways = false);
    void hidePermissionRequest();
    void showConsoleInput(QString transcript);
    void showUserQuestion(QString question);
    void showContinuationRequest(QString error);
    void showToolBudgetRequest(int chunk);
    void showSubtreeBudgetRequest(QString text);
    void setStopEnabled(bool enabled);
    void setReportToParentVisible(bool visible);
    void applyTheme();
    bool hasActivePrompt() const;
    /** Readable plan text from a plan_work call. Empty when the call has no plan. */
    static QString formatPlanWorkBody(const QString &arguments);
    /**
     * @brief Worker state that overrides the agent's reading
     *        (waiting for children, permission, or the queue). Empty clears it.
     */
    void setExternalActivity(QString text);
    /** Refresh the activity line and buttons if work started before the chat was opened. */
    void syncRunningState();
    /** Scroll to the end when this chat is pinned to the latest message. */
    void revealEnd();
    /** Hide the chat activity line (the journal has its own line). */
    void setChatLiveActionVisible(bool visible);

signals:
    void sendRequested(const QString &text);
    void stopRequested();
    void workerInterruptRequested();
    void reportToParentRequested();
    void compactRequested();
    void permissionAnswered(bool approved, bool always, const QString &comment);
    void consoleInputAnswered(const QString &line, bool sent);
    void continuationAnswered(bool retry);
    void subtreeBudgetAnswered(bool granted);
    void messageLinkActivated(const QUrl &url);

public slots:
    void retranslateUi();
    void sltBusyChanged(bool busy);
    void sltCompactingChanged(bool compacting);

protected:
    virtual bool eventFilter(QObject *watched, QEvent *event);

private slots:
    void sltSend();
    void sltUserMessage(const QString &text);
    void sltAssistantMessage(const QString &text);
    void sltAssistantRetracted(const QString &text);
    void sltToolCallStarted(const QString &name, const QString &arguments,
                            const QString &forUser);
    void sltToolCallFinished(const QString &name, const QString &result, bool ok);
    void sltNotice(const QString &text);
    void sltForwardedContext(const QString &text);
    void sltFailed(const QString &error);
    void sltScrollToBottom();
    void sltScrollRangeChanged(int min, int max);
    void sltScrollValueChanged(int value);
    void sltPermissionApprove();
    void sltPermissionAlways();
    void sltPermissionReject();
    void sltAutoApproveToggled(bool checked);
    void sltDisplayOptionsChanged();
    void sltToolBudgetExhausted(int chunk);
    void sltActivityTick();

protected:
    QWidget *addBubble(QString title, QString html, QString preview, QString objectName,
                       bool alignRight, bool collapsed, QString authorKey = QString());
    QWidget *addPlainBubble(QString title, QString text, QString objectName, bool alignRight,
                            bool forceCollapsed = false, bool forceExpanded = false,
                            QString authorKey = QString());
    void removeBubble(QWidget *bubble);
    void scheduleScrollToBottom();
    bool isNearBottom() const;
    bool isRestoring() const { return _restoring; }
    void setPinBottom(bool pin) { _pinBottom = pin; }
    bool pinBottom() const { return _pinBottom; }
    QWidget *messagesHost() const { return _messagesHost; }
    QVBoxLayout *messagesLayout() const { return _messagesLayout; }
    void ensureLiveActionAtEnd();
    void applyChatLiveActionStyle();

private:
    void recordDisplay(QString kind, QString title, QString text,
                       QString authorKey = QString());
    void replayDisplayItem(const ChatDisplayItem &item);
    /** Consecutive function calls share one collapsed card. */
    void appendServiceToolCall(const QString &kind, const QString &body);
    ChatMessageFrame *lastMessageFrame() const;
    QList<ChatDisplayItem> coalesceFeed(const QList<ChatDisplayItem> &items) const;
    void trimOldBubbles();
    void setActivity(QString text);
    void refreshActivity();
    QString userAuthorLabel() const;
    QString assistantAuthorLabel() const;
    /** Bubble title from a stable kind, so a language change can rebuild it. */
    QString titleForKind(const QString &kind) const;
    /** Normalize the kind of old feed records to a stable code. */
    QString normalizeDisplayKind(const ChatDisplayItem &item) const;
    void refreshDisplayTitles();

    Agent          *_agent;
    QLabel         *_liveActionLine;
    QTimer         *_activityTimer;
    QElapsedTimer   _activityClock;
    QString         _activityText;
    QString         _externalActivity;
    QString         _shownActivity;
    QString         _userAuthor;
    QString         _assistantAuthor;
    QScrollArea    *_scroll;
    QWidget        *_messagesHost;
    QVBoxLayout    *_messagesLayout;
    QPlainTextEdit *_input;
    QPushButton    *_sendButton;
    QCheckBox      *_autoApproveBox;
    QPushButton    *_stopButton;
    QPushButton    *_reportButton;
    QWidget        *_reportHost;
    QWidget        *_inputHost;
    QWidget        *_permissionHost;
    QLabel         *_permissionLabel;
    QLineEdit      *_consoleEdit;
    QPushButton    *_approveButton;
    QPushButton    *_alwaysButton;
    QPushButton    *_rejectButton;
    int             _promptMode;
    bool            _pinBottom;
    QString         _pendingToolName;
    QString         _pendingToolArgs;
    QString         _pendingForUser;
    QList<ChatDisplayItem> _displayLog;
    bool            _restoring;
    bool            _userInputEnabled;
    bool            _scrollScheduled;
    bool            _adjustingScroll;
    bool            _scrollRetried;
};

#endif // CONVERSATIONWIDGET_H
