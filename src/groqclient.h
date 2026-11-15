#ifndef GROQCLIENT_H
#define GROQCLIENT_H

#include "openaicompatibleclient.h"

//----------------------------------------------

/**
 * @brief Groq Chat Completions client (OpenAI format).
 *
 * The key is created at https://console.groq.com/keys
 * API: https://api.groq.com/openai/v1
 *
 * Groq does not accept the messages[].name field (HTTP 400), so it is not sent.
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
