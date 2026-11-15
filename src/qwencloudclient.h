#ifndef QWENCLOUDCLIENT_H
#define QWENCLOUDCLIENT_H

#include "openaicompatibleclient.h"

//----------------------------------------------

/**
 * @brief Qwen Cloud Chat Completions client (OpenAI-compatible DashScope).
 *
 * Key: https://www.qwencloud.com/ (variable DASHSCOPE_API_KEY or QWENCLOUD_API_KEY).
 * API: https://dashscope-intl.aliyuncs.com/compatible-mode/v1
 */
class QwenCloudClient : public OpenAiCompatibleClient
{
public:
    explicit QwenCloudClient(QObject *parent = 0);

    virtual QString     providerId() const;
    virtual QString     displayName() const;
    virtual QStringList availableModels() const;

protected:
    virtual QString missingApiKeyMessage() const;
    virtual QString describeHttpError(int status, QString apiMessage) const;
    virtual bool    sendReasoningContent() const;
    virtual int     contextWindowForModel(QString model) const;
    virtual void    decorateRequestBody(QJsonObject *root, const LlmRequest &request) const;
};

#endif // QWENCLOUDCLIENT_H
