#include "modalitytools.h"
//----------------------------------------------
#include "agent.h"
#include "modalityapi.h"
#include "modelcapabilities.h"
#include "projectworkspace.h"
#include "worker.h"
//----------------------------------------------
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMimeDatabase>

//----------------------------------------------

static bool parseObject(QString argumentsJson, QJsonObject *out, QString *error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(argumentsJson.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (error)
            *error = QString::fromUtf8("Invalid JSON arguments: %1").arg(pe.errorString());
        return false;
    }
    *out = doc.object();
    return true;
}

//----------------------------------------------

LlmProfile modalityProfileForWorker(Worker *owner)
{
    LlmProfile profile;
    if (owner && !owner->llmProfileId().trimmed().isEmpty())
        profile = AppSettings::instance()->profileById(owner->llmProfileId());
    if (profile.id.isEmpty() && owner)
        profile = AppSettings::instance()->profileForWorkerType(owner->type());
    return profile;
}

//----------------------------------------------

static QString extractPrompt(const QJsonObject &args, QString taskFallback)
{
    QString prompt = args.value(QString::fromUtf8("prompt")).toString().trimmed();
    if (prompt.isEmpty())
        prompt = args.value(QString::fromUtf8("text")).toString().trimmed();
    if (prompt.isEmpty())
        prompt = args.value(QString::fromUtf8("input")).toString().trimmed();
    if (prompt.isEmpty())
        prompt = taskFallback.trimmed();
    return prompt;
}

//----------------------------------------------

static bool ttsMetaLine(const QString &line)
{
    const QString t = line.trimmed();
    if (t.isEmpty())
        return false;
    QRegExp re(QString::fromUtf8(
        "^(voice(_id|_description)?|format|instructions?|style|speed|model|output(_path)?|"
        "response_format|голос|формат|указания|инструкция|стиль|скорость|модель|"
        "описание\\s+голоса)\\s*[:\\-]"),
               Qt::CaseInsensitive);
    return re.indexIn(t) >= 0;
}

//----------------------------------------------

static bool ttsContentMarkerLine(const QString &line, QString *restOut)
{
    const QString t = line.trimmed();
    QRegExp re(QString::fromUtf8(
        "^(text\\s+to\\s+speak|speak|script|spoken\\s+text|speech\\s+text|utterance|content|"
        "input\\s+text|озвуч(ить|и|ка)|текст\\s+для\\s+озвучки|сказать|прочитать|"
        "синтезировать)\\s*[:\\-]\\s*(.*)$"),
               Qt::CaseInsensitive);
    if (re.indexIn(t) < 0)
        return false;
    if (restOut)
        *restOut = re.cap(2).trimmed();
    return true;
}

//----------------------------------------------

static QString ttsSpeakTextFromObject(const QJsonObject &obj)
{
    static const char *keys[] = {
        "text", "script", "spoken_text", "speech_text", "utterance", "content", "input_text", 0
    };
    for (int i = 0; keys[i]; ++i)
    {
        const QString v = obj.value(QString::fromUtf8(keys[i])).toString().trimmed();
        if (!v.isEmpty())
            return v;
    }
    return obj.value(QString::fromUtf8("prompt")).toString().trimmed();
}

//----------------------------------------------

static QString ttsHeuristicPlainTask(QString task)
{
    task = task.trimmed();
    if (task.isEmpty())
        return task;

    const QStringList lines = task.split(QChar('\n'));
    QStringList body;
    bool afterMarker = false;
    for (int i = 0; i < lines.size(); ++i)
    {
        const QString line = lines.at(i);
        QString rest;
        if (ttsContentMarkerLine(line, &rest))
        {
            afterMarker = true;
            if (!rest.isEmpty())
                body.append(rest);
            continue;
        }
        if (afterMarker)
            body.append(line);
        else if (!ttsMetaLine(line))
            body.append(line);
    }
    if (!body.isEmpty())
        return body.join(QString::fromUtf8("\n")).trimmed();

    QStringList nonMeta;
    foreach (const QString &line, lines)
    {
        if (!ttsMetaLine(line))
            nonMeta.append(line);
    }
    const QString joined = nonMeta.join(QString::fromUtf8("\n")).trimmed();
    return joined.isEmpty() ? task : joined;
}

//----------------------------------------------

QJsonObject buildTextToSpeechToolArgs(const QString &taskText, const QJsonObject *toolArgs)
{
    QJsonObject obj;
    const QString trimmedTask = taskText.trimmed();
    QJsonParseError pe;
    const QJsonDocument taskDoc = QJsonDocument::fromJson(trimmedTask.toUtf8(), &pe);
    if (pe.error == QJsonParseError::NoError && taskDoc.isObject())
        obj = taskDoc.object();

    if (toolArgs)
    {
        const QStringList keys = toolArgs->keys();
        foreach (const QString &key, keys)
            obj.insert(key, toolArgs->value(key));
    }

    QJsonObject out;
    QString speak = ttsSpeakTextFromObject(obj);
    if (speak.isEmpty() && !trimmedTask.isEmpty() && !taskDoc.isObject())
        speak = ttsHeuristicPlainTask(trimmedTask);
    else if (!speak.isEmpty() && speak.contains(QChar('\n')) && ttsMetaLine(speak.split(QChar('\n')).first()))
        speak = ttsHeuristicPlainTask(speak);

    out.insert(QString::fromUtf8("text"), speak.trimmed());

    QString voice = obj.value(QString::fromUtf8("voice")).toString().trimmed();
    if (voice.isEmpty())
        voice = obj.value(QString::fromUtf8("voice_id")).toString().trimmed();
    if (!voice.isEmpty())
        out.insert(QString::fromUtf8("voice"), voice);

    const QString format = obj.value(QString::fromUtf8("format")).toString().trimmed();
    if (!format.isEmpty())
        out.insert(QString::fromUtf8("format"), format);
    const QString outputPath = obj.value(QString::fromUtf8("output_path")).toString().trimmed();
    if (!outputPath.isEmpty())
        out.insert(QString::fromUtf8("output_path"), outputPath);

    return out;
}

//----------------------------------------------

static QByteArray decodeDataUrlOrBase64(QString value)
{
    QString v = value.trimmed();
    const int comma = v.indexOf(QChar(','));
    if (v.startsWith(QString::fromUtf8("data:"), Qt::CaseInsensitive) && comma > 0)
        v = v.mid(comma + 1);
    return QByteArray::fromBase64(v.toLatin1());
}

//----------------------------------------------

static QString saveImagePayload(ModalityApiClient *client, ProjectWorkspace *workspace,
                                Worker *owner, const QJsonObject &response,
                                QString outputPath, QString *error)
{
    const QJsonArray data = response.value(QString::fromUtf8("data")).toArray();
    if (data.isEmpty())
    {
        // Некоторые провайдеры кладут url/b64 в корень.
        QString b64 = response.value(QString::fromUtf8("b64_json")).toString();
        QString url = response.value(QString::fromUtf8("url")).toString();
        if (b64.isEmpty() && url.isEmpty())
        {
            if (error)
                *error = QString::fromUtf8("Image response has no data[].");
            return QString();
        }
        QJsonObject one;
        if (!b64.isEmpty())
            one.insert(QString::fromUtf8("b64_json"), b64);
        if (!url.isEmpty())
            one.insert(QString::fromUtf8("url"), url);
        QJsonArray arr;
        arr.append(one);
        QJsonObject wrap;
        wrap.insert(QString::fromUtf8("data"), arr);
        return saveImagePayload(client, workspace, owner, wrap, outputPath, error);
    }

    QString path = outputPath.trimmed();
    if (path.isEmpty())
        path = ModalityApiClient::defaultArtifactPath(workspace, QString::fromUtf8("image"),
                                                      QString::fromUtf8("png"));

    QStringList saved;
    for (int i = 0; i < data.size(); ++i)
    {
        const QJsonObject item = data.at(i).toObject();
        QByteArray bytes;
        const QString b64 = item.value(QString::fromUtf8("b64_json")).toString();
        if (!b64.isEmpty())
        {
            bytes = decodeDataUrlOrBase64(b64);
        }
        else
        {
            const QString url = item.value(QString::fromUtf8("url")).toString().trimmed();
            if (url.isEmpty())
                continue;
            const ModalityApiClient::Result dl = client->getBytes(url);
            if (!dl.ok)
            {
                if (error)
                    *error = dl.error;
                return QString();
            }
            bytes = dl.body;
        }
        if (bytes.isEmpty())
            continue;

        QString onePath = path;
        if (data.size() > 1)
        {
            const QFileInfo info(path);
            onePath = info.path() + QChar('/') + info.completeBaseName()
                      + QString::fromUtf8("_%1.").arg(i + 1) + info.suffix();
        }
        QString saveError;
        const QString rel = ModalityApiClient::saveBytes(workspace, onePath, bytes, &saveError);
        if (rel.isEmpty())
        {
            if (error)
                *error = saveError;
            return QString();
        }
        if (owner)
            owner->noteFileChanged(rel);
        saved.append(rel);
    }
    if (saved.isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("No image bytes in provider response.");
        return QString();
    }
    return saved.join(QString::fromUtf8(", "));
}

//----------------------------------------------

GenerateImageTool::GenerateImageTool(ProjectWorkspace *workspace, Worker *owner,
                                     QObject *parent)
    : AgentTool(parent)
    , _workspace(workspace)
    , _owner(owner)
{
}

//----------------------------------------------

QString GenerateImageTool::name() const
{
    return QString::fromUtf8("generate_image");
}

//----------------------------------------------

QString GenerateImageTool::description() const
{
    return QString::fromUtf8(
        "Generate or edit an image via the worker's image model. "
        "Minimal payload by default (model+prompt). Pass reference_path for "
        "image-to-image models that require input_references (e.g. ming-image "
        "design-layer). Do NOT pass n unless the provider documents it. "
        "Saves PNG/JPEG into the project.");
}

//----------------------------------------------

QJsonObject GenerateImageTool::parametersSchema() const
{
    QJsonObject prompt;
    prompt.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    prompt.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("Image description / prompt."));
    QJsonObject size;
    size.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    size.insert(QString::fromUtf8("description"),
                QString::fromUtf8("Optional size (omit if provider rejects size)."));
    QJsonObject n;
    n.insert(QString::fromUtf8("type"), QString::fromUtf8("integer"));
    n.insert(QString::fromUtf8("description"),
             QString::fromUtf8("Optional. Only send if provider supports n; "
                               "many OpenRouter/Novita models reject n."));
    QJsonObject refPath;
    refPath.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    refPath.insert(QString::fromUtf8("description"),
                   QString::fromUtf8(
                       "Project-relative path to a reference image for image-to-image "
                       "(sent as input_references[0] data URL). Required by models like "
                       "inclusionai/ming-image-0.1-design-layer."));
    QJsonObject refUrl;
    refUrl.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    refUrl.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("HTTP(S) or data: URL reference image (alternative "
                                   "to reference_path)."));
    QJsonObject output;
    output.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    output.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("Relative output path inside the project."));
    QJsonObject properties;
    properties.insert(QString::fromUtf8("prompt"), prompt);
    properties.insert(QString::fromUtf8("size"), size);
    properties.insert(QString::fromUtf8("n"), n);
    properties.insert(QString::fromUtf8("reference_path"), refPath);
    properties.insert(QString::fromUtf8("reference_url"), refUrl);
    properties.insert(QString::fromUtf8("output_path"), output);
    QJsonArray required;
    required.append(QString::fromUtf8("prompt"));
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

static void modalityNotice(QObject *toolParent, QString text)
{
    Agent *agent = qobject_cast<Agent *>(toolParent);
    if (agent)
        agent->postNotice(text);
}

//----------------------------------------------

static QString fileToDataUrl(ProjectWorkspace *workspace, QString relativePath, QString *error)
{
    if (!workspace)
    {
        if (error)
            *error = QString::fromUtf8("Workspace is missing.");
        return QString();
    }
    QString abs;
    QString resolveError;
    if (!workspace->resolvePath(relativePath, true, false, &abs, &resolveError))
    {
        if (error)
            *error = resolveError;
        return QString();
    }
    QFile file(abs);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = QString::fromUtf8("Cannot read %1: %2")
                         .arg(relativePath, file.errorString());
        return QString();
    }
    const QByteArray bytes = file.readAll();
    file.close();
    if (bytes.isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("Reference image is empty: %1").arg(relativePath);
        return QString();
    }
    QMimeDatabase mimeDb;
    QString mime = mimeDb.mimeTypeForFile(abs).name();
    if (mime.isEmpty() || !mime.startsWith(QString::fromUtf8("image/")))
        mime = QString::fromUtf8("image/png");
    return QString::fromUtf8("data:%1;base64,%2")
        .arg(mime, QString::fromLatin1(bytes.toBase64()));
}

//----------------------------------------------

static QJsonObject buildImageReferencePart(QString urlOrData)
{
    QJsonObject imageUrl;
    imageUrl.insert(QString::fromUtf8("url"), urlOrData);
    QJsonObject part;
    part.insert(QString::fromUtf8("type"), QString::fromUtf8("image_url"));
    part.insert(QString::fromUtf8("image_url"), imageUrl);
    return part;
}

//----------------------------------------------

static bool errorMentions(QString error, QString token)
{
    return error.contains(token, Qt::CaseInsensitive);
}

//----------------------------------------------

QString GenerateImageTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    const QString prompt = extractPrompt(args, _owner ? _owner->task() : QString());
    if (prompt.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("prompt is empty.");
    }

    const LlmProfile profile = modalityProfileForWorker(_owner);
    ModalityApiClient client(profile);
    const ModelCapabilities caps =
        ModelCapabilityService::instance()->capabilitiesFor(profile);

    modalityNotice(parent(),
                   QString::fromUtf8("Image: capabilities %1")
                       .arg(caps.summary()));
    modalityNotice(parent(),
                   QString::fromUtf8("Image: preparing request (model=%1)…")
                       .arg(client.model()));

    QJsonObject body;
    body.insert(QString::fromUtf8("model"), client.model());
    body.insert(QString::fromUtf8("prompt"), prompt);

    const QString size = args.value(QString::fromUtf8("size")).toString().trimmed();
    if (!size.isEmpty() && caps.supportsSize)
        body.insert(QString::fromUtf8("size"), size);
    else if (!size.isEmpty() && !caps.supportsSize)
    {
        modalityNotice(parent(),
                       QString::fromUtf8("Image: size ignored (model does not support size)."));
    }

    // n — только если caps.supportsN (обычно nMax>1) и явно передали.
    if (args.contains(QString::fromUtf8("n")) && caps.supportsN)
    {
        int n = args.value(QString::fromUtf8("n")).toInt(1);
        if (caps.nMin > 0 && n < caps.nMin)
            n = caps.nMin;
        if (caps.nMax > 0 && n > caps.nMax)
            n = caps.nMax;
        if (n < 1)
            n = 1;
        body.insert(QString::fromUtf8("n"), n);
    }

    QString referenceUrl = args.value(QString::fromUtf8("reference_url")).toString().trimmed();
    const QString referencePath =
        args.value(QString::fromUtf8("reference_path")).toString().trimmed();

    if (!caps.allowsImageReference()
        && (!referencePath.isEmpty() || !referenceUrl.isEmpty()))
    {
        modalityNotice(parent(),
                       QString::fromUtf8(
                           "Image: model does not accept input_references — ignoring reference."));
        referenceUrl.clear();
    }
    else
    {
        if (referenceUrl.isEmpty() && !referencePath.isEmpty())
        {
            modalityNotice(parent(),
                           QString::fromUtf8("Image: loading reference %1…").arg(referencePath));
            referenceUrl = fileToDataUrl(_workspace, referencePath, &error);
            if (referenceUrl.isEmpty())
            {
                if (ok)
                    *ok = false;
                return error;
            }
        }
        if (!referenceUrl.isEmpty())
        {
            QJsonArray refs;
            refs.append(buildImageReferencePart(referenceUrl));
            body.insert(QString::fromUtf8("input_references"), refs);
            modalityNotice(parent(), QString::fromUtf8("Image: attached 1 reference image."));
        }
    }

    if (caps.requiresImageReference()
        && !body.contains(QString::fromUtf8("input_references")))
    {
        if (ok)
            *ok = false;
        const QString msg = QString::fromUtf8(
            "This model requires a reference image (input_modalities include image, "
            "input_references min=%1). Pass reference_path or reference_url. "
            "Capabilities: %2")
                                .arg(caps.inputReferencesMin)
                                .arg(caps.summary());
        modalityNotice(parent(), msg);
        return msg;
    }

    const QString base = client.baseUrl().toLower();
    QStringList endpoints;
    if (base.contains(QString::fromUtf8("openrouter.ai")))
    {
        endpoints << QString::fromUtf8("/images")
                  << QString::fromUtf8("/images/generations");
    }
    else
    {
        endpoints << QString::fromUtf8("/images/generations")
                  << QString::fromUtf8("/images");
    }

    ModalityApiClient::Result result;
    QString usedEndpoint;
    for (int pass = 0; pass < 3; ++pass)
    {
        foreach (const QString &endpoint, endpoints)
        {
            modalityNotice(parent(),
                           QString::fromUtf8("Image: POST %1 (pass %2)…")
                               .arg(endpoint)
                               .arg(pass + 1));
            result = client.postJson(endpoint, body);
            usedEndpoint = endpoint;
            if (result.ok)
                break;
            if (result.status == 404 || result.status == 405)
                continue;
            break;
        }
        if (result.ok)
            break;

        const QString err = result.error;
        bool changed = false;
        if (errorMentions(err, QString::fromUtf8("\"n\""))
            || errorMentions(err, QString::fromUtf8("parameter(s): n"))
            || errorMentions(err, QString::fromUtf8(" n ")))
        {
            if (body.contains(QString::fromUtf8("n")))
            {
                body.remove(QString::fromUtf8("n"));
                modalityNotice(parent(),
                               QString::fromUtf8("Image: provider rejected n — retrying without n."));
                changed = true;
            }
        }
        if (errorMentions(err, QString::fromUtf8("response_format")))
        {
            if (body.contains(QString::fromUtf8("response_format")))
            {
                body.remove(QString::fromUtf8("response_format"));
                changed = true;
            }
        }
        if (errorMentions(err, QString::fromUtf8("size"))
            || errorMentions(err, QString::fromUtf8("aspect_ratio")))
        {
            if (body.contains(QString::fromUtf8("size")))
            {
                body.remove(QString::fromUtf8("size"));
                modalityNotice(parent(),
                               QString::fromUtf8("Image: provider rejected size — retrying without size."));
                changed = true;
            }
        }
        if (errorMentions(err, QString::fromUtf8("input_references")))
        {
            if (!body.contains(QString::fromUtf8("input_references")))
            {
                if (ok)
                    *ok = false;
                modalityNotice(parent(),
                               QString::fromUtf8(
                                   "Image: provider requires input_references "
                                   "(pass reference_path or reference_url)."));
                return QString::fromUtf8(
                    "This image model requires a reference image "
                    "(input_references must have exactly 1 item). "
                    "Call generate_image with reference_path=\"path/in/project.png\" "
                    "or reference_url=\"https://...\" / data URL. "
                    "For pure text-to-image use a model that does not require references "
                    "(e.g. inclusionai/ming-image-0.1-design, not design-layer).\n"
                    "Provider error: %1")
                    .arg(err);
            }
        }
        if (!changed)
            break;
    }

    if (!result.ok)
    {
        if (ok)
            *ok = false;
        modalityNotice(parent(),
                       QString::fromUtf8("Image: request failed: %1")
                           .arg(result.error.left(240)));
        return result.error;
    }

    modalityNotice(parent(),
                   QString::fromUtf8("Image: response OK from %1 (%2 bytes), saving…")
                       .arg(usedEndpoint)
                       .arg(result.body.size()));

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(result.body, &pe);
    if (!doc.isObject())
    {
        // Сырые байты картинки
        if (result.body.size() > 32
            && (result.body.startsWith("\x89PNG")
                || result.body.startsWith("\xFF\xD8")))
        {
            QString path = args.value(QString::fromUtf8("output_path")).toString().trimmed();
            if (path.isEmpty())
                path = ModalityApiClient::defaultArtifactPath(
                    _workspace, QString::fromUtf8("image"),
                    result.body.startsWith("\x89PNG") ? QString::fromUtf8("png")
                                                      : QString::fromUtf8("jpg"));
            const QString rel =
                ModalityApiClient::saveBytes(_workspace, path, result.body, &error);
            if (rel.isEmpty())
            {
                if (ok)
                    *ok = false;
                return error;
            }
            if (_owner)
                _owner->noteFileChanged(rel);
            if (ok)
                *ok = true;
            modalityNotice(parent(),
                           QString::fromUtf8("Image: saved binary response → %1").arg(rel));
            return QString::fromUtf8("Generated image(s): %1").arg(rel);
        }
        if (ok)
            *ok = false;
        return QString::fromUtf8("Invalid image JSON: %1").arg(pe.errorString());
    }
    const QString paths = saveImagePayload(
        &client, _workspace, _owner, doc.object(),
        args.value(QString::fromUtf8("output_path")).toString(), &error);
    if (paths.isEmpty())
    {
        if (ok)
            *ok = false;
        modalityNotice(parent(),
                       QString::fromUtf8("Image: could not decode payload: %1").arg(error));
        return error;
    }
    if (ok)
        *ok = true;
    modalityNotice(parent(),
                   QString::fromUtf8("Image: saved → %1").arg(paths));
    return QString::fromUtf8("Generated image(s): %1").arg(paths);
}

//----------------------------------------------

TextToSpeechTool::TextToSpeechTool(ProjectWorkspace *workspace, Worker *owner,
                                   QObject *parent)
    : AgentTool(parent)
    , _workspace(workspace)
    , _owner(owner)
{
}

//----------------------------------------------

QString TextToSpeechTool::name() const
{
    return QString::fromUtf8("text_to_speech");
}

//----------------------------------------------

QString TextToSpeechTool::description() const
{
    return QString::fromUtf8(
        "Synthesize speech (TTS) and save audio in the project. "
        "Put ONLY the exact words to read aloud in `text`. "
        "Voice id goes in `voice`; delivery/style notes in `instructions` (not spoken).");
}

//----------------------------------------------

QJsonObject TextToSpeechTool::parametersSchema() const
{
    QJsonObject text;
    text.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    text.insert(QString::fromUtf8("description"),
                QString::fromUtf8("Exact words to synthesize — no voice/style instructions."));
    QJsonObject script;
    script.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    script.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("Alias for `text` (spoken script only)."));
    QJsonObject voice;
    voice.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    voice.insert(QString::fromUtf8("description"),
                 QString::fromUtf8("Optional voice id (alloy, verse, …)."));
    QJsonObject instructions;
    instructions.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    instructions.insert(QString::fromUtf8("description"),
                        QString::fromUtf8("Optional delivery/style; NOT read aloud."));
    QJsonObject format;
    format.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    format.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("Audio format: mp3, wav, opus (default mp3)."));
    QJsonObject output;
    output.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    QJsonObject properties;
    properties.insert(QString::fromUtf8("text"), text);
    properties.insert(QString::fromUtf8("script"), script);
    properties.insert(QString::fromUtf8("voice"), voice);
    properties.insert(QString::fromUtf8("instructions"), instructions);
    properties.insert(QString::fromUtf8("format"), format);
    properties.insert(QString::fromUtf8("output_path"), output);
    QJsonArray required;
    required.append(QString::fromUtf8("text"));
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString TextToSpeechTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    const QString workerTask = _owner ? _owner->task() : QString();
    const QJsonObject built = buildTextToSpeechToolArgs(workerTask, &args);
    const QString text = built.value(QString::fromUtf8("text")).toString().trimmed();
    if (text.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("text is empty.");
    }
    QString format = built.value(QString::fromUtf8("format")).toString().trimmed().toLower();
    if (format.isEmpty())
        format = QString::fromUtf8("mp3");
    QString path = built.value(QString::fromUtf8("output_path")).toString().trimmed();
    if (path.isEmpty())
        path = ModalityApiClient::defaultArtifactPath(_workspace, QString::fromUtf8("tts"),
                                                      format);

    const LlmProfile profile = modalityProfileForWorker(_owner);
    ModalityApiClient client(profile);
    QJsonObject body;
    body.insert(QString::fromUtf8("model"), client.model());
    body.insert(QString::fromUtf8("input"), text);
    const QString voice = built.value(QString::fromUtf8("voice")).toString().trimmed();
    body.insert(QString::fromUtf8("voice"),
                voice.isEmpty() ? QString::fromUtf8("alloy") : voice);
    body.insert(QString::fromUtf8("response_format"), format);

    const ModalityApiClient::Result result =
        client.postJson(QString::fromUtf8("/audio/speech"), body);
    if (!result.ok)
    {
        if (ok)
            *ok = false;
        return result.error;
    }

    // Ответ — сырые байты аудио (или JSON с ошибкой/url).
    QByteArray bytes = result.body;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(result.body, &pe);
    if (doc.isObject())
    {
        const QJsonObject obj = doc.object();
        const QString b64 = obj.value(QString::fromUtf8("audio")).toString();
        const QString url = obj.value(QString::fromUtf8("url")).toString();
        if (!b64.isEmpty())
            bytes = decodeDataUrlOrBase64(b64);
        else if (!url.isEmpty())
        {
            const ModalityApiClient::Result dl = client.getBytes(url);
            if (!dl.ok)
            {
                if (ok)
                    *ok = false;
                return dl.error;
            }
            bytes = dl.body;
        }
    }
    if (bytes.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Empty TTS audio payload.");
    }
    const QString rel = ModalityApiClient::saveBytes(_workspace, path, bytes, &error);
    if (rel.isEmpty())
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (_owner)
        _owner->noteFileChanged(rel);
    if (ok)
        *ok = true;
    return QString::fromUtf8("TTS audio saved: %1").arg(rel);
}

//----------------------------------------------

SpeechToTextTool::SpeechToTextTool(ProjectWorkspace *workspace, Worker *owner,
                                   QObject *parent)
    : AgentTool(parent)
    , _workspace(workspace)
    , _owner(owner)
{
}

//----------------------------------------------

QString SpeechToTextTool::name() const
{
    return QString::fromUtf8("speech_to_text");
}

//----------------------------------------------

QString SpeechToTextTool::description() const
{
    return QString::fromUtf8(
        "Transcribe an audio file from the project (STT / Whisper-compatible).");
}

//----------------------------------------------

QJsonObject SpeechToTextTool::parametersSchema() const
{
    QJsonObject path;
    path.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    path.insert(QString::fromUtf8("description"),
                QString::fromUtf8("Relative path to an audio file in the project."));
    QJsonObject language;
    language.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    language.insert(QString::fromUtf8("description"),
                    QString::fromUtf8("Optional BCP-47 language hint."));
    QJsonObject properties;
    properties.insert(QString::fromUtf8("path"), path);
    properties.insert(QString::fromUtf8("language"), language);
    QJsonArray required;
    required.append(QString::fromUtf8("path"));
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString SpeechToTextTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    const QString relPath = args.value(QString::fromUtf8("path")).toString().trimmed();
    if (relPath.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("path is empty.");
    }
    QString abs;
    if (!_workspace || !_workspace->resolvePath(relPath, true, false, &abs, &error))
    {
        if (ok)
            *ok = false;
        return error.isEmpty() ? QString::fromUtf8("Cannot resolve audio path.") : error;
    }
    QFile file(abs);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Cannot read %1: %2").arg(relPath, file.errorString());
    }
    const QByteArray audio = file.readAll();
    file.close();
    if (audio.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Audio file is empty.");
    }

    const LlmProfile profile = modalityProfileForWorker(_owner);
    ModalityApiClient client(profile);
    const QString boundary = QString::fromUtf8("----dsagent%1")
                                 .arg(QString::number(qAbs(qint64(audio.size()) ^ 0x5a5a)));
    QByteArray body;
    auto addField = [&](QString name, QByteArray value) {
        body += QByteArray("--") + boundary.toUtf8() + QByteArray("\r\n");
        body += QByteArray("Content-Disposition: form-data; name=\"")
                + name.toUtf8() + QByteArray("\"\r\n\r\n");
        body += value + QByteArray("\r\n");
    };
    addField(QString::fromUtf8("model"), client.model().toUtf8());
    const QString language = args.value(QString::fromUtf8("language")).toString().trimmed();
    if (!language.isEmpty())
        addField(QString::fromUtf8("language"), language.toUtf8());
    const QString fileName = QFileInfo(abs).fileName();
    QMimeDatabase mimeDb;
    const QString mime = mimeDb.mimeTypeForFile(abs).name();
    body += QByteArray("--") + boundary.toUtf8() + QByteArray("\r\n");
    body += QByteArray("Content-Disposition: form-data; name=\"file\"; filename=\"")
            + fileName.toUtf8() + QByteArray("\"\r\n");
    body += QByteArray("Content-Type: ")
            + (mime.isEmpty() ? QByteArray("application/octet-stream") : mime.toUtf8())
            + QByteArray("\r\n\r\n");
    body += audio + QByteArray("\r\n");
    body += QByteArray("--") + boundary.toUtf8() + QByteArray("--\r\n");

    const ModalityApiClient::Result result =
        client.postRaw(QString::fromUtf8("/audio/transcriptions"), body,
                       QByteArray("multipart/form-data; boundary=") + boundary.toUtf8());
    if (!result.ok)
    {
        if (ok)
            *ok = false;
        return result.error;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(result.body, &pe);
    QString text;
    if (doc.isObject())
        text = doc.object().value(QString::fromUtf8("text")).toString();
    if (text.isEmpty())
        text = QString::fromUtf8(result.body);
    if (ok)
        *ok = true;
    return QString::fromUtf8("Transcription of %1:\n%2").arg(relPath, text.trimmed());
}

//----------------------------------------------

GenerateAudioTool::GenerateAudioTool(ProjectWorkspace *workspace, Worker *owner,
                                     QObject *parent)
    : AgentTool(parent)
    , _workspace(workspace)
    , _owner(owner)
{
}

//----------------------------------------------

QString GenerateAudioTool::name() const
{
    return QString::fromUtf8("generate_audio");
}

//----------------------------------------------

QString GenerateAudioTool::description() const
{
    return QString::fromUtf8(
        "Generate music/sound audio from a text prompt (not TTS). "
        "Uses the worker's audio_generation model.");
}

//----------------------------------------------

QJsonObject GenerateAudioTool::parametersSchema() const
{
    QJsonObject prompt;
    prompt.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    QJsonObject duration;
    duration.insert(QString::fromUtf8("type"), QString::fromUtf8("number"));
    duration.insert(QString::fromUtf8("description"),
                    QString::fromUtf8("Optional duration in seconds."));
    QJsonObject output;
    output.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    QJsonObject properties;
    properties.insert(QString::fromUtf8("prompt"), prompt);
    properties.insert(QString::fromUtf8("duration"), duration);
    properties.insert(QString::fromUtf8("output_path"), output);
    QJsonArray required;
    required.append(QString::fromUtf8("prompt"));
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString GenerateAudioTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    const QString prompt = extractPrompt(args, _owner ? _owner->task() : QString());
    if (prompt.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("prompt is empty.");
    }
    QString path = args.value(QString::fromUtf8("output_path")).toString().trimmed();
    if (path.isEmpty())
        path = ModalityApiClient::defaultArtifactPath(_workspace, QString::fromUtf8("audio"),
                                                      QString::fromUtf8("mp3"));

    const LlmProfile profile = modalityProfileForWorker(_owner);
    ModalityApiClient client(profile);
    QJsonObject body;
    body.insert(QString::fromUtf8("model"), client.model());
    body.insert(QString::fromUtf8("prompt"), prompt);
    if (args.contains(QString::fromUtf8("duration")))
        body.insert(QString::fromUtf8("duration"),
                    args.value(QString::fromUtf8("duration")).toDouble());

    static const char *kPaths[] = {
        "/audio/generations", "/sound/generations", "/v1/audio/generations", 0
    };
    ModalityApiClient::Result result;
    for (int i = 0; kPaths[i]; ++i)
    {
        result = client.postJson(QString::fromUtf8(kPaths[i]), body);
        if (result.ok)
            break;
    }
    if (!result.ok)
    {
        if (ok)
            *ok = false;
        return result.error;
    }

    QByteArray bytes = result.body;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(result.body, &pe);
    if (doc.isObject())
    {
        const QJsonObject obj = doc.object();
        QString b64 = obj.value(QString::fromUtf8("b64_json")).toString();
        if (b64.isEmpty())
            b64 = obj.value(QString::fromUtf8("audio")).toString();
        QString url = obj.value(QString::fromUtf8("url")).toString();
        const QJsonArray data = obj.value(QString::fromUtf8("data")).toArray();
        if (!data.isEmpty())
        {
            const QJsonObject first = data.at(0).toObject();
            if (b64.isEmpty())
                b64 = first.value(QString::fromUtf8("b64_json")).toString();
            if (url.isEmpty())
                url = first.value(QString::fromUtf8("url")).toString();
        }
        if (!b64.isEmpty())
            bytes = decodeDataUrlOrBase64(b64);
        else if (!url.isEmpty())
        {
            const ModalityApiClient::Result dl = client.getBytes(url);
            if (!dl.ok)
            {
                if (ok)
                    *ok = false;
                return dl.error;
            }
            bytes = dl.body;
        }
        else
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8("Audio generation JSON has no audio payload: %1")
                .arg(QString::fromUtf8(result.body.left(400)));
        }
    }
    if (bytes.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Empty audio generation payload.");
    }
    const QString rel = ModalityApiClient::saveBytes(_workspace, path, bytes, &error);
    if (rel.isEmpty())
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (_owner)
        _owner->noteFileChanged(rel);
    if (ok)
        *ok = true;
    return QString::fromUtf8("Generated audio: %1").arg(rel);
}

//----------------------------------------------

GenerateVideoTool::GenerateVideoTool(ProjectWorkspace *workspace, Worker *owner,
                                     QObject *parent)
    : AgentTool(parent)
    , _workspace(workspace)
    , _owner(owner)
{
}

//----------------------------------------------

QString GenerateVideoTool::name() const
{
    return QString::fromUtf8("generate_video");
}

//----------------------------------------------

QString GenerateVideoTool::description() const
{
    return QString::fromUtf8(
        "Generate a short video from a text prompt via the worker's video model. "
        "Saves the clip into the project when the provider returns bytes or a URL.");
}

//----------------------------------------------

QJsonObject GenerateVideoTool::parametersSchema() const
{
    QJsonObject prompt;
    prompt.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    QJsonObject seconds;
    seconds.insert(QString::fromUtf8("type"), QString::fromUtf8("number"));
    seconds.insert(QString::fromUtf8("description"),
                   QString::fromUtf8("Optional clip length in seconds."));
    QJsonObject size;
    size.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    size.insert(QString::fromUtf8("description"),
                QString::fromUtf8("Optional size, e.g. 1280x720."));
    QJsonObject output;
    output.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    QJsonObject properties;
    properties.insert(QString::fromUtf8("prompt"), prompt);
    properties.insert(QString::fromUtf8("seconds"), seconds);
    properties.insert(QString::fromUtf8("size"), size);
    properties.insert(QString::fromUtf8("output_path"), output);
    QJsonArray required;
    required.append(QString::fromUtf8("prompt"));
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString GenerateVideoTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    const QString prompt = extractPrompt(args, _owner ? _owner->task() : QString());
    if (prompt.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("prompt is empty.");
    }
    QString path = args.value(QString::fromUtf8("output_path")).toString().trimmed();
    if (path.isEmpty())
        path = ModalityApiClient::defaultArtifactPath(_workspace, QString::fromUtf8("video"),
                                                      QString::fromUtf8("mp4"));

    const LlmProfile profile = modalityProfileForWorker(_owner);
    ModalityApiClient client(profile);
    QJsonObject body;
    body.insert(QString::fromUtf8("model"), client.model());
    body.insert(QString::fromUtf8("prompt"), prompt);
    if (args.contains(QString::fromUtf8("seconds")))
        body.insert(QString::fromUtf8("seconds"),
                    args.value(QString::fromUtf8("seconds")).toDouble());
    const QString size = args.value(QString::fromUtf8("size")).toString().trimmed();
    if (!size.isEmpty())
        body.insert(QString::fromUtf8("size"), size);

    static const char *kPaths[] = {
        "/videos/generations", "/video/generations", "/videos", 0
    };
    ModalityApiClient::Result result;
    for (int i = 0; kPaths[i]; ++i)
    {
        result = client.postJson(QString::fromUtf8(kPaths[i]), body);
        if (result.ok)
            break;
    }
    if (!result.ok)
    {
        if (ok)
            *ok = false;
        return result.error;
    }

    QByteArray bytes;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(result.body, &pe);
    if (doc.isObject())
    {
        const QJsonObject obj = doc.object();
        QString b64 = obj.value(QString::fromUtf8("b64_json")).toString();
        QString url = obj.value(QString::fromUtf8("url")).toString();
        const QJsonArray data = obj.value(QString::fromUtf8("data")).toArray();
        if (!data.isEmpty())
        {
            const QJsonObject first = data.at(0).toObject();
            if (b64.isEmpty())
                b64 = first.value(QString::fromUtf8("b64_json")).toString();
            if (url.isEmpty())
                url = first.value(QString::fromUtf8("url")).toString();
        }
        // Асинхронные job id (Sora и др.) — возвращаем статус, файл ещё не готов.
        const QString id = obj.value(QString::fromUtf8("id")).toString();
        const QString status = obj.value(QString::fromUtf8("status")).toString();
        if (b64.isEmpty() && url.isEmpty() && !id.isEmpty())
        {
            if (ok)
                *ok = true;
            return QString::fromUtf8(
                       "Video job accepted: id=%1 status=%2. "
                       "Provider is async; poll or download when ready.")
                .arg(id, status.isEmpty() ? QString::fromUtf8("submitted") : status);
        }
        if (!b64.isEmpty())
            bytes = decodeDataUrlOrBase64(b64);
        else if (!url.isEmpty())
        {
            const ModalityApiClient::Result dl = client.getBytes(url);
            if (!dl.ok)
            {
                if (ok)
                    *ok = false;
                return dl.error;
            }
            bytes = dl.body;
        }
        else
        {
            if (ok)
                *ok = true;
            return QString::fromUtf8("Video API response (no file yet):\n%1")
                .arg(QString::fromUtf8(result.body.left(1200)));
        }
    }
    else
    {
        bytes = result.body;
    }
    if (bytes.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Empty video payload.");
    }
    const QString rel = ModalityApiClient::saveBytes(_workspace, path, bytes, &error);
    if (rel.isEmpty())
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (_owner)
        _owner->noteFileChanged(rel);
    if (ok)
        *ok = true;
    return QString::fromUtf8("Generated video: %1").arg(rel);
}

//----------------------------------------------

EmbedTextsTool::EmbedTextsTool(ProjectWorkspace *workspace, Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _workspace(workspace)
    , _owner(owner)
{
}

//----------------------------------------------

QString EmbedTextsTool::name() const
{
    return QString::fromUtf8("embed_texts");
}

//----------------------------------------------

QString EmbedTextsTool::description() const
{
    return QString::fromUtf8(
        "Create embedding vectors for one or more texts. Saves JSON "
        "(texts + vectors) into the project and returns the path.");
}

//----------------------------------------------

QJsonObject EmbedTextsTool::parametersSchema() const
{
    QJsonObject texts;
    texts.insert(QString::fromUtf8("type"), QString::fromUtf8("array"));
    QJsonObject item;
    item.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    texts.insert(QString::fromUtf8("items"), item);
    texts.insert(QString::fromUtf8("description"),
                 QString::fromUtf8("Texts to embed. Or pass a single 'text' string."));
    QJsonObject text;
    text.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    QJsonObject output;
    output.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    QJsonObject properties;
    properties.insert(QString::fromUtf8("texts"), texts);
    properties.insert(QString::fromUtf8("text"), text);
    properties.insert(QString::fromUtf8("output_path"), output);
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    return schema;
}

//----------------------------------------------

QString EmbedTextsTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    QStringList texts;
    const QJsonArray arr = args.value(QString::fromUtf8("texts")).toArray();
    for (int i = 0; i < arr.size(); ++i)
    {
        const QString t = arr.at(i).toString().trimmed();
        if (!t.isEmpty())
            texts.append(t);
    }
    const QString one = args.value(QString::fromUtf8("text")).toString().trimmed();
    if (!one.isEmpty())
        texts.append(one);
    if (texts.isEmpty() && _owner && !_owner->task().trimmed().isEmpty())
        texts.append(_owner->task().trimmed());
    if (texts.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("No texts to embed.");
    }

    const LlmProfile profile = modalityProfileForWorker(_owner);
    ModalityApiClient client(profile);
    QJsonObject body;
    body.insert(QString::fromUtf8("model"), client.model());
    QJsonArray input;
    foreach (const QString &t, texts)
        input.append(t);
    body.insert(QString::fromUtf8("input"), input);

    const ModalityApiClient::Result result =
        client.postJson(QString::fromUtf8("/embeddings"), body);
    if (!result.ok)
    {
        if (ok)
            *ok = false;
        return result.error;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(result.body, &pe);
    if (!doc.isObject())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Invalid embeddings JSON: %1").arg(pe.errorString());
    }

    QString path = args.value(QString::fromUtf8("output_path")).toString().trimmed();
    if (path.isEmpty())
        path = ModalityApiClient::defaultArtifactPath(_workspace, QString::fromUtf8("embeddings"),
                                                      QString::fromUtf8("json"));
    QJsonObject out;
    out.insert(QString::fromUtf8("model"), client.model());
    out.insert(QString::fromUtf8("texts"), input);
    out.insert(QString::fromUtf8("response"), doc.object());
    const QByteArray bytes = QJsonDocument(out).toJson(QJsonDocument::Indented);
    const QString rel = ModalityApiClient::saveBytes(_workspace, path, bytes, &error);
    if (rel.isEmpty())
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (_owner)
        _owner->noteFileChanged(rel);
    const QJsonArray data = doc.object().value(QString::fromUtf8("data")).toArray();
    int dim = 0;
    if (!data.isEmpty())
        dim = data.at(0).toObject().value(QString::fromUtf8("embedding")).toArray().size();
    if (ok)
        *ok = true;
    return QString::fromUtf8("Embeddings saved: %1 (count=%2 dim=%3)")
        .arg(rel)
        .arg(texts.size())
        .arg(dim);
}

//----------------------------------------------

ProposeDecisionTool::ProposeDecisionTool(ProjectWorkspace *workspace, Worker *owner,
                                         QObject *parent)
    : AgentTool(parent)
    , _workspace(workspace)
    , _owner(owner)
{
}

//----------------------------------------------

QString ProposeDecisionTool::name() const
{
    return QString::fromUtf8("propose_decision");
}

//----------------------------------------------

QString ProposeDecisionTool::description() const
{
    return QString::fromUtf8(
        "Record a structured decision: options considered, chosen option, rationale, "
        "and confidence. Writes a markdown/JSON artifact in the project.");
}

//----------------------------------------------

QJsonObject ProposeDecisionTool::parametersSchema() const
{
    QJsonObject question;
    question.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    QJsonObject options;
    options.insert(QString::fromUtf8("type"), QString::fromUtf8("array"));
    QJsonObject optItem;
    optItem.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    options.insert(QString::fromUtf8("items"), optItem);
    QJsonObject chosen;
    chosen.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    QJsonObject rationale;
    rationale.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    QJsonObject confidence;
    confidence.insert(QString::fromUtf8("type"), QString::fromUtf8("number"));
    confidence.insert(QString::fromUtf8("description"),
                      QString::fromUtf8("0..1 confidence in the choice."));
    QJsonObject output;
    output.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    QJsonObject properties;
    properties.insert(QString::fromUtf8("question"), question);
    properties.insert(QString::fromUtf8("options"), options);
    properties.insert(QString::fromUtf8("chosen"), chosen);
    properties.insert(QString::fromUtf8("rationale"), rationale);
    properties.insert(QString::fromUtf8("confidence"), confidence);
    properties.insert(QString::fromUtf8("output_path"), output);
    QJsonArray required;
    required.append(QString::fromUtf8("question"));
    required.append(QString::fromUtf8("options"));
    required.append(QString::fromUtf8("chosen"));
    required.append(QString::fromUtf8("rationale"));
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString ProposeDecisionTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    const QString question = args.value(QString::fromUtf8("question")).toString().trimmed();
    const QString chosen = args.value(QString::fromUtf8("chosen")).toString().trimmed();
    const QString rationale = args.value(QString::fromUtf8("rationale")).toString().trimmed();
    const QJsonArray options = args.value(QString::fromUtf8("options")).toArray();
    if (question.isEmpty() || chosen.isEmpty() || rationale.isEmpty() || options.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("question, options, chosen and rationale are required.");
    }

    QString path = args.value(QString::fromUtf8("output_path")).toString().trimmed();
    if (path.isEmpty())
        path = ModalityApiClient::defaultArtifactPath(_workspace, QString::fromUtf8("decision"),
                                                      QString::fromUtf8("md"));

    QString md;
    md += QString::fromUtf8("# Decision\n\n");
    md += QString::fromUtf8("## Question\n\n%1\n\n").arg(question);
    md += QString::fromUtf8("## Options\n\n");
    for (int i = 0; i < options.size(); ++i)
        md += QString::fromUtf8("%1. %2\n").arg(i + 1).arg(options.at(i).toString());
    md += QString::fromUtf8("\n## Chosen\n\n%1\n\n").arg(chosen);
    md += QString::fromUtf8("## Rationale\n\n%1\n\n").arg(rationale);
    if (args.contains(QString::fromUtf8("confidence")))
    {
        md += QString::fromUtf8("## Confidence\n\n%1\n")
                  .arg(args.value(QString::fromUtf8("confidence")).toDouble());
    }

    QJsonObject meta = args;
    meta.insert(QString::fromUtf8("saved_as"), path);
    const QByteArray bytes = md.toUtf8();
    const QString rel = ModalityApiClient::saveBytes(_workspace, path, bytes, &error);
    if (rel.isEmpty())
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (_owner)
        _owner->noteFileChanged(rel);
    // Рядом JSON для машинного разбора.
    const QString jsonPath = QFileInfo(rel).path() + QChar('/')
                             + QFileInfo(rel).completeBaseName()
                             + QString::fromUtf8(".json");
    QString jsonError;
    const QString jsonRel = ModalityApiClient::saveBytes(
        _workspace, jsonPath, QJsonDocument(meta).toJson(QJsonDocument::Indented), &jsonError);
    if (!jsonRel.isEmpty() && _owner)
        _owner->noteFileChanged(jsonRel);

    if (ok)
        *ok = true;
    return QString::fromUtf8("Decision recorded: %1 (chosen=%2)").arg(rel, chosen);
}
