#ifndef LLMCLIENT_H
#define LLMCLIENT_H

#include "llmtypes.h"
//----------------------------------------------
#include <QObject>
#include <QStringList>

//----------------------------------------------

/**
 * @brief Common interface of a language-model client.
 *
 * Concrete providers (DeepSeek and others) inherit this class
 * and send requests through their own HTTP API.
 */
class LlmClient : public QObject
{
    Q_OBJECT
public:
    explicit LlmClient(QObject *parent = 0);
    virtual ~LlmClient();

    virtual QString     providerId() const = 0;
    virtual QString     displayName() const = 0;
    virtual QStringList availableModels() const = 0;

    virtual void    setApiKey(QString key) = 0;
    virtual QString apiKey() const = 0;
    virtual void    setBaseUrl(QString url) = 0;
    virtual QString baseUrl() const = 0;
    virtual void    setModel(QString model) = 0;
    virtual QString model() const = 0;

    virtual bool isBusy() const = 0;

    /**
     * @brief Context-window size of the current model, in tokens.
     */
    virtual int contextWindowTokens() const = 0;
    virtual void refreshModelCatalog();

    /**
     * @brief Send a request to the model.
     *
     * The result arrives as responseReceived() or failed().
     */
    virtual void send(const LlmRequest &request) = 0;
    virtual void abort() = 0;

    /**
     * @brief The request body as it goes on the wire, for inspection.
     */
    virtual QString encodeRequestText(const LlmRequest &request) const;

signals:
    void responseReceived(const LlmResponse &response);
    void failed(const QString &error);
    void busyChanged(bool busy);
    void contextWindowChanged();
};

#endif // LLMCLIENT_H
