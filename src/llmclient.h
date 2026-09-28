#ifndef LLMCLIENT_H
#define LLMCLIENT_H

#include "llmtypes.h"
//----------------------------------------------
#include <QObject>
#include <QStringList>

//----------------------------------------------

/**
 * @brief Общий интерфейс клиента языковой модели.
 *
 * Конкретные провайдеры (DeepSeek и др.) наследуются от этого класса
 * и реализуют отправку запросов через свой HTTP API.
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
     * @brief Размер окна контекста текущей модели (токены).
     */
    virtual int contextWindowTokens() const = 0;
    virtual void refreshModelCatalog();

    /**
     * @brief Отправить запрос к модели.
     *
     * Результат приходит сигналом responseReceived() либо failed().
     */
    virtual void send(const LlmRequest &request) = 0;
    virtual void abort() = 0;

    /**
     * @brief Тело запроса так, как оно уходит по сети (для просмотра).
     */
    virtual QString encodeRequestText(const LlmRequest &request) const;

signals:
    void responseReceived(const LlmResponse &response);
    void failed(const QString &error);
    void busyChanged(bool busy);
    void contextWindowChanged();
};

#endif // LLMCLIENT_H
