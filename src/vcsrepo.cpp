#include "vcsrepo.h"
//----------------------------------------------
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QTimer>

//----------------------------------------------

bool VcsRepo::isRepository(const QString &root)
{
    if (root.trimmed().isEmpty())
        return false;
    const QFileInfo info(QDir(root).filePath(QString::fromUtf8(".git")));
    return info.exists() && (info.isDir() || info.isFile());
}

//----------------------------------------------

static QString gitDirectory(const QString &root)
{
    const QFileInfo info(QDir(root).filePath(QString::fromUtf8(".git")));
    if (info.isDir())
        return info.absoluteFilePath();
    if (!info.isFile())
        return QString();
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const QString text = QString::fromUtf8(file.readAll()).trimmed();
    const QString prefix = QString::fromUtf8("gitdir:");
    if (!text.startsWith(prefix))
        return QString();
    QString path = text.mid(prefix.size()).trimmed();
    if (QDir::isRelativePath(path))
        path = QDir(root).absoluteFilePath(path);
    return QDir::cleanPath(path);
}

//----------------------------------------------

QString VcsRepo::currentBranch(const QString &root)
{
    const QString dir = gitDirectory(root);
    if (dir.isEmpty())
        return QString();
    QFile head(QDir(dir).filePath(QString::fromUtf8("HEAD")));
    if (!head.open(QIODevice::ReadOnly))
        return QString();
    const QString text = QString::fromUtf8(head.readAll()).trimmed();
    const QString prefix = QString::fromUtf8("ref: refs/heads/");
    if (text.startsWith(prefix))
        return text.mid(prefix.size()).trimmed();
    if (text.size() >= 7)
        return text.left(7);
    return text;
}

//----------------------------------------------

static QString realGitExecutable(const QString &path)
{
    if (path.isEmpty())
        return path;
    const QFileInfo info(path);
    const QString dir = QDir::fromNativeSeparators(info.absolutePath());
    if (dir.endsWith(QString::fromUtf8("/cmd"), Qt::CaseInsensitive))
    {
        const QString bin = QDir::cleanPath(dir + QString::fromUtf8("/../bin/git.exe"));
        if (QFileInfo(bin).isFile())
            return QDir::toNativeSeparators(bin);
    }
    return info.absoluteFilePath();
}

//----------------------------------------------

QString VcsRepo::gitProgram()
{
    const QStringList preferred = QStringList()
        << QString::fromUtf8("C:/Program Files/Git/bin/git.exe")
        << QString::fromUtf8("C:/Program Files (x86)/Git/bin/git.exe");
    foreach (const QString &path, preferred)
    {
        if (QFileInfo(path).isFile())
            return QDir::toNativeSeparators(path);
    }
    return realGitExecutable(QStandardPaths::findExecutable(QString::fromUtf8("git")));
}

//----------------------------------------------

static QString cleanGitOutput(const QString &raw)
{
    QString text = raw;
    text.replace(QString::fromUtf8("\r\n"), QString::fromUtf8("\n"));
    QStringList lines;
    foreach (QString line, text.split(QLatin1Char('\n')))
    {
        const int cr = line.lastIndexOf(QLatin1Char('\r'));
        if (cr >= 0)
            line = line.mid(cr + 1);
        line = line.trimmed();
        if (!line.isEmpty())
            lines << line;
    }
    return lines.join(QLatin1Char('\n'));
}

//----------------------------------------------

bool VcsRepo::run(QWidget *parent, const QString &root, const QStringList &args,
                  const QByteArray &input, QString *text, bool *canceled)
{
    if (canceled)
        *canceled = false;
    const QString program = gitProgram();
    if (program.isEmpty())
    {
        if (text)
            *text = QObject::tr("Git не найден. Установите Git и перезапустите программу.");
        return false;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QString::fromUtf8("GIT_TERMINAL_PROMPT"), QString::fromUtf8("0"));
    env.insert(QString::fromUtf8("GCM_INTERACTIVE"), QString::fromUtf8("Never"));

    QProcess proc;
    proc.setWorkingDirectory(root);
    proc.setProcessEnvironment(env);
    proc.setProgram(program);
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(8000))
    {
        if (text)
            *text = QObject::tr("Не удалось запустить Git.");
        return false;
    }
    if (!input.isEmpty())
        proc.write(input);
    proc.closeWriteChannel();

    QProgressDialog progress(QObject::tr("Выполняется Git…"),
                             QObject::tr("Отмена"), 0, 0, parent);
    progress.setWindowTitle(QObject::tr("Контроль версий"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.hide();

    QEventLoop loop;
    QTimer tick;
    tick.setInterval(100);
    QObject::connect(&tick, SIGNAL(timeout()), &loop, SLOT(quit()));
    QObject::connect(&proc, SIGNAL(finished(int,QProcess::ExitStatus)),
                     &loop, SLOT(quit()));
    tick.start();

    QElapsedTimer timer;
    timer.start();
    bool shown = false;
    QByteArray outBytes;
    QByteArray errBytes;
    while (proc.state() != QProcess::NotRunning)
    {
        loop.exec();
        outBytes += proc.readAllStandardOutput();
        errBytes += proc.readAllStandardError();
        if (proc.state() == QProcess::NotRunning)
            break;
        if (!shown && timer.elapsed() >= 400)
        {
            progress.show();
            shown = true;
        }
        if (shown)
        {
            const QString partial = QString::fromUtf8(errBytes.isEmpty() ? outBytes : errBytes);
            const int nl = partial.lastIndexOf(QLatin1Char('\n'));
            const QString line = (nl >= 0 ? partial.mid(nl + 1) : partial).trimmed();
            if (!line.isEmpty())
                progress.setLabelText(line);
        }
        if (shown && progress.wasCanceled())
        {
            proc.kill();
            proc.waitForFinished(2000);
            if (canceled)
                *canceled = true;
            if (text)
                *text = QObject::tr("Отменено.");
            return false;
        }
        if (timer.elapsed() >= 120000)
        {
            proc.kill();
            proc.waitForFinished(2000);
            QString message = QObject::tr("Команда Git не завершилась вовремя.");
            const QString partial = QString::fromUtf8(errBytes.isEmpty() ? outBytes : errBytes).trimmed();
            if (!partial.isEmpty())
                message += QString::fromUtf8("\n") + partial.right(1500);
            if (text)
                *text = message;
            return false;
        }
    }

    outBytes += proc.readAllStandardOutput();
    errBytes += proc.readAllStandardError();
    const QString out = QString::fromUtf8(outBytes).trimmed();
    const QString err = QString::fromUtf8(errBytes).trimmed();
    const bool ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    if (text)
    {
        QString combined = err;
        if (!out.isEmpty())
        {
            if (!combined.isEmpty())
                combined += QLatin1Char('\n');
            combined += out;
        }
        combined = cleanGitOutput(combined);
        if (combined.isEmpty() && !ok)
            combined = QObject::tr("Команда Git завершилась с ошибкой.");
        *text = combined;
    }
    return ok;
}

//----------------------------------------------

QStringList VcsRepo::localBranches(QWidget *parent, const QString &root,
                                   QString *text, bool *canceled)
{
    QStringList args;
    args << QString::fromUtf8("for-each-ref");
    args << QString::fromUtf8("--format=%(refname:short)");
    args << QString::fromUtf8("refs/heads");
    QString body;
    if (!run(parent, root, args, QByteArray(), &body, canceled))
    {
        if (text)
            *text = body;
        return QStringList();
    }
    if (text)
        text->clear();
    QStringList names;
    foreach (const QString &line, body.split(QLatin1Char('\n')))
    {
        const QString name = line.trimmed();
        if (!name.isEmpty() && !names.contains(name))
            names << name;
    }
    return names;
}
