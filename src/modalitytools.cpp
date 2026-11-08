#include "modalitytools.h"
//----------------------------------------------
#include "agent.h"
#include "modalityapi.h"
#include "modelcapabilities.h"
#include "projectworkspace.h"
#include "worker.h"
//----------------------------------------------
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMimeDatabase>
#include <QRegExp>
#include <QTimer>

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
    const QString low = t.toLower();
    static const char *prefixes[] = {
        "voice:", "voice_id:", "voice_description:", "format:", "instruction:",
        "instructions:", "style:", "speed:", "model:", "output:", "output_path:",
        "response_format:",
        "голос:", "формат:", "указания:", "инструкция:", "стиль:", "скорость:",
        "модель:", "описание голоса:",
        0
    };
    for (int i = 0; prefixes[i]; ++i)
    {
        if (low.startsWith(QString::fromUtf8(prefixes[i])))
            return true;
    }
    return false;
}

//----------------------------------------------

static bool ttsInstructionSuffix(const QString &s)
{
    const QString t = s.trimmed().toLower();
    if (t.isEmpty())
        return false;
    static const char *heads[] = {
        "верни", "возврат", "сохрани", "сохраните", "сообщи", "сообщите",
        "укажи", "укажите", "return", "save", "report", "tell", "provide",
        "write", "put",
        0
    };
    for (int i = 0; heads[i]; ++i)
    {
        const QString h = QString::fromUtf8(heads[i]);
        if (t == h || t.startsWith(h + QChar(' ')) || t.startsWith(h + QChar(',')))
            return true;
    }
    return false;
}

//----------------------------------------------

static bool ttsContentMarkerLine(const QString &line, QString *restOut)
{
    QString t = line.trimmed();
    if (t.isEmpty())
        return false;
    const QString low = t.toLower();
    static const char *markers[] = {
        "text to speak", "spoken text", "speech text", "input text",
        "synthesize speech", "synthesize", "speak", "script", "utterance",
        "content", "read aloud", "read out loud", "read the following",
        "озвучить", "озвучьте", "озвучь", "озвучка",
        "текст для озвучки", "сказать", "прочитать",
        "синтезируй речь по тексту", "синтезируйте речь по тексту",
        "синтезируй речь", "синтезируйте речь",
        "синтезировать речь по тексту", "синтезировать речь",
        "синтезируй", "синтезируйте", "синтезировать",
        0
    };
    for (int i = 0; markers[i]; ++i)
    {
        const QString m = QString::fromUtf8(markers[i]);
        if (!low.startsWith(m))
            continue;
        QString rest = t.mid(m.size()).trimmed();
        if (rest.startsWith(QChar(':')) || rest.startsWith(QChar(0xFF1A))
            || rest.startsWith(QChar('-')))
            rest = rest.mid(1).trimmed();
        if (restOut)
            *restOut = rest;
        return true;
    }
    return false;
}

//----------------------------------------------

/** Достать реплику из «…» / "…" / “…” — без QRegExp (кириллица/UTF). */
static QString ttsExtractQuotedSpeech(const QString &text)
{
    struct Hit
    {
        int     start;
        QString body;
    };
    QList<Hit> hits;

    const QChar openG(0x00AB);  // «
    const QChar closeG(0x00BB); // »
    int pos = 0;
    while (pos < text.size())
    {
        const int a = text.indexOf(openG, pos);
        if (a < 0)
            break;
        const int b = text.indexOf(closeG, a + 1);
        if (b < 0)
            break;
        Hit h;
        h.start = a;
        h.body = text.mid(a + 1, b - a - 1).trimmed();
        if (!h.body.isEmpty())
            hits.append(h);
        pos = b + 1;
    }

    pos = 0;
    while (pos < text.size())
    {
        const int a = text.indexOf(QChar('"'), pos);
        if (a < 0)
            break;
        const int b = text.indexOf(QChar('"'), a + 1);
        if (b < 0)
            break;
        Hit h;
        h.start = a;
        h.body = text.mid(a + 1, b - a - 1).trimmed();
        if (!h.body.isEmpty())
            hits.append(h);
        pos = b + 1;
    }

    const QChar openC(0x201C);  // “
    const QChar closeC(0x201D); // ”
    pos = 0;
    while (pos < text.size())
    {
        const int a = text.indexOf(openC, pos);
        if (a < 0)
            break;
        const int b = text.indexOf(closeC, a + 1);
        if (b < 0)
            break;
        Hit h;
        h.start = a;
        h.body = text.mid(a + 1, b - a - 1).trimmed();
        if (!h.body.isEmpty())
            hits.append(h);
        pos = b + 1;
    }

    if (hits.isEmpty())
        return QString();

    Hit best = hits.first();
    foreach (const Hit &h, hits)
    {
        const QString before = text.left(h.start).toLower();
        const bool afterSynth =
            before.contains(QString::fromUtf8("синтезир"))
            || before.contains(QString::fromUtf8("озвуч"))
            || before.contains(QString::fromUtf8("speak"))
            || before.contains(QString::fromUtf8("synthes"))
            || before.contains(QString::fromUtf8("read"))
            || before.contains(QString::fromUtf8("текст"));
        const QString bestBefore = text.left(best.start).toLower();
        const bool bestAfterSynth =
            bestBefore.contains(QString::fromUtf8("синтезир"))
            || bestBefore.contains(QString::fromUtf8("озвуч"))
            || bestBefore.contains(QString::fromUtf8("speak"))
            || bestBefore.contains(QString::fromUtf8("synthes"));
        if (afterSynth && !bestAfterSynth)
            best = h;
        else if (afterSynth == bestAfterSynth && h.body.size() > best.body.size())
            best = h;
    }
    return best.body;
}

//----------------------------------------------

static QString ttsStripInstructionWrapper(QString text)
{
    text = text.trimmed();
    if (text.isEmpty())
        return text;

    QString rest;
    if (ttsContentMarkerLine(text, &rest) && !rest.isEmpty())
        text = rest;

    // Хвост: «Верни аудио…» / Return the audio…
    const QString low = text.toLower();
    static const char *tails[] = {
        " верни аудио", " верните аудио", " сообщи", " сообщите",
        " укажи", " укажите", " сохрани", " сохраните",
        " return the audio", " return audio", " save the audio",
        " tell me", " report ",
        0
    };
    int cut = -1;
    for (int i = 0; tails[i]; ++i)
    {
        const int at = low.indexOf(QString::fromUtf8(tails[i]));
        if (at >= 0 && (cut < 0 || at < cut))
            cut = at;
    }
    if (cut > 0)
        text = text.left(cut).trimmed();
    return text.trimmed();
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

    const QString quoted = ttsExtractQuotedSpeech(task);
    if (!quoted.isEmpty())
        return quoted;

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
        {
            if (ttsInstructionSuffix(line))
                break;
            body.append(line);
        }
        else if (!ttsMetaLine(line) && !ttsInstructionSuffix(line))
        {
            body.append(line);
        }
    }
    if (!body.isEmpty())
        return ttsStripInstructionWrapper(body.join(QString::fromUtf8("\n")).trimmed());

    QStringList nonMeta;
    foreach (const QString &line, lines)
    {
        if (!ttsMetaLine(line) && !ttsInstructionSuffix(line))
            nonMeta.append(line);
    }
    const QString joined = nonMeta.join(QString::fromUtf8("\n")).trimmed();
    return ttsStripInstructionWrapper(joined.isEmpty() ? task : joined);
}

//----------------------------------------------

/** Итоговая строка для TTS: только речь, без мета-инструкций LLM. */
static QString ttsSanitizeSpeakText(QString raw)
{
    raw = raw.trimmed();
    if (raw.isEmpty())
        return raw;
    const QString quoted = ttsExtractQuotedSpeech(raw);
    if (!quoted.isEmpty())
        return quoted;
    return ttsHeuristicPlainTask(raw);
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
        speak = trimmedTask;
    speak = ttsSanitizeSpeakText(speak);

    out.insert(QString::fromUtf8("text"), speak.trimmed());

    QString voice = obj.value(QString::fromUtf8("voice")).toString().trimmed();
    if (voice.isEmpty())
        voice = obj.value(QString::fromUtf8("voice_id")).toString().trimmed();
    if (!voice.isEmpty())
        out.insert(QString::fromUtf8("voice"), voice);

    QString format = obj.value(QString::fromUtf8("format")).toString().trimmed();
    if (format.isEmpty())
        format = obj.value(QString::fromUtf8("response_format")).toString().trimmed();
    if (!format.isEmpty())
        out.insert(QString::fromUtf8("format"), format);
    const QString outputPath = obj.value(QString::fromUtf8("output_path")).toString().trimmed();
    if (!outputPath.isEmpty())
        out.insert(QString::fromUtf8("output_path"), outputPath);

    const QString instructions = obj.value(QString::fromUtf8("instructions")).toString().trimmed();
    if (!instructions.isEmpty())
        out.insert(QString::fromUtf8("instructions"), instructions);

    return out;
}

//----------------------------------------------

static void modalityNotice(QObject *toolParent, QString text);

//----------------------------------------------

static void waitMs(int ms)
{
    if (ms <= 0)
        return;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));
    timer.start(ms);
    loop.exec();
}

//----------------------------------------------

static QStringList urlsFromJsonValue(const QJsonValue &value)
{
    QStringList urls;
    if (value.isString())
    {
        const QString url = value.toString().trimmed();
        if (!url.isEmpty())
            urls.append(url);
    }
    else if (value.isArray())
    {
        const QJsonArray list = value.toArray();
        for (int i = 0; i < list.size(); ++i)
            urls.append(urlsFromJsonValue(list.at(i)));
    }
    return urls;
}

//----------------------------------------------

static QJsonObject taskObjectFromRoot(const QJsonObject &root)
{
    const QJsonValue data = root.value(QString::fromUtf8("data"));
    if (data.isObject())
        return data.toObject();
    if (data.isArray() && !data.toArray().isEmpty() && data.toArray().at(0).isObject())
        return data.toArray().at(0).toObject();
    return root;
}

//----------------------------------------------

static QString submittedTaskId(const QJsonObject &root)
{
    const QJsonObject item = taskObjectFromRoot(root);
    if (item.contains(QString::fromUtf8("result")))
        return QString();
    if (!item.value(QString::fromUtf8("b64_json")).toString().trimmed().isEmpty())
        return QString();
    if (!urlsFromJsonValue(item.value(QString::fromUtf8("url"))).isEmpty())
        return QString();
    QString id = item.value(QString::fromUtf8("task_id")).toString().trimmed();
    if (id.isEmpty())
        id = item.value(QString::fromUtf8("id")).toString().trimmed();
    if (id.isEmpty())
        return QString();
    const QString status = item.value(QString::fromUtf8("status")).toString().trimmed().toLower();
    if (status == QString::fromUtf8("completed")
        || status == QString::fromUtf8("succeeded")
        || status == QString::fromUtf8("success")
        || status == QString::fromUtf8("failed")
        || status == QString::fromUtf8("error"))
        return QString();
    return id;
}

//----------------------------------------------

/** Ссылка на файл результата: ключ, где она лежала, и сам URL. */
struct MediaLink
{
    QString key;
    QString url;
};

//----------------------------------------------

static void collectMediaLinks(const QJsonValue &value, const QString &key,
                              QList<MediaLink> *out)
{
    if (!out)
        return;
    if (value.isObject())
    {
        const QJsonObject obj = value.toObject();
        foreach (const QString &k, obj.keys())
            collectMediaLinks(obj.value(k), k, out);
        return;
    }
    if (value.isArray())
    {
        const QJsonArray arr = value.toArray();
        for (int i = 0; i < arr.size(); ++i)
            collectMediaLinks(arr.at(i), key, out);
        return;
    }
    if (!value.isString())
        return;
    const QString text = value.toString().trimmed();
    if (!text.startsWith(QString::fromUtf8("http://"), Qt::CaseInsensitive)
        && !text.startsWith(QString::fromUtf8("https://"), Qt::CaseInsensitive)
        && !text.startsWith(QString::fromUtf8("data:"), Qt::CaseInsensitive))
        return;
    MediaLink link;
    link.key = key.toLower();
    link.url = text;
    out->append(link);
}

//----------------------------------------------

static bool keyMentionsAny(const QString &key, const char *const *words)
{
    for (int i = 0; words[i]; ++i)
    {
        if (key.contains(QString::fromUtf8(words[i])))
            return true;
    }
    return false;
}

//----------------------------------------------

static bool urlHasAnyExtension(const QString &url, const char *const *exts)
{
    QString path = url;
    const int q = path.indexOf(QChar('?'));
    if (q >= 0)
        path = path.left(q);
    path = path.toLower();
    for (int i = 0; exts[i]; ++i)
    {
        if (path.endsWith(QString::fromUtf8(".") + QString::fromUtf8(exts[i])))
            return true;
    }
    return false;
}

//----------------------------------------------

/**
 * @brief Все ссылки на файлы результата, подходящие под класс задач.
 *
 * kind: "image" | "video" | "audio". Сначала ссылки, у которых ключ или
 * расширение говорят о нужной модальности; если таких нет — любые ссылки,
 * кроме служебных (статус, превью, обложка).
 */
static QStringList resultMediaUrls(const QJsonObject &root, const QString &kind)
{
    static const char *imageWords[] = { "image", "img", "picture", "photo", 0 };
    static const char *videoWords[] = { "video", "clip", "movie", 0 };
    static const char *audioWords[] = { "audio", "sound", "speech", "voice", "track", 0 };
    static const char *imageExts[] = { "png", "jpg", "jpeg", "webp", "gif", "bmp", "svg", 0 };
    static const char *videoExts[] = { "mp4", "webm", "mov", "mkv", "avi", 0 };
    static const char *audioExts[] = { "mp3", "wav", "m4a", "ogg", "flac", "aac", "opus", 0 };
    static const char *serviceWords[] = { "status", "poll", "task", "callback", "thumb",
                                          "preview", "cover", "icon", "avatar", 0 };

    const char *const *words = audioWords;
    const char *const *exts = audioExts;
    const char *const *otherWords1 = imageWords;
    const char *const *otherWords2 = videoWords;
    if (kind == QString::fromUtf8("image"))
    {
        words = imageWords;
        exts = imageExts;
        otherWords1 = videoWords;
        otherWords2 = audioWords;
    }
    else if (kind == QString::fromUtf8("video"))
    {
        words = videoWords;
        exts = videoExts;
        otherWords1 = imageWords;
        otherWords2 = audioWords;
    }

    QList<MediaLink> links;
    collectMediaLinks(root, QString(), &links);

    QStringList matched;
    QStringList neutral;
    foreach (const MediaLink &link, links)
    {
        if (keyMentionsAny(link.key, serviceWords)
            || link.url.contains(QString::fromUtf8("/tasks/"), Qt::CaseInsensitive))
            continue;
        const bool sameKind = keyMentionsAny(link.key, words) || urlHasAnyExtension(link.url, exts);
        const bool otherKind = keyMentionsAny(link.key, otherWords1)
                               || keyMentionsAny(link.key, otherWords2);
        if (sameKind)
        {
            if (!matched.contains(link.url))
                matched.append(link.url);
        }
        else if (!otherKind && !neutral.contains(link.url))
            neutral.append(link.url);
    }
    return matched.isEmpty() ? neutral : matched;
}

//----------------------------------------------

static QJsonObject pollProviderTask(ModalityApiClient *client, const QString &taskId,
                                    const QStringList &statusUrls,
                                    QObject *noticeParent, QString *error)
{
    const int budgetMs = qMax(30000, AppSettings::instance()->llmTimeoutSec() * 1000);
    QElapsedTimer elapsed;
    elapsed.start();
    QString pollUrl;
    modalityNotice(noticeParent,
                   QString::fromUtf8("Waiting for provider task %1…").arg(taskId));
    while (elapsed.elapsed() < budgetMs)
    {
        QStringList candidates;
        if (!pollUrl.isEmpty())
            candidates << pollUrl;
        else
            candidates = statusUrls;
        ModalityApiClient::Result status;
        for (int i = 0; i < candidates.size(); ++i)
        {
            status = client->getBytes(candidates.at(i), 15000);
            if (status.ok || (status.status != 404 && status.status != 405))
            {
                pollUrl = candidates.at(i);
                break;
            }
        }
        if (!status.ok)
        {
            if (error)
            {
                *error = status.error;
                if (status.status == 404 || status.status == 405)
                    *error += QString::fromUtf8(
                        " (no task status URL worked; set the task status path in the "
                        "model profile)");
            }
            return QJsonObject();
        }
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(status.body, &pe);
        if (!doc.isObject())
        {
            if (error)
                *error = QString::fromUtf8("Task status is not JSON.");
            return QJsonObject();
        }
        const QJsonObject root = doc.object();
        const QJsonObject item = taskObjectFromRoot(root);
        const QString state = item.value(QString::fromUtf8("status")).toString().trimmed().toLower();
        if (state == QString::fromUtf8("completed")
            || state == QString::fromUtf8("succeeded")
            || state == QString::fromUtf8("success"))
            return root;
        if (state == QString::fromUtf8("failed")
            || state == QString::fromUtf8("error")
            || state == QString::fromUtf8("cancelled")
            || state == QString::fromUtf8("canceled"))
        {
            QString message = item.value(QString::fromUtf8("error")).toObject()
                                  .value(QString::fromUtf8("message")).toString().trimmed();
            if (message.isEmpty())
                message = QString::fromUtf8(status.body.left(500));
            if (error)
                *error = QString::fromUtf8("Task %1 failed: %2").arg(taskId, message);
            return QJsonObject();
        }
        waitMs(3000);
    }
    if (error)
        *error = QString::fromUtf8("Task %1 timed out before the file was ready.").arg(taskId);
    return QJsonObject();
}

//----------------------------------------------

static bool takeAsyncMedia(ModalityApiClient *client, const QByteArray &body,
                           const QString &submitPath, const QString &kind,
                           QObject *noticeParent, QList<QByteArray> *files, QString *error)
{
    if (!files)
        return false;
    files->clear();
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &pe);
    if (!doc.isObject())
        return false;
    const QString taskId = submittedTaskId(doc.object());
    if (taskId.isEmpty())
        return false;
    const QStringList statusUrls = client->taskStatusUrls(doc.object(), taskId, submitPath);
    const QJsonObject done = pollProviderTask(client, taskId, statusUrls, noticeParent, error);
    if (done.isEmpty())
        return true;
    const QStringList urls = resultMediaUrls(done, kind);
    if (urls.isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("Task %1 completed without a file URL.").arg(taskId);
        return true;
    }
    foreach (const QString &url, urls)
    {
        modalityNotice(noticeParent, QString::fromUtf8("Downloading %1").arg(url.left(120)));
        const ModalityApiClient::Result dl = client->getBytes(url);
        if (!dl.ok)
        {
            if (error)
                *error = dl.error;
            files->clear();
            return true;
        }
        if (!dl.body.isEmpty())
            files->append(dl.body);
    }
    if (files->isEmpty() && error)
        *error = QString::fromUtf8("Task %1 completed but the download was empty.").arg(taskId);
    return true;
}

//----------------------------------------------

/**
 * @brief POST по пути профиля, либо по каталогу и стандартным путям.
 *        404/405 открывает следующий путь только когда путь профиля пуст.
 *        В ошибку попадают все попытки, а не только последняя.
 */
static ModalityApiClient::Result postToFirstEndpoint(ModalityApiClient *client,
                                                     const QStringList &standardPaths,
                                                     const QString &catalogEndpoint,
                                                     const QJsonObject &body,
                                                     QObject *noticeParent,
                                                     const QString &label,
                                                     QString *usedPath)
{
    ModalityApiClient::Result result;
    if (!client)
        return result;
    QStringList ordered = standardPaths;
    const QString fromCatalog = catalogEndpoint.trimmed();
    if (!fromCatalog.isEmpty())
        ordered.prepend(fromCatalog);
    const QStringList paths = client->requestPaths(ordered);
    const bool profilePath = !client->requestPath().isEmpty();
    QStringList attempts;
    foreach (const QString &path, paths)
    {
        const QString url = client->resolveUrl(path);
        modalityNotice(noticeParent, QString::fromUtf8("%1: POST %2…").arg(label, url));
        result = client->postJson(path, body);
        if (usedPath)
            *usedPath = path;
        if (result.ok)
            return result;
        attempts << QString::fromUtf8("%1 — %2").arg(url, result.error);
        if (profilePath)
            break;
        if (result.status != 404 && result.status != 405)
            break;
    }
    if (attempts.isEmpty())
        return result;
    if (profilePath)
    {
        result.error = QString::fromUtf8(
            "Request path from the model profile failed. No other path was tried.\n%1")
            .arg(attempts.join(QString::fromUtf8("\n")));
    }
    else
    {
        result.error = QString::fromUtf8(
            "Generation request failed. The model profile request path is empty, "
            "so standard paths were tried:\n%1\n"
            "Set the request path on the model profile to the provider endpoint.")
            .arg(attempts.join(QString::fromUtf8("\n")));
    }
    return result;
}

//----------------------------------------------

static QString saveDownloadedFiles(ProjectWorkspace *workspace, Worker *owner,
                                   const QList<QByteArray> &files, QString outputPath,
                                   const QString &prefix, const QString &extension,
                                   QString *error)
{
    if (files.isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("No downloaded files.");
        return QString();
    }
    QString path = outputPath.trimmed();
    if (path.isEmpty())
        path = ModalityApiClient::defaultArtifactPath(workspace, prefix, extension);
    QStringList saved;
    for (int i = 0; i < files.size(); ++i)
    {
        QString onePath = path;
        if (files.size() > 1)
        {
            const QFileInfo info(path);
            onePath = info.path() + QChar('/') + info.completeBaseName()
                      + QString::fromUtf8("_%1.").arg(i + 1) + info.suffix();
        }
        QString saveError;
        const QString rel = ModalityApiClient::saveBytes(workspace, onePath, files.at(i),
                                                        &saveError);
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
    return saved.join(QString::fromUtf8(", "));
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
        QString b64 = item.value(QString::fromUtf8("b64_json")).toString();
        if (b64.isEmpty())
            b64 = item.value(QString::fromUtf8("b64")).toString();
        if (b64.isEmpty())
            b64 = item.value(QString::fromUtf8("image_base64")).toString();
        if (!b64.isEmpty())
        {
            bytes = decodeDataUrlOrBase64(b64);
        }
        else
        {
            QString url = item.value(QString::fromUtf8("url")).toString().trimmed();
            if (url.isEmpty())
            {
                const QJsonObject imageUrl = item.value(QString::fromUtf8("image_url")).toObject();
                url = imageUrl.value(QString::fromUtf8("url")).toString().trimmed();
            }
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
        {
            QStringList keys;
            if (!data.isEmpty() && data.at(0).isObject())
                keys = data.at(0).toObject().keys();
            *error = QString::fromUtf8(
                "No image bytes in provider response. First data item keys: %1.")
                         .arg(keys.isEmpty() ? QString::fromUtf8("(none)")
                                             : keys.join(QString::fromUtf8(", ")));
        }
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
        "Minimal payload by default (model+prompt). Optional provider fields "
        "size, n, resolution, quality, output_format and background are forwarded "
        "when set and dropped if the provider rejects them. Pass reference_path "
        "for image-to-image models that require input_references. "
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
             QString::fromUtf8("Optional count. Sent when set; dropped if the provider rejects n."));
    QJsonObject refPath;
    refPath.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    refPath.insert(QString::fromUtf8("description"),
                   QString::fromUtf8(
                       "Project-relative path to a reference image for image-to-image "
                       "(sent as input_references[0] data URL). Required when the model "
                       "needs an input image."));
    QJsonObject refUrl;
    refUrl.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    refUrl.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("HTTP(S) or data: URL reference image (alternative "
                                   "to reference_path)."));
    QJsonObject output;
    output.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    output.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("Relative output path inside the project."));
    QJsonObject resolution;
    resolution.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    resolution.insert(QString::fromUtf8("description"),
                      QString::fromUtf8("Optional provider resolution (for example 1k, 2k)."));
    QJsonObject quality;
    quality.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    quality.insert(QString::fromUtf8("description"),
                   QString::fromUtf8("Optional provider quality."));
    QJsonObject outputFormat;
    outputFormat.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    outputFormat.insert(QString::fromUtf8("description"),
                        QString::fromUtf8("Optional output format (png, jpeg, webp)."));
    QJsonObject background;
    background.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    background.insert(QString::fromUtf8("description"),
                      QString::fromUtf8("Optional background (for example transparent)."));
    QJsonObject properties;
    properties.insert(QString::fromUtf8("prompt"), prompt);
    properties.insert(QString::fromUtf8("size"), size);
    properties.insert(QString::fromUtf8("n"), n);
    properties.insert(QString::fromUtf8("resolution"), resolution);
    properties.insert(QString::fromUtf8("quality"), quality);
    properties.insert(QString::fromUtf8("output_format"), outputFormat);
    properties.insert(QString::fromUtf8("background"), background);
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

static void copyArgString(QJsonObject *body, const QJsonObject &args, const char *key)
{
    if (!body || !key)
        return;
    const QString value = args.value(QString::fromUtf8(key)).toString().trimmed();
    if (!value.isEmpty())
        body->insert(QString::fromUtf8(key), value);
}

//----------------------------------------------

static bool mediaFieldHandled(const QString &key)
{
    static const char *skip[] = {
        "prompt", "text", "input", "script", "model", "output_path",
        "reference_path", "reference_url", "path", "instructions",
        "format", "voice", "voice_id", "n", "size", "seconds", "duration",
        "resolution", "quality", "output_format", "background", "aspect_ratio",
        0
    };
    for (int i = 0; skip[i]; ++i)
    {
        if (key == QString::fromUtf8(skip[i]))
            return true;
    }
    return false;
}

//----------------------------------------------

static void copyExtraMediaFields(QJsonObject *body, const QJsonObject &args)
{
    if (!body)
        return;
    foreach (const QString &key, args.keys())
    {
        if (mediaFieldHandled(key) || body->contains(key))
            continue;
        const QJsonValue value = args.value(key);
        if (value.isNull() || value.isUndefined() || value.isObject())
            continue;
        if (value.isString() && value.toString().trimmed().isEmpty())
            continue;
        if (value.isArray() && value.toArray().isEmpty())
            continue;
        body->insert(key, value);
    }
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
    if (!size.isEmpty())
        body.insert(QString::fromUtf8("size"), size);
    copyArgString(&body, args, "resolution");
    copyArgString(&body, args, "quality");
    copyArgString(&body, args, "output_format");
    copyArgString(&body, args, "background");
    copyArgString(&body, args, "aspect_ratio");
    copyExtraMediaFields(&body, args);

    if (args.contains(QString::fromUtf8("n")))
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

    client.applyBodyDefaults(&body);
    QStringList endpoints;
    endpoints << QString::fromUtf8("/images/generations")
              << QString::fromUtf8("/images");

    ModalityApiClient::Result result;
    QString usedEndpoint;
    for (int pass = 0; pass < 3; ++pass)
    {
        result = postToFirstEndpoint(&client, endpoints, caps.requestEndpoint, body,
                                     parent(), QString::fromUtf8("Image"), &usedEndpoint);
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
            if (body.contains(QString::fromUtf8("aspect_ratio"))
                && errorMentions(err, QString::fromUtf8("aspect_ratio")))
            {
                body.remove(QString::fromUtf8("aspect_ratio"));
                changed = true;
            }
        }
        foreach (const QString &key, body.keys())
        {
            if (key == QString::fromUtf8("model")
                || key == QString::fromUtf8("prompt")
                || key == QString::fromUtf8("n")
                || key == QString::fromUtf8("input_references")
                || key == QString::fromUtf8("image_urls")
                || key.size() < 4)
                continue;
            if (!errorMentions(err, key))
                continue;
            body.remove(key);
            modalityNotice(parent(),
                           QString::fromUtf8("Image: provider rejected %1 — retrying without it.")
                               .arg(key));
            changed = true;
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
                    "For pure text-to-image use a model that does not require references.\n"
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

    QList<QByteArray> asyncFiles;
    QString asyncError;
    if (takeAsyncMedia(&client, result.body, usedEndpoint, QString::fromUtf8("image"),
                       parent(), &asyncFiles, &asyncError))
    {
        if (asyncFiles.isEmpty())
        {
            if (ok)
                *ok = false;
            return asyncError;
        }
        const QString saved = saveDownloadedFiles(
            _workspace, _owner, asyncFiles,
            args.value(QString::fromUtf8("output_path")).toString(),
            QString::fromUtf8("image"), QString::fromUtf8("png"), &error);
        if (saved.isEmpty())
        {
            if (ok)
                *ok = false;
            return error;
        }
        if (ok)
            *ok = true;
        modalityNotice(parent(), QString::fromUtf8("Image: saved → %1").arg(saved));
        return QString::fromUtf8("Generated image(s): %1").arg(saved);
    }

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
        "Put ONLY the exact spoken words in `text` (no 'synthesize…', no 'return audio…'). "
        "If you wrap a quote in «…» or \"…\", only the quote is spoken. "
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
    client.applyBodyDefaults(&body);

    const ModelCapabilities caps =
        ModelCapabilityService::instance()->capabilitiesFor(profile);
    QString usedPath;
    const ModalityApiClient::Result result = postToFirstEndpoint(
        &client, QStringList() << QString::fromUtf8("/audio/speech"),
        caps.requestEndpoint, body, parent(), QString::fromUtf8("Speech"), &usedPath);
    if (!result.ok)
    {
        if (ok)
            *ok = false;
        return result.error;
    }

    QList<QByteArray> asyncFiles;
    QString asyncError;
    if (takeAsyncMedia(&client, result.body, usedPath, QString::fromUtf8("audio"),
                       parent(), &asyncFiles, &asyncError))
    {
        if (asyncFiles.isEmpty())
        {
            if (ok)
                *ok = false;
            return asyncError;
        }
        const QString saved = saveDownloadedFiles(
            _workspace, _owner, asyncFiles, path,
            QString::fromUtf8("tts"), format, &error);
        if (saved.isEmpty())
        {
            if (ok)
                *ok = false;
            return error;
        }
        if (ok)
            *ok = true;
        return QString::fromUtf8("Speech audio: %1").arg(saved);
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
    return QString::fromUtf8("TTS audio saved: %1\nSpoken text: %2")
        .arg(rel, text);
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
    const QString boundary = QString::fromUtf8("----unique%1")
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
    copyExtraMediaFields(&body, args);
    client.applyBodyDefaults(&body);

    const ModelCapabilities caps =
        ModelCapabilityService::instance()->capabilitiesFor(profile);
    QString usedPath;
    const ModalityApiClient::Result result = postToFirstEndpoint(
        &client,
        QStringList() << QString::fromUtf8("/audio/generations")
                      << QString::fromUtf8("/sound/generations"),
        caps.requestEndpoint, body, parent(), QString::fromUtf8("Audio"), &usedPath);
    if (!result.ok)
    {
        if (ok)
            *ok = false;
        return result.error;
    }

    QList<QByteArray> asyncFiles;
    QString asyncError;
    if (takeAsyncMedia(&client, result.body, usedPath, QString::fromUtf8("audio"),
                       parent(), &asyncFiles, &asyncError))
    {
        if (asyncFiles.isEmpty())
        {
            if (ok)
                *ok = false;
            return asyncError;
        }
        const QString saved = saveDownloadedFiles(
            _workspace, _owner, asyncFiles, path,
            QString::fromUtf8("audio"), QString::fromUtf8("mp3"), &error);
        if (saved.isEmpty())
        {
            if (ok)
                *ok = false;
            return error;
        }
        if (ok)
            *ok = true;
        return QString::fromUtf8("Generated audio: %1").arg(saved);
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
    copyExtraMediaFields(&body, args);
    client.applyBodyDefaults(&body);

    const ModelCapabilities caps =
        ModelCapabilityService::instance()->capabilitiesFor(profile);
    QString usedPath;
    const ModalityApiClient::Result result = postToFirstEndpoint(
        &client,
        QStringList() << QString::fromUtf8("/videos/generations")
                      << QString::fromUtf8("/video/generations")
                      << QString::fromUtf8("/videos"),
        caps.requestEndpoint, body, parent(), QString::fromUtf8("Video"), &usedPath);
    if (!result.ok)
    {
        if (ok)
            *ok = false;
        return result.error;
    }

    QList<QByteArray> asyncFiles;
    QString asyncError;
    if (takeAsyncMedia(&client, result.body, usedPath, QString::fromUtf8("video"),
                       parent(), &asyncFiles, &asyncError))
    {
        if (asyncFiles.isEmpty())
        {
            if (ok)
                *ok = false;
            return asyncError;
        }
        const QString saved = saveDownloadedFiles(
            _workspace, _owner, asyncFiles, path,
            QString::fromUtf8("video"), QString::fromUtf8("mp4"), &error);
        if (saved.isEmpty())
        {
            if (ok)
                *ok = false;
            return error;
        }
        if (ok)
            *ok = true;
        return QString::fromUtf8("Generated video: %1").arg(saved);
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

    client.applyBodyDefaults(&body);
    QString usedPath;
    const ModalityApiClient::Result result = postToFirstEndpoint(
        &client, QStringList() << QString::fromUtf8("/embeddings"), QString(), body,
        parent(), QString::fromUtf8("Embeddings"), &usedPath);
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
