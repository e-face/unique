#ifndef LLMNETWORKWORKER_H
#define LLMNETWORKWORKER_H

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QThread;

//----------------------------------------------

/**
 * @brief HTTP requests to the LLM on a separate thread.
 *
 * Each request has a watchdog: if the server does not answer within
 * the given time, the request is aborted with "timeout" instead of hanging forever.
 */
class LlmNetworkWorker : public QObject
{
    Q_OBJECT
public:
    static LlmNetworkWorker *instance();
    void ensureStarted();

    int post(QString url, QByteArray body, QHash<QByteArray, QByteArray> headers);
    int get(QString url, QHash<QByteArray, QByteArray> headers);

signals:
    void replyReady(int requestId, QByteArray data, int httpStatus, QString errorText);
    void replyNetworkError(int requestId, QString errorText);

public slots:
    void abortRequest(int requestId);
    void doPost(QString url, QByteArray body, QString authorization, int requestId,
                int timeoutMs);
    void doGet(QString url, QString authorization, int requestId, int timeoutMs);
    void doAbort(int requestId);

private slots:
    void sltFinished();
    void sltReadyRead();
    void sltWatchdog();

private:
    explicit LlmNetworkWorker(QObject *parent = 0);
    ~LlmNetworkWorker();
    void trackReply(QNetworkReply *reply, int requestId, int timeoutMs);
    int  requestTimeoutMs() const;

    QNetworkAccessManager *_nam;
    QHash<QNetworkReply *, int> _replyIds;
    QHash<int, QNetworkReply *> _activeReplies;
    QHash<QNetworkReply *, QByteArray> _received;
    QSet<QNetworkReply *>    _timedOut;
    int                      _nextId;
    QThread                 *_thread;
};

#endif // LLMNETWORKWORKER_H
