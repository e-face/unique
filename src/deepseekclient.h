#ifndef DEEPSEEKCLIENT_H
#define DEEPSEEKCLIENT_H

#include "openaicompatibleclient.h"

//----------------------------------------------

/**
 * @brief DeepSeek Chat Completions client (OpenAI format).
 */
class DeepSeekClient : public OpenAiCompatibleClient
{
public:
    explicit DeepSeekClient(QObject *parent = 0);

    virtual QString     providerId() const;
    virtual QString     displayName() const;
    virtual QStringList availableModels() const;

protected:
    virtual QString missingApiKeyMessage() const;
    virtual QString describeHttpError(int status, QString apiMessage) const;
    virtual bool    sendReasoningContent() const;
    virtual int     contextWindowForModel(QString model) const;
};

#endif // DEEPSEEKCLIENT_H
