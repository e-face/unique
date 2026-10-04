#include "openaicompatibleclient.h"
//----------------------------------------------
#include "appsettings.h"
#include "llmnetworkworker.h"
//----------------------------------------------
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QList>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QUrl>
#include <QVariant>

//----------------------------------------------

static QHash<QString, int> g_rememberedContextWindows;

//----------------------------------------------

static QString contextCacheKey(QString baseUrl, QString model)
{
    return baseUrl.trimmed().toLower() + QChar('\n') + model.trimmed().toLower();
}

//----------------------------------------------

static int jsonPositiveInt(const QJsonValue &value)
{
    if (value.isDouble())
    {
        const double d = value.toDouble();
        if (d >= 1.0 && d <= 16000000.0)
            return int(d);
        return 0;
    }
    if (value.isString())
    {
        bool ok = false;
        const int n = value.toString().trimmed().toInt(&ok);
        return (ok && n > 0) ? n : 0;
    }
    const int n = value.toInt();
    return n > 0 ? n : 0;
}

//----------------------------------------------

static void rememberContextWindow(QString baseUrl, QString model, int tokens)
{
    if (tokens <= 0 || model.trimmed().isEmpty())
        return;
    g_rememberedContextWindows.insert(contextCacheKey(baseUrl, model), tokens);
}

//----------------------------------------------

struct CatalogFetchJob
{
    QString url;
    QString key;
    QString model;
};

static QList<CatalogFetchJob> g_catalogQueue;

//----------------------------------------------

OpenAiCompatibleClient::OpenAiCompatibleClient(QString defaultBaseUrl, QString defaultModel, QObject *parent)
    : LlmClient(parent)
    , _activeRequestId(0)
    , _catalogRequestId(0)
    , _modelMetaRequestId(0)
    , _defaultBaseUrl(defaultBaseUrl)
    , _defaultModel(defaultModel)
    , _baseUrl(defaultBaseUrl)
    , _model(defaultModel)
    , _busy(false)
{
    LlmNetworkWorker *worker = LlmNetworkWorker::instance();
    connect(worker, SIGNAL(replyReady(int,QByteArray,int,QString)),
            this, SLOT(sltNetworkReply(int,QByteArray,int,QString)),
            Qt::QueuedConnection);
    connect(worker, SIGNAL(replyNetworkError(int,QString)),
            this, SLOT(sltNetworkError(int,QString)),
            Qt::QueuedConnection);
}

//----------------------------------------------

OpenAiCompatibleClient::~OpenAiCompatibleClient()
{
    if (_activeRequestId > 0)
        LlmNetworkWorker::instance()->abortRequest(_activeRequestId);
    if (_catalogRequestId > 0)
        LlmNetworkWorker::instance()->abortRequest(_catalogRequestId);
    if (_modelMetaRequestId > 0)
        LlmNetworkWorker::instance()->abortRequest(_modelMetaRequestId);
}

//----------------------------------------------

void OpenAiCompatibleClient::setIdentity(QString id, QString name)
{
    _providerId = id.trimmed();
    _displayName = name.trimmed();
}

//----------------------------------------------

QString OpenAiCompatibleClient::providerId() const
{
    if (!_providerId.isEmpty())
        return _providerId;
    return QString::fromUtf8("openai");
}

//----------------------------------------------

QString OpenAiCompatibleClient::displayName() const
{
    if (!_displayName.isEmpty())
        return _displayName;
    return QString::fromUtf8("OpenAI");
}

//----------------------------------------------

QStringList OpenAiCompatibleClient::availableModels() const
{
    if (_model.isEmpty())
        return QStringList();
    return QStringList() << _model;
}

//----------------------------------------------

void OpenAiCompatibleClient::setApiKey(QString key)
{
    _apiKey = key.trimmed();
    refreshModelCatalog();
}

//----------------------------------------------

QString OpenAiCompatibleClient::apiKey() const
{
    return _apiKey;
}

//----------------------------------------------

void OpenAiCompatibleClient::setBaseUrl(QString url)
{
    QString next = url.trimmed();
    if (next.isEmpty())
        next = _defaultBaseUrl;
    if (_baseUrl == next)
        return;
    _baseUrl = next;
    _catalogKey.clear();
    emit contextWindowChanged();
}

//----------------------------------------------

QString OpenAiCompatibleClient::baseUrl() const
{
    return _baseUrl;
}

//----------------------------------------------

void OpenAiCompatibleClient::setModel(QString model)
{
    const QString previous = _model;
    _model = model.trimmed();
    if (_model.isEmpty())
        _model = _defaultModel;
    if (previous != _model)
        emit contextWindowChanged();
}

//----------------------------------------------

QString OpenAiCompatibleClient::model() const
{
    return _model;
}

//----------------------------------------------

bool OpenAiCompatibleClient::isBusy() const
{
    return _busy;
}

//----------------------------------------------

int OpenAiCompatibleClient::contextWindowTokens() const
{
    return resolvedContextWindow(_baseUrl, _model);
}

//----------------------------------------------

int OpenAiCompatibleClient::resolvedContextWindow(QString baseUrl, QString model)
{
    const QString key = contextCacheKey(baseUrl, model);
    const int remembered = g_rememberedContextWindows.value(key, 0);
    const int estimated = estimatedContextWindow(baseUrl, model);
    if (remembered > 0)
    {
        if (remembered == 128000 && estimated >= 256000)
            return estimated;
        return remembered;
    }
    return estimated;
}

//----------------------------------------------

bool OpenAiCompatibleClient::hasApiContextWindow(QString baseUrl, QString model)
{
    return g_rememberedContextWindows.value(contextCacheKey(baseUrl, model), 0) > 0;
}

//----------------------------------------------

OpenAiCompatibleClient *OpenAiCompatibleClient::catalogHub()
{
    static OpenAiCompatibleClient *hub = 0;
    if (!hub)
        hub = new OpenAiCompatibleClient(QString(), QString(), 0);
    return hub;
}

//----------------------------------------------

bool OpenAiCompatibleClient::hasInFlightCatalog() const
{
    return _catalogRequestId > 0 || _modelMetaRequestId > 0;
}

//----------------------------------------------

void OpenAiCompatibleClient::pumpCatalogQueue()
{
    OpenAiCompatibleClient *hub = catalogHub();
    if (!hub || hub->hasInFlightCatalog())
        return;
    if (g_catalogQueue.isEmpty())
        return;
    const CatalogFetchJob job = g_catalogQueue.takeFirst();
    hub->setBaseUrl(job.url);
    if (!job.model.isEmpty())
        hub->setModel(job.model);
    hub->setApiKey(job.key);
    if (!hub->hasInFlightCatalog() && !g_catalogQueue.isEmpty())
        pumpCatalogQueue();
}

//----------------------------------------------

void OpenAiCompatibleClient::fetchContextCatalog(QString baseUrl, QString apiKey, QString model)
{
    CatalogFetchJob job;
    job.url = baseUrl.trimmed();
    job.key = apiKey.trimmed();
    job.model = model.trimmed();
    if (job.url.isEmpty() || job.key.isEmpty())
        return;
    for (int i = 0; i < g_catalogQueue.size(); ++i)
    {
        const CatalogFetchJob &item = g_catalogQueue.at(i);
        if (item.url == job.url && item.key == job.key && item.model == job.model)
            return;
    }
    g_catalogQueue.append(job);
    pumpCatalogQueue();
}

//----------------------------------------------

int OpenAiCompatibleClient::estimatedContextWindow(QString baseUrl, QString model)
{
    const QString url = baseUrl.toLower();
    const QString m = model.toLower();

    if (url.contains(QString::fromUtf8("deepseek")) || m.contains(QString::fromUtf8("deepseek")))
    {
        if ((m.contains(QString::fromUtf8("chat")) || m.contains(QString::fromUtf8("reasoner")))
            && !m.contains(QString::fromUtf8("v4"))
            && !m.contains(QString::fromUtf8("flash"))
            && !m.contains(QString::fromUtf8("pro")))
            return 64000;
        return 1000000;
    }

    if (url.contains(QString::fromUtf8("groq")))
    {
        if (m.contains(QString::fromUtf8("minimax")))
            return 196608;
        if (m.contains(QString::fromUtf8("allam")))
            return 4096;
        if (m.contains(QString::fromUtf8("prompt-guard")))
            return 512;
        if (m.contains(QString::fromUtf8("qwen3.6")) || m.contains(QString::fromUtf8("qwen3.8"))
            || m.contains(QString::fromUtf8("qwen/qwen3")))
            return 262144;
        return 131072;
    }

    const bool qwen = url.contains(QString::fromUtf8("dashscope"))
                      || url.contains(QString::fromUtf8("aliyuncs"))
                      || url.contains(QString::fromUtf8("qwen"))
                      || m.contains(QString::fromUtf8("qwen"));
    if (qwen)
    {
        if (m.contains(QString::fromUtf8("qwen-mt")))
            return 16384;
        if (m.contains(QString::fromUtf8("qwen3-max")) && !m.contains(QString::fromUtf8("qwen3.")))
            return 256000;
        if (m.contains(QString::fromUtf8("qwen3.6-max"))
            || m.contains(QString::fromUtf8("qwen3.6-27b"))
            || m.contains(QString::fromUtf8("qwen3.6-35b"))
            || m.contains(QString::fromUtf8("qwen3.5-27b"))
            || m.contains(QString::fromUtf8("qwen3.5-35b"))
            || m.contains(QString::fromUtf8("qwen3.5-122b"))
            || m.contains(QString::fromUtf8("qwen3.5-397b")))
            return 256000;
        return 1000000;
    }

    if (m.contains(QString::fromUtf8("gpt-4.1")) || m.contains(QString::fromUtf8("gpt-5"))
        || m.contains(QString::fromUtf8("gemini")))
        return 1048576;
    if (m.contains(QString::fromUtf8("claude")))
        return 200000;
    if (m.contains(QString::fromUtf8("gpt-4o")) || m.contains(QString::fromUtf8("gpt-4-turbo")))
        return 128000;
    if (m.contains(QString::fromUtf8("gpt-3.5")))
        return 16384;
    return 128000;
}

//----------------------------------------------

void OpenAiCompatibleClient::refreshModelCatalog()
{
    if (_apiKey.isEmpty())
        return;
    if (_catalogKey == _apiKey && _catalogUrl == _baseUrl)
        return;
    if (_catalogRequestId > 0)
        LlmNetworkWorker::instance()->abortRequest(_catalogRequestId);
    if (_modelMetaRequestId > 0)
    {
        LlmNetworkWorker::instance()->abortRequest(_modelMetaRequestId);
        _modelMetaRequestId = 0;
    }

    QHash<QByteArray, QByteArray> headers;
    headers.insert("Authorization", QByteArray("Bearer ") + _apiKey.toUtf8());
    headers.insert("Accept", "application/json");
    _catalogRequestId = LlmNetworkWorker::instance()->get(modelsUrl(), headers);
}

//----------------------------------------------

int OpenAiCompatibleClient::contextWindowForModel(QString model) const
{
    return resolvedContextWindow(_baseUrl, model);
}

//----------------------------------------------

void OpenAiCompatibleClient::decorateRequestBody(QJsonObject *root, const LlmRequest &request) const
{
    if (!root)
        return;
    const QString url = _baseUrl.toLower();
    if ((url.contains(QString::fromUtf8("dashscope")) || url.contains(QString::fromUtf8("aliyuncs")))
        && !request.tools.isEmpty())
    {
        root->insert(QString::fromUtf8("enable_thinking"), false);
    }
}

//----------------------------------------------

void OpenAiCompatibleClient::send(const LlmRequest &request)
{
    if (_busy)
    {
        emit failed(tr("Клиент занят предыдущим запросом."));
        return;
    }
    if (_apiKey.isEmpty())
    {
        emit failed(missingApiKeyMessage());
        return;
    }
    if (!QSslSocket::supportsSsl())
    {
        emit failed(tr("Qt собрана без SSL. Положите библиотеки OpenSSL рядом с программой."));
        return;
    }

    const QUrl url(completionsUrl());
    QHash<QByteArray, QByteArray> headers;
    headers.insert("Content-Type", "application/json");
    headers.insert("Authorization", QByteArray("Bearer ") + _apiKey.toUtf8());
    headers.insert("Accept", "application/json");

    _activeRequestId = LlmNetworkWorker::instance()->post(url.toString(), buildBody(request), headers);
    setBusy(true);
}

//----------------------------------------------

static QJsonArray redactContentPartsForInspect(const QJsonArray &parts)
{
    QJsonArray out;
    for (int i = 0; i < parts.size(); ++i)
    {
        QJsonObject part = parts.at(i).toObject();
        const QString type = part.value(QString::fromUtf8("type")).toString();
        if (type == QString::fromUtf8("image_url"))
        {
            QJsonObject imageUrl = part.value(QString::fromUtf8("image_url")).toObject();
            const QString url = imageUrl.value(QString::fromUtf8("url")).toString();
            imageUrl.insert(QString::fromUtf8("url"),
                            QString::fromUtf8("[image data omitted, %1 chars]")
                                .arg(url.size()));
            part.insert(QString::fromUtf8("image_url"), imageUrl);
        }
        else if (type == QString::fromUtf8("input_audio"))
        {
            QJsonObject audio = part.value(QString::fromUtf8("input_audio")).toObject();
            const QString data = audio.value(QString::fromUtf8("data")).toString();
            audio.insert(QString::fromUtf8("data"),
                         QString::fromUtf8("[audio data omitted, %1 chars]")
                             .arg(data.size()));
            part.insert(QString::fromUtf8("input_audio"), audio);
        }
        else if (type == QString::fromUtf8("file"))
        {
            QJsonObject file = part.value(QString::fromUtf8("file")).toObject();
            if (file.contains(QString::fromUtf8("file_data")))
            {
                const QString data = file.value(QString::fromUtf8("file_data")).toString();
                file.insert(QString::fromUtf8("file_data"),
                            QString::fromUtf8("[file data omitted, %1 chars]")
                                .arg(data.size()));
                part.insert(QString::fromUtf8("file"), file);
            }
        }
        else if (type == QString::fromUtf8("text"))
        {
            QString text = part.value(QString::fromUtf8("text")).toString();
            if (text.size() > 4000)
            {
                part.insert(QString::fromUtf8("text"),
                            text.left(4000) + QString::fromUtf8("\n…[truncated]…"));
            }
        }
        out.append(part);
    }
    return out;
}

//----------------------------------------------

QString OpenAiCompatibleClient::encodeRequestText(const LlmRequest &request) const
{
    // Без редактирования base64 Inspect/логи съедают сотни МБ (32-bit → OOM).
    LlmRequest view = request;
    for (int i = 0; i < view.messages.size(); ++i)
    {
        if (!view.messages.at(i).contentParts.isEmpty())
            view.messages[i].contentParts =
                redactContentPartsForInspect(view.messages.at(i).contentParts);
    }
    const QByteArray body = buildBody(view);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return QString::fromUtf8(body);
    return QString::fromUtf8(document.toJson(QJsonDocument::Indented));
}

//----------------------------------------------

void OpenAiCompatibleClient::abort()
{
    if (_activeRequestId > 0)
    {
        LlmNetworkWorker::instance()->abortRequest(_activeRequestId);
        _activeRequestId = 0;
    }
    // Иначе после Stop клиент остаётся busy=true, а сетевой callback
    // с «aborted» отбрасывается (id уже обнулён) — следующий send падает.
    setBusy(false);
}

//----------------------------------------------

QString OpenAiCompatibleClient::missingApiKeyMessage() const
{
    return tr("Не задан API-ключ в профиле «%1».").arg(displayName());
}

//----------------------------------------------

QString OpenAiCompatibleClient::describeHttpError(int status, QString apiMessage) const
{
    switch (status)
    {
    case 401:
        return tr("HTTP 401: неверный API-ключ.");
    case 413:
        return tr("HTTP 413: слишком большой запрос.");
    case 429:
        return tr("HTTP 429: слишком частые запросы, подождите и повторите.");
    case 500:
    case 503:
        return tr("HTTP %1: сервер временно недоступен, повторите позже.").arg(status);
    default:
        if (!apiMessage.isEmpty())
            return tr("HTTP %1: %2").arg(status).arg(apiMessage);
        return tr("HTTP %1").arg(status);
    }
}

//----------------------------------------------

bool OpenAiCompatibleClient::sendMessageName() const
{
    return !_baseUrl.toLower().contains(QString::fromUtf8("groq.com"));
}

//----------------------------------------------

bool OpenAiCompatibleClient::sendReasoningContent() const
{
    const QString url = _baseUrl.toLower();
    return url.contains(QString::fromUtf8("deepseek"))
           || url.contains(QString::fromUtf8("dashscope"))
           || url.contains(QString::fromUtf8("aliyuncs"));
}

//----------------------------------------------

void OpenAiCompatibleClient::sltNetworkReply(int requestId, QByteArray data,
                                               int httpStatus, QString errorText)
{
    if (requestId == _catalogRequestId)
    {
        _catalogRequestId = 0;
        if (httpStatus >= 200 && httpStatus < 300)
            handleModelsData(data);
        else
            requestModelMetaIfNeeded();
        if (!hasInFlightCatalog())
            pumpCatalogQueue();
        return;
    }
    if (requestId == _modelMetaRequestId)
    {
        _modelMetaRequestId = 0;
        if (httpStatus >= 200 && httpStatus < 300)
            handleModelMetaData(data);
        notifyCatalogUpdated();
        pumpCatalogQueue();
        return;
    }
    if (requestId != _activeRequestId)
        return;
    _activeRequestId = 0;
    handleNetworkData(data, httpStatus, errorText);
}

//----------------------------------------------

void OpenAiCompatibleClient::sltNetworkError(int requestId, QString errorText)
{
    if (requestId == _catalogRequestId)
    {
        _catalogRequestId = 0;
        requestModelMetaIfNeeded();
        if (!hasInFlightCatalog())
            pumpCatalogQueue();
        return;
    }
    if (requestId == _modelMetaRequestId)
    {
        _modelMetaRequestId = 0;
        notifyCatalogUpdated();
        pumpCatalogQueue();
        return;
    }
    if (requestId != _activeRequestId)
        return;
    _activeRequestId = 0;
    setBusy(false);
    if (errorText == QString::fromUtf8("aborted"))
        emit failed(tr("Запрос отменён."));
    else if (errorText == QString::fromUtf8("timeout"))
        emit failed(tr("Модель не ответила за %1 с — запрос прерван по таймауту. "
                       "Повторите запрос или увеличьте таймаут в настройках.")
                    .arg(AppSettings::instance()->llmTimeoutSec()));
    else
        emit failed(tr("Сетевая ошибка: %1").arg(errorText));
}

//----------------------------------------------

void OpenAiCompatibleClient::handleNetworkData(QByteArray data, int status, QString networkError)
{
    Q_UNUSED(networkError);
    if (data.isEmpty() && status <= 0)
    {
        setBusy(false);
        emit failed(tr("Сетевая ошибка: пустой ответ."));
        return;
    }

    LlmResponse response;
    QString error;
    if (!parseResponse(data, &response, &error))
    {
        setBusy(false);
        if (status > 0)
            emit failed(describeHttpError(status, error));
        else
            emit failed(error);
        return;
    }

    setBusy(false);
    emit responseReceived(response);
}

//----------------------------------------------

QByteArray OpenAiCompatibleClient::buildBody(const LlmRequest &request) const
{
    QJsonObject root;
    const QString model = request.model.isEmpty() ? _model : request.model;
    root.insert(QString::fromUtf8("model"), model);
    root.insert(QString::fromUtf8("stream"), false);

    QJsonArray messages;
    foreach (const LlmMessage &message, request.messages)
        messages.append(messageToJson(message));
    root.insert(QString::fromUtf8("messages"), messages);

    if (!request.tools.isEmpty())
    {
        QJsonArray tools;
        foreach (const LlmTool &tool, request.tools)
        {
            QJsonObject function;
            function.insert(QString::fromUtf8("name"), tool.name);
            function.insert(QString::fromUtf8("description"), tool.description);
            function.insert(QString::fromUtf8("parameters"), tool.parameters);

            QJsonObject item;
            item.insert(QString::fromUtf8("type"), QString::fromUtf8("function"));
            item.insert(QString::fromUtf8("function"), function);
            tools.append(item);
        }
        root.insert(QString::fromUtf8("tools"), tools);
        root.insert(QString::fromUtf8("tool_choice"), QString::fromUtf8("auto"));
    }

    if (request.temperature >= 0.0)
        root.insert(QString::fromUtf8("temperature"), request.temperature);
    if (request.maxTokens > 0)
        root.insert(QString::fromUtf8("max_tokens"), request.maxTokens);

    decorateRequestBody(&root, request);

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

//----------------------------------------------

QJsonObject OpenAiCompatibleClient::messageToJson(const LlmMessage &message) const
{
    QJsonObject object;
    object.insert(QString::fromUtf8("role"), llmRoleToString(message.role));

    if (!message.toolCalls.isEmpty())
    {
        if (!message.contentParts.isEmpty())
            object.insert(QString::fromUtf8("content"), message.contentParts);
        else if (message.content.isEmpty())
            object.insert(QString::fromUtf8("content"), QJsonValue::Null);
        else
            object.insert(QString::fromUtf8("content"), message.content);

        QJsonArray calls;
        foreach (const LlmToolCall &call, message.toolCalls)
        {
            QJsonObject function;
            function.insert(QString::fromUtf8("name"), call.name);
            function.insert(QString::fromUtf8("arguments"), call.arguments);

            QJsonObject item;
            item.insert(QString::fromUtf8("id"), call.id);
            item.insert(QString::fromUtf8("type"), QString::fromUtf8("function"));
            item.insert(QString::fromUtf8("function"), function);
            calls.append(item);
        }
        object.insert(QString::fromUtf8("tool_calls"), calls);
    }
    else if (!message.contentParts.isEmpty())
    {
        object.insert(QString::fromUtf8("content"), message.contentParts);
    }
    else
    {
        object.insert(QString::fromUtf8("content"), message.content);
    }

    if (!message.toolCallId.isEmpty())
        object.insert(QString::fromUtf8("tool_call_id"), message.toolCallId);
    if (sendMessageName() && !message.name.isEmpty())
        object.insert(QString::fromUtf8("name"), message.name);
    if (sendReasoningContent() && message.role == LlmRoleAssistant)
        object.insert(QString::fromUtf8("reasoning_content"), message.reasoningContent);

    return object;
}

//----------------------------------------------

bool OpenAiCompatibleClient::parseResponse(const QByteArray &data, LlmResponse *response, QString *error) const
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        if (error)
            *error = tr("Некорректный JSON: %1").arg(parseError.errorString());
        return false;
    }
    if (!document.isObject())
    {
        if (error)
            *error = tr("Ответ API не является JSON-объектом.");
        return false;
    }

    const QJsonObject root = document.object();
    if (root.contains(QString::fromUtf8("error")))
    {
        const QJsonObject err = root.value(QString::fromUtf8("error")).toObject();
        QString message = err.value(QString::fromUtf8("message")).toString();
        if (message.isEmpty())
            message = QString::fromUtf8(data);
        if (error)
            *error = message;
        return false;
    }

    const QJsonArray choices = root.value(QString::fromUtf8("choices")).toArray();
    if (choices.isEmpty())
    {
        if (error)
            *error = tr("В ответе нет choices.");
        return false;
    }

    const QJsonObject choice = choices.at(0).toObject();
    const QJsonObject message = choice.value(QString::fromUtf8("message")).toObject();

    response->finishReason = choice.value(QString::fromUtf8("finish_reason")).toString();
    response->message.role = llmRoleFromString(message.value(QString::fromUtf8("role")).toString());
    response->message.content = message.value(QString::fromUtf8("content")).toString();
    response->message.reasoningContent =
        message.value(QString::fromUtf8("reasoning_content")).toString();
    if (response->message.reasoningContent.isEmpty())
        response->message.reasoningContent =
            message.value(QString::fromUtf8("reasoning")).toString();

    const QJsonArray toolCalls = message.value(QString::fromUtf8("tool_calls")).toArray();
    for (int i = 0; i < toolCalls.size(); ++i)
    {
        const QJsonObject item = toolCalls.at(i).toObject();
        const QJsonObject function = item.value(QString::fromUtf8("function")).toObject();
        LlmToolCall call;
        call.id = item.value(QString::fromUtf8("id")).toString();
        call.name = function.value(QString::fromUtf8("name")).toString();
        call.arguments = function.value(QString::fromUtf8("arguments")).toString();
        response->message.toolCalls.append(call);
    }

    const QJsonObject usage = root.value(QString::fromUtf8("usage")).toObject();
    response->usage.promptTokens = usage.value(QString::fromUtf8("prompt_tokens")).toInt();
    response->usage.completionTokens = usage.value(QString::fromUtf8("completion_tokens")).toInt();
    response->usage.totalTokens = usage.value(QString::fromUtf8("total_tokens")).toInt();
    // Кеш префикса: OpenAI/Groq/Qwen — prompt_tokens_details.cached_tokens,
    // DeepSeek — prompt_cache_hit_tokens.
    int cached = usage.value(QString::fromUtf8("prompt_tokens_details")).toObject()
                     .value(QString::fromUtf8("cached_tokens")).toInt();
    if (cached <= 0)
        cached = usage.value(QString::fromUtf8("prompt_cache_hit_tokens")).toInt();
    response->usage.cachedPromptTokens = qMax(0, cached);
    return true;
}

//----------------------------------------------

QString OpenAiCompatibleClient::completionsUrl() const
{
    QString base = _baseUrl;
    while (base.endsWith(QChar('/')))
        base.chop(1);
    return base + QString::fromUtf8("/chat/completions");
}

//----------------------------------------------

QString OpenAiCompatibleClient::modelsUrl() const
{
    QString base = _baseUrl;
    while (base.endsWith(QChar('/')))
        base.chop(1);
    return base + QString::fromUtf8("/models");
}

//----------------------------------------------

QString OpenAiCompatibleClient::modelMetaUrl(QString model) const
{
    QString id = model.trimmed();
    if (id.isEmpty())
        id = _model.trimmed();
    if (id.isEmpty())
        return QString();
    return modelsUrl() + QChar('/') + QString::fromUtf8(QUrl::toPercentEncoding(id, "/"));
}

//----------------------------------------------

int OpenAiCompatibleClient::contextWindowFromObject(const QJsonObject &object) const
{
    static const char *keys[] = {
        "context_window",
        "context_length",
        "max_model_len",
        "max_input_tokens",
        "input_token_limit",
        "max_position_embeddings",
        "max_context_length",
        0
    };
    for (int i = 0; keys[i]; ++i)
    {
        const int n = jsonPositiveInt(object.value(QString::fromUtf8(keys[i])));
        if (n > 0)
            return n;
    }
    const QJsonObject meta = object.value(QString::fromUtf8("metadata")).toObject();
    if (!meta.isEmpty())
        return contextWindowFromObject(meta);
    const QJsonObject caps = object.value(QString::fromUtf8("capabilities")).toObject();
    if (!caps.isEmpty())
        return contextWindowFromObject(caps);
    return 0;
}

//----------------------------------------------

void OpenAiCompatibleClient::notifyCatalogUpdated()
{
    emit contextWindowChanged();
    OpenAiCompatibleClient *hub = catalogHub();
    if (hub && hub != this)
        emit hub->contextWindowChanged();
}

//----------------------------------------------

void OpenAiCompatibleClient::requestModelMetaIfNeeded()
{
    if (_modelMetaRequestId > 0)
        return;
    if (_model.trimmed().isEmpty())
        return;
    if (hasApiContextWindow(_baseUrl, _model))
        return;
    const QString url = modelMetaUrl(_model);
    if (url.isEmpty())
        return;
    QHash<QByteArray, QByteArray> headers;
    headers.insert("Authorization", QByteArray("Bearer ") + _apiKey.toUtf8());
    headers.insert("Accept", "application/json");
    _modelMetaRequestId = LlmNetworkWorker::instance()->get(url, headers);
}

//----------------------------------------------

void OpenAiCompatibleClient::handleModelMetaData(QByteArray data)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return;

    QJsonObject item;
    if (document.isObject())
    {
        item = document.object();
        const QJsonValue nested = item.value(QString::fromUtf8("data"));
        if (nested.isObject())
            item = nested.toObject();
        else if (nested.isArray() && !nested.toArray().isEmpty())
            item = nested.toArray().at(0).toObject();
    }
    QString id = item.value(QString::fromUtf8("id")).toString();
    if (id.isEmpty())
        id = _model;
    const int window = contextWindowFromObject(item);
    if (window <= 0)
        return;
    rememberContextWindow(_baseUrl, id, window);
    _modelContextWindows.insert(id, window);
}

//----------------------------------------------

void OpenAiCompatibleClient::handleModelsData(QByteArray data)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return;

    QJsonArray models;
    if (document.isObject())
        models = document.object().value(QString::fromUtf8("data")).toArray();
    else if (document.isArray())
        models = document.array();

    QHash<QString, int> windows;
    for (int i = 0; i < models.size(); ++i)
    {
        const QJsonObject item = models.at(i).toObject();
        const QString id = item.value(QString::fromUtf8("id")).toString();
        if (id.isEmpty())
            continue;
        const int window = contextWindowFromObject(item);
        if (window > 0)
        {
            windows.insert(id, window);
            rememberContextWindow(_baseUrl, id, window);
        }
    }
    if (windows.isEmpty() && document.isObject() && models.isEmpty())
        handleModelMetaData(data);

    _catalogKey = _apiKey;
    _catalogUrl = _baseUrl;
    if (!windows.isEmpty())
        _modelContextWindows = windows;

    requestModelMetaIfNeeded();
    notifyCatalogUpdated();
}

//----------------------------------------------

void OpenAiCompatibleClient::setBusy(bool busy)
{
    if (_busy == busy)
        return;
    _busy = busy;
    emit busyChanged(_busy);
}
