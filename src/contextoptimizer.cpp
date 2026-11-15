#include "contextoptimizer.h"
//----------------------------------------------
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>

//----------------------------------------------

static const int kMessageOverheadTokens = 8;
/** A function result longer than this is truncated in the summary. */
static const int kSummaryToolClipChars  = 6000;
/** Other messages (model replies, utterances) are cut more gently. */
static const int kSummaryTextClipChars  = 20000;

//----------------------------------------------

ContextOptimizer::ContextOptimizer()
    : _scale(1.0)
{
}

//----------------------------------------------

void ContextOptimizer::calibrate(int actualPromptTokens, int estimatedPromptTokens)
{
    if (actualPromptTokens <= 0 || estimatedPromptTokens <= 0)
        return;
    const double sample = double(actualPromptTokens) / double(estimatedPromptTokens);
    _scale = 0.65 * _scale + 0.35 * sample;
    if (_scale < 0.5)
        _scale = 0.5;
    if (_scale > 2.5)
        _scale = 2.5;
}

//----------------------------------------------

void ContextOptimizer::reset()
{
    _scale = 1.0;
}

//----------------------------------------------

int ContextOptimizer::estimateText(QString text) const
{
    if (text.isEmpty())
        return 0;
    int ascii = 0;
    int other = 0;
    for (int i = 0; i < text.size(); ++i)
    {
        if (text.at(i).unicode() < 128)
            ++ascii;
        else
            ++other;
    }
    const int raw = (ascii + 3) / 4 + (other + 1) / 2;
    return qMax(1, int(raw * _scale + 0.5));
}

//----------------------------------------------

int ContextOptimizer::estimateMessage(const LlmMessage &message) const
{
    int tokens = kMessageOverheadTokens;
    tokens += estimateText(message.content);
    tokens += estimateText(message.reasoningContent);
    tokens += estimateText(message.name);
    tokens += estimateText(message.toolCallId);
    // Multimodal: a rough estimate of image_url (base64 is about 0.75 bytes per char, about 1 token per 4 chars).
    for (int i = 0; i < message.contentParts.size(); ++i)
    {
        const QJsonObject part = message.contentParts.at(i).toObject();
        const QString type = part.value(QString::fromUtf8("type")).toString();
        if (type == QString::fromUtf8("text"))
            tokens += estimateText(part.value(QString::fromUtf8("text")).toString());
        else if (type == QString::fromUtf8("image_url"))
        {
            const QString url = part.value(QString::fromUtf8("image_url")).toObject()
                                    .value(QString::fromUtf8("url")).toString();
            // Upper bound of about 1024 tokens per image on DeepSeek Flash; do not inflate the estimate.
            tokens += qMin(1200, qMax(256, url.size() / 64));
        }
        else
            tokens += 64;
    }
    foreach (const LlmToolCall &call, message.toolCalls)
    {
        tokens += 12;
        tokens += estimateText(call.name);
        tokens += estimateText(call.arguments);
    }
    return tokens;
}

//----------------------------------------------

int ContextOptimizer::estimateMessages(const QList<LlmMessage> &messages) const
{
    int tokens = 0;
    foreach (const LlmMessage &message, messages)
        tokens += estimateMessage(message);
    return tokens;
}

//----------------------------------------------

int ContextOptimizer::estimateTools(const QList<LlmTool> &tools) const
{
    int tokens = 0;
    foreach (const LlmTool &tool, tools)
    {
        tokens += 16;
        tokens += estimateText(tool.name);
        tokens += estimateText(tool.description);
        const QByteArray json = QJsonDocument(tool.parameters).toJson(QJsonDocument::Compact);
        tokens += estimateText(QString::fromUtf8(json));
    }
    return tokens;
}

//----------------------------------------------

int ContextOptimizer::estimatePrompt(const QList<LlmMessage> &messages, const QList<LlmTool> &tools) const
{
    return estimateMessages(messages) + estimateTools(tools);
}

//----------------------------------------------

int ContextOptimizer::tailKeepIndex(const QList<LlmMessage> &messages, int firstMovable,
                                    int tokenBudget) const
{
    const int size = messages.size();
    if (firstMovable < 0)
        firstMovable = 0;
    if (tokenBudget <= 0)
        return size;
    int used = 0;
    int keepFrom = size;
    for (int i = size - 1; i >= firstMovable; --i)
    {
        const int cost = estimateMessage(messages.at(i));
        if (used + cost > tokenBudget)
            break;
        used += cost;
        keepFrom = i;
    }
    return keepFrom;
}

//----------------------------------------------

int ContextOptimizer::expandKeepIndexForTools(const QList<LlmMessage> &messages, int keepFrom) const
{
    if (keepFrom <= 0 || keepFrom >= messages.size())
        return keepFrom;

    int adjusted = keepFrom;
    while (adjusted > 0 && messages.at(adjusted).role == LlmRoleTool)
        --adjusted;

    for (int i = adjusted; i < messages.size(); ++i)
    {
        if (messages.at(i).role != LlmRoleTool)
            continue;
        const QString id = messages.at(i).toolCallId;
        int owner = -1;
        for (int j = i - 1; j >= 0; --j)
        {
            if (messages.at(j).role != LlmRoleAssistant)
                continue;
            if (messages.at(j).toolCalls.isEmpty())
                continue;
            bool match = id.isEmpty();
            foreach (const LlmToolCall &call, messages.at(j).toolCalls)
            {
                if (!id.isEmpty() && call.id == id)
                    match = true;
            }
            if (match || owner < 0)
                owner = j;
            break;
        }
        if (owner >= 0 && owner < adjusted)
            adjusted = owner;
    }
    return adjusted;
}

//----------------------------------------------

void ContextOptimizer::repairToolTurns(QList<LlmMessage> *messages) const
{
    if (!messages || messages->isEmpty())
        return;

    const QList<LlmMessage> source = *messages;
    QList<int> used;
    used.reserve(source.size());
    for (int i = 0; i < source.size(); ++i)
        used.append(0);

    QList<LlmMessage> out;
    int synthetic = 0;
    for (int i = 0; i < source.size(); ++i)
    {
        if (used.at(i))
            continue;
        if (source.at(i).role == LlmRoleTool)
            continue;

        used[i] = 1;
        LlmMessage message = source.at(i);
        if (message.role == LlmRoleAssistant)
        {
            for (int c = 0; c < message.toolCalls.size(); ++c)
            {
                if (!message.toolCalls.at(c).id.trimmed().isEmpty())
                    continue;
                ++synthetic;
                message.toolCalls[c].id = QString::fromUtf8("call_%1_%2")
                                              .arg(out.size())
                                              .arg(synthetic);
            }
        }
        out.append(message);

        if (message.role != LlmRoleAssistant || message.toolCalls.isEmpty())
            continue;

        foreach (const LlmToolCall &call, message.toolCalls)
        {
            int found = -1;
            if (!call.id.isEmpty())
            {
                for (int j = 0; j < source.size(); ++j)
                {
                    if (used.at(j) || source.at(j).role != LlmRoleTool)
                        continue;
                    if (source.at(j).toolCallId == call.id)
                    {
                        found = j;
                        break;
                    }
                }
            }
            if (found < 0 && !call.name.isEmpty())
            {
                for (int j = i + 1; j < source.size(); ++j)
                {
                    if (used.at(j) || source.at(j).role != LlmRoleTool)
                        continue;
                    if (source.at(j).name == call.name)
                    {
                        found = j;
                        break;
                    }
                }
            }

            LlmMessage toolMessage;
            if (found >= 0)
            {
                used[found] = 1;
                toolMessage = source.at(found);
            }
            else
            {
                toolMessage.role = LlmRoleTool;
                toolMessage.content = QString::fromUtf8(
                    "Tool result is missing from history.");
            }
            if (toolMessage.toolCallId.isEmpty())
                toolMessage.toolCallId = call.id;
            if (toolMessage.name.isEmpty())
                toolMessage.name = call.name;
            toolMessage.role = LlmRoleTool;
            out.append(toolMessage);
        }
    }

    for (int j = 0; j < source.size(); ++j)
    {
        if (used.at(j) || source.at(j).role != LlmRoleTool)
            continue;
        LlmMessage note;
        note.role = LlmRoleUser;
        QString body = source.at(j).content.trimmed();
        if (body.isEmpty())
            continue;
        note.content = QString::fromUtf8("Tool result:\n") + body;
        out.append(note);
    }

    *messages = out;
}

//----------------------------------------------

QStringList ContextOptimizer::splitForSummary(const QList<LlmMessage> &oldPart,
                                              int chunkTokenBudget) const
{
    QStringList chunks;
    if (chunkTokenBudget < 500)
        chunkTokenBudget = 500;
    // One message must not exceed a chunk: characters per token
    // are about two in the worst case, so a margin is kept.
    const int hardClipChars = chunkTokenBudget * 2;

    QString current;
    int currentTokens = 0;
    foreach (const LlmMessage &message, oldPart)
    {
        const int clip = (message.role == LlmRoleTool)
                         ? qMin(kSummaryToolClipChars, hardClipChars)
                         : qMin(kSummaryTextClipChars, hardClipChars);
        const QString piece = messageAsPlain(message, clip);
        if (piece.isEmpty())
            continue;
        const int cost = estimateText(piece) + 1;
        if (!current.isEmpty() && currentTokens + cost > chunkTokenBudget)
        {
            chunks.append(current);
            current.clear();
            currentTokens = 0;
        }
        current += piece;
        current += QChar('\n');
        currentTokens += cost;
    }
    if (!current.isEmpty())
        chunks.append(current);
    return chunks;
}

//----------------------------------------------

QList<LlmMessage> ContextOptimizer::buildSummarizeMessages(QString previousSummary, QString chunk,
                                                           int index, int total,
                                                           bool anchorKept) const
{
    QList<LlmMessage> messages;
    LlmMessage system;
    system.role = LlmRoleSystem;
    QString rules = QString::fromUtf8(
        "You compress the working history of an autonomous coding agent so it can continue "
        "the same job in a limited context window. Write a dense, factual summary in the "
        "language of the history. Keep everything the agent needs to continue without "
        "redoing work: what was done and in which order; files and paths created, changed, "
        "read or deleted and what is in them now; commands run and their outcomes; errors, "
        "their causes and fixes; decisions and the reasons; facts discovered about the "
        "project (structure, build system, conventions); results reported by child workers "
        "and tools; open questions and what remains to be done. Preserve exact identifiers: "
        "file names, function and class names, worker ids, command lines, error messages. "
        "No filler, no invented details, do not copy the dialogue verbatim.");
    if (anchorKept)
    {
        rules += QString::fromUtf8(
            " The original assignment text is stored separately and stays in the context "
            "verbatim, so do not restate it; summarize the progress on it.");
    }
    system.content = rules;
    messages.append(system);

    LlmMessage user;
    user.role = LlmRoleUser;
    QString body;
    if (!previousSummary.trimmed().isEmpty())
    {
        body += QString::fromUtf8("Summary of the earlier part of the history:\n\n");
        body += previousSummary.trimmed();
        body += QString::fromUtf8("\n\n");
    }
    if (total > 1)
    {
        body += QString::fromUtf8("History, part %1 of %2:\n\n").arg(index).arg(total);
    }
    else
        body += QString::fromUtf8("History to compress:\n\n");
    body += chunk;
    body += QString::fromUtf8("\n\n");
    if (!previousSummary.trimmed().isEmpty())
    {
        body += QString::fromUtf8(
            "Write ONE updated summary that merges the earlier summary with this part. "
            "Output only the summary.");
    }
    else
        body += QString::fromUtf8("Write the compressed summary. Output only the summary.");
    user.content = body;
    messages.append(user);
    return messages;
}

//----------------------------------------------

QString ContextOptimizer::extractiveSummary(const QList<LlmMessage> &oldPart, int maxChars) const
{
    QString text;
    foreach (const LlmMessage &message, oldPart)
    {
        const QString piece = messageAsPlain(message, 1500);
        if (piece.isEmpty())
            continue;
        if (text.size() + piece.size() + 2 > maxChars)
        {
            const int remain = maxChars - text.size() - 2;
            if (remain > 80)
                text += piece.left(remain) + QString::fromUtf8("\n...\n");
            break;
        }
        text += piece;
        text += QChar('\n');
    }
    if (text.isEmpty())
        text = QString::fromUtf8("(no text to summarize)");
    return text;
}

//----------------------------------------------

LlmMessage ContextOptimizer::makeSummaryMessage(QString summary) const
{
    LlmMessage message;
    message.role = LlmRoleSystem;
    message.content = QString::fromUtf8(
        "Compressed summary of the previous conversation "
        "(the context window was full, history was folded, substance kept):\n\n")
                      + summary.trimmed();
    return message;
}

//----------------------------------------------

QList<LlmMessage> ContextOptimizer::rebuildHistory(const LlmMessage &systemPrompt,
                                                   const QList<LlmMessage> &anchors,
                                                   QString summary,
                                                   const QList<LlmMessage> &recent) const
{
    QList<LlmMessage> history;
    history.append(systemPrompt);
    history.append(anchors);
    if (!summary.trimmed().isEmpty())
        history.append(makeSummaryMessage(summary));
    history.append(recent);
    repairToolTurns(&history);
    return history;
}

//----------------------------------------------

bool ContextOptimizer::isContextOverflowError(QString error)
{
    const QString e = error.toLower();
    if (e.contains(QString::fromUtf8("413")))
        return true;
    if (e.contains(QString::fromUtf8("context length")))
        return true;
    if (e.contains(QString::fromUtf8("maximum context")))
        return true;
    if (e.contains(QString::fromUtf8("too many tokens")))
        return true;
    if (e.contains(QString::fromUtf8("reduce the length")))
        return true;
    if (e.contains(QString::fromUtf8("context window")))
        return true;
    if (e.contains(QString::fromUtf8("context_length")))
        return true;
    if (e.contains(QString::fromUtf8("контекст")) && e.contains(QString::fromUtf8("превыш")))
        return true;
    return false;
}

//----------------------------------------------

QString ContextOptimizer::messageAsPlain(const LlmMessage &message, int maxChars) const
{
    QString role;
    switch (message.role)
    {
    case LlmRoleUser:
        role = QString::fromUtf8("User");
        break;
    case LlmRoleAssistant:
        role = QString::fromUtf8("Assistant");
        break;
    case LlmRoleTool:
        role = QString::fromUtf8("Tool");
        if (!message.name.isEmpty())
            role += QString::fromUtf8(" ") + message.name;
        break;
    case LlmRoleSystem:
        role = QString::fromUtf8("System");
        break;
    }
    QString body = message.content.trimmed();
    if (!message.toolCalls.isEmpty())
    {
        QStringList calls;
        foreach (const LlmToolCall &call, message.toolCalls)
        {
            QString args = call.arguments.trimmed();
            if (args.size() > 600)
                args = args.left(600) + QString::fromUtf8("...");
            calls.append(call.name + QString::fromUtf8("(") + args + QString::fromUtf8(")"));
        }
        if (!body.isEmpty())
            body += QChar('\n');
        body += QString::fromUtf8("calls: ") + calls.join(QString::fromUtf8("; "));
    }
    if (body.isEmpty())
        return QString();
    if (maxChars > 0 && body.size() > maxChars)
    {
        // Head and tail: the end of command output is usually the result (error or success).
        const int head = maxChars * 2 / 3;
        const int tail = maxChars - head;
        body = body.left(head)
               + QString::fromUtf8("\n... [%1 chars omitted] ...\n").arg(body.size() - maxChars)
               + body.right(tail);
    }
    return QString::fromUtf8("- ") + role + QString::fromUtf8(": ") + body;
}
