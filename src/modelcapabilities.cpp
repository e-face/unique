#include "modelcapabilities.h"
//----------------------------------------------
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

//----------------------------------------------

bool ModelCapabilities::acceptsText() const
{
    return inputModalities.contains(QString::fromUtf8("text"), Qt::CaseInsensitive)
           || inputModalities.isEmpty();
}

//----------------------------------------------

bool ModelCapabilities::acceptsImage() const
{
    return inputModalities.contains(QString::fromUtf8("image"), Qt::CaseInsensitive)
           || allowsImageReference();
}

//----------------------------------------------

bool ModelCapabilities::acceptsAudio() const
{
    return inputModalities.contains(QString::fromUtf8("audio"), Qt::CaseInsensitive);
}

//----------------------------------------------

bool ModelCapabilities::acceptsVideo() const
{
    return inputModalities.contains(QString::fromUtf8("video"), Qt::CaseInsensitive);
}

//----------------------------------------------

bool ModelCapabilities::acceptsFile() const
{
    return inputModalities.contains(QString::fromUtf8("file"), Qt::CaseInsensitive)
           || acceptsText(); // агент может прочитать файл инструментами
}

//----------------------------------------------

bool ModelCapabilities::requiresImageReference() const
{
    return inputReferencesMin >= 1;
}

//----------------------------------------------

bool ModelCapabilities::allowsImageReference() const
{
    return inputReferencesMax > 0 || inputReferencesMin >= 1;
}

//----------------------------------------------

QString ModelCapabilities::summary() const
{
    QStringList parts;
    if (!inputModalities.isEmpty())
        parts << QString::fromUtf8("in=%1").arg(inputModalities.join(QChar('+')));
    if (!outputModalities.isEmpty())
        parts << QString::fromUtf8("out=%1").arg(outputModalities.join(QChar('+')));
    if (requiresImageReference())
        parts << QString::fromUtf8("refs_required=%1..%2")
                     .arg(inputReferencesMin)
                     .arg(inputReferencesMax > 0 ? inputReferencesMax : inputReferencesMin);
    else if (allowsImageReference())
        parts << QString::fromUtf8("refs_optional=0..%1").arg(inputReferencesMax);
    if (!source.isEmpty())
        parts << QString::fromUtf8("src=%1").arg(source);
    return parts.isEmpty() ? QString::fromUtf8("unknown") : parts.join(QString::fromUtf8(", "));
}

//----------------------------------------------

ModelCapabilityService::ModelCapabilityService()
{
}

//----------------------------------------------

ModelCapabilityService *ModelCapabilityService::instance()
{
    static ModelCapabilityService *svc = 0;
    if (!svc)
        svc = new ModelCapabilityService();
    return svc;
}

//----------------------------------------------

static QString cacheKey(const LlmProfile &profile)
{
    return profile.baseUrl.trimmed().toLower() + QChar('|')
           + profile.model.trimmed().toLower() + QChar('|')
           + workerTaskKindToString(profile.modality);
}

//----------------------------------------------

ModelCapabilities ModelCapabilityService::heuristicFor(const LlmProfile &profile)
{
    ModelCapabilities caps;
    caps.source = QString::fromUtf8("heuristic");
    const QString model = profile.model.trimmed().toLower();
    const QString name = profile.name.trimmed().toLower();
    const QString blob = model + QChar(' ') + name;

    switch (profile.modality)
    {
    case WorkerTaskKindImageGeneration:
        caps.inputModalities << QString::fromUtf8("text");
        caps.outputModalities << QString::fromUtf8("image");
        if (blob.contains(QString::fromUtf8("layer"))
            || blob.contains(QString::fromUtf8("edit"))
            || blob.contains(QString::fromUtf8("img2img"))
            || blob.contains(QString::fromUtf8("image-to-image"))
            || blob.contains(QString::fromUtf8("instruct-pix2pix")))
        {
            caps.inputModalities << QString::fromUtf8("image");
            caps.inputReferencesMin = 1;
            caps.inputReferencesMax = 1;
        }
        break;
    case WorkerTaskKindSpeech:
        if (blob.contains(QString::fromUtf8("whisper"))
            || blob.contains(QString::fromUtf8("transcri"))
            || blob.contains(QString::fromUtf8("stt"))
            || blob.contains(QString::fromUtf8("asr")))
        {
            caps.inputModalities << QString::fromUtf8("audio");
            caps.outputModalities << QString::fromUtf8("text");
        }
        else
        {
            caps.inputModalities << QString::fromUtf8("text");
            caps.outputModalities << QString::fromUtf8("audio");
        }
        break;
    case WorkerTaskKindAudioGeneration:
        caps.inputModalities << QString::fromUtf8("text");
        if (blob.contains(QString::fromUtf8("remix"))
            || blob.contains(QString::fromUtf8("extend")))
            caps.inputModalities << QString::fromUtf8("audio");
        caps.outputModalities << QString::fromUtf8("audio");
        break;
    case WorkerTaskKindVideoGeneration:
        caps.inputModalities << QString::fromUtf8("text");
        if (blob.contains(QString::fromUtf8("i2v"))
            || blob.contains(QString::fromUtf8("image-to-video"))
            || blob.contains(QString::fromUtf8("ref")))
            caps.inputModalities << QString::fromUtf8("image");
        caps.outputModalities << QString::fromUtf8("video");
        break;
    case WorkerTaskKindEmbedding:
        caps.inputModalities << QString::fromUtf8("text");
        caps.outputModalities << QString::fromUtf8("embeddings");
        break;
    case WorkerTaskKindDecisions:
    case WorkerTaskKindLlm:
    case WorkerTaskKindOther:
    default:
        caps.inputModalities << QString::fromUtf8("text");
        caps.inputModalities << QString::fromUtf8("file"); // пути → read_file / инструменты
        caps.outputModalities << QString::fromUtf8("text");

        // Vision / multimodal LLM (не путать с обычным deepseek-chat / qwen-plus).
        const bool visionHint =
            blob.contains(QString::fromUtf8("vision"))
            || blob.contains(QString::fromUtf8("-vl"))
            || blob.contains(QString::fromUtf8("_vl"))
            || blob.contains(QString::fromUtf8("vl-"))
            || blob.contains(QString::fromUtf8("vl."))
            || blob.contains(QString::fromUtf8("qwen2-vl"))
            || blob.contains(QString::fromUtf8("qwen2.5-vl"))
            || blob.contains(QString::fromUtf8("qwen3-vl"))
            || blob.contains(QString::fromUtf8("qwen-vl"))
            || blob.contains(QString::fromUtf8("omni"))
            || blob.contains(QString::fromUtf8("4o"))
            || blob.contains(QString::fromUtf8("gpt-4.1"))
            || blob.contains(QString::fromUtf8("gemini"))
            || blob.contains(QString::fromUtf8("claude-3"))
            || blob.contains(QString::fromUtf8("claude-sonnet"))
            || blob.contains(QString::fromUtf8("claude-opus"))
            || blob.contains(QString::fromUtf8("claude-haiku"))
            || blob.contains(QString::fromUtf8("pixtral"))
            || blob.contains(QString::fromUtf8("llava"))
            || blob.contains(QString::fromUtf8("internvl"))
            || blob.contains(QString::fromUtf8("glm-4v"))
            || blob.contains(QString::fromUtf8("step-1v"));
        if (visionHint)
            caps.inputModalities << QString::fromUtf8("image");

        if (blob.contains(QString::fromUtf8("omni"))
            || blob.contains(QString::fromUtf8("audio"))
            || blob.contains(QString::fromUtf8("qwen2-audio"))
            || blob.contains(QString::fromUtf8("gpt-4o-audio")))
            caps.inputModalities << QString::fromUtf8("audio");

        if (blob.contains(QString::fromUtf8("omni"))
            || blob.contains(QString::fromUtf8("video")))
            caps.inputModalities << QString::fromUtf8("video");
        break;
    }
    caps.known = true;
    return caps;
}

//----------------------------------------------

static QByteArray httpGetSync(QString url, QString apiKey, int *status, QString *error)
{
    QNetworkAccessManager nam;
    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setRawHeader("Accept", "application/json");
    if (!apiKey.trimmed().isEmpty())
        request.setRawHeader("Authorization",
                             QByteArray("Bearer ") + apiKey.toUtf8());

    QNetworkReply *reply = nam.get(request);
    QEventLoop loop;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, SIGNAL(timeout()), &loop, SLOT(quit()));
    QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
    watchdog.start(20000);
    loop.exec();

    if (!watchdog.isActive() && reply->isRunning())
    {
        reply->abort();
        if (error)
            *error = QString::fromUtf8("timeout");
        reply->deleteLater();
        return QByteArray();
    }
    watchdog.stop();

    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status)
        *status = code;
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError && code < 200)
    {
        if (error)
            *error = reply->errorString();
        reply->deleteLater();
        return QByteArray();
    }
    if (code >= 400)
    {
        if (error)
            *error = QString::fromUtf8("HTTP %1").arg(code);
        reply->deleteLater();
        return QByteArray();
    }
    reply->deleteLater();
    return body;
}

//----------------------------------------------

static QString joinApi(QString baseUrl, QString path)
{
    QString base = baseUrl.trimmed();
    while (base.endsWith(QChar('/')))
        base.chop(1);
    if (!path.startsWith(QChar('/')))
        path.prepend(QChar('/'));
    return base + path;
}

//----------------------------------------------

static void applyArchitecture(ModelCapabilities *caps, const QJsonObject &arch)
{
    if (!caps)
        return;
    const QJsonArray in = arch.value(QString::fromUtf8("input_modalities")).toArray();
    for (int i = 0; i < in.size(); ++i)
    {
        const QString m = in.at(i).toString().trimmed().toLower();
        if (!m.isEmpty() && !caps->inputModalities.contains(m))
            caps->inputModalities.append(m);
    }
    const QJsonArray out = arch.value(QString::fromUtf8("output_modalities")).toArray();
    for (int i = 0; i < out.size(); ++i)
    {
        const QString m = out.at(i).toString().trimmed().toLower();
        if (!m.isEmpty() && !caps->outputModalities.contains(m))
            caps->outputModalities.append(m);
    }
}

//----------------------------------------------

static void applySupportedParameters(ModelCapabilities *caps, const QJsonValue &paramsVal)
{
    if (!caps)
        return;
    if (paramsVal.isObject())
    {
        const QJsonObject params = paramsVal.toObject();
        if (params.contains(QString::fromUtf8("input_references")))
        {
            const QJsonObject refs = params.value(QString::fromUtf8("input_references")).toObject();
            caps->inputReferencesMin = refs.value(QString::fromUtf8("min")).toInt(0);
            caps->inputReferencesMax = refs.value(QString::fromUtf8("max")).toInt(0);
            if (caps->inputReferencesMax > 0
                && !caps->inputModalities.contains(QString::fromUtf8("image")))
                caps->inputModalities.append(QString::fromUtf8("image"));
        }
        if (params.contains(QString::fromUtf8("n")))
        {
            const QJsonObject n = params.value(QString::fromUtf8("n")).toObject();
            caps->nMin = n.value(QString::fromUtf8("min")).toInt(-1);
            caps->nMax = n.value(QString::fromUtf8("max")).toInt(-1);
            // n max==1 часто отвергается провайдером как параметр — считаем
            // supportsN только если можно запросить больше одной картинки.
            caps->supportsN = (caps->nMax > 1);
        }
        if (params.contains(QString::fromUtf8("size")))
        {
            const QJsonValue size = params.value(QString::fromUtf8("size"));
            if (size.isBool())
                caps->supportsSize = size.toBool();
            else if (size.isObject())
                caps->supportsSize = true;
        }
        if (params.contains(QString::fromUtf8("output_format")))
        {
            const QJsonObject fmt = params.value(QString::fromUtf8("output_format")).toObject();
            const QJsonArray values = fmt.value(QString::fromUtf8("values")).toArray();
            for (int i = 0; i < values.size(); ++i)
                caps->outputFormats.append(values.at(i).toString());
        }
        return;
    }
    // LLM /models: supported_parameters часто массив строк.
    if (paramsVal.isArray())
    {
        const QJsonArray arr = paramsVal.toArray();
        for (int i = 0; i < arr.size(); ++i)
        {
            const QString p = arr.at(i).toString().toLower();
            if (p == QString::fromUtf8("n"))
                caps->supportsN = true;
        }
    }
}

//----------------------------------------------

static ModelCapabilities parseImagesModelObject(const QJsonObject &obj)
{
    ModelCapabilities caps;
    caps.source = QString::fromUtf8("openrouter_images");
    applyArchitecture(&caps, obj.value(QString::fromUtf8("architecture")).toObject());
    applySupportedParameters(&caps, obj.value(QString::fromUtf8("supported_parameters")));
    if (caps.inputModalities.isEmpty())
        caps.inputModalities << QString::fromUtf8("text");
    if (caps.outputModalities.isEmpty())
        caps.outputModalities << QString::fromUtf8("image");
    caps.known = true;
    return caps;
}

//----------------------------------------------

static ModelCapabilities parseLlmModelObject(const QJsonObject &obj)
{
    ModelCapabilities caps;
    caps.source = QString::fromUtf8("openrouter_models");
    applyArchitecture(&caps, obj.value(QString::fromUtf8("architecture")).toObject());
    applySupportedParameters(&caps, obj.value(QString::fromUtf8("supported_parameters")));
    // Старый формат: architecture.modality = "text+image->text"
    const QString modality = obj.value(QString::fromUtf8("architecture")).toObject()
                                 .value(QString::fromUtf8("modality")).toString().toLower();
    if (!modality.isEmpty() && caps.inputModalities.isEmpty())
    {
        const int arrow = modality.indexOf(QString::fromUtf8("->"));
        const QString inPart = arrow >= 0 ? modality.left(arrow) : modality;
        foreach (const QString &token, inPart.split(QChar('+')))
        {
            const QString t = token.trimmed();
            if (!t.isEmpty())
                caps.inputModalities.append(t);
        }
        if (arrow >= 0)
        {
            foreach (const QString &token, modality.mid(arrow + 2).split(QChar('+')))
            {
                const QString t = token.trimmed();
                if (!t.isEmpty())
                    caps.outputModalities.append(t);
            }
        }
    }
    if (caps.inputModalities.isEmpty())
        caps.inputModalities << QString::fromUtf8("text");
    if (caps.outputModalities.isEmpty())
        caps.outputModalities << QString::fromUtf8("text");
    caps.known = true;
    return caps;
}

//----------------------------------------------

static bool modelIdMatches(QString catalogId, QString wanted)
{
    const QString a = catalogId.trimmed().toLower();
    const QString b = wanted.trimmed().toLower();
    if (a.isEmpty() || b.isEmpty())
        return false;
    if (a == b)
        return true;
    if (a.endsWith(QChar('/') + b) || b.endsWith(QChar('/') + a))
        return true;
    // slug без провайдера
    const int slashA = a.lastIndexOf(QChar('/'));
    const int slashB = b.lastIndexOf(QChar('/'));
    const QString slugA = slashA >= 0 ? a.mid(slashA + 1) : a;
    const QString slugB = slashB >= 0 ? b.mid(slashB + 1) : b;
    return slugA == slugB;
}

//----------------------------------------------

ModelCapabilities ModelCapabilityService::fetchOpenRouterImages(const LlmProfile &profile) const
{
    ModelCapabilities empty;
    int status = 0;
    QString error;
    const QString url = joinApi(profile.baseUrl, QString::fromUtf8("/images/models"));
    const QByteArray body = httpGetSync(url, profile.apiKey, &status, &error);
    if (body.isEmpty())
        return empty;

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return empty;
    const QJsonArray data = doc.object().value(QString::fromUtf8("data")).toArray();
    for (int i = 0; i < data.size(); ++i)
    {
        const QJsonObject obj = data.at(i).toObject();
        if (modelIdMatches(obj.value(QString::fromUtf8("id")).toString(), profile.model))
            return parseImagesModelObject(obj);
    }
    return empty;
}

//----------------------------------------------

ModelCapabilities ModelCapabilityService::fetchOpenRouterModels(const LlmProfile &profile) const
{
    ModelCapabilities empty;
    int status = 0;
    QString error;
    const QString url = joinApi(profile.baseUrl, QString::fromUtf8("/models"));
    const QByteArray body = httpGetSync(url, profile.apiKey, &status, &error);
    if (body.isEmpty())
        return empty;

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return empty;
    const QJsonArray data = doc.object().value(QString::fromUtf8("data")).toArray();
    for (int i = 0; i < data.size(); ++i)
    {
        const QJsonObject obj = data.at(i).toObject();
        if (modelIdMatches(obj.value(QString::fromUtf8("id")).toString(), profile.model))
            return parseLlmModelObject(obj);
    }
    return empty;
}

//----------------------------------------------

ModelCapabilities ModelCapabilityService::cachedOrHeuristic(const LlmProfile &profile) const
{
    const QString key = cacheKey(profile);
    if (_cache.contains(key))
        return _cache.value(key);
    return heuristicFor(profile);
}

//----------------------------------------------

ModelCapabilities ModelCapabilityService::fetchOpenRouterPublicByModel(QString modelId) const
{
    ModelCapabilities empty;
    if (modelId.trimmed().isEmpty())
        return empty;
    // Публичный каталог OpenRouter — у многих провайдеров нет architecture в /models.
    LlmProfile probe;
    probe.baseUrl = QString::fromUtf8("https://openrouter.ai/api/v1");
    probe.model = modelId.trimmed();
    ModelCapabilities caps = fetchOpenRouterModels(probe);
    if (!caps.known)
    {
        // Попробовать с типичными префиксами провайдера.
        static const char *kPrefixes[] = {
            "qwen/", "deepseek/", "openai/", "google/", "anthropic/", "meta-llama/", 0
        };
        const QString slug = modelId.contains(QChar('/'))
            ? modelId.mid(modelId.lastIndexOf(QChar('/')) + 1)
            : modelId;
        for (int i = 0; kPrefixes[i] && !caps.known; ++i)
        {
            probe.model = QString::fromUtf8(kPrefixes[i]) + slug;
            caps = fetchOpenRouterModels(probe);
        }
    }
    if (caps.known)
        caps.source = QString::fromUtf8("openrouter_public");
    return caps;
}

//----------------------------------------------

ModelCapabilities ModelCapabilityService::capabilitiesFor(const LlmProfile &profile,
                                                          bool forceRefresh)
{
    const QString key = cacheKey(profile);
    if (!forceRefresh && _cache.contains(key))
        return _cache.value(key);

    ModelCapabilities caps;
    const QString base = profile.baseUrl.toLower();
    const bool openRouter = base.contains(QString::fromUtf8("openrouter.ai"));

    if (openRouter || profile.modality == WorkerTaskKindImageGeneration
        || profile.modality == WorkerTaskKindVideoGeneration)
    {
        if (profile.modality == WorkerTaskKindImageGeneration
            || profile.modality == WorkerTaskKindVideoGeneration)
            caps = fetchOpenRouterImages(profile);
        if (!caps.known)
            caps = fetchOpenRouterModels(profile);
    }
    else if (!profile.baseUrl.trimmed().isEmpty())
    {
        // Совместимые каталоги /models у своего провайдера
        caps = fetchOpenRouterModels(profile);
    }

    // DeepSeek/DashScope/Qwen native /models часто без architecture —
    // уточняем по публичному каталогу OpenRouter (vl/omni/vision).
    if (!openRouter
        && (!caps.known
            || (caps.inputModalities.size() == 1
                && caps.inputModalities.first() == QString::fromUtf8("text"))))
    {
        const ModelCapabilities pub = fetchOpenRouterPublicByModel(profile.model);
        if (pub.known
            && (pub.acceptsImage() || pub.acceptsAudio() || pub.acceptsVideo()))
            caps = pub;
    }

    if (!caps.known)
        caps = heuristicFor(profile);
    else
    {
        // У LLM-агента файлы всегда доступны через инструменты проекта.
        if ((profile.modality == WorkerTaskKindLlm
             || profile.modality == WorkerTaskKindDecisions
             || profile.modality == WorkerTaskKindOther)
            && !caps.inputModalities.contains(QString::fromUtf8("file")))
            caps.inputModalities.append(QString::fromUtf8("file"));
    }

    _cache.insert(key, caps);
    return caps;
}
