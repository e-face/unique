#include "modalityapi.h"
//----------------------------------------------
#include "projectworkspace.h"
//----------------------------------------------
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

//----------------------------------------------

ModalityApiClient::ModalityApiClient(const LlmProfile &profile)
    : _profile(profile)
{
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

QString ModalityApiClient::resolveUrl(QString relativeOrAbsolutePath) const
{
    const QString path = relativeOrAbsolutePath.trimmed();
    if (path.startsWith(QString::fromUtf8("http://"), Qt::CaseInsensitive)
        || path.startsWith(QString::fromUtf8("https://"), Qt::CaseInsensitive))
        return path;

    QString base = baseUrl();
    while (base.endsWith(QChar('/')))
        base.chop(1);
    if (path.isEmpty())
        return base;
    if (path.startsWith(QChar('/')))
        return base + path;
    return base + QChar('/') + path;
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

    QNetworkAccessManager nam;
    QNetworkRequest request;
    request.setUrl(QUrl(url));
    if (!contentType.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, QString::fromUtf8(contentType));
    request.setRawHeader("Authorization",
                         QByteArray("Bearer ") + apiKey().toUtf8());
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = nam.post(request, body);
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

ModalityApiClient::Result ModalityApiClient::doLocalGet(QString url) const
{
    Result result;
    if (url.trimmed().isEmpty())
    {
        result.error = QString::fromUtf8("Download URL is empty.");
        return result;
    }

    QNetworkAccessManager nam;
    QNetworkRequest request;
    request.setUrl(QUrl(url));
    if (!apiKey().isEmpty())
        request.setRawHeader("Authorization",
                             QByteArray("Bearer ") + apiKey().toUtf8());

    QNetworkReply *reply = nam.get(request);
    QEventLoop loop;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    const int timeoutMs = AppSettings::instance()->llmTimeoutSec() * 1000;
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

ModalityApiClient::Result ModalityApiClient::getBytes(QString absoluteUrl) const
{
    return doLocalGet(absoluteUrl);
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
