#include "updatechecker.h"
//----------------------------------------------
#include "appsettings.h"
#include "appversion.h"
#include "mainwindow.h"
//----------------------------------------------
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSysInfo>
#include <QProgressBar>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>
#include <QVBoxLayout>

//----------------------------------------------

static const int kRequestTimeoutMs = 20000;

//----------------------------------------------

static QString operatingSystemName()
{
    return QSysInfo::productType().trimmed().toLower();
}

//----------------------------------------------

static QString readUpdatePageUrl();

//----------------------------------------------

#if defined(Q_OS_LINUX)
static QString packagedOsToken()
{
    const QUrl page(readUpdatePageUrl());
    return QUrlQuery(page).queryItemValue(QLatin1String("os")).trimmed().toLower();
}
#endif

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

static QString shellQuote(QString value)
{
    value.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + value + QLatin1Char('\'');
}

//----------------------------------------------

static bool writeExecutable(const QString &path, const QString &body)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray bytes = body.toUtf8();
    if (file.write(bytes) != qint64(bytes.size()) || !file.flush())
    {
        file.close();
        QFile::remove(path);
        return false;
    }
    file.close();
    return file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                               | QFile::ReadGroup | QFile::ExeGroup
                               | QFile::ReadOther | QFile::ExeOther);
}

//----------------------------------------------

static bool writeOwnerFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (file.write(bytes) != qint64(bytes.size()) || !file.flush())
    {
        file.close();
        QFile::remove(path);
        return false;
    }
    file.close();
    return file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
}

//----------------------------------------------

static QString publishedPackagePath(const QString &packagePath)
{
    const QFile::Permissions openPerms =
        QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther;
    QFile::setPermissions(packagePath, openPerms);
    const QString published = QString::fromLatin1("/var/tmp/")
                              + QFileInfo(packagePath).fileName();
    if (QFileInfo(published).absoluteFilePath()
        == QFileInfo(packagePath).absoluteFilePath())
        return published;
    QFile::remove(published);
    if (!QFile::copy(packagePath, published))
        return QFileInfo(packagePath).absoluteFilePath();
    QFile::setPermissions(published, openPerms);
    return QFileInfo(published).absoluteFilePath();
}

//----------------------------------------------

static QString debInstallBody(const QString &debPath)
{
    const QString quoted = shellQuote(debPath);
    QString body;
    body += QLatin1String("export DEBIAN_FRONTEND=noninteractive\n");
    body += QLatin1String("if ! dpkg -i ") + quoted + QLatin1String("\n");
    body += QLatin1String("then\n");
    body += QLatin1String("    apt-get install -f -y || exit 1\n");
    body += QLatin1String("    dpkg -i ") + quoted + QLatin1String(" || exit 1\n");
    body += QLatin1String("fi\n");
    return body;
}

//----------------------------------------------

static QString rpmInstallBody(const QString &rpmPath)
{
    const QString quoted = shellQuote(rpmPath);
    QString body;
    body += QLatin1String("if [ -x /usr/bin/dnf ]; then\n");
    body += QLatin1String("    dnf install -y ") + quoted + QLatin1String(" || exit 1\n");
    body += QLatin1String("elif [ -x /usr/bin/yum ]; then\n");
    body += QLatin1String("    yum install -y ") + quoted + QLatin1String(" || exit 1\n");
    body += QLatin1String("else\n");
    body += QLatin1String("    rpm -Uvh ") + quoted + QLatin1String(" || exit 1\n");
    body += QLatin1String("fi\n");
    return body;
}

//----------------------------------------------

bool UpdateChecker::installLinuxPackage(const QString &packagePath,
                                        const QString &rootInstall)
{
    // Ask for install rights while this window is still open. Polkit cannot
    // find a session after the program has already exited, and the old copy
    // would just be started again.
    endDownloadProgress();
    const QString rootPath = QDir(QDir::tempPath()).filePath(
        QLatin1String("unique-update-root.sh"));
    const QString userPath = QDir(QDir::tempPath()).filePath(
        QLatin1String("unique-update.sh"));
    const QString authPath = QDir(QDir::tempPath()).filePath(
        QLatin1String("unique-update.authorized"));
    const QString statusPath = QDir(QDir::tempPath()).filePath(
        QLatin1String("unique-update.status"));
    const QString passPath = QDir(QDir::tempPath()).filePath(
        QLatin1String("unique-update.pass"));
    const QString askPath = QDir(QDir::tempPath()).filePath(
        QLatin1String("unique-update-askpass.sh"));
    const QString logPath = QDir(QDir::tempPath()).filePath(
        QLatin1String("unique-update.err"));
    QFile::remove(authPath);
    QFile::remove(statusPath);
    QFile::remove(logPath);
    QFile::remove(passPath);
    QFile::remove(askPath);

    const bool havePkexec = QFile::exists(QLatin1String("/usr/bin/pkexec"));
    const bool haveSudo = QFile::exists(QLatin1String("/usr/bin/sudo"));
    const QString failText = tr("Не удалось установить обновление.");
    if (!havePkexec && !haveSudo)
    {
        showMessage(QMessageBox::Warning, failText);
        return false;
    }

    // A detached installer has no terminal. sudo -n then answers
    // "a password is required" even when this process could use sudo.
    // Ask here and hand the password to sudo -A. The helper only prints
    // the file; the password is not placed on the command line.
    bool usePassword = false;
    QString password;
    if (!havePkexec && haveSudo)
    {
        if (!askInstallPassword(&password))
        {
            showMessage(QMessageBox::Warning, tr("Обновление отменено."));
            return false;
        }
        if (!writeOwnerFile(passPath, password.toUtf8() + '\n'))
        {
            showMessage(QMessageBox::Warning, failText);
            return false;
        }
        password.fill(QLatin1Char('x'));
        password.clear();
        usePassword = true;
    }
    const qint64 pid = QCoreApplication::applicationPid();

    QString root;
    root += QLatin1String("#!/bin/sh\n");
    root += QLatin1String("exec </dev/null\n");
    root += QLatin1String("/bin/rm -f ") + shellQuote(passPath) + QLatin1String("\n");
    root += QLatin1String("touch ") + shellQuote(authPath) + QLatin1String("\n");
    root += QLatin1String("pid=") + QString::number(pid) + QLatin1String("\n");
    root += QLatin1String("deadline=$(($(date +%s) + 60))\n");
    root += QLatin1String("while kill -0 \"$pid\" 2>/dev/null; do\n");
    root += QLatin1String("    if [ \"$(date +%s)\" -ge \"$deadline\" ]; then\n");
    root += QLatin1String("        exit 1\n");
    root += QLatin1String("    fi\n");
    root += QLatin1String("    sleep 1\n");
    root += QLatin1String("done\n");
    root += QLatin1String("sleep 1\n");
    root += rootInstall;
    root += QLatin1String("exit 0\n");

    const QString quotedRoot = shellQuote(rootPath);
    const QString quotedLog = shellQuote(logPath);
    const QString quotedPass = shellQuote(passPath);
    const QString quotedAsk = shellQuote(askPath);
    QString ask;
    if (usePassword)
    {
        ask += QLatin1String("#!/bin/sh\n");
        ask += QLatin1String("/bin/cat ") + quotedPass
                + QLatin1String(" 2>/dev/null || exit 1\n");
    }
    QString user;
    user += QLatin1String("#!/bin/sh\n");
    user += QLatin1String("if [ -x /usr/bin/pkexec ]; then\n");
    user += QLatin1String("    pkexec /bin/sh ") + quotedRoot
            + QLatin1String(" >> ") + quotedLog + QLatin1String(" 2>&1\n");
    user += QLatin1String("    rc=$?\n");
    user += QLatin1String("elif [ -x /usr/bin/sudo ]; then\n");
    user += QLatin1String("    if [ -f ") + quotedPass + QLatin1String(" ]; then\n");
    user += QLatin1String("        SUDO_ASKPASS=") + quotedAsk
            + QLatin1String(" sudo -A -k -- /bin/sh ") + quotedRoot
            + QLatin1String(" >> ") + quotedLog + QLatin1String(" 2>&1\n");
    user += QLatin1String("        rc=$?\n");
    user += QLatin1String("        /bin/rm -f ") + quotedPass + QLatin1String("\n");
    user += QLatin1String("    else\n");
    user += QLatin1String("        echo \"sudo: a password is required\" >> ")
            + quotedLog + QLatin1String("\n");
    user += QLatin1String("        rc=1\n");
    user += QLatin1String("    fi\n");
    user += QLatin1String("else\n");
    user += QLatin1String("    echo \"pkexec: not found\" >> ") + quotedLog + QLatin1String("\n");
    user += QLatin1String("    echo \"sudo: not found\" >> ") + quotedLog + QLatin1String("\n");
    user += QLatin1String("    rc=127\n");
    user += QLatin1String("fi\n");
    user += QLatin1String("echo \"$rc\" > ") + shellQuote(statusPath) + QLatin1String("\n");
    user += QLatin1String("if [ \"$rc\" -eq 0 ]; then\n");
    user += QLatin1String("    if [ -x /opt/unique/Unique ]; then\n");
    user += QLatin1String("        setsid /opt/unique/Unique >/dev/null 2>&1 &\n");
    user += QLatin1String("    fi\n");
    user += QLatin1String("elif [ -f ") + shellQuote(authPath) + QLatin1String(" ]; then\n");
    user += QLatin1String("    if [ -x /usr/bin/zenity ]; then\n");
    user += QLatin1String("        zenity --error --text=") + shellQuote(failText) + QLatin1String("\n");
    user += QLatin1String("    elif [ -x /usr/bin/kdialog ]; then\n");
    user += QLatin1String("        kdialog --error ") + shellQuote(failText) + QLatin1String("\n");
    user += QLatin1String("    fi\n");
    user += QLatin1String("    if [ -x /opt/unique/Unique ]; then\n");
    user += QLatin1String("        setsid /opt/unique/Unique >/dev/null 2>&1 &\n");
    user += QLatin1String("    fi\n");
    user += QLatin1String("fi\n");
    user += QLatin1String("rm -f ") + shellQuote(userPath) + QLatin1String(" ")
            + shellQuote(rootPath) + QLatin1String(" ")
            + shellQuote(authPath) + QLatin1String(" ")
            + shellQuote(passPath) + QLatin1String(" ")
            + shellQuote(askPath) + QLatin1String("\n");

    const QString shownPath = QDir::toNativeSeparators(packagePath);
    if (!writeExecutable(rootPath, root) || !writeExecutable(userPath, user)
        || (usePassword && !writeExecutable(askPath, ask)))
    {
        QFile::remove(passPath);
        QFile::remove(askPath);
        showMessage(QMessageBox::Warning,
                    tr("Не удалось запустить установщик: %1").arg(shownPath));
        return false;
    }
    if (!QProcess::startDetached(QString::fromLatin1("/bin/sh"),
                                 QStringList() << userPath,
                                 QFileInfo(packagePath).absolutePath()))
    {
        QFile::remove(passPath);
        QFile::remove(askPath);
        showMessage(QMessageBox::Warning,
                    tr("Не удалось запустить установщик: %1").arg(shownPath));
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 300000)
    {
        if (QFile::exists(authPath))
            return true;
        if (QFile::exists(statusPath))
        {
            QString detail;
            QFile err(logPath);
            if (err.open(QIODevice::ReadOnly))
                detail = QString::fromUtf8(err.readAll()).trimmed();
            if (detail.size() > 400)
                detail = detail.right(400).trimmed();
            if (detail.isEmpty())
                showMessage(QMessageBox::Warning, tr("Обновление отменено."));
            else
                showMessage(QMessageBox::Warning,
                            tr("Не удалось установить обновление.")
                            + QLatin1Char('\n') + detail);
            return false;
        }
        QApplication::processEvents(QEventLoop::AllEvents, 100);
    }
    showMessage(QMessageBox::Warning, failText);
    return false;
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

static QString readUpdatePageUrl()
{
    const QString path = QCoreApplication::applicationDirPath()
                         + QString::fromLatin1("/config/update.ini");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    while (!file.atEnd())
    {
        QString line = QString::fromUtf8(file.readLine());
        if (line.endsWith(QLatin1Char('\n')))
            line.chop(1);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()
            || trimmed.startsWith(QLatin1Char(';'))
            || trimmed.startsWith(QLatin1Char('#'))
            || trimmed.startsWith(QLatin1Char('[')))
        {
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        if (line.left(eq).trimmed() != QLatin1String("url"))
            continue;
        return line.mid(eq + 1).trimmed();
    }
    return QString();
}

//----------------------------------------------

QUrl UpdateChecker::checkUrl() const
{
    QString page = readUpdatePageUrl();
    if (page.isEmpty())
        page = QString::fromLatin1("https://www.e-face.org/softcraft/api/");
    QUrl url(page);
    QUrlQuery query(url);
    if (!query.hasQueryItem(QLatin1String("operation")))
        query.addQueryItem(QLatin1String("operation"), QLatin1String("chkupd"));
    if (!query.hasQueryItem(QLatin1String("app")))
        query.addQueryItem(QLatin1String("app"), QLatin1String("unique"));
    if (!query.hasQueryItem(QLatin1String("ver")))
        query.addQueryItem(QLatin1String("ver"), QString::fromUtf8(UNIQUE_APP_VERSION));
    if (!query.hasQueryItem(QLatin1String("os")))
        query.addQueryItem(QLatin1String("os"), operatingSystemName());
    if (!query.hasQueryItem(QLatin1String("arch")))
    {
        QString arch;
        if (sizeof(void *) == 4)
            arch = QLatin1String("32");
        else if (sizeof(void *) == 8)
            arch = QLatin1String("64");
        else
            arch = QString::number(int(sizeof(void *) * 8));
        query.addQueryItem(QLatin1String("arch"), arch);
    }
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
    else if (suffix.compare(QLatin1String("deb"), Qt::CaseInsensitive) == 0)
    {
        if (!installLinuxPackage(path, debInstallBody(publishedPackagePath(path))))
        {
            setIdle();
            return;
        }
        started = true;
    }
    else if (suffix.compare(QLatin1String("rpm"), Qt::CaseInsensitive) == 0)
    {
        if (!installLinuxPackage(path, rpmInstallBody(publishedPackagePath(path))))
        {
            setIdle();
            return;
        }
        started = true;
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
#if defined(Q_OS_LINUX)
        const QString ext = (packagedOsToken() == QLatin1String("redhat"))
                            ? QString::fromLatin1("rpm")
                            : QString::fromLatin1("deb");
        name = QFileInfo(QString::fromUtf8("unique-%1.%2").arg(_newVersion, ext)).fileName();
        if (name.isEmpty())
            name = QString::fromUtf8("unique-setup.") + ext;
#else
        name = QFileInfo(QString::fromUtf8("unique-%1-setup.exe").arg(_newVersion)).fileName();
        if (name.isEmpty())
            name = QString::fromUtf8("unique-setup.exe");
#endif
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

bool UpdateChecker::askInstallPassword(QString *password)
{
    if (!password)
        return false;
    QDialog dialog(_dialogParent);
    dialog.setWindowTitle(tr("Обновление"));
    dialog.setModal(true);
    QLabel *label = new QLabel(tr("Введите пароль для установки обновления."), &dialog);
    label->setWordWrap(true);
    QLineEdit *edit = new QLineEdit(&dialog);
    edit->setEchoMode(QLineEdit::Password);
    QPushButton *ok = new QPushButton(tr("ОК"), &dialog);
    QPushButton *cancel = new QPushButton(tr("Отмена"), &dialog);
    ok->setDefault(true);
    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->addWidget(label);
    layout->addWidget(edit);
    layout->addLayout(buttons);
    connect(ok, SIGNAL(clicked()), &dialog, SLOT(accept()));
    connect(cancel, SIGNAL(clicked()), &dialog, SLOT(reject()));
    connect(edit, SIGNAL(returnPressed()), &dialog, SLOT(accept()));
    AppSettings *settings = AppSettings::instance();
    dialog.setStyleSheet(QString::fromUtf8(
                             "QDialog { background:%1; color:%2; }"
                             "QLabel { color:%2; background:transparent; }"
                             "QLineEdit { background:%3; color:%2; border:1px solid %4; padding:4px; }"
                             "QPushButton { background:%3; color:%2; border:1px solid %4;"
                             "  border-radius:4px; padding:5px 12px; }")
                             .arg(settings->panelColor().name(),
                                  settings->textColor().name(),
                                  settings->inputColor().name(),
                                  settings->borderColor().name()));
    if (dialog.exec() != QDialog::Accepted)
        return false;
    *password = edit->text();
    return true;
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
