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

static QString withLocalOutput(WorkerTaskKind kind, QString providerLine)
{
    const bool savesFile = kind == WorkerTaskKindImageGeneration
                           || kind == WorkerTaskKindSpeech
                           || kind == WorkerTaskKindAudioGeneration
                           || kind == WorkerTaskKindVideoGeneration;
    if (!savesFile)
        return providerLine;
    if (providerLine.contains(QString::fromUtf8("output_path")))
        return providerLine;
    if (!providerLine.isEmpty())
        providerLine += QString::fromUtf8(", ");
    providerLine += QString::fromUtf8("output_path (local file, not sent to the provider)");
    return providerLine;
}

//----------------------------------------------

static bool modalityListed(const ModelCapabilities &caps, const char *name)
{
    const QString key = QString::fromUtf8(name);
    return caps.inputModalities.contains(key, Qt::CaseInsensitive)
           || caps.referenceModalities.contains(key);
}

//----------------------------------------------

QStringList referenceCategoriesFor(WorkerTaskKind kind, const ModelCapabilities &caps)
{
    QStringList out;
    const bool image = modalityListed(caps, "image") || caps.allowsImageReference();
    const bool audio = modalityListed(caps, "audio");
    const bool video = modalityListed(caps, "video");
    switch (kind)
    {
    case WorkerTaskKindImageGeneration:
        if (image)
            out << QString::fromUtf8("image");
        break;
    case WorkerTaskKindVideoGeneration:
        if (image)
            out << QString::fromUtf8("image");
        if (audio)
            out << QString::fromUtf8("audio");
        if (video)
            out << QString::fromUtf8("video");
        break;
    case WorkerTaskKindAudioGeneration:
        if (audio)
            out << QString::fromUtf8("audio");
        if (image)
            out << QString::fromUtf8("image");
        break;
    case WorkerTaskKindSpeech:
        if (caps.acceptsText() && audio
            && caps.outputModalities.contains(QString::fromUtf8("audio"),
                                              Qt::CaseInsensitive))
            out << QString::fromUtf8("audio");
        break;
    default:
        break;
    }
    return out;
}

//----------------------------------------------

static int referenceMinOf(const ModelCapabilities &caps, const QString &category)
{
    int minCount = caps.referenceMin.value(category, 0);
    if (category == QString::fromUtf8("image") && caps.inputReferencesMin > minCount)
        minCount = caps.inputReferencesMin;
    return minCount;
}

//----------------------------------------------

static int referenceMaxOf(const ModelCapabilities &caps, const QString &category)
{
    int maxCount = caps.referenceMax.value(category, 0);
    if (category == QString::fromUtf8("image") && caps.inputReferencesMax > maxCount)
        maxCount = caps.inputReferencesMax;
    return maxCount;
}

//----------------------------------------------

static QString referenceArgsLine(WorkerTaskKind kind, const ModelCapabilities &caps)
{
    const QStringList categories = referenceCategoriesFor(kind, caps);
    QStringList keys;
    foreach (const QString &category, categories)
    {
        const int minCount = referenceMinOf(caps, category);
        const int maxCount = referenceMaxOf(caps, category);
        QString how;
        if (minCount >= 1 && maxCount > 1)
            how = QString::fromUtf8("required, %1..%2 project files").arg(minCount).arg(maxCount);
        else if (minCount >= 1)
            how = QString::fromUtf8("required, project file");
        else if (maxCount > 1)
            how = QString::fromUtf8("optional, up to %1 project files").arg(maxCount);
        else
            how = QString::fromUtf8("optional, project file");
        if (category == QString::fromUtf8("image"))
            how += QString::fromUtf8("; alias reference_path");
        keys << QString::fromUtf8("reference_%1 (%2)").arg(category, how);
    }
    return keys.join(QString::fromUtf8(", "));
}

//----------------------------------------------

static QString finishMediaParams(WorkerTaskKind kind, const ModelCapabilities &caps, QString line)
{
    const QString refs = referenceArgsLine(kind, caps);
    if (!refs.isEmpty())
    {
        if (!line.isEmpty())
            line += QString::fromUtf8(", ");
        line += refs;
    }
    return withLocalOutput(kind, line);
}

//----------------------------------------------

QString mediaParamsLine(WorkerTaskKind kind, const ModelCapabilities &caps)
{
    if (!caps.providerParams.trimmed().isEmpty()
        && kind != WorkerTaskKindLlm
        && kind != WorkerTaskKindDecisions
        && kind != WorkerTaskKindOther)
        return finishMediaParams(kind, caps, caps.providerParams.trimmed());

    switch (kind)
    {
    case WorkerTaskKindImageGeneration:
    {
        QStringList keys;
        keys << QString::fromUtf8("prompt (required)");
        keys << QString::fromUtf8("output_path");
        if (caps.supportsSize)
            keys << QString::fromUtf8("size");
        if (caps.supportsN && caps.nMax > 0)
        {
            keys << QString::fromUtf8("n (%1..%2)")
                        .arg(caps.nMin > 0 ? caps.nMin : 1)
                        .arg(caps.nMax);
        }
        else if (caps.supportsN)
            keys << QString::fromUtf8("n");
        if (!caps.outputFormats.isEmpty())
        {
            keys << QString::fromUtf8("output_format (%1)")
                        .arg(caps.outputFormats.join(QString::fromUtf8("|")));
        }
        return finishMediaParams(kind, caps, keys.join(QString::fromUtf8(", ")));
    }
    case WorkerTaskKindSpeech:
    {
        QString line;
        if (caps.acceptsAudio() && !caps.acceptsText())
            line = QString::fromUtf8("path (required, existing audio to transcribe)");
        else if (caps.acceptsText()
                 && caps.outputModalities.contains(QString::fromUtf8("audio")))
        {
            line = QString::fromUtf8(
                "text (required, words to speak), voice, format, output_path, instructions");
        }
        else
        {
            line = QString::fromUtf8(
                "text (required, words to speak), voice, format, output_path, instructions; "
                "or path (existing audio) to transcribe");
        }
        const QString refs = referenceArgsLine(kind, caps);
        if (!refs.isEmpty())
            line += QString::fromUtf8(", ") + refs;
        return line;
    }
    case WorkerTaskKindAudioGeneration:
        return finishMediaParams(
            kind, caps,
            QString::fromUtf8("prompt (required), duration (seconds), output_path"));
    case WorkerTaskKindVideoGeneration:
        return finishMediaParams(
            kind, caps,
            QString::fromUtf8("prompt (required), seconds, size, output_path"));
    case WorkerTaskKindEmbedding:
        return QString::fromUtf8("text (required)");
    default:
        return QString();
    }
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
           + workerTaskKindToString(profile.modality) + QChar('|')
           + profile.catalogUrl.trimmed().toLower();
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

        // Vision / multimodal LLM.
        // DeepSeek V4.1 Flash (и алиасы deepseek-flash / deepseek-v4-flash*) —
        // нативный image+text; deepseek-chat / reasoner — только текст.
        const bool deepseekVision =
            model == QString::fromUtf8("deepseek-flash")
            || model.startsWith(QString::fromUtf8("deepseek-v4-flash"))
            || model.startsWith(QString::fromUtf8("deepseek-v4.1"))
            || model.startsWith(QString::fromUtf8("deepseek-v4_1"))
            || (blob.contains(QString::fromUtf8("deepseek"))
                && (blob.contains(QString::fromUtf8("vision"))
                    || blob.contains(QString::fromUtf8("vl"))));
        const bool visionHint =
            deepseekVision
            || blob.contains(QString::fromUtf8("vision"))
            || blob.contains(QString::fromUtf8("-vl"))
            || blob.contains(QString::fromUtf8("_vl"))
            || blob.contains(QString::fromUtf8("vl-"))
            || blob.contains(QString::fromUtf8("vl."))
            || blob.contains(QString::fromUtf8("qwen2-vl"))
            || blob.contains(QString::fromUtf8("qwen2.5-vl"))
            || blob.contains(QString::fromUtf8("qwen3-vl"))
            || blob.contains(QString::fromUtf8("qwen-vl"))
            || blob.contains(QString::fromUtf8("qwen2.5-omni"))
            || blob.contains(QString::fromUtf8("omni"))
            || blob.contains(QString::fromUtf8("4o"))
            || blob.contains(QString::fromUtf8("gpt-4.1"))
            || blob.contains(QString::fromUtf8("gpt-5"))
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
            || blob.contains(QString::fromUtf8("qwen2-audio"))
            || blob.contains(QString::fromUtf8("gpt-4o-audio"))
            || (blob.contains(QString::fromUtf8("audio"))
                && !blob.contains(QString::fromUtf8("whisper"))))
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

static void appendModality(QStringList *list, QString modality)
{
    if (!list)
        return;
    const QString m = modality.trimmed().toLower();
    if (m.isEmpty() || list->contains(m))
        return;
    // Нормализация синонимов каталогов.
    if (m == QString::fromUtf8("images") || m == QString::fromUtf8("vision"))
    {
        if (!list->contains(QString::fromUtf8("image")))
            list->append(QString::fromUtf8("image"));
        return;
    }
    if (m == QString::fromUtf8("audios"))
    {
        if (!list->contains(QString::fromUtf8("audio")))
            list->append(QString::fromUtf8("audio"));
        return;
    }
    if (m == QString::fromUtf8("videos"))
    {
        if (!list->contains(QString::fromUtf8("video")))
            list->append(QString::fromUtf8("video"));
        return;
    }
    list->append(m);
}

//----------------------------------------------

static void applyModalityArray(QStringList *list, const QJsonValue &val)
{
    if (!list)
        return;
    if (val.isArray())
    {
        const QJsonArray arr = val.toArray();
        for (int i = 0; i < arr.size(); ++i)
            appendModality(list, arr.at(i).toString());
        return;
    }
    // "text+image" / "text,image"
    if (val.isString())
    {
        QString s = val.toString().toLower();
        s.replace(QChar('+'), QChar(','));
        s.replace(QChar('|'), QChar(','));
        s.replace(QChar(' '), QChar(','));
        foreach (const QString &token, s.split(QChar(',')))
            appendModality(list, token);
    }
}

//----------------------------------------------

static void applyArchitecture(ModelCapabilities *caps, const QJsonObject &arch)
{
    if (!caps)
        return;
    applyModalityArray(&caps->inputModalities,
                       arch.value(QString::fromUtf8("input_modalities")));
    applyModalityArray(&caps->outputModalities,
                       arch.value(QString::fromUtf8("output_modalities")));
}

//----------------------------------------------

/**
 * @brief Modalities с корня объекта модели (DeepSeek /models) и из architecture (OpenRouter).
 */
static void applyModelModalities(ModelCapabilities *caps, const QJsonObject &obj)
{
    if (!caps)
        return;
    // DeepSeek: input_modalities / output_modalities на корне.
    applyModalityArray(&caps->inputModalities,
                       obj.value(QString::fromUtf8("input_modalities")));
    applyModalityArray(&caps->outputModalities,
                       obj.value(QString::fromUtf8("output_modalities")));
    // Иногда modalities / supported_input_modalities.
    applyModalityArray(&caps->inputModalities,
                       obj.value(QString::fromUtf8("modalities")));
    applyModalityArray(&caps->inputModalities,
                       obj.value(QString::fromUtf8("supported_input_modalities")));
    applyModalityArray(&caps->outputModalities,
                       obj.value(QString::fromUtf8("supported_output_modalities")));

    const QJsonObject arch = obj.value(QString::fromUtf8("architecture")).toObject();
    if (!arch.isEmpty())
        applyArchitecture(caps, arch);

    // Старый OpenRouter: architecture.modality = "text+image->text"
    const QString modality = arch.value(QString::fromUtf8("modality")).toString().toLower();
    if (!modality.isEmpty()
        && caps->inputModalities.isEmpty())
    {
        const int arrow = modality.indexOf(QString::fromUtf8("->"));
        const QString inPart = arrow >= 0 ? modality.left(arrow) : modality;
        applyModalityArray(&caps->inputModalities, inPart);
        if (arrow >= 0)
            applyModalityArray(&caps->outputModalities, modality.mid(arrow + 2));
    }
}

//----------------------------------------------

static QString jsonScalarText(const QJsonValue &value)
{
    if (value.isString())
        return value.toString().trimmed();
    if (value.isBool())
        return value.toBool() ? QString::fromUtf8("true") : QString::fromUtf8("false");
    if (value.isDouble())
    {
        const double n = value.toDouble();
        if (n == static_cast<double>(static_cast<int>(n)))
            return QString::number(static_cast<int>(n));
        return QString::number(n);
    }
    return QString();
}

//----------------------------------------------

static QString joinEnumValues(const QJsonArray &values)
{
    QStringList parts;
    for (int i = 0; i < values.size() && parts.size() < 8; ++i)
    {
        const QString text = jsonScalarText(values.at(i));
        if (!text.isEmpty())
            parts << text;
    }
    if (parts.isEmpty())
        return QString();
    QString line = parts.join(QString::fromUtf8("|"));
    if (values.size() > parts.size())
        line += QString::fromUtf8("+%1").arg(values.size() - parts.size());
    return line;
}

//----------------------------------------------

static QString schemaFieldLine(const QString &name, const QJsonObject &prop, bool required)
{
    QStringList bits;
    bits << (required ? QString::fromUtf8("required") : QString::fromUtf8("optional"));
    const QString enums = joinEnumValues(prop.value(QString::fromUtf8("enum")).toArray());
    if (!enums.isEmpty())
        bits << enums;
    else
    {
        const QString itemEnums = joinEnumValues(
            prop.value(QString::fromUtf8("items")).toObject()
                .value(QString::fromUtf8("enum")).toArray());
        if (!itemEnums.isEmpty())
            bits << itemEnums;
    }
    if (prop.contains(QString::fromUtf8("minimum"))
        || prop.contains(QString::fromUtf8("maximum")))
    {
        const QString minText = jsonScalarText(prop.value(QString::fromUtf8("minimum")));
        const QString maxText = jsonScalarText(prop.value(QString::fromUtf8("maximum")));
        if (!minText.isEmpty() || !maxText.isEmpty())
            bits << QString::fromUtf8("%1..%2").arg(minText, maxText);
    }
    if (prop.contains(QString::fromUtf8("minItems"))
        || prop.contains(QString::fromUtf8("maxItems")))
    {
        const int minItems = prop.value(QString::fromUtf8("minItems")).toInt(0);
        const int maxItems = prop.value(QString::fromUtf8("maxItems")).toInt(0);
        if (maxItems > 0)
            bits << QString::fromUtf8("%1..%2 items").arg(minItems).arg(maxItems);
    }
    const QString def = jsonScalarText(prop.value(QString::fromUtf8("default")));
    if (!def.isEmpty() && def.size() <= 24)
        bits << QString::fromUtf8("default %1").arg(def);
    return QString::fromUtf8("%1 (%2)").arg(name, bits.join(QString::fromUtf8(", ")));
}

//----------------------------------------------

static void noteReference(ModelCapabilities *caps, const QString &modality,
                           int minCount, int maxCount, const QString &field)
{
    if (!caps)
        return;
    const QString kind = modality.trimmed().toLower();
    if (kind != QString::fromUtf8("image")
        && kind != QString::fromUtf8("audio")
        && kind != QString::fromUtf8("video"))
        return;
    if (!caps->referenceModalities.contains(kind))
        caps->referenceModalities.append(kind);
    if (!caps->inputModalities.contains(kind))
        caps->inputModalities.append(kind);
    if (minCount > caps->referenceMin.value(kind, 0))
        caps->referenceMin.insert(kind, minCount);
    if (maxCount > caps->referenceMax.value(kind, 0))
        caps->referenceMax.insert(kind, maxCount);
    if (!field.trimmed().isEmpty() && !caps->referenceField.contains(kind))
        caps->referenceField.insert(kind, field.trimmed());
    if (kind == QString::fromUtf8("image"))
    {
        if (minCount > caps->inputReferencesMin)
            caps->inputReferencesMin = minCount;
        if (maxCount > caps->inputReferencesMax)
            caps->inputReferencesMax = maxCount;
    }
}

//----------------------------------------------

static QString referenceModalityForProperty(const QString &name, const QJsonObject &prop)
{
    const QString type = prop.value(QString::fromUtf8("type")).toString().toLower();
    if (type == QString::fromUtf8("number")
        || type == QString::fromUtf8("integer")
        || type == QString::fromUtf8("boolean"))
        return QString();
    const QString n = name.trimmed().toLower();
    if (n == QString::fromUtf8("input_references")
        || n == QString::fromUtf8("image_urls")
        || n == QString::fromUtf8("image_url")
        || n == QString::fromUtf8("images")
        || n == QString::fromUtf8("image")
        || n == QString::fromUtf8("init_image")
        || n == QString::fromUtf8("reference_image")
        || n == QString::fromUtf8("reference_images")
        || n == QString::fromUtf8("input_image")
        || n == QString::fromUtf8("input_images"))
        return QString::fromUtf8("image");
    if (n == QString::fromUtf8("audio_url")
        || n == QString::fromUtf8("audio_urls")
        || n == QString::fromUtf8("input_audio")
        || n == QString::fromUtf8("reference_audio")
        || n == QString::fromUtf8("reference_audios"))
        return QString::fromUtf8("audio");
    if (n == QString::fromUtf8("video_url")
        || n == QString::fromUtf8("video_urls")
        || n == QString::fromUtf8("input_video")
        || n == QString::fromUtf8("reference_video")
        || n == QString::fromUtf8("reference_videos"))
        return QString::fromUtf8("video");
    return QString();
}

//----------------------------------------------

static void noteSchemaProperty(ModelCapabilities *caps, const QString &name,
                               const QJsonObject &prop, bool required)
{
    if (!caps)
        return;
    if (name == QString::fromUtf8("size") || name == QString::fromUtf8("aspect_ratio"))
        caps->supportsSize = true;
    if (name == QString::fromUtf8("n"))
    {
        if (prop.contains(QString::fromUtf8("minimum")))
            caps->nMin = prop.value(QString::fromUtf8("minimum")).toInt(caps->nMin);
        if (prop.contains(QString::fromUtf8("maximum")))
            caps->nMax = prop.value(QString::fromUtf8("maximum")).toInt(caps->nMax);
        caps->supportsN = caps->nMax > 1;
    }
    if (name == QString::fromUtf8("output_format"))
    {
        const QJsonArray values = prop.value(QString::fromUtf8("enum")).toArray();
        for (int i = 0; i < values.size(); ++i)
        {
            const QString text = jsonScalarText(values.at(i));
            if (!text.isEmpty() && !caps->outputFormats.contains(text))
                caps->outputFormats.append(text);
        }
    }
    const QString refKind = referenceModalityForProperty(name, prop);
    if (!refKind.isEmpty())
    {
        int minItems = prop.value(QString::fromUtf8("minItems")).toInt(0);
        int maxItems = prop.value(QString::fromUtf8("maxItems")).toInt(0);
        const QString type = prop.value(QString::fromUtf8("type")).toString().toLower();
        if (maxItems <= 0)
            maxItems = 1;
        if (required && minItems < 1 && type != QString::fromUtf8("array"))
            minItems = 1;
        noteReference(caps, refKind, minItems, maxItems, name);
    }
}

//----------------------------------------------

static void noteSchemaCombinations(ModelCapabilities *caps, const QJsonObject &schema)
{
    if (!caps)
        return;
    const QJsonObject props = schema.value(QString::fromUtf8("properties")).toObject();
    const char *keys[] = { "anyOf", "oneOf", "allOf", 0 };
    for (int k = 0; keys[k]; ++k)
    {
        const QJsonArray combos = schema.value(QString::fromUtf8(keys[k])).toArray();
        for (int i = 0; i < combos.size(); ++i)
        {
            const QJsonObject combo = combos.at(i).toObject();
            const QJsonArray required = combo.value(QString::fromUtf8("required")).toArray();
            for (int r = 0; r < required.size(); ++r)
            {
                const QString name = required.at(r).toString();
                const QJsonObject prop = props.value(name).toObject();
                if (!referenceModalityForProperty(name, prop).isEmpty())
                    noteSchemaProperty(caps, name, prop, false);
            }
            if (combo.contains(QString::fromUtf8("properties")))
                noteSchemaCombinations(caps, combo);
        }
    }
}

//----------------------------------------------

static QString schemaParamsLine(const QJsonObject &schema, ModelCapabilities *caps)
{
    noteSchemaCombinations(caps, schema);
    const QJsonObject props = schema.value(QString::fromUtf8("properties")).toObject();
    if (props.isEmpty())
        return QString();
    QStringList requiredNames;
    const QJsonArray required = schema.value(QString::fromUtf8("required")).toArray();
    for (int i = 0; i < required.size(); ++i)
        requiredNames << required.at(i).toString();

    QStringList requiredLines;
    QStringList optionalLines;
    foreach (const QString &name, props.keys())
    {
        if (name == QString::fromUtf8("model"))
            continue;
        const QJsonObject prop = props.value(name).toObject();
        const bool isRequired = requiredNames.contains(name);
        noteSchemaProperty(caps, name, prop, isRequired);
        const QString line = schemaFieldLine(name, prop, isRequired);
        if (isRequired)
            requiredLines << line;
        else
            optionalLines << line;
    }
    requiredLines << optionalLines;
    return requiredLines.join(QString::fromUtf8(", "));
}

//----------------------------------------------

static QString supportedDescriptorLine(const QString &name, const QJsonValue &value)
{
    if (value.isBool())
        return value.toBool() ? name : QString();
    if (value.isArray())
    {
        const QString enums = joinEnumValues(value.toArray());
        if (enums.isEmpty())
            return name;
        return QString::fromUtf8("%1 (%2)").arg(name, enums);
    }
    if (!value.isObject())
        return name;
    const QJsonObject obj = value.toObject();
    const QString type = obj.value(QString::fromUtf8("type")).toString();
    const QString enums = joinEnumValues(obj.value(QString::fromUtf8("values")).toArray());
    if (!enums.isEmpty() || type == QString::fromUtf8("enum"))
    {
        if (enums.isEmpty())
            return name;
        return QString::fromUtf8("%1 (%2)").arg(name, enums);
    }
    if (type == QString::fromUtf8("range")
        || obj.contains(QString::fromUtf8("min"))
        || obj.contains(QString::fromUtf8("max")))
    {
        const QString minText = jsonScalarText(obj.value(QString::fromUtf8("min")));
        const QString maxText = jsonScalarText(obj.value(QString::fromUtf8("max")));
        if (minText.isEmpty() && maxText.isEmpty())
            return name;
        return QString::fromUtf8("%1 (%2..%3)").arg(name, minText, maxText);
    }
    if (type == QString::fromUtf8("boolean"))
        return name;
    const QJsonArray legacy = obj.value(QString::fromUtf8("values")).toArray();
    const QString legacyEnums = joinEnumValues(legacy);
    if (!legacyEnums.isEmpty())
        return QString::fromUtf8("%1 (%2)").arg(name, legacyEnums);
    return name;
}

//----------------------------------------------

static QString supportedParamsLine(const QJsonValue &paramsVal)
{
    if (!paramsVal.isObject())
        return QString();
    const QJsonObject params = paramsVal.toObject();
    QStringList lines;
    foreach (const QString &name, params.keys())
    {
        const QString line = supportedDescriptorLine(name, params.value(name));
        if (!line.isEmpty())
            lines << line;
    }
    return lines.join(QString::fromUtf8(", "));
}

//----------------------------------------------

static QJsonObject parameterSchemaObject(const QJsonObject &model)
{
    const QJsonValue params = model.value(QString::fromUtf8("parameters"));
    if (!params.isObject())
        return QJsonObject();
    const QJsonObject obj = params.toObject();
    const QJsonValue input = obj.value(QString::fromUtf8("input_schema"));
    if (input.isObject())
        return input.toObject();
    if (obj.contains(QString::fromUtf8("properties")))
        return obj;
    return QJsonObject();
}

//----------------------------------------------

static void applySupportedParameters(ModelCapabilities *caps, const QJsonValue &paramsVal);

//----------------------------------------------

static QString catalogRequestEndpoint(const QJsonObject &model)
{
    // Каталог может сообщить путь запроса рядом со схемой параметров.
    const QJsonObject params = model.value(QString::fromUtf8("parameters")).toObject();
    QString endpoint = params.value(QString::fromUtf8("endpoint")).toString().trimmed();
    if (endpoint.isEmpty())
        endpoint = params.value(QString::fromUtf8("path")).toString().trimmed();
    if (endpoint.isEmpty())
        endpoint = model.value(QString::fromUtf8("endpoint")).toString().trimmed();
    if (endpoint.isEmpty())
        endpoint = model.value(QString::fromUtf8("request_path")).toString().trimmed();
    return endpoint;
}

//----------------------------------------------

static void applyCapabilityTags(ModelCapabilities *caps, const QJsonValue &tagsVal)
{
    if (!caps || !tagsVal.isArray())
        return;
    const QJsonArray tags = tagsVal.toArray();
    for (int i = 0; i < tags.size(); ++i)
    {
        QString tag = tags.at(i).toString().trimmed().toLower();
        tag.replace(QChar('_'), QChar(' '));
        if (tag.contains(QString::fromUtf8("image to video"))
            || tag.contains(QString::fromUtf8("image-to-video"))
            || tag.contains(QString::fromUtf8("image to image"))
            || tag.contains(QString::fromUtf8("image-to-image")))
            noteReference(caps, QString::fromUtf8("image"), 0, 1, QString());
        else if (tag.contains(QString::fromUtf8("video to video"))
                 || tag.contains(QString::fromUtf8("video-to-video")))
            noteReference(caps, QString::fromUtf8("video"), 0, 1, QString());
        else if (tag.contains(QString::fromUtf8("audio to video"))
                 || tag.contains(QString::fromUtf8("audio-to-video")))
            noteReference(caps, QString::fromUtf8("audio"), 0, 1, QString());
    }
}

//----------------------------------------------

static void applyProviderCatalog(ModelCapabilities *caps, const QJsonObject &model)
{
    if (!caps)
        return;
    applyCapabilityTags(caps, model.value(QString::fromUtf8("capability_tags")));
    applySupportedParameters(caps, model.value(QString::fromUtf8("supported_parameters")));
    const QString endpoint = catalogRequestEndpoint(model);
    if (!endpoint.isEmpty())
        caps->requestEndpoint = endpoint;
    const QString schemaLine = schemaParamsLine(parameterSchemaObject(model), caps);
    const QString supportedLine =
        supportedParamsLine(model.value(QString::fromUtf8("supported_parameters")));
    QString line = !schemaLine.isEmpty() ? schemaLine : supportedLine;
    if (line.size() > 700)
        line = line.left(697) + QString::fromUtf8("...");
    if (!line.isEmpty())
        caps->providerParams = line;
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
            const QJsonValue raw = params.value(QString::fromUtf8("input_references"));
            int minCount = 0;
            int maxCount = 0;
            QStringList kinds;
            if (raw.isObject())
            {
                const QJsonObject refs = raw.toObject();
                minCount = refs.value(QString::fromUtf8("min")).toInt(0);
                maxCount = refs.value(QString::fromUtf8("max")).toInt(0);
                applyModalityArray(&kinds, refs.value(QString::fromUtf8("modalities")));
                applyModalityArray(&kinds, refs.value(QString::fromUtf8("type")));
            }
            if (kinds.isEmpty())
                kinds << QString::fromUtf8("image");
            if (maxCount <= 0)
                maxCount = 1;
            foreach (const QString &kind, kinds)
            {
                noteReference(caps, kind, minCount, maxCount,
                              QString::fromUtf8("input_references"));
            }
        }
        foreach (const QString &key, params.keys())
        {
            if (key == QString::fromUtf8("input_references")
                || key == QString::fromUtf8("n")
                || key == QString::fromUtf8("size")
                || key == QString::fromUtf8("output_format"))
                continue;
            const QString kind = referenceModalityForProperty(key, QJsonObject());
            if (!kind.isEmpty())
                noteReference(caps, kind, 0, 1, key);
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
            const QString kind = referenceModalityForProperty(p, QJsonObject());
            if (!kind.isEmpty())
                noteReference(caps, kind, 0, 1, p);
        }
    }
}

//----------------------------------------------

static ModelCapabilities parseImagesModelObject(const QJsonObject &obj)
{
    ModelCapabilities caps;
    caps.source = QString::fromUtf8("openrouter_images");
    applyModelModalities(&caps, obj);
    applyProviderCatalog(&caps, obj);
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
    caps.source = QString::fromUtf8("provider_models");
    applyModelModalities(&caps, obj);
    applyProviderCatalog(&caps, obj);
    // Если провайдер отдал только id без modalities — не помечаем known,
    // чтобы сработали OpenRouter enrich / эвристика.
    const bool hadModalities = !caps.inputModalities.isEmpty()
                               || !caps.outputModalities.isEmpty()
                               || obj.contains(QString::fromUtf8("input_modalities"))
                               || obj.contains(QString::fromUtf8("output_modalities"))
                               || obj.contains(QString::fromUtf8("architecture"))
                               || obj.contains(QString::fromUtf8("modalities"));
    if (!hadModalities && caps.providerParams.isEmpty())
        return ModelCapabilities();
    if (!hadModalities)
    {
        caps.known = true;
        caps.source = QString::fromUtf8("provider_parameters");
        return caps;
    }

    if (caps.inputModalities.isEmpty())
        caps.inputModalities << QString::fromUtf8("text");
    if (caps.outputModalities.isEmpty())
        caps.outputModalities << QString::fromUtf8("text");
    caps.known = true;
    // DeepSeek и др.: корень объекта — надёжнее OpenRouter-обёртки.
    if (obj.contains(QString::fromUtf8("input_modalities"))
        || obj.contains(QString::fromUtf8("output_modalities")))
        caps.source = QString::fromUtf8("provider_models");
    else if (obj.contains(QString::fromUtf8("architecture")))
        caps.source = QString::fromUtf8("openrouter_models");
    return caps;
}

//----------------------------------------------

static bool onlyTextLikeInputs(const ModelCapabilities &caps)
{
    foreach (const QString &m, caps.inputModalities)
    {
        const QString t = m.toLower();
        if (t != QString::fromUtf8("text") && t != QString::fromUtf8("file"))
            return false;
    }
    return !caps.inputModalities.isEmpty() || caps.known;
}

//----------------------------------------------

static void mergeMissingModalities(ModelCapabilities *dest, const ModelCapabilities &src)
{
    if (!dest)
        return;
    foreach (const QString &m, src.inputModalities)
        appendModality(&dest->inputModalities, m);
    foreach (const QString &m, src.outputModalities)
        appendModality(&dest->outputModalities, m);
    if (src.inputReferencesMin > dest->inputReferencesMin)
        dest->inputReferencesMin = src.inputReferencesMin;
    if (src.inputReferencesMax > dest->inputReferencesMax)
        dest->inputReferencesMax = src.inputReferencesMax;
    foreach (const QString &kind, src.referenceModalities)
    {
        noteReference(dest, kind,
                      src.referenceMin.value(kind, 0),
                      src.referenceMax.value(kind, 0),
                      src.referenceField.value(kind));
    }
    if (dest->providerParams.isEmpty() && !src.providerParams.isEmpty())
        dest->providerParams = src.providerParams;
    if (dest->requestEndpoint.isEmpty() && !src.requestEndpoint.isEmpty())
        dest->requestEndpoint = src.requestEndpoint;
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

static QJsonObject modelObjectFromCatalog(const QByteArray &body, const QString &modelId)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return QJsonObject();
    const QJsonArray data = doc.object().value(QString::fromUtf8("data")).toArray();
    for (int i = 0; i < data.size(); ++i)
    {
        const QJsonObject obj = data.at(i).toObject();
        if (modelIdMatches(obj.value(QString::fromUtf8("id")).toString(), modelId))
            return obj;
    }
    return QJsonObject();
}

//----------------------------------------------

static QString catalogUrlFor(const LlmProfile &profile)
{
    const QString custom = profile.catalogUrl.trimmed();
    if (custom.isEmpty())
        return joinApi(profile.baseUrl, QString::fromUtf8("/models"));
    if (custom.startsWith(QString::fromUtf8("http://"), Qt::CaseInsensitive)
        || custom.startsWith(QString::fromUtf8("https://"), Qt::CaseInsensitive))
        return custom;
    return joinApi(profile.baseUrl, custom);
}

//----------------------------------------------

static QString catalogUrlWithParameterSchema(QString url)
{
    if (url.contains(QString::fromUtf8("expand="), Qt::CaseInsensitive))
        return url;
    const QString sep = url.contains(QChar('?'))
                            ? QString::fromUtf8("&")
                            : QString::fromUtf8("?");
    // Каталоги вроде списка моделей отдают имена без контракта.
    // expand=parameters — необязательный запрос схемы; чужой сервер его игнорирует.
    return url + sep + QString::fromUtf8("expand=parameters");
}

//----------------------------------------------

ModelCapabilities ModelCapabilityService::fetchOpenRouterModels(const LlmProfile &profile) const
{
    ModelCapabilities empty;
    int status = 0;
    QString error;
    const QString url = catalogUrlFor(profile);
    const QByteArray body = httpGetSync(url, profile.apiKey, &status, &error);
    QJsonObject found = modelObjectFromCatalog(body, profile.model);
    if (parameterSchemaObject(found).isEmpty())
    {
        const QString expanded = catalogUrlWithParameterSchema(url);
        if (expanded != url)
        {
            const QByteArray rich = httpGetSync(expanded, profile.apiKey, &status, &error);
            const QJsonObject richer = modelObjectFromCatalog(rich, profile.model);
            if (!richer.isEmpty()
                && (found.isEmpty() || !parameterSchemaObject(richer).isEmpty()))
                found = richer;
        }
    }
    if (found.isEmpty() && !profile.catalogUrl.trimmed().isEmpty())
    {
        // Свой каталог не знает модель — обычный /models хотя бы даст modalities.
        const QString plain = joinApi(profile.baseUrl, QString::fromUtf8("/models"));
        if (plain != url)
            found = modelObjectFromCatalog(httpGetSync(plain, profile.apiKey, &status, &error),
                                           profile.model);
    }
    if (found.isEmpty())
        return empty;
    return parseLlmModelObject(found);
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
        if (!caps.known || caps.providerParams.isEmpty())
        {
            const ModelCapabilities listed = fetchOpenRouterModels(profile);
            if (!caps.known)
                caps = listed;
            else if (listed.known)
            {
                if (!listed.providerParams.isEmpty())
                    caps.providerParams = listed.providerParams;
                if (caps.requestEndpoint.isEmpty())
                    caps.requestEndpoint = listed.requestEndpoint;
                if (!caps.supportsSize && listed.supportsSize)
                    caps.supportsSize = true;
                if (!caps.supportsN && listed.supportsN)
                {
                    caps.supportsN = true;
                    caps.nMin = listed.nMin;
                    caps.nMax = listed.nMax;
                }
                if (caps.outputFormats.isEmpty())
                    caps.outputFormats = listed.outputFormats;
                mergeMissingModalities(&caps, listed);
            }
        }
    }
    else if (!profile.baseUrl.trimmed().isEmpty())
    {
        // Совместимые каталоги /models у своего провайдера
        caps = fetchOpenRouterModels(profile);
    }

    // DeepSeek/DashScope: если /models без modalities или только text —
    // добираем image/audio/video из OpenRouter и эвристики по имени.
    const ModelCapabilities guessed = heuristicFor(profile);
    if (!openRouter && (!caps.known || onlyTextLikeInputs(caps)))
    {
        const ModelCapabilities pub = fetchOpenRouterPublicByModel(profile.model);
        if (pub.known
            && (pub.acceptsImage() || pub.acceptsAudio() || pub.acceptsVideo()))
        {
            if (!caps.known)
                caps = pub;
            else
                mergeMissingModalities(&caps, pub);
        }
    }

    if (!caps.known)
        caps = guessed;
    else if (onlyTextLikeInputs(caps)
             && (guessed.acceptsImage() || guessed.acceptsAudio()
                 || guessed.acceptsVideo()))
    {
        // Алиасы вроде deepseek-v4-flash → V4.1-Flash (vision), пока каталог
        // ещё отдаёт только text: подмешиваем эвристику.
        mergeMissingModalities(&caps, guessed);
        if (caps.source.isEmpty())
            caps.source = QString::fromUtf8("heuristic_merge");
        else
            caps.source += QString::fromUtf8("+heuristic");
    }

    // У LLM-агента файлы всегда доступны через инструменты проекта.
    if ((profile.modality == WorkerTaskKindLlm
         || profile.modality == WorkerTaskKindDecisions
         || profile.modality == WorkerTaskKindOther)
        && !caps.inputModalities.contains(QString::fromUtf8("file"),
                                          Qt::CaseInsensitive))
        caps.inputModalities.append(QString::fromUtf8("file"));

    _cache.insert(key, caps);
    return caps;
}
