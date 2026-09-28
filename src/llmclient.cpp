#include "llmclient.h"
//----------------------------------------------

LlmClient::LlmClient(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<LlmResponse>("LlmResponse");
}

//----------------------------------------------

LlmClient::~LlmClient()
{
}

//----------------------------------------------

void LlmClient::refreshModelCatalog()
{
}

//----------------------------------------------

QString LlmClient::encodeRequestText(const LlmRequest &request) const
{
    Q_UNUSED(request);
    return QString();
}
