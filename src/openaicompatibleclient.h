#ifndef OPENAICOMPATIBLECLIENT_H
#define OPENAICOMPATIBLECLIENT_H

#include "llmclient.h"
//----------------------------------------------
#include <QByteArray>
#include <QHash>
#include <QJsonObject>

class QNetworkReply;

//----------------------------------------------

/**
 * @brief Shared Chat Completions implementation in the OpenAI format.
 *
 * DeepSeek, Groq, and other compatible providers set the URL, the models,
 * and the JSON differences (a name in messages, reasoning_content).
 */
class OpenAiCompatibleClient : public LlmClient
{
    Q_OBJECT
public:
    OpenAiCompatibleClient(QString defaultBaseUrl, QString defaultModel, QObject *parent = 0);
    virtual ~OpenAiCompatibleClient();

    virtual QString     providerId() const;
    virtual QString     displayName() const;
    virtual QStringList availableModels() const;

    virtual void    setApiKey(QString key);
    virtual QString apiKey() const;
    virtual void    setBaseUrl(QString url);
    virtual QString baseUrl() const;
    virtual void    setModel(QString model);
    virtual QString model() const;
    void            setIdentity(QString id, QString name);
    /**
     * @brief Context-window estimate from the provider URL and the model name.
     *
     * Needed when the /models catalog does not report the window length, but the chat
     * still has to be able to take the model's full context.
     */
    static int estimatedContextWindow(QString baseUrl, QString model);
    static int resolvedContextWindow(QString baseUrl, QString model);
    static bool hasApiContextWindow(QString baseUrl, QString model);
    static void fetchContextCatalog(QString baseUrl, QString apiKey, QString model = QString());
    static OpenAiCompatibleClient *catalogHub();

    virtual bool isBusy() const;
    virtual int  contextWindowTokens() const;
    virtual void refreshModelCatalog();
    virtual void send(const LlmRequest &request);
    virtual void abort();
    virtual QString encodeRequestText(const LlmRequest &request) const;

protected:
    virtual QString missingApiKeyMessage() const;
    virtual QString describeHttpError(int status, QString apiMessage) const;
    virtual bool    sendMessageName() const;
    virtual bool    sendReasoningContent() const;
    virtual int     contextWindowForModel(QString model) const;
    virtual void    decorateRequestBody(QJsonObject *root, const LlmRequest &request) const;

private slots:
    void sltNetworkReply(int requestId, QByteArray data, int httpStatus, QString errorText);
    void sltNetworkError(int requestId, QString errorText);

private:
    QByteArray buildBody(const LlmRequest &request) const;
    QJsonObject messageToJson(const LlmMessage &message) const;
    bool parseResponse(const QByteArray &data, LlmResponse *response, QString *error) const;
    QString completionsUrl() const;
    QString modelsUrl() const;
    void setBusy(bool busy);
    void handleNetworkData(QByteArray data, int status, QString networkError);
    void handleModelsData(QByteArray data);
    void handleModelMetaData(QByteArray data);
    void requestModelMetaIfNeeded();
    void notifyCatalogUpdated();
    bool hasInFlightCatalog() const;
    static void pumpCatalogQueue();
    int contextWindowFromObject(const QJsonObject &object) const;
    QString modelMetaUrl(QString model) const;

    int                    _activeRequestId;
    int                    _catalogRequestId;
    int                    _modelMetaRequestId;
    QString                _defaultBaseUrl;
    QString                _defaultModel;
    QString                _providerId;
    QString                _displayName;
    QString                _apiKey;
    QString                _baseUrl;
    QString                _model;
    bool                   _busy;
    QString                _catalogKey;
    QString                _catalogUrl;
    QHash<QString, int>    _modelContextWindows;
};

#endif // OPENAICOMPATIBLECLIENT_H
