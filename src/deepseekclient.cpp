#include "deepseekclient.h"

//----------------------------------------------

DeepSeekClient::DeepSeekClient(QObject *parent)
    : OpenAiCompatibleClient(QString::fromUtf8("https://api.deepseek.com"),
                             QString::fromUtf8("deepseek-v4-flash"),
                             parent)
{
}

//----------------------------------------------

QString DeepSeekClient::providerId() const
{
    return QString::fromUtf8("deepseek");
}

//----------------------------------------------

QString DeepSeekClient::displayName() const
{
    return QString::fromUtf8("DeepSeek");
}

//----------------------------------------------

QStringList DeepSeekClient::availableModels() const
{
    QStringList list;
    list << QString::fromUtf8("deepseek-v4-flash")
         << QString::fromUtf8("deepseek-v4-pro")
         << QString::fromUtf8("deepseek-chat");
    return list;
}

//----------------------------------------------

QString DeepSeekClient::missingApiKeyMessage() const
{
    return tr("Не задан API-ключ DeepSeek.");
}

//----------------------------------------------

QString DeepSeekClient::describeHttpError(int status, QString apiMessage) const
{
    if (status == 402)
    {
        return tr("HTTP 402: на счёте DeepSeek нет средств. "
                  "Пополните баланс: https://platform.deepseek.com/top_up");
    }
    if (status == 500 || status == 503)
    {
        return tr("HTTP %1: сервер DeepSeek временно недоступен, повторите позже.")
               .arg(status);
    }
    return OpenAiCompatibleClient::describeHttpError(status, apiMessage);
}

//----------------------------------------------

bool DeepSeekClient::sendReasoningContent() const
{
    return true;
}

//----------------------------------------------

int DeepSeekClient::contextWindowForModel(QString model) const
{
    return OpenAiCompatibleClient::resolvedContextWindow(baseUrl(), model);
}
