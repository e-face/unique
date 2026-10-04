#ifndef LLMTYPES_H
#define LLMTYPES_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>

//----------------------------------------------

/**
 * @brief Роль сообщения в диалоге с языковой моделью.
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
 * @brief Вызов функции, запрошенный моделью.
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
 * @brief Сообщение диалога.
 *
 * @a content — текст (UI, сводки, эвристика токенов).
 * @a contentParts — multimodal content для Chat Completions (text / image_url);
 *                   если не пусто, в API уходит массив parts вместо строки content.
 */
struct LlmMessage
{
    LlmMessage()
        : role(LlmRoleUser)
    {
    }

    LlmRole            role;
    QString            content;
    QJsonArray         contentParts;
    QString            name;
    QString            toolCallId;
    QString            reasoningContent;
    QList<LlmToolCall> toolCalls;
};

//----------------------------------------------

/**
 * @brief Описание инструмента (функции) для модели.
 *
 * Поле @a parameters — JSON Schema объекта аргументов.
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
 * @brief Запрос к языковой модели.
 *
 * @a temperature < 0 означает «не передавать параметр».
 * @a maxTokens == 0 — не ограничивать явно.
 */
struct LlmRequest
{
    LlmRequest()
        : temperature(-1.0)
        , maxTokens(0)
    {
    }

    QList<LlmMessage> messages;
    QList<LlmTool>    tools;
    QString           model;
    double            temperature;
    int               maxTokens;
};

//----------------------------------------------

/**
 * @brief Учёт токенов в ответе провайдера.
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
    /** Часть prompt-токенов, отданная провайдером из кеша префикса (дешевле). */
    int cachedPromptTokens;
};

//----------------------------------------------

/**
 * @brief Ответ языковой модели.
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
