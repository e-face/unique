#ifndef CONVERSATIONWIDGET_H
#define CONVERSATIONWIDGET_H

#include "llmtypes.h"
//----------------------------------------------
#include <QElapsedTimer>
#include <QFrame>
#include <QList>
#include <QString>
#include <QWidget>

class Agent;
class QLabel;
class QTimer;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QTextBrowser;
class QVBoxLayout;
class QWidget;

//----------------------------------------------

/**
 * @brief Карточка одного сообщения: один HTML-документ, без лейбла на строку.
 */
class ChatMessageFrame : public QFrame
{
    Q_OBJECT
public:
    ChatMessageFrame(QString title, QString html, QString preview,
                     QString objectName, bool alignRight, bool collapsed,
                     QWidget *parent = 0);
    void applyTheme();

protected:
    virtual void resizeEvent(QResizeEvent *event);

private slots:
    void sltToggle();
    void sltFitDocumentHeight();

private:
    void applyCollapsed();
    void fitDocumentHeight();
    QString collapsedHtml() const;
    QString expandedHtml() const;

    QString      _title;
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
 * @brief Элемент визуальной ленты чата (не обязательно совпадает с историей модели).
 */
struct ChatDisplayItem
{
    QString kind;
    QString title;
    QString text;
};

//----------------------------------------------

/**
 * @brief Виджет беседы: лента сообщений, шкала контекста, ввод.
 *
 * Визуальная история не стирается при сжатии контекста модели —
 * оптимизатор меняет только то, что уходит в API.
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
    void showPermissionRequest(QString action, bool offerAlways = false);
    void hidePermissionRequest();
    void showContinuationRequest(QString error);
    void setStopEnabled(bool enabled);
    void setReportToParentVisible(bool visible);
    void applyTheme();
    bool hasActivePrompt() const;
    void setContextRowVisible(bool visible);
    /**
     * @brief Состояние воркера, перекрывающее показания агента
     *        (ожидание детей, разрешения, очередь). Пусто — снять.
     */
    void setExternalActivity(QString text);

signals:
    void sendRequested(const QString &text);
    void stopRequested();
    void workerInterruptRequested();
    void reportToParentRequested();
    void compactRequested();
    void permissionAnswered(bool approved, bool always, const QString &comment);
    void continuationAnswered(bool retry);

public slots:
    void sltBusyChanged(bool busy);
    void sltContextUsageChanged(int usedTokens, int limitTokens);
    void sltCompactingChanged(bool compacting);

protected:
    virtual bool eventFilter(QObject *watched, QEvent *event);

private slots:
    void sltSend();
    void sltUserMessage(const QString &text);
    void sltAssistantMessage(const QString &text);
    void sltToolCallStarted(const QString &name, const QString &arguments);
    void sltToolCallFinished(const QString &name, const QString &result, bool ok);
    void sltNotice(const QString &text);
    void sltFailed(const QString &error);
    void sltScrollToBottom();
    void sltScrollRangeChanged(int min, int max);
    void sltPermissionApprove();
    void sltPermissionAlways();
    void sltPermissionReject();
    void sltActivityTick();

protected:
    QWidget *addBubble(QString title, QString html, QString preview, QString objectName,
                       bool alignRight, bool collapsed);
    QWidget *addPlainBubble(QString title, QString text, QString objectName, bool alignRight,
                            bool forceCollapsed = false, bool forceExpanded = false);
    void removeBubble(QWidget *bubble);

private:
    void recordDisplay(QString kind, QString title, QString text);
    void replayDisplayItem(const ChatDisplayItem &item);
    void trimOldBubbles();
    void scheduleScrollToBottom();
    void setActivity(QString text);
    void refreshActivity();

    Agent          *_agent;
    QLabel         *_contextLabel;
    QLabel         *_activityLabel;
    QTimer         *_activityTimer;
    QElapsedTimer   _activityClock;
    QString         _activityText;
    QString         _externalActivity;
    QString         _shownActivity;
    QProgressBar   *_contextBar;
    QPushButton    *_compactButton;
    QScrollArea    *_scroll;
    QWidget        *_messagesHost;
    QVBoxLayout    *_messagesLayout;
    QPlainTextEdit *_input;
    QPushButton    *_sendButton;
    QPushButton    *_stopButton;
    QPushButton    *_interruptButton;
    QPushButton    *_reportButton;
    QWidget        *_inputHost;
    QWidget        *_permissionHost;
    QLabel         *_permissionLabel;
    QPushButton    *_approveButton;
    QPushButton    *_alwaysButton;
    QPushButton    *_rejectButton;
    int             _promptMode;
    bool            _pinBottom;
    QString         _pendingToolName;
    QString         _pendingToolArgs;
    QList<ChatDisplayItem> _displayLog;
    bool            _restoring;
    bool            _userInputEnabled;
    bool            _scrollScheduled;
};

#endif // CONVERSATIONWIDGET_H
