#ifndef AGENT_H
#define AGENT_H

#include "contextoptimizer.h"
#include "llmtypes.h"
#include "workertypes.h"
//----------------------------------------------
#include <QList>
#include <QObject>
#include <QStringList>

class AgentTool;
class LlmClient;

//----------------------------------------------

/** Источник сообщения роли user (лента/журнал). */
enum AgentUserIngress
{
    AgentUserIngressHuman      = 0, /**< Реальный пользователь. */
    AgentUserIngressAssignment = 1, /**< Задание от родителя / старт задачи. */
    AgentUserIngressFollowUp   = 2, /**< Уточнение от родителя. */
    AgentUserIngressGroup      = 3, /**< Ход группового обсуждения. */
    AgentUserIngressSystem     = 4  /**< Служебный пинг (не для журнала). */
};

//----------------------------------------------

/**
 * @brief Агент: диалог, вызов функций и удержание истории в окне контекста.
 */
class Agent : public QObject
{
    Q_OBJECT
public:
    explicit Agent(QObject *parent = 0);
    virtual ~Agent();

    void setClient(LlmClient *client);
    LlmClient *client() const;

    void addTool(AgentTool *tool);
    void clearTools();
    void setSystemPrompt(QString prompt);
    QString systemPrompt() const;
    void setProjectDirectory(QString path);
    QString projectDirectory() const;
    void setMaxToolIterations(int count);
    /**
     * @brief Дополнительный потолок контекста (токены). 0 — окно модели.
     */
    void setContextCap(int tokens);
    /**
     * @brief Бюджет роли: лимит функций и потолок контекста.
     */
    void applyRoleSettings(WorkerType type);
    /**
     * @brief Закрепить исходную формулировку задачи: при сжатии контекста она
     *        не сворачивается в сводку, а остаётся в истории дословно.
     */
    void setTaskAnchor(QString text);
    QString taskAnchor() const;

    /**
     * @brief Если включено, каждый запрос и каждый ответ показываются
     *        сигналом inspectRequested() до продолжения обмена с моделью.
     */
    void setInspectIo(bool enabled);
    bool inspectIo() const;

    bool isBusy() const;
    /**
     * @brief Сейчас на стеке выполняется invoke() инструмента (вложенный
     *        цикл событий). Удалять агента в этот момент нельзя.
     */
    bool isInsideToolCall() const;
    bool hasPendingContinue() const;
    int  estimatedPromptTokens() const;
    int  contextLimit() const;
    QList<LlmMessage> history() const;
    void setHistory(const QList<LlmMessage> &history);
    void requestStopAfterTools();
    void appendContextNote(QString text);
    /**
     * @brief Добавить текст в историю модели, не показывая его в чате.
     */
    void injectContext(QString text);
    void holdForExternal();
    void sealIncompleteToolCalls(QString reason);
    /**
     * @brief Ждём ответа пользователя: продолжить с доп. вызовами или прекратить.
     */
    bool isWaitingToolBudget() const;
    int  toolBudgetChunk() const;

    /**
     * @brief Источник последнего сообщения роли user (для журнала/ленты).
     */
    int lastUserIngress() const;

public slots:
    /**
     * @param ingress AgentUserIngress*: Human / Assignment / FollowUp / Group / System.
     */
    void sendUserMessage(QString text, int ingress = 0);
    /**
     * @brief Ответ на исчерпание лимита вызовов функций.
     * @param continueWork true — добавить ещё столько же вызовов и продолжить;
     *                     false — запросить финальный ответ / complete_task.
     */
    void resolveToolBudget(bool continueWork);
    /**
     * @brief Сначала свернуть всю накопленную историю в сводку силами модели,
     *        затем отправить сообщение. Так дочерний воркер получает новую
     *        задачу от родителя с чистым, но не забытым контекстом.
     */
    void sendUserMessageAfterCompact(QString text, int ingress = 0);
    void continueChat();
    void compactContext();
    void abort();
    void clearHistory();
    void refreshContextUsage();

signals:
    void userMessage(const QString &text);
    void assistantMessage(const QString &text);
    void toolCallStarted(const QString &name, const QString &arguments);
    void toolCallFinished(const QString &name, const QString &result, bool ok);
    void notice(const QString &text);
    /** Пересланный между воркерами контекст (отчёты детей и т.п.). */
    void forwardedContext(const QString &text);
    void failed(const QString &error);
    void busyChanged(bool busy);
    void compactingChanged(bool compacting);
    void contextUsageChanged(int usedTokens, int limitTokens);
    void usageUpdated(int promptTokens, int completionTokens, int totalTokens,
                      int cachedPromptTokens);
    void inspectRequested(const QString &title, const QString &text);
    void toolBatchFinished();
    /**
     * @brief Лимит вызовов функций исчерпан: спросить продолжить (+chunk) или прекратить.
     */
    void toolBudgetExhausted(int chunk);
    /**
     * @brief История изменилась и её стоит сохранить на диск.
     */
    void historyChanged();

private slots:
    void sltResponseReceived(const LlmResponse &response);
    void sltClientFailed(const QString &error);

private:
    enum Phase
    {
        PhaseIdle,
        PhaseChat,
        PhaseCompact
    };

    enum CompactMode
    {
        CompactRegular,
        CompactEmergency,
        CompactFull
    };

    void resetHistory();
    void setBusy(bool busy);
    void setPhase(Phase phase);
    void maybeOptimizeThenComplete();
    void runCompact(CompactMode mode);
    void requestNextSummaryChunk();
    void applyCompactResult(QString summary);
    void finishCompactFallback(QString reason);
    void continueAfterCompact();
    void clearCompactState();
    void beginUserTurn(QString text, int ingress);
    QList<LlmMessage> anchorMessages() const;
    bool isAnchorMessage(const LlmMessage &message) const;
    void requestCompletion();
    void emitContextUsage();
    void publishContextUsage();
    QList<LlmTool> toolList() const;
    LlmTool toolDescription(AgentTool *tool) const;
    AgentTool *findTool(QString name) const;
    void handleToolCalls(LlmMessage message);
    void flushPendingNotes();
    void ensureToolCallIds(LlmMessage *message) const;
    bool isBrokenToolHistoryError(QString error) const;
    QString effectiveSystemPrompt() const;
    bool historyIsOnlySystem() const;
    QList<LlmTool> toolsForRequest() const;
    void gateInspect(QString title, QString text);
    QString requestAsText(const LlmRequest &request) const;
    QString responseAsText(const LlmResponse &response) const;

    LlmClient         *_client;
    QList<AgentTool *> _tools;
    QList<LlmMessage>  _history;
    QList<LlmMessage>  _pendingRecent;
    QStringList        _summaryChunks;
    QString            _runningSummary;
    QString            _pendingUserMessage;
    int                _pendingUserIngress;
    int                _lastUserIngress;
    QString            _taskAnchor;
    QString            _followUpAnchor;
    int                _summaryChunkTotal;
    ContextOptimizer   _optimizer;
    QString            _systemPrompt;
    QString            _projectDirectory;
    Phase              _phase;
    int                _maxToolIterations;
    int                _toolBudgetChunk;
    int                _toolIterations;
    int                _contextCap;
    int                _lastEstimated;
    bool               _busy;
    bool               _compactThenChat;
    bool               _overflowRetried;
    bool               _inspectIo;
    bool               _stopAfterTools;
    bool               _heldForExternal;
    bool               _waitingToolBudget;
    bool               _executingTools;
    int                _toolCallDepth;
    bool               _repairedToolHistory;
    bool               _pendingContinueChat;
    bool               _finalAnswerRound;
    QStringList        _pendingNotes;
    mutable bool       _estimateDirty;
    mutable int        _estimateCache;
};

#endif // AGENT_H
