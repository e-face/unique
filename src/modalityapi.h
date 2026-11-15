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
 * @brief Synchronous HTTP to OpenAI-compatible media and embeddings endpoints.
 *
 * Blocks the calling thread through QEventLoop, like run_command.
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
     * @brief Where to send generation. A filled profile path is the only one.
     *        Empty means the provider catalog, then the standard paths for the task class.
     */
    QStringList requestPaths(const QStringList &standardPaths) const;

    /**
     * @brief Task poll URL: from the provider response, from the profile template,
     *        otherwise /tasks/{id} next to the request path and at the API root.
     */
    QStringList taskStatusUrls(const QJsonObject &submitResponse, QString taskId,
                               QString submitPath) const;

    /** Add the profile's constant request fields, without overriding ones already set. */
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
