#include "llmnetworkworker.h"
//----------------------------------------------
#include "appsettings.h"

#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QTimer>
#include <QUrl>

//----------------------------------------------

LlmNetworkWorker::LlmNetworkWorker(QObject *parent)
    : QObject(parent)
    , _nam(0)
    , _nextId(1)
    , _thread(0)
{
}

//----------------------------------------------

LlmNetworkWorker::~LlmNetworkWorker()
{
    if (_thread)
    {
        _thread->quit();
        _thread->wait(3000);
    }
}

//----------------------------------------------

LlmNetworkWorker *LlmNetworkWorker::instance()
{
    static LlmNetworkWorker *worker = 0;
    if (!worker)
        worker = new LlmNetworkWorker();
    return worker;
}

//----------------------------------------------

void LlmNetworkWorker::ensureStarted()
{
    if (_thread)
        return;
    _thread = new QThread();
    moveToThread(_thread);
    connect(_thread, SIGNAL(finished()), _thread, SLOT(deleteLater()));
    _thread->start();
}

//----------------------------------------------

int LlmNetworkWorker::requestTimeoutMs() const
{
    // Читается в потоке вызывающего (GUI), где живут настройки.
    return AppSettings::instance()->llmTimeoutSec() * 1000;
}

//----------------------------------------------

int LlmNetworkWorker::post(QString url, QByteArray body, QHash<QByteArray, QByteArray> headers)
{
    ensureStarted();
    const int requestId = _nextId++;
    const QString auth = QString::fromUtf8(headers.value("Authorization"));
    QMetaObject::invokeMethod(this, "doPost", Qt::QueuedConnection,
                              Q_ARG(QString, url),
                              Q_ARG(QByteArray, body),
                              Q_ARG(QString, auth),
                              Q_ARG(int, requestId),
                              Q_ARG(int, requestTimeoutMs()));
    return requestId;
}

//----------------------------------------------

int LlmNetworkWorker::get(QString url, QHash<QByteArray, QByteArray> headers)
{
    ensureStarted();
    const int requestId = _nextId++;
    const QString auth = QString::fromUtf8(headers.value("Authorization"));
    QMetaObject::invokeMethod(this, "doGet", Qt::QueuedConnection,
                              Q_ARG(QString, url),
                              Q_ARG(QString, auth),
                              Q_ARG(int, requestId),
                              Q_ARG(int, requestTimeoutMs()));
    return requestId;
}

//----------------------------------------------

void LlmNetworkWorker::trackReply(QNetworkReply *reply, int requestId, int timeoutMs)
{
    _replyIds.insert(reply, requestId);
    _activeReplies.insert(requestId, reply);
    connect(reply, SIGNAL(finished()), this, SLOT(sltFinished()));

    if (timeoutMs <= 0)
        return;
    // Таймер — ребёнок ответа: умирает вместе с ним. Пока тело запроса
    // ещё уходит на сервер, отсчёт перезапускается.
    QTimer *watchdog = new QTimer(reply);
    watchdog->setSingleShot(true);
    watchdog->setInterval(timeoutMs);
    connect(watchdog, SIGNAL(timeout()), this, SLOT(sltWatchdog()));
    connect(reply, SIGNAL(uploadProgress(qint64,qint64)), watchdog, SLOT(start()));
    watchdog->start();
}

//----------------------------------------------

void LlmNetworkWorker::doPost(QString url, QByteArray body, QString authorization,
                              int requestId, int timeoutMs)
{
    if (!_nam)
        _nam = new QNetworkAccessManager(this);

    QNetworkRequest httpRequest;
    httpRequest.setUrl(QUrl(url));
    httpRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                          QString::fromUtf8("application/json"));
    if (!authorization.isEmpty())
        httpRequest.setRawHeader("Authorization", authorization.toUtf8());
    httpRequest.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = _nam->post(httpRequest, body);
    trackReply(reply, requestId, timeoutMs);
}

//----------------------------------------------

void LlmNetworkWorker::doGet(QString url, QString authorization, int requestId, int timeoutMs)
{
    if (!_nam)
        _nam = new QNetworkAccessManager(this);

    QNetworkRequest httpRequest;
    httpRequest.setUrl(QUrl(url));
    if (!authorization.isEmpty())
        httpRequest.setRawHeader("Authorization", authorization.toUtf8());
    httpRequest.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = _nam->get(httpRequest);
    trackReply(reply, requestId, timeoutMs);
}

//----------------------------------------------

void LlmNetworkWorker::abortRequest(int requestId)
{
    ensureStarted();
    QMetaObject::invokeMethod(this, "doAbort", Qt::QueuedConnection,
                              Q_ARG(int, requestId));
}

//----------------------------------------------

void LlmNetworkWorker::doAbort(int requestId)
{
    QNetworkReply *reply = _activeReplies.value(requestId, 0);
    if (reply)
        reply->abort();
}

//----------------------------------------------

void LlmNetworkWorker::sltWatchdog()
{
    QTimer *watchdog = qobject_cast<QTimer *>(sender());
    QNetworkReply *reply = watchdog ? qobject_cast<QNetworkReply *>(watchdog->parent()) : 0;
    if (!reply || !_replyIds.contains(reply))
        return;
    _timedOut.insert(reply);
    reply->abort();
}

//----------------------------------------------

void LlmNetworkWorker::sltFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;

    const int requestId = _replyIds.value(reply, 0);
    _replyIds.remove(reply);
    _activeReplies.remove(requestId);
    const bool timedOut = _timedOut.remove(reply);

    const QByteArray data = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QString errorText = reply->errorString();
    if (reply->error() == QNetworkReply::NoError)
        errorText.clear();

    if (timedOut)
        emit replyNetworkError(requestId, QString::fromUtf8("timeout"));
    else if (reply->error() == QNetworkReply::OperationCanceledError)
        emit replyNetworkError(requestId, QString::fromUtf8("aborted"));
    else if (reply->error() != QNetworkReply::NoError)
        emit replyNetworkError(requestId, errorText);
    else
        emit replyReady(requestId, data, httpStatus, errorText);

    reply->deleteLater();
}
