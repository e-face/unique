#ifndef AGENT_H
#define AGENT_H

#include "appsettings.h"
#include "contextoptimizer.h"
#include "llmtypes.h"
#include "workertypes.h"
//----------------------------------------------
#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QStringList>

class AgentTool;
class LlmClient;

//----------------------------------------------

/** Where a user-role message came from (chat feed / journal). */
enum AgentUserIngress
{
    AgentUserIngressHuman      = 0, /**< A real user. */
    AgentUserIngressAssignment = 1, /**< Task from the parent / task start. */
    AgentUserIngressFollowUp   = 2, /**< Clarification from the parent. */
    AgentUserIngressGroup      = 3, /**< A turn in a group discussion. */
    AgentUserIngressSystem     = 4  /**< Service ping (not for the journal). */
};

//----------------------------------------------

/**
 * @brief Agent: dialogue, function calls, and keeping history inside the context window.
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
    bool hasTool(QString name) const;
    QStringList toolNames() const;
    /** Drop installed tools whose names are not in the list. */
    void retainTools(const QStringList &names);
    /**
     * @brief Call an installed tool directly (media executor and tests).
     */
    QString invokeTool(QString name, QString argumentsJson, bool *ok);
    /**
     * @brief Note in the chat/journal (media-tool progress and similar).
     */
    void postNotice(QString text);
    /**
     * @brief Record a media-executor turn in history and the feed without calling the LLM.
     */
    void recordLocalExchange(QString userText, QString assistantText, int ingress = 1);
    void setSystemPrompt(QString prompt);
    QString systemPrompt() const;
    /** Write the current system prompt into an already loaded history. */
    void rewriteSystemPrompt();
    void setProjectDirectory(QString path);
    QString projectDirectory() const;
    void setMaxToolIterations(int count);
    /**
     * @brief Extra context ceiling (tokens). 0 means the model window.
     */
    void setContextCap(int tokens);
    /**
     * @brief Limits from the model profile: function calls and the context ceiling.
     */
    void applyProfileLimits(const LlmProfile &profile);
    /**
     * @brief Pin the original task wording: context compression keeps it
     *        verbatim in history instead of folding it into the summary.
     */
    void setTaskAnchor(QString text);
    QString taskAnchor() const;

    /**
     * @brief When enabled, each request and each reply is shown via
     *        inspectRequested() before the exchange with the model continues.
     */
    void setInspectIo(bool enabled);
    bool inspectIo() const;
    /**
     * @brief When false, this agent does not ask the model to reason.
     *        Tools stay off: a long chain of thought is what makes a tool look stuck.
     */
    void setUseReasoning(bool enabled);

    bool isBusy() const;
    /**
     * @brief A tool invoke() is on the stack (nested event loop).
     *        The agent must not be deleted at that moment.
     */
    bool isInsideToolCall() const;
    bool hasPendingContinue() const;
    int  estimatedPromptTokens() const;
    int  contextLimit() const;
    /** A media worker does not track a context window: no compression and no counting. */
    bool tracksContext() const;
    QList<LlmMessage> history() const;
    void setHistory(const QList<LlmMessage> &history);
    void requestStopAfterTools();
    void appendContextNote(QString text);
    /**
     * @brief Add text to the model history without showing it in the chat.
     */
    void injectContext(QString text);
    void holdForExternal();
    void sealIncompleteToolCalls(QString reason);
    /**
     * @brief Waiting for the user: continue with more calls, or stop.
     */
    bool isWaitingToolBudget() const;
    int  toolBudgetChunk() const;

    /**
     * @brief Source of the last user-role message (journal / feed).
     */
    int lastUserIngress() const;
    /** Who sent the last user turn (short worker name, or the user). */
    QString turnAuthor() const;
    void setTurnAuthor(const QString &name);
    /** Who this worker replies to in the journal: the parent or the user. */
    QString journalReplyTo() const;
    void setJournalReplyTo(const QString &name);

public slots:
    /**
     * @param ingress AgentUserIngress*: Human / Assignment / FollowUp / Group / System.
     */
    void sendUserMessage(QString text, int ingress = 0);
    /**
     * @brief User message with multimodal content (text + image_url and similar).
     * @a text is the caption for the UI and summaries; @a contentParts is sent to the API as content[].
     */
    void sendUserMultimodal(QString text, QJsonArray contentParts, int ingress = 0);
    /**
     * @brief Answer when the function-call limit is exhausted.
     * @param continueWork true — add another chunk of calls and continue;
     *                     false — ask for a final reply / complete_task.
     */
    void resolveToolBudget(bool continueWork);
    /**
     * @brief First fold the accumulated history into a model summary,
     *        then send the message. A child worker then receives a new
     *        parent task with a clean context that has not forgotten the past.
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
    /** Service note: chat feed only, not the journal. */
    void serviceNotice(const QString &text);
    /** Context forwarded between workers (child reports and similar). */
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
     * @brief Function-call limit reached: ask to continue (+chunk) or stop.
     */
    void toolBudgetExhausted(int chunk);
    /**
     * @brief History changed and should be saved to disk.
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
    void beginUserTurn(QString text, int ingress, QJsonArray contentParts = QJsonArray());
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
    void beginModelWork();
    void endModelWork();
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
    QString            _turnAuthor;
    QString            _journalReplyTo;
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
    bool               _tracksContext;
    int                _lastEstimated;
    int                _modelWorkId;
    bool               _busy;
    bool               _compactThenChat;
    bool               _overflowRetried;
    bool               _inspectIo;
    bool               _useReasoning;
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
