#ifndef GROQCLIENT_H
#define GROQCLIENT_H

#include "openaicompatibleclient.h"

//----------------------------------------------

/**
 * @brief Клиент Groq Chat Completions (формат OpenAI).
 *
 * Ключ создаётся на https://console.groq.com/keys
 * API: https://api.groq.com/openai/v1
 *
 * Поле messages[].name Groq не принимает (HTTP 400), поэтому не отправляется.
 */
class GroqClient : public OpenAiCompatibleClient
{
public:
    explicit GroqClient(QObject *parent = 0);

    virtual QString     providerId() const;
    virtual QString     displayName() const;
    virtual QStringList availableModels() const;

protected:
    virtual QString missingApiKeyMessage() const;
    virtual QString describeHttpError(int status, QString apiMessage) const;
    virtual bool    sendMessageName() const;
    virtual int     contextWindowForModel(QString model) const;
};

#endif // GROQCLIENT_H
