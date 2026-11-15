#ifndef CONTEXTOPTIMIZER_H
#define CONTEXTOPTIMIZER_H

#include "llmtypes.h"
//----------------------------------------------
#include <QList>
#include <QString>
#include <QStringList>

//----------------------------------------------

/**
 * @brief Token estimate and preparing history for compression by the model.
 *
 * The extract itself is made by the language model, the same way for every worker:
 * the system prompt and the pinned task wording stay as they are,
 * the fresh tail (by token budget) is kept verbatim, and everything else
 * goes to the model in chunks and comes back as one summary. The text extract
 * (extractiveSummary) is only a fallback when the model is unavailable.
 */
class ContextOptimizer
{
public:
    ContextOptimizer();

    void calibrate(int actualPromptTokens, int estimatedPromptTokens);
    void reset();
    int  estimateText(QString text) const;
    int  estimateMessage(const LlmMessage &message) const;
    int  estimateMessages(const QList<LlmMessage> &messages) const;
    int  estimateTools(const QList<LlmTool> &tools) const;
    int  estimatePrompt(const QList<LlmMessage> &messages, const QList<LlmTool> &tools) const;

    /**
     * @brief Index of the first tail message that fits in @p tokenBudget.
     *
     * Messages are counted from the end; @p firstMovable is the first index that
     * may be compressed at all (after the system prompt). A budget of 0 means no tail,
     * and the list size is returned.
     */
    int tailKeepIndex(const QList<LlmMessage> &messages, int firstMovable, int tokenBudget) const;
    /**
     * @brief Move the boundary back so a tool_call/tool pair is not split.
     */
    int expandKeepIndexForTools(const QList<LlmMessage> &messages, int keepFrom) const;
    void repairToolTurns(QList<LlmMessage> *messages) const;

    /**
     * @brief Split the old part of history into text chunks for the model.
     *
     * Each chunk is no longer than @p chunkTokenBudget tokens (by estimate);
     * very long individual messages are truncated so they fit.
     */
    QStringList splitForSummary(const QList<LlmMessage> &oldPart, int chunkTokenBudget) const;
    /**
     * @brief Model request to summarize the next chunk.
     * @param previousSummary Summary of the previous chunks (empty for the first).
     * @param chunk           Text of the chunk.
     * @param index           Chunk number (from 1); @p total is the number of chunks.
     * @param anchorKept      The task wording is stored separately and must not be repeated.
     */
    QList<LlmMessage> buildSummarizeMessages(QString previousSummary, QString chunk,
                                             int index, int total, bool anchorKept) const;
    QString extractiveSummary(const QList<LlmMessage> &oldPart, int maxChars) const;
    LlmMessage makeSummaryMessage(QString summary) const;
    QList<LlmMessage> rebuildHistory(const LlmMessage &systemPrompt,
                                     const QList<LlmMessage> &anchors,
                                     QString summary,
                                     const QList<LlmMessage> &recent) const;

    static bool isContextOverflowError(QString error);

private:
    QString messageAsPlain(const LlmMessage &message, int maxChars) const;

    double _scale;
};

#endif // CONTEXTOPTIMIZER_H
