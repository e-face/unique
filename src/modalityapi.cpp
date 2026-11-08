#include "modalityapi.h"
//----------------------------------------------
#include "projectworkspace.h"
//----------------------------------------------
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

//----------------------------------------------

ModalityApiClient::ModalityApiClient(const LlmProfile &profile)
    : _profile(profile)
    , _network(0)
{
}

//----------------------------------------------

ModalityApiClient::~ModalityApiClient()
{
    delete _network;
    _network = 0;
}

//----------------------------------------------

QNetworkAccessManager *ModalityApiClient::network() const
{
    if (!_network)
        _network = new QNetworkAccessManager();
    return _network;
}

//----------------------------------------------

QString ModalityApiClient::baseUrl() const
{
    return _profile.baseUrl.trimmed();
}

//----------------------------------------------

QString ModalityApiClient::model() const
{
    return _profile.model.trimmed();
}

//----------------------------------------------

QString ModalityApiClient::apiKey() const
{
    return _profile.apiKey.trimmed();
}

//----------------------------------------------

static void collectTaskLinks(const QJsonValue &value, const QString &key, QStringList *out)
{
    if (!out)
        return;
    if (value.isObject())
    {
        const QJsonObject obj = value.toObject();
        foreach (const QString &k, obj.keys())
            collectTaskLinks(obj.value(k), k, out);
        return;
    }
    if (value.isArray())
    {
        const QJsonArray arr = value.toArray();
        for (int i = 0; i < arr.size(); ++i)
            collectTaskLinks(arr.at(i), key, out);
        return;
    }
    if (!value.isString())
        return;
    const QString text = value.toString().trimmed();
    if (!text.startsWith(QString::fromUtf8("http://"), Qt::CaseInsensitive)
        && !text.startsWith(QString::fromUtf8("https://"), Qt::CaseInsensitive))
        return;
    const QString low = key.toLower();
    // Ссылка на статус задачи: ключ говорит об этом, или путь содержит /tasks/.
    const bool linkKey = low.contains(QString::fromUtf8("status"))
                         || low.contains(QString::fromUtf8("poll"))
                         || low.contains(QString::fromUtf8("task"))
                         || low.contains(QString::fromUtf8("query"))
                         || low == QString::fromUtf8("self")
                         || low == QString::fromUtf8("href");
    if (linkKey || text.contains(QString::fromUtf8("/tasks/"), Qt::CaseInsensitive))
    {
        if (!out->contains(text))
            out->append(text);
    }
}

//----------------------------------------------

QString ModalityApiClient::requestPath() const
{
    return _profile.requestPath.trimmed();
}

//----------------------------------------------

QStringList ModalityApiClient::requestPaths(const QStringList &standardPaths) const
{
    const QString custom = requestPath();
    if (!custom.isEmpty())
        return QStringList() << custom;

    QStringList paths;
    foreach (const QString &path, standardPaths)
    {
        if (!path.trimmed().isEmpty() && !paths.contains(path))
            paths.append(path);
    }
    return paths;
}

//----------------------------------------------

QStringList ModalityApiClient::taskStatusUrls(const QJsonObject &submitResponse,
                                              QString taskId, QString submitPath) const
{
    QStringList urls;
    const QString id = taskId.trimmed();

    const QString tmpl = _profile.taskPath.trimmed();
    if (!tmpl.isEmpty() && !id.isEmpty())
    {
        QString path = tmpl;
        if (path.contains(QString::fromUtf8("{id}")))
            path.replace(QString::fromUtf8("{id}"), id);
        else
            path = path + (path.endsWith(QChar('/')) ? QString() : QString(QChar('/'))) + id;
        urls.append(resolveUrl(path));
    }

    QStringList links;
    collectTaskLinks(submitResponse, QString(), &links);
    foreach (const QString &link, links)
    {
        if (!urls.contains(link))
            urls.append(link);
    }

    if (id.isEmpty())
        return urls;

    // Рядом с путём запроса: /x/y/generations → /x/y/tasks/{id}, затем /x/tasks/{id} …
    QString rel = submitPath.trimmed();
    if (rel.startsWith(QString::fromUtf8("http://"), Qt::CaseInsensitive)
        || rel.startsWith(QString::fromUtf8("https://"), Qt::CaseInsensitive))
    {
        const QUrl url(rel);
        rel = url.path();
        const QString basePath = QUrl(baseUrl()).path();
        if (!basePath.isEmpty() && rel.startsWith(basePath))
            rel = rel.mid(basePath.size());
    }
    while (rel.startsWith(QChar('/')))
        rel.remove(0, 1);
    QStringList parts = rel.split(QChar('/'), QString::SkipEmptyParts);
    if (!parts.isEmpty())
        parts.removeLast(); // сам endpoint (generations, speech, …)
    QStringList derived;
    while (!parts.isEmpty())
    {
        derived.append(QString::fromUtf8("/%1/tasks/%2").arg(parts.join(QChar('/')), id));
        parts.removeLast();
    }
    const QString root = QString::fromUtf8("/tasks/%1").arg(id);
    // Сначала корневой вариант, затем вложенные: у большинства API задачи общие.
    QStringList ordered;
    ordered.append(root);
    ordered.append(derived);
    foreach (const QString &path, ordered)
    {
        const QString full = resolveUrl(path);
        if (!urls.contains(full))
            urls.append(full);
    }
    return urls;
}

//----------------------------------------------

void ModalityApiClient::applyBodyDefaults(QJsonObject *body) const
{
    if (!body)
        return;
    const QString extra = _profile.extraBody.trimmed();
    if (extra.isEmpty())
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(extra.toUtf8());
    if (!doc.isObject())
        return;
    const QJsonObject defaults = doc.object();
    foreach (const QString &key, defaults.keys())
    {
        if (!body->contains(key))
            body->insert(key, defaults.value(key));
    }
}

//----------------------------------------------

QString ModalityApiClient::resolveUrl(QString relativeOrAbsolutePath) const
{
    const QString path = relativeOrAbsolutePath.trimmed();
    if (path.startsWith(QString::fromUtf8("http://"), Qt::CaseInsensitive)
        || path.startsWith(QString::fromUtf8("https://"), Qt::CaseInsensitive))
        return path;

    QString base = baseUrl();
    while (base.endsWith(QChar('/')))
        base.chop(1);
    QString rel = path;
    while (rel.startsWith(QChar('/')))
        rel.remove(0, 1);
    // База уже кончается на /v1, а путь снова начинается с v1/.
    if (base.endsWith(QString::fromUtf8("/v1"), Qt::CaseInsensitive)
        && (rel == QString::fromUtf8("v1")
            || rel.startsWith(QString::fromUtf8("v1/"), Qt::CaseInsensitive)))
    {
        rel = rel.mid(2);
        while (rel.startsWith(QChar('/')))
            rel.remove(0, 1);
    }
    if (rel.isEmpty())
        return base;
    return base + QChar('/') + rel;
}

//----------------------------------------------

ModalityApiClient::Result ModalityApiClient::doLocalPost(QString url, QByteArray body,
                                                         QByteArray contentType) const
{
    Result result;
    if (apiKey().isEmpty())
    {
        result.error = QString::fromUtf8("API key is empty for this model profile.");
        return result;
    }
    if (url.trimmed().isEmpty())
    {
        result.error = QString::fromUtf8("API URL is empty.");
        return result;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    if (!contentType.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, QString::fromUtf8(contentType));
    request.setRawHeader("Authorization",
                         QByteArray("Bearer ") + apiKey().toUtf8());
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = network()->post(request, body);
    QEventLoop loop;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    const int timeoutMs = AppSettings::instance()->llmTimeoutSec() * 1000;
    QObject::connect(&watchdog, SIGNAL(timeout()), &loop, SLOT(quit()));
    QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
    QObject::connect(reply, SIGNAL(uploadProgress(qint64,qint64)), &watchdog, SLOT(start()));
    if (timeoutMs > 0)
        watchdog.start(timeoutMs);
    loop.exec();

    if (watchdog.isActive())
        watchdog.stop();
    else if (reply->isRunning())
    {
        reply->abort();
        result.error = QString::fromUtf8("timeout");
        reply->deleteLater();
        return result;
    }

    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError
        && reply->error() != QNetworkReply::UnknownContentError)
    {
        result.error = reply->errorString();
        if (result.error.isEmpty())
            result.error = QString::fromUtf8("HTTP %1").arg(result.status);
        if (!result.body.isEmpty())
            result.error += QString::fromUtf8(": ")
                            + QString::fromUtf8(result.body.left(400));
        reply->deleteLater();
        return result;
    }
    if (result.status >= 400)
    {
        result.error = QString::fromUtf8("HTTP %1: %2")
                           .arg(result.status)
                           .arg(QString::fromUtf8(result.body.left(600)));
        reply->deleteLater();
        return result;
    }
    result.ok = true;
    reply->deleteLater();
    return result;
}

//----------------------------------------------

ModalityApiClient::Result ModalityApiClient::doLocalGet(QString url, int timeoutMs) const
{
    Result result;
    if (url.trimmed().isEmpty())
    {
        result.error = QString::fromUtf8("Download URL is empty.");
        return result;
    }

    QNetworkRequest request;
    request.setUrl(QUrl(url));
    if (!apiKey().isEmpty())
        request.setRawHeader("Authorization",
                             QByteArray("Bearer ") + apiKey().toUtf8());

    QNetworkReply *reply = network()->get(request);
    QEventLoop loop;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    if (timeoutMs <= 0)
        timeoutMs = AppSettings::instance()->llmTimeoutSec() * 1000;
    QObject::connect(&watchdog, SIGNAL(timeout()), &loop, SLOT(quit()));
    QObject::connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
    if (timeoutMs > 0)
        watchdog.start(timeoutMs);
    loop.exec();

    if (watchdog.isActive())
        watchdog.stop();
    else if (reply->isRunning())
    {
        reply->abort();
        result.error = QString::fromUtf8("timeout");
        reply->deleteLater();
        return result;
    }

    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError)
    {
        result.error = reply->errorString();
        reply->deleteLater();
        return result;
    }
    if (result.status >= 400)
    {
        result.error = QString::fromUtf8("HTTP %1 while downloading").arg(result.status);
        reply->deleteLater();
        return result;
    }
    result.ok = true;
    reply->deleteLater();
    return result;
}

//----------------------------------------------

ModalityApiClient::Result ModalityApiClient::postJson(QString relativeOrAbsolutePath,
                                                      const QJsonObject &body) const
{
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return doLocalPost(resolveUrl(relativeOrAbsolutePath), payload,
                       QByteArray("application/json"));
}

//----------------------------------------------

ModalityApiClient::Result ModalityApiClient::postRaw(QString relativeOrAbsolutePath,
                                                     QByteArray body,
                                                     QByteArray contentType) const
{
    return doLocalPost(resolveUrl(relativeOrAbsolutePath), body, contentType);
}

//----------------------------------------------

ModalityApiClient::Result ModalityApiClient::getBytes(QString absoluteUrl,
                                                       int timeoutMs) const
{
    return doLocalGet(absoluteUrl, timeoutMs);
}

//----------------------------------------------

QString ModalityApiClient::saveBytes(ProjectWorkspace *workspace, QString relativePath,
                                     const QByteArray &bytes, QString *error)
{
    if (!workspace || !workspace->isSet())
    {
        if (error)
            *error = QString::fromUtf8("Project workspace is not set.");
        return QString();
    }
    QString abs;
    QString resolveError;
    if (!workspace->resolvePath(relativePath, false, true, &abs, &resolveError))
    {
        if (error)
            *error = resolveError;
        return QString();
    }
    const QFileInfo info(abs);
    if (!QDir().mkpath(info.absolutePath()))
    {
        if (error)
            *error = QString::fromUtf8("Cannot create directory for %1").arg(relativePath);
        return QString();
    }
    QFile file(abs);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (error)
            *error = QString::fromUtf8("Cannot write %1: %2")
                         .arg(relativePath, file.errorString());
        return QString();
    }
    if (file.write(bytes) != bytes.size())
    {
        if (error)
            *error = QString::fromUtf8("Short write to %1").arg(relativePath);
        file.close();
        return QString();
    }
    file.close();
    return workspace->toRelative(abs);
}

//----------------------------------------------

QString ModalityApiClient::defaultArtifactPath(ProjectWorkspace *workspace, QString prefix,
                                               QString extension)
{
    QString dir = QString::fromUtf8("___project___/tmp");
    if (workspace && workspace->isSet() && workspace->ensureTempDir())
        dir = workspace->toRelative(workspace->tempDir());
    const QString stamp = QDateTime::currentDateTime().toString(
        QString::fromUtf8("yyyyMMdd_hhmmss_zzz"));
    QString ext = extension.trimmed();
    if (ext.startsWith(QChar('.')))
        ext = ext.mid(1);
    if (ext.isEmpty())
        ext = QString::fromUtf8("bin");
    QString pref = prefix.trimmed();
    if (pref.isEmpty())
        pref = QString::fromUtf8("artifact");
    return dir + QChar('/') + pref + QChar('_') + stamp + QChar('.') + ext;
}
