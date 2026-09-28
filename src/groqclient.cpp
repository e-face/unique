#include "groqclient.h"

//----------------------------------------------

GroqClient::GroqClient(QObject *parent)
    : OpenAiCompatibleClient(QString::fromUtf8("https://api.groq.com/openai/v1"),
                             QString::fromUtf8("llama-3.3-70b-versatile"),
                             parent)
{
}

//----------------------------------------------

QString GroqClient::providerId() const
{
    return QString::fromUtf8("groq");
}

//----------------------------------------------

QString GroqClient::displayName() const
{
    return QString::fromUtf8("Groq");
}

//----------------------------------------------

QStringList GroqClient::availableModels() const
{
    QStringList list;
    list << QString::fromUtf8("llama-3.3-70b-versatile")
         << QString::fromUtf8("llama-3.1-8b-instant")
         << QString::fromUtf8("openai/gpt-oss-120b")
         << QString::fromUtf8("openai/gpt-oss-20b")
         << QString::fromUtf8("openai/gpt-oss-safeguard-20b")
         << QString::fromUtf8("qwen/qwen3.6-27b")
         << QString::fromUtf8("qwen/qwen3.8-27b")
         << QString::fromUtf8("minimaxai/minimax-m2.7")
         << QString::fromUtf8("allam-2-7b")
         << QString::fromUtf8("groq/compound")
         << QString::fromUtf8("groq/compound-mini")
         << QString::fromUtf8("meta-llama/llama-prompt-guard-2-22m")
         << QString::fromUtf8("meta-llama/llama-prompt-guard-2-86m");
    return list;
}

//----------------------------------------------

QString GroqClient::missingApiKeyMessage() const
{
    return tr("Не задан API-ключ Groq. Создайте ключ: https://console.groq.com/keys");
}

//----------------------------------------------

QString GroqClient::describeHttpError(int status, QString apiMessage) const
{
    if (status == 429)
    {
        return tr("HTTP 429: лимит Groq исчерпан, подождите и повторите. "
                  "Лимиты: https://console.groq.com/settings/limits");
    }
    if (status == 500 || status == 503)
    {
        return tr("HTTP %1: сервер Groq временно недоступен, повторите позже.")
               .arg(status);
    }
    return OpenAiCompatibleClient::describeHttpError(status, apiMessage);
}

//----------------------------------------------

bool GroqClient::sendMessageName() const
{
    return false;
}

//----------------------------------------------

int GroqClient::contextWindowForModel(QString model) const
{
    return OpenAiCompatibleClient::resolvedContextWindow(baseUrl(), model);
}
