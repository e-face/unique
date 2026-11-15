#include "updatechecker.h"
//----------------------------------------------
#include "appsettings.h"
#include "appversion.h"
#include "mainwindow.h"
//----------------------------------------------
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>

//----------------------------------------------

static const int kRequestTimeoutMs = 20000;

//----------------------------------------------

static QString psQuote(QString value)
{
    value.replace(QLatin1Char('\''), QLatin1String("''"));
    return QLatin1Char('\'') + value + QLatin1Char('\'');
}

//----------------------------------------------

static QString installedUniquePath()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString root = env.value(QLatin1String("ProgramFiles"));
    if (root.isEmpty())
        root = QLatin1String("C:\\Program Files");
    return QDir::toNativeSeparators(
        QDir(root).filePath(QLatin1String("Unique\\Unique.exe")));
}

//----------------------------------------------

static QString updateUpgradeCode()
{
    if (sizeof(void *) == 8)
        return QLatin1String("{9E2A6C48-5B71-4D0F-A3E8-1C7F5D9B60A2}");
    return QLatin1String("{7C4E9A2B-1F6D-4C8A-9E3B-5A1D8F0C2E64}");
}

//----------------------------------------------

static bool launchMsiThenApp(const QString &msiPath)
{
    const QString scriptPath = QDir(QDir::tempPath()).filePath(
        QLatin1String("unique-update.ps1"));
    const QString nativeMsi = QDir::toNativeSeparators(msiPath);
    const QString nativeExe = installedUniquePath();
    const QString nativeScript = QDir::toNativeSeparators(scriptPath);

    QString body;
    body += QLatin1String("$deadline = (Get-Date).AddSeconds(30)\r\n");
    body += QLatin1String("do {\r\n");
    body += QLatin1String("    $running = @(Get-Process -Name 'Unique' -ErrorAction SilentlyContinue)\r\n");
    body += QLatin1String("    if ($running.Count -eq 0) { break }\r\n");
    body += QLatin1String("    Start-Sleep -Milliseconds 200\r\n");
    body += QLatin1String("} while ((Get-Date) -lt $deadline)\r\n");
    body += QLatin1String("Start-Sleep -Seconds 1\r\n");
    body += QLatin1String("$target = ") + psQuote(nativeExe) + QLatin1String("\r\n");
    body += QLatin1String("$installed = $false\r\n");
    body += QLatin1String("try {\r\n");
    body += QLatin1String("    $installer = New-Object -ComObject WindowsInstaller.Installer\r\n");
    body += QLatin1String("    $installer.UILevel = 5\r\n");
    body += QLatin1String("    $installer.InstallProduct(") + psQuote(nativeMsi)
         + QLatin1String(", '')\r\n");
    body += QLatin1String("    $installed = $true\r\n");
    body += QLatin1String("    try {\r\n");
    body += QLatin1String("        $products = @($installer.RelatedProducts(")
         + psQuote(updateUpgradeCode()) + QLatin1String("))\r\n");
    body += QLatin1String("        foreach ($product in $products) {\r\n");
    body += QLatin1String("            $loc = [string]$installer.ProductInfo($product, 'InstallLocation')\r\n");
    body += QLatin1String("            if ([string]::IsNullOrWhiteSpace($loc)) { continue }\r\n");
    body += QLatin1String("            $candidate = Join-Path $loc 'Unique.exe'\r\n");
    body += QLatin1String("            if (Test-Path -LiteralPath $candidate) { $target = $candidate }\r\n");
    body += QLatin1String("        }\r\n");
    body += QLatin1String("    } catch {}\r\n");
    body += QLatin1String("} catch {\r\n");
    body += QLatin1String("    $installed = $false\r\n");
    body += QLatin1String("}\r\n");
    body += QLatin1String("if ($installed -and (Test-Path -LiteralPath $target)) {\r\n");
    body += QLatin1String("    Start-Process -FilePath $target\r\n");
    body += QLatin1String("}\r\n");
    body += QLatin1String("Remove-Item -LiteralPath ") + psQuote(nativeScript)
         + QLatin1String(" -Force -ErrorAction SilentlyContinue\r\n");

    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    script.write("\xEF\xBB\xBF");
    script.write(body.toUtf8());
    if (!script.flush())
    {
        script.close();
        QFile::remove(scriptPath);
        return false;
    }
    script.close();

    QString systemRoot = QProcessEnvironment::systemEnvironment()
                             .value(QLatin1String("SystemRoot"));
    if (systemRoot.isEmpty())
        systemRoot = QLatin1String("C:\\Windows");
    const QString powershell = QDir(systemRoot).filePath(
        QLatin1String("System32/WindowsPowerShell/v1.0/powershell.exe"));

    QStringList args;
    args << QLatin1String("-NoProfile");
    args << QLatin1String("-ExecutionPolicy");
    args << QLatin1String("Bypass");
    args << QLatin1String("-WindowStyle");
    args << QLatin1String("Hidden");
    args << QLatin1String("-File");
    args << nativeScript;
    return QProcess::startDetached(powershell, args, QFileInfo(msiPath).absolutePath());
}

//----------------------------------------------

static void styleMessageBox(QMessageBox *box)
{
    if (!box)
        return;
    AppSettings *settings = AppSettings::instance();
    box->setStyleSheet(QString::fromUtf8(
                           "QMessageBox { background:%1; color:%2; }"
                           "QMessageBox QLabel { color:%2; background:transparent; }"
                           "QPushButton { background:%3; color:%2; border:1px solid %4;"
                           "  border-radius:4px; padding:5px 12px; }")
                           .arg(settings->panelColor().name(),
                                settings->textColor().name(),
                                settings->inputColor().name(),
                                settings->borderColor().name()));
}

//----------------------------------------------

UpdateChecker::UpdateChecker(QWidget *parent)
    : QObject(parent)
    , _nam(new QNetworkAccessManager(this))
    , _reply(0)
    , _timeout(new QTimer(this))
    , _dialogParent(parent)
    , _expectedSize(0)
    , _manual(false)
    , _busy(false)
    , _timedOut(false)
    , _progressOpen(false)
    , _phase(PhaseIdle)
{
    _timeout->setSingleShot(true);
    connect(_timeout, SIGNAL(timeout()), this, SLOT(sltTimeout()));
}

//----------------------------------------------

UpdateChecker::~UpdateChecker()
{
    _timeout->stop();
    if (_reply)
    {
        disconnect(_reply, SIGNAL(finished()), this, SLOT(sltFinished()));
        disconnect(_reply, SIGNAL(downloadProgress(qint64,qint64)),
                   this, SLOT(sltDownloadProgress(qint64,qint64)));
        _reply->abort();
        _reply->deleteLater();
        _reply = 0;
    }
}

//----------------------------------------------

bool UpdateChecker::isBusy() const
{
    return _busy;
}

//----------------------------------------------

void UpdateChecker::sltAutoCheck()
{
    check(false);
}

//----------------------------------------------

void UpdateChecker::check(bool manual)
{
    if (_busy)
        return;
    _busy = true;
    _manual = manual;
    _phase = PhaseCheck;
    _timedOut = false;
    _expectedSize = 0;
    _newVersion.clear();
    if (_manual)
        showStatus(tr("Проверка обновлений…"));
    startRequest(checkUrl());
}

//----------------------------------------------

QUrl UpdateChecker::checkUrl() const
{
    QUrl url(QLatin1String("https://www.e-face.org/softcraft/api/"));
    QUrlQuery query;
    query.addQueryItem(QLatin1String("operation"), QLatin1String("chkupd"));
    query.addQueryItem(QLatin1String("app"), QLatin1String("unique"));
    query.addQueryItem(QLatin1String("ver"), QString::fromUtf8(UNIQUE_APP_VERSION));
    query.addQueryItem(QLatin1String("os"), QLatin1String("windows"));

    QString arch;
    if (sizeof(void *) == 4)
        arch = QLatin1String("32");
    else if (sizeof(void *) == 8)
        arch = QLatin1String("64");
    else
        arch = QString::number(int(sizeof(void *) * 8));
    query.addQueryItem(QLatin1String("arch"), arch);
    url.setQuery(query);
    return url;
}

//----------------------------------------------

void UpdateChecker::startRequest(const QUrl &url)
{
    if (_reply)
    {
        disconnect(_reply, SIGNAL(finished()), this, SLOT(sltFinished()));
        disconnect(_reply, SIGNAL(downloadProgress(qint64,qint64)),
                   this, SLOT(sltDownloadProgress(qint64,qint64)));
        QNetworkReply *old = _reply;
        _reply = 0;
        old->abort();
        old->deleteLater();
    }

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute,
                         QVariant(int(QNetworkRequest::Manual)));
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute,
                         QVariant(int(QNetworkRequest::Manual)));

    _timedOut = false;
    _reply = _nam->get(request);
    connect(_reply, SIGNAL(finished()), this, SLOT(sltFinished()));
    connect(_reply, SIGNAL(downloadProgress(qint64,qint64)),
            this, SLOT(sltDownloadProgress(qint64,qint64)));
    _timeout->start(kRequestTimeoutMs);
}

//----------------------------------------------

void UpdateChecker::sltDownloadProgress(qint64 received, qint64 total)
{
    // The check is a hard 20 seconds. The download aborts if 20 seconds pass with no new bytes.
    if (_phase != PhaseDownload)
        return;
    if (received > 0 && _timeout)
        _timeout->start(kRequestTimeoutMs);
    updateDownloadProgress(received, total);
}

//----------------------------------------------

void UpdateChecker::sltTimeout()
{
    if (!_reply)
        return;
    _timedOut = true;
    _reply->abort();
}

//----------------------------------------------

void UpdateChecker::sltFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    if (reply != _reply)
    {
        reply->deleteLater();
        return;
    }

    _timeout->stop();
    _reply = 0;

    const bool timedOut = _timedOut;
    _timedOut = false;
    const QByteArray data = reply->readAll();
    const QNetworkReply::NetworkError netError = reply->error();
    const QString errorText = reply->errorString();
    const QByteArray contentType = reply->rawHeader("Content-Type");
    const QByteArray disposition = reply->rawHeader("Content-Disposition");
    const Phase phase = _phase;
    reply->deleteLater();

    if (timedOut)
    {
        failTimeout(phase);
        return;
    }
    if (netError != QNetworkReply::NoError)
    {
        failNetwork(errorText, phase);
        return;
    }
    if (phase == PhaseCheck)
        handleCheck(data);
    else if (phase == PhaseDownload)
        handleDownload(data, contentType, disposition);
    else
        setIdle();
}

//----------------------------------------------

void UpdateChecker::handleCheck(const QByteArray &data)
{
    const QStringList cells = splitCells(data);
    const QString status = cells.value(0);
    if (status == QLatin1String("info"))
    {
        QString message;
        if (cells.size() > 2)
            message = cells.mid(2).join(QLatin1Char(';')).trimmed();
        const QString canonical = QString::fromUtf8("Ваша версия программы самая новая!");
        if (message.isEmpty() || message == canonical)
            message = tr("Ваша версия программы самая новая!");
        if (_manual)
            showMessage(QMessageBox::Information, message);
        setIdle();
        return;
    }
    if (status == QLatin1String("unknown"))
    {
        if (_manual)
            showMessage(QMessageBox::Warning, unknownText(cells.value(2), false));
        setIdle();
        return;
    }
    if (status == QLatin1String("ok"))
    {
        offerInstall(cells);
        return;
    }
    if (_manual)
        showMessage(QMessageBox::Warning, tr("Проверка обновления не удалась."));
    setIdle();
}

//----------------------------------------------

void UpdateChecker::offerInstall(const QStringList &cells)
{
    bool sizeOk = false;
    const qint64 sizeBytes = cells.value(6).toLongLong(&sizeOk);
    const QString newVersion = cells.value(7).trimmed();
    const QString downloadUrl = (cells.size() > 8) ? cells.at(8).trimmed() : QString();
    if (cells.size() < 9 || !sizeOk || sizeBytes <= 0
        || newVersion.isEmpty() || downloadUrl.isEmpty())
    {
        if (_manual)
            showMessage(QMessageBox::Warning, tr("Проверка обновления не удалась."));
        setIdle();
        return;
    }

    if (_manual)
        showStatus(QString());
    const int answer = askYesNo(
        tr("Обновление"),
        tr("Найдена версия %1. Скачать и установить?").arg(newVersion));
    if (answer != QMessageBox::Yes)
    {
        setIdle();
        return;
    }

    _newVersion = newVersion;
    _expectedSize = sizeBytes;
    _phase = PhaseDownload;
    beginDownloadProgress();
    startRequest(QUrl(downloadUrl));
}

//----------------------------------------------

void UpdateChecker::handleDownload(const QByteArray &data, const QByteArray &contentType,
                                   const QByteArray &disposition)
{
    updateDownloadProgress(qint64(data.size()), _expectedSize);

    QByteArray head = data.left(64);
    if (head.startsWith("\xEF\xBB\xBF"))
        head.remove(0, 3);
    while (!head.isEmpty()
           && (head.at(0) == ' ' || head.at(0) == '\t'
               || head.at(0) == '\r' || head.at(0) == '\n'))
        head.remove(0, 1);
    if (head.startsWith("unknown;"))
    {
        const QStringList cells = splitCells(data);
        showMessage(QMessageBox::Warning, unknownText(cells.value(2), true));
        setIdle();
        return;
    }

    QString type = QString::fromLatin1(contentType).trimmed();
    const int semi = type.indexOf(QLatin1Char(';'));
    if (semi >= 0)
        type = type.left(semi).trimmed();
    if (type.compare(QLatin1String("application/installer"), Qt::CaseInsensitive) != 0)
    {
        showMessage(QMessageBox::Warning, tr("Сервер не отдал файл установщика."));
        setIdle();
        return;
    }

    if (qint64(data.size()) != _expectedSize)
    {
        showMessage(QMessageBox::Warning,
                    tr("Загруженный файл не совпал по размеру с ответом сервера."));
        setIdle();
        return;
    }

    const QString path = writeInstaller(data, installerFileName(disposition));
    if (path.isEmpty())
    {
        showMessage(QMessageBox::Warning, tr("Не удалось сохранить установщик."));
        setIdle();
        return;
    }
    if (QFileInfo(path).size() != _expectedSize)
    {
        QFile::remove(path);
        showMessage(QMessageBox::Warning,
                    tr("Загруженный файл не совпал по размеру с ответом сервера."));
        setIdle();
        return;
    }

    bool started = false;
    const QString suffix = QFileInfo(path).suffix();
    if (suffix.compare(QLatin1String("msi"), Qt::CaseInsensitive) == 0)
    {
        started = launchMsiThenApp(path);
    }
    else
    {
        started = QProcess::startDetached(
            path, QStringList(), QFileInfo(path).absolutePath());
    }
    if (!started)
    {
        showMessage(QMessageBox::Warning,
                    tr("Не удалось запустить установщик: %1")
                        .arg(QDir::toNativeSeparators(path)));
        setIdle();
        return;
    }

    QApplication::quit();
}

//----------------------------------------------

void UpdateChecker::failNetwork(const QString &errorText, Phase phase)
{
    const bool download = (phase == PhaseDownload);
    const bool manual = _manual;
    QString detail = errorText.trimmed();
    if (detail.isEmpty())
        detail = tr("Проверка обновления не удалась.");
    if (download || manual)
    {
        const QString text = download
                             ? tr("Не удалось скачать обновление: %1").arg(detail)
                             : tr("Не удалось проверить обновления: %1").arg(detail);
        showMessage(QMessageBox::Warning, text);
    }
    setIdle();
}

//----------------------------------------------

void UpdateChecker::failTimeout(Phase phase)
{
    const bool download = (phase == PhaseDownload);
    const bool manual = _manual;
    if (download || manual)
    {
        const QString text = download
                             ? tr("Не удалось скачать обновление: превышено время ожидания.")
                             : tr("Не удалось проверить обновления: превышено время ожидания.");
        showMessage(QMessageBox::Warning, text);
    }
    setIdle();
}

//----------------------------------------------

QStringList UpdateChecker::splitCells(const QByteArray &data) const
{
    QByteArray raw = data;
    if (raw.startsWith("\xEF\xBB\xBF"))
        raw.remove(0, 3);
    const QString text = QString::fromUtf8(raw).trimmed();
    QStringList cells = text.split(QLatin1Char(';'), QString::KeepEmptyParts);
    for (int i = 0; i < cells.size(); ++i)
        cells[i] = cells[i].trimmed();
    return cells;
}

//----------------------------------------------

QString UpdateChecker::unknownText(const QString &reason, bool download) const
{
    if (reason == QLatin1String("emptysource"))
        return tr("На сайте ещё нет файла установщика.");
    if (reason == QLatin1String("argserror"))
        return tr("Сервер не принял параметры запроса.");
    if (reason == QLatin1String("appname"))
        return tr("Неизвестное имя программы.");
    if (reason == QLatin1String("os"))
        return tr("Неизвестная система.");
    if (reason == QLatin1String("arch"))
        return tr("Нет сборки для этой разрядности.");
    if (reason == QLatin1String("version"))
        return tr("Сервер не принял номер версии.");
    if (reason.isEmpty())
    {
        if (download)
            return tr("Сервер не отдал файл установщика.");
        return tr("Проверка обновления не удалась.");
    }
    if (download)
        return tr("Не удалось скачать обновление (%1).").arg(reason);
    return tr("Проверка обновления не удалась (%1).").arg(reason);
}

//----------------------------------------------

QString UpdateChecker::installerFileName(const QByteArray &disposition) const
{
    const QString header = QString::fromUtf8(disposition);
    const QLatin1String key("filename=");
    const int at = header.indexOf(key, 0, Qt::CaseInsensitive);
    if (at < 0)
        return QString();
    QString name = header.mid(at + key.size()).trimmed();
    if (name.startsWith(QLatin1Char('"')))
    {
        name.remove(0, 1);
        const int end = name.indexOf(QLatin1Char('"'));
        if (end >= 0)
            name = name.left(end);
    }
    else
    {
        const int end = name.indexOf(QLatin1Char(';'));
        if (end >= 0)
            name = name.left(end);
        name = name.trimmed();
    }
    name = QFileInfo(name).fileName();
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return QString();
    if (name.contains(QLatin1Char(':')) || name.contains(QLatin1Char('/'))
        || name.contains(QLatin1Char('\\')))
        return QString();
    return name;
}

//----------------------------------------------

QString UpdateChecker::writeInstaller(const QByteArray &data, const QString &fileName) const
{
    QString name = QFileInfo(fileName).fileName();
    if (name == QLatin1String(".") || name == QLatin1String("..")
        || name.contains(QLatin1Char(':')) || name.contains(QLatin1Char('/'))
        || name.contains(QLatin1Char('\\')))
        name.clear();
    if (name.isEmpty())
    {
        name = QFileInfo(QString::fromUtf8("unique-%1-setup.exe").arg(_newVersion)).fileName();
        if (name.isEmpty())
            name = QString::fromUtf8("unique-setup.exe");
    }

    const QString dirPath = QDir::tempPath();
    QString path = QDir(dirPath).filePath(name);
    if (QFile::exists(path) && !QFile::remove(path))
    {
        const QString base = QFileInfo(name).completeBaseName();
        const QString suffix = QFileInfo(name).suffix();
        QString unique = base + QLatin1Char('-')
                         + QString::number(QDateTime::currentMSecsSinceEpoch());
        if (!suffix.isEmpty())
            unique += QLatin1Char('.') + suffix;
        path = QDir(dirPath).filePath(unique);
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QString();
    const qint64 written = file.write(data);
    const bool flushed = file.flush();
    file.close();
    if (!flushed || written != qint64(data.size())
        || QFileInfo(path).size() != qint64(data.size()))
    {
        QFile::remove(path);
        return QString();
    }
    return path;
}

//----------------------------------------------

void UpdateChecker::showStatus(const QString &text)
{
    QMainWindow *window = qobject_cast<QMainWindow *>(_dialogParent);
    if (!window || !window->statusBar())
        return;
    if (text.isEmpty())
        window->statusBar()->clearMessage();
    else
        window->statusBar()->showMessage(text);
}

//----------------------------------------------

void UpdateChecker::beginDownloadProgress()
{
    if (_progressOpen)
        return;
    MainWindow *window = qobject_cast<MainWindow *>(_dialogParent);
    if (!window)
        return;
    _progressOpen = true;
    const int total = _expectedSize > 0 ? 100 : 0;
    window->beginStatusProgress(tr("Загрузка обновления…"), total);
}

//----------------------------------------------

void UpdateChecker::updateDownloadProgress(qint64 received, qint64 total)
{
    if (!_progressOpen)
        beginDownloadProgress();
    MainWindow *window = qobject_cast<MainWindow *>(_dialogParent);
    if (!window)
        return;

    qint64 known = total > 0 ? total : _expectedSize;
    int percent = 0;
    if (known > 0)
    {
        if (received >= known)
            percent = 100;
        else if (received > 0)
            percent = int((double(received) * 100.0) / double(known));
        if (percent < 0)
            percent = 0;
        if (percent > 100)
            percent = 100;
        window->updateStatusProgress(percent, 100, QString(), false);
        return;
    }
    window->updateStatusProgress(0, 0);
}

//----------------------------------------------

void UpdateChecker::endDownloadProgress()
{
    if (!_progressOpen)
        return;
    _progressOpen = false;
    MainWindow *window = qobject_cast<MainWindow *>(_dialogParent);
    if (!window)
        return;
    window->endStatusProgress();
    if (!window->statusBar())
        return;
    QProgressBar *bar = window->statusBar()->findChild<QProgressBar *>();
    if (!bar || !bar->isVisible())
        window->statusBar()->clearMessage();
}

//----------------------------------------------

void UpdateChecker::showMessage(QMessageBox::Icon icon, const QString &text)
{
    if (_progressOpen)
        endDownloadProgress();
    else if (_manual || _phase == PhaseDownload)
        showStatus(QString());
    QMessageBox box(_dialogParent);
    box.setIcon(icon);
    box.setWindowTitle(tr("Обновление"));
    box.setTextFormat(Qt::PlainText);
    box.setText(text);
    box.setStandardButtons(QMessageBox::Ok);
    box.setButtonText(QMessageBox::Ok, tr("ОК"));
    styleMessageBox(&box);
    box.exec();
}

//----------------------------------------------

int UpdateChecker::askYesNo(const QString &title, const QString &text)
{
    QMessageBox box(_dialogParent);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(title);
    box.setTextFormat(Qt::PlainText);
    box.setText(text);
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    box.setButtonText(QMessageBox::Yes, tr("Да"));
    box.setButtonText(QMessageBox::No, tr("Нет"));
    styleMessageBox(&box);
    return box.exec();
}

//----------------------------------------------

void UpdateChecker::setIdle()
{
    endDownloadProgress();
    _phase = PhaseIdle;
    _busy = false;
    _manual = false;
    _timedOut = false;
    _expectedSize = 0;
    _newVersion.clear();
    if (_timeout)
        _timeout->stop();
}
