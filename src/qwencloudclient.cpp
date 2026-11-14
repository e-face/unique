#include "qwencloudclient.h"
//----------------------------------------------
#include "llmtypes.h"
//----------------------------------------------
#include <QJsonObject>

//----------------------------------------------

QwenCloudClient::QwenCloudClient(QObject *parent)
    : OpenAiCompatibleClient(
          QString::fromUtf8("https://dashscope-intl.aliyuncs.com/compatible-mode/v1"),
          QString::fromUtf8("qwen3.7-plus"),
          parent)
{
}

//----------------------------------------------

QString QwenCloudClient::providerId() const
{
    return QString::fromUtf8("qwencloud");
}

//----------------------------------------------

QString QwenCloudClient::displayName() const
{
    return QString::fromUtf8("Qwen Cloud");
}

//----------------------------------------------

QStringList QwenCloudClient::availableModels() const
{
    QStringList list;
    list << QString::fromUtf8("qwen3.7-plus")
         << QString::fromUtf8("qwen3.8-max")
         << QString::fromUtf8("qwen3.8-flash")
         << QString::fromUtf8("qwen3.7-max")
         << QString::fromUtf8("qwen3.7-flash")
         << QString::fromUtf8("qwen3.6-plus")
         << QString::fromUtf8("qwen3.6-flash")
         << QString::fromUtf8("qwen3.5-plus")
         << QString::fromUtf8("qwen3.5-flash")
         << QString::fromUtf8("qwen3-max");
    return list;
}

//----------------------------------------------

QString QwenCloudClient::missingApiKeyMessage() const
{
    return tr("Не задан API-ключ Qwen Cloud. Создайте ключ: https://www.qwencloud.com/");
}

//----------------------------------------------

QString QwenCloudClient::describeHttpError(int status, QString apiMessage) const
{
    if (status == 401)
    {
        return tr("HTTP 401: ключ Qwen Cloud отклонён. Проверьте ключ на https://www.qwencloud.com/");
    }
    if (status == 402)
    {
        return tr("HTTP 402: на счёте Qwen Cloud нет средств или исчерпана квота.");
    }
    if (status == 429)
    {
        return tr("HTTP 429: лимит Qwen Cloud исчерпан, подождите и повторите.");
    }
    if (status == 500 || status == 503)
    {
        return tr("HTTP %1: сервер Qwen Cloud временно недоступен, повторите позже.")
               .arg(status);
    }
    return OpenAiCompatibleClient::describeHttpError(status, apiMessage);
}

//----------------------------------------------

bool QwenCloudClient::sendReasoningContent() const
{
    return true;
}

//----------------------------------------------

int QwenCloudClient::contextWindowForModel(QString model) const
{
    return OpenAiCompatibleClient::resolvedContextWindow(baseUrl(), model);
}

//----------------------------------------------

void QwenCloudClient::decorateRequestBody(QJsonObject *root, const LlmRequest &request) const
{
    if (!root)
        return;
    root->insert(QString::fromUtf8("enable_thinking"), request.reasoning);
}
