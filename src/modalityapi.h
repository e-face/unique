#ifndef MODALITYAPI_H
#define MODALITYAPI_H

#include "appsettings.h"
//----------------------------------------------
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;

class ProjectWorkspace;

//----------------------------------------------

/**
 * @brief Синхронный HTTP к OpenAI-совместимым media/embeddings endpoint'ам.
 *
 * Блокирует вызывающий поток через QEventLoop (как run_command).
 */
class ModalityApiClient
{
public:
    struct Result
    {
        Result()
            : ok(false)
            , status(0)
        {
        }

        bool       ok;
        int        status;
        QByteArray body;
        QString    error;
    };

    explicit ModalityApiClient(const LlmProfile &profile);
    ~ModalityApiClient();

    QString baseUrl() const;
    QString model() const;
    QString apiKey() const;
    QString requestPath() const;

    /**
     * @brief Куда слать генерацию. Заполненный путь профиля — единственный.
     *        Пустой — каталог провайдера, затем стандартные пути класса задач.
     */
    QStringList requestPaths(const QStringList &standardPaths) const;

    /**
     * @brief URL опроса задачи: из ответа провайдера, из шаблона профиля,
     *        иначе /tasks/{id} рядом с путём запроса и у корня API.
     */
    QStringList taskStatusUrls(const QJsonObject &submitResponse, QString taskId,
                               QString submitPath) const;

    /** Добавить постоянные поля запроса из профиля (не перекрывая заданные). */
    void applyBodyDefaults(QJsonObject *body) const;

    Result postJson(QString relativeOrAbsolutePath, const QJsonObject &body) const;
    Result postRaw(QString relativeOrAbsolutePath, QByteArray body,
                   QByteArray contentType) const;
    Result getBytes(QString absoluteUrl, int timeoutMs = 0) const;

    QString resolveUrl(QString relativeOrAbsolutePath) const;

    static QString saveBytes(ProjectWorkspace *workspace, QString relativePath,
                             const QByteArray &bytes, QString *error);
    static QString defaultArtifactPath(ProjectWorkspace *workspace, QString prefix,
                                       QString extension);

private:
    Q_DISABLE_COPY(ModalityApiClient)

    Result doLocalPost(QString url, QByteArray body, QByteArray contentType) const;
    Result doLocalGet(QString url, int timeoutMs) const;
    QNetworkAccessManager *network() const;

    LlmProfile _profile;
    mutable QNetworkAccessManager *_network;
};

#endif // MODALITYAPI_H
