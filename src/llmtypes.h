#ifndef LLMTYPES_H
#define LLMTYPES_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>

//----------------------------------------------

/**
 * @brief Role of a message in the dialogue with the language model.
 */
enum LlmRole
{
    LlmRoleSystem,
    LlmRoleUser,
    LlmRoleAssistant,
    LlmRoleTool
};

//----------------------------------------------

/**
 * @brief A function call requested by the model.
 */
struct LlmToolCall
{
    LlmToolCall()
    {
    }

    QString id;
    QString name;
    QString arguments;
};

//----------------------------------------------

/**
 * @brief A dialogue message.
 *
 * @a content is the text (UI, summaries, token heuristic).
 * @a contentParts is multimodal content for Chat Completions (text / image_url);
 *                   if non-empty, the API receives a parts array instead of the content string.
 */
struct LlmMessage
{
    LlmMessage()
        : role(LlmRoleUser)
        , hideInUi(false)
    {
    }

    LlmRole            role;
    QString            content;
    QJsonArray         contentParts;
    QString            name;
    QString            toolCallId;
    QString            reasoningContent;
    QList<LlmToolCall> toolCalls;
    /**
     * @brief Service context for the model (injectContext): it goes to the API
     *        and is not shown in the chat feed.
     */
    bool               hideInUi;
};

//----------------------------------------------

/**
 * @brief Description of a tool (function) for the model.
 *
 * The @a parameters field is a JSON Schema of the arguments object.
 */
struct LlmTool
{
    LlmTool()
    {
    }

    QString     name;
    QString     description;
    QJsonObject parameters;
};

//----------------------------------------------

/**
 * @brief A request to the language model.
 *
 * @a temperature below 0 means the parameter is not sent.
 * @a maxTokens of 0 means no explicit limit.
 */
struct LlmRequest
{
    LlmRequest()
        : temperature(-1.0)
        , maxTokens(0)
        , reasoning(false)
    {
    }

    QList<LlmMessage> messages;
    QList<LlmTool>    tools;
    QString           model;
    double            temperature;
    int               maxTokens;
    /** Checkbox in the input field: the model reasons first. Off by default. */
    bool              reasoning;
};

//----------------------------------------------

/**
 * @brief Token accounting in the provider response.
 */
struct LlmUsage
{
    LlmUsage()
        : promptTokens(0)
        , completionTokens(0)
        , totalTokens(0)
        , cachedPromptTokens(0)
    {
    }

    int promptTokens;
    int completionTokens;
    int totalTokens;
    /** Part of the prompt tokens served by the provider from the prefix cache (cheaper). */
    int cachedPromptTokens;
};

//----------------------------------------------

/**
 * @brief The language model's reply.
 */
struct LlmResponse
{
    LlmResponse()
    {
    }

    LlmMessage message;
    QString    finishReason;
    LlmUsage   usage;
};

//----------------------------------------------

inline QString llmRoleToString(LlmRole role)
{
    switch (role)
    {
    case LlmRoleSystem:
        return QString::fromUtf8("system");
    case LlmRoleUser:
        return QString::fromUtf8("user");
    case LlmRoleAssistant:
        return QString::fromUtf8("assistant");
    case LlmRoleTool:
        return QString::fromUtf8("tool");
    }
    return QString::fromUtf8("user");
}

//----------------------------------------------

inline LlmRole llmRoleFromString(QString role)
{
    if (role == QString::fromUtf8("system"))
        return LlmRoleSystem;
    if (role == QString::fromUtf8("assistant"))
        return LlmRoleAssistant;
    if (role == QString::fromUtf8("tool"))
        return LlmRoleTool;
    return LlmRoleUser;
}

//----------------------------------------------

Q_DECLARE_METATYPE(LlmToolCall)
Q_DECLARE_METATYPE(LlmMessage)
Q_DECLARE_METATYPE(LlmResponse)

#endif // LLMTYPES_H
