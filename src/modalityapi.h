#ifndef MODALITYAPI_H
#define MODALITYAPI_H

#include "appsettings.h"
//----------------------------------------------
#include <QByteArray>
#include <QJsonObject>
#include <QString>

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

    QString baseUrl() const;
    QString model() const;
    QString apiKey() const;

    Result postJson(QString relativeOrAbsolutePath, const QJsonObject &body) const;
    Result postRaw(QString relativeOrAbsolutePath, QByteArray body,
                   QByteArray contentType) const;
    Result getBytes(QString absoluteUrl) const;

    QString resolveUrl(QString relativeOrAbsolutePath) const;

    static QString saveBytes(ProjectWorkspace *workspace, QString relativePath,
                             const QByteArray &bytes, QString *error);
    static QString defaultArtifactPath(ProjectWorkspace *workspace, QString prefix,
                                       QString extension);

private:
    Result doLocalPost(QString url, QByteArray body, QByteArray contentType) const;
    Result doLocalGet(QString url) const;

    LlmProfile _profile;
};

#endif // MODALITYAPI_H
