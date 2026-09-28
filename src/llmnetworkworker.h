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
 * @brief HTTP-запросы к LLM в отдельном потоке.
 *
 * У каждого запроса есть сторожевой таймер: если сервер не ответил за
 * заданное время, запрос прерывается с ошибкой "timeout", а не висит вечно.
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
    void sltWatchdog();

private:
    explicit LlmNetworkWorker(QObject *parent = 0);
    ~LlmNetworkWorker();
    void trackReply(QNetworkReply *reply, int requestId, int timeoutMs);
    int  requestTimeoutMs() const;

    QNetworkAccessManager *_nam;
    QHash<QNetworkReply *, int> _replyIds;
    QHash<int, QNetworkReply *> _activeReplies;
    QSet<QNetworkReply *>    _timedOut;
    int                      _nextId;
    QThread                 *_thread;
};

#endif // LLMNETWORKWORKER_H
