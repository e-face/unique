#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QByteArray>
#include <QMessageBox>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QUrl;
class QWidget;

//----------------------------------------------

/**
 * @brief Check for a Unique update on the site and launch the installer.
 *
 * One GET with no cookies and no extra headers. The decision comes only
 * from the first cell of the reply: info, unknown, or ok. The file is downloaded after
 * the user agrees, checked by size, and launched separately.
 */
class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    explicit UpdateChecker(QWidget *parent = 0);
    ~UpdateChecker();

    bool isBusy() const;
    void check(bool manual);

public slots:
    void sltAutoCheck();

private slots:
    void sltFinished();
    void sltTimeout();
    void sltDownloadProgress(qint64 received, qint64 total);

private:
    enum Phase
    {
        PhaseIdle,
        PhaseCheck,
        PhaseDownload
    };

    QUrl checkUrl() const;
    void startRequest(const QUrl &url);
    void handleCheck(const QByteArray &data);
    void handleDownload(const QByteArray &data, const QByteArray &contentType,
                        const QByteArray &disposition);
    void offerInstall(const QStringList &cells);
    void failNetwork(const QString &errorText, Phase phase);
    void failTimeout(Phase phase);
    QStringList splitCells(const QByteArray &data) const;
    QString unknownText(const QString &reason, bool download) const;
    QString installerFileName(const QByteArray &disposition) const;
    QString writeInstaller(const QByteArray &data, const QString &fileName) const;
    bool installLinuxPackage(const QString &packagePath, const QString &rootInstall);
    bool askInstallPassword(QString *password);
    void showStatus(const QString &text);
    void beginDownloadProgress();
    void updateDownloadProgress(qint64 received, qint64 total);
    void endDownloadProgress();
    void showMessage(QMessageBox::Icon icon, const QString &text);
    int askYesNo(const QString &title, const QString &text);
    void setIdle();

    QNetworkAccessManager *_nam;
    QNetworkReply         *_reply;
    QTimer                *_timeout;
    QWidget               *_dialogParent;
    QString                _newVersion;
    qint64                 _expectedSize;
    bool                   _manual;
    bool                   _busy;
    bool                   _timedOut;
    bool                   _progressOpen;
    Phase                  _phase;
};

#endif // UPDATECHECKER_H
