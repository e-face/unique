#include "projectworkspace.h"
//----------------------------------------------
#include "projectbrief.h"
//----------------------------------------------
#include <QDir>
#include <QFileInfo>
#include <QTextCodec>
#include <QtGlobal>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

//----------------------------------------------

const int ProjectWorkspace::maxReadBytes      = 4 * 1024 * 1024;
const int ProjectWorkspace::maxReadChunkChars = 32 * 1024;
const int ProjectWorkspace::maxWriteBytes     = 2 * 1024 * 1024;
const int ProjectWorkspace::maxListEntries = 1000;

//----------------------------------------------

/** Строго проверить, что байты — валидный UTF-8; при успехе вернуть текст. */
static bool tryDecodeUtf8(const QByteArray &bytes, QString *out)
{
    QTextCodec *utf8 = QTextCodec::codecForName("UTF-8");
    if (!utf8)
        return false;
    QTextCodec::ConverterState state;
    const QString text = utf8->toUnicode(bytes.constData(), bytes.size(), &state);
    if (state.invalidChars > 0)
        return false;
    if (out)
        *out = text;
    return true;
}

//----------------------------------------------

QString ProjectWorkspace::decodeText(const QByteArray &bytes)
{
    QString text;
    if (tryDecodeUtf8(bytes, &text))
        return text;
    return QString::fromLocal8Bit(bytes);
}

//----------------------------------------------

QString ProjectWorkspace::decodeConsoleOutput(const QByteArray &bytes)
{
    QString text;
    if (tryDecodeUtf8(bytes, &text))
        return text;
#ifdef Q_OS_WIN
    // cmd.exe и системные утилиты пишут в OEM-странице (866 для русской
    // Windows), а fromLocal8Bit ждёт ANSI (1251) — отсюда кракозябры.
    static QTextCodec *oem = 0;
    static bool oemResolved = false;
    if (!oemResolved)
    {
        oemResolved = true;
        const UINT page = GetOEMCP();
        oem = QTextCodec::codecForName(QByteArray("IBM ") + QByteArray::number(int(page)));
        if (!oem)
            oem = QTextCodec::codecForName(QByteArray("IBM") + QByteArray::number(int(page)));
        if (!oem)
            oem = QTextCodec::codecForName(QByteArray("CP") + QByteArray::number(int(page)));
        if (!oem && page == 866)
            oem = QTextCodec::codecForName("IBM 866");
    }
    if (oem)
        return oem->toUnicode(bytes);
#endif
    return QString::fromLocal8Bit(bytes);
}

//----------------------------------------------

ProjectWorkspace::ProjectWorkspace(QObject *parent)
    : QObject(parent)
{
}

//----------------------------------------------

void ProjectWorkspace::setRoot(QString path)
{
    _root = normalizeDir(path.trimmed());
    ProjectBrief::instance()->setRoot(_root);
    if (!_root.isEmpty())
        ensureTempDir();
}

//----------------------------------------------

QString ProjectWorkspace::root() const
{
    return _root;
}

//----------------------------------------------

bool ProjectWorkspace::isSet() const
{
    return !_root.isEmpty();
}

//----------------------------------------------

bool ProjectWorkspace::resolvePath(QString userPath, bool mustExist, bool forWrite,
                                   QString *absolutePath, QString *error,
                                   bool allowOutside) const
{
    if (_root.isEmpty() && !allowOutside)
    {
        if (error)
            *error = QString::fromUtf8("Project directory is not selected.");
        return false;
    }

    QString raw = userPath.trimmed();
    raw.replace(QChar('\\'), QChar('/'));
    if (raw.isEmpty() || raw == QString::fromUtf8(".") || raw == QString::fromUtf8(".."))
    {
        if (error)
            *error = QString::fromUtf8("Specify a file path.");
        return false;
    }
    if (raw.contains(QChar(0)))
    {
        if (error)
            *error = QString::fromUtf8("Invalid path.");
        return false;
    }

    QString candidate;
    if (QDir::isAbsolutePath(raw))
        candidate = QDir::cleanPath(raw);
    else if (!_root.isEmpty())
        candidate = QDir::cleanPath(_root + QChar('/') + raw);
    else
        candidate = QDir::cleanPath(raw);

    const bool inside = !_root.isEmpty() && isInsideRoot(candidate);
    if (!inside && !allowOutside)
    {
        if (error)
            *error = QString::fromUtf8("Path outside the project directory is not allowed.");
        return false;
    }

    QFileInfo info(candidate);
    if (info.exists())
    {
        const QString canon = QDir::cleanPath(info.canonicalFilePath());
        if (canon.isEmpty())
        {
            if (error)
                *error = QString::fromUtf8("Invalid path.");
            return false;
        }
        if (!allowOutside && !isInsideRoot(canon))
        {
            if (error)
                *error = QString::fromUtf8("Path outside the project directory is not allowed.");
            return false;
        }
        if (info.isDir())
        {
            if (error)
                *error = QString::fromUtf8("A directory was given; a file is expected.");
            return false;
        }
        if (absolutePath)
            *absolutePath = canon;
        return true;
    }

    if (mustExist)
    {
        if (error)
            *error = QString::fromUtf8("File not found: %1").arg(raw);
        return false;
    }
    if (!forWrite)
    {
        if (error)
            *error = QString::fromUtf8("File not found: %1").arg(raw);
        return false;
    }

    QString parent = QFileInfo(candidate).absolutePath();
    parent = QDir::cleanPath(parent);
    if (!allowOutside && !isInsideRoot(parent))
    {
        if (error)
            *error = QString::fromUtf8("Path outside the project directory is not allowed.");
        return false;
    }

    if (!allowOutside)
    {
        QFileInfo parentInfo(parent);
        while (!parentInfo.exists())
        {
            const QString up = QDir::cleanPath(parentInfo.absolutePath());
            if (up == parent || !isInsideRoot(up))
                break;
            parent = up;
            parentInfo.setFile(parent);
        }
        if (parentInfo.exists())
        {
            const QString parentCanon = QDir::cleanPath(parentInfo.canonicalFilePath());
            if (parentCanon.isEmpty() || !parentInfo.isDir() || !isInsideRoot(parentCanon))
            {
                if (error)
                    *error = QString::fromUtf8("Path outside the project directory is not allowed.");
                return false;
            }
        }
        else if (!isInsideRoot(parent))
        {
            if (error)
                *error = QString::fromUtf8("Path outside the project directory is not allowed.");
            return false;
        }
    }

    if (absolutePath)
        *absolutePath = candidate;
    return true;
}

//----------------------------------------------

bool ProjectWorkspace::resolveDirectory(QString userPath, QString *absolutePath, QString *error,
                                        bool allowOutside) const
{
    if (_root.isEmpty() && !allowOutside)
    {
        if (error)
            *error = QString::fromUtf8("Project directory is not selected.");
        return false;
    }

    QString raw = userPath.trimmed();
    raw.replace(QChar('\\'), QChar('/'));
    if (raw.isEmpty() || raw == QString::fromUtf8(".") || raw == QString::fromUtf8("./"))
    {
        if (_root.isEmpty())
        {
            if (error)
                *error = QString::fromUtf8("Project directory is not selected.");
            return false;
        }
        if (absolutePath)
            *absolutePath = _root;
        return true;
    }
    if (raw.contains(QChar(0)))
    {
        if (error)
            *error = QString::fromUtf8("Invalid path.");
        return false;
    }

    QString candidate;
    if (QDir::isAbsolutePath(raw))
        candidate = QDir::cleanPath(raw);
    else if (!_root.isEmpty())
        candidate = QDir::cleanPath(_root + QChar('/') + raw);
    else
        candidate = QDir::cleanPath(raw);

    if (!allowOutside && !isInsideRoot(candidate))
    {
        if (error)
            *error = QString::fromUtf8("Path outside the project directory is not allowed.");
        return false;
    }

    QFileInfo info(candidate);
    if (!info.exists())
    {
        const QString want = tempDir();
        if (!want.isEmpty()
            && QDir::cleanPath(candidate).compare(want, Qt::CaseInsensitive) == 0
            && ensureTempDir())
        {
            info.setFile(want);
        }
    }
    if (!info.exists())
    {
        if (error)
            *error = QString::fromUtf8("Directory not found: %1").arg(raw);
        return false;
    }
    if (!info.isDir())
    {
        if (error)
            *error = QString::fromUtf8("A file was given; a directory is expected.");
        return false;
    }

    QString canon = QDir::cleanPath(info.canonicalFilePath());
    if (canon.isEmpty())
        canon = QDir::cleanPath(info.absoluteFilePath());
    if (canon.isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("Invalid path.");
        return false;
    }
    if (!allowOutside && !isInsideRoot(canon))
    {
        if (error)
            *error = QString::fromUtf8("Path outside the project directory is not allowed.");
        return false;
    }
    if (absolutePath)
        *absolutePath = canon;
    return true;
}

//----------------------------------------------

bool ProjectWorkspace::contains(QString absolutePath) const
{
    if (_root.isEmpty() || absolutePath.trimmed().isEmpty())
        return false;
    return isInsideRoot(QDir::cleanPath(absolutePath));
}

//----------------------------------------------

QString ProjectWorkspace::tempDir() const
{
    if (_root.isEmpty())
        return QString();
    return QDir::cleanPath(_root + QString::fromUtf8("/___project___/tmp"));
}

//----------------------------------------------

bool ProjectWorkspace::ensureTempDir() const
{
    const QString dir = tempDir();
    if (dir.isEmpty())
        return false;
    return QDir().mkpath(dir);
}

//----------------------------------------------

QString ProjectWorkspace::toRelative(QString absolutePath) const
{
    if (_root.isEmpty() || absolutePath.trimmed().isEmpty())
        return absolutePath;
    QString rel = QDir(_root).relativeFilePath(absolutePath);
    rel.replace(QChar('\\'), QChar('/'));
    return rel;
}

//----------------------------------------------

bool ProjectWorkspace::refersOutside(QString userPath, QString baseDir) const
{
    if (_root.isEmpty())
        return true;

    QString raw = userPath.trimmed();
    raw.replace(QChar('\\'), QChar('/'));
    if (raw.isEmpty())
        return false;

    QString base = QDir::cleanPath(baseDir.trimmed());
    if (base.isEmpty())
        base = _root;

    QString candidate;
    const bool driveAbs = (raw.size() >= 2
                           && raw.at(0).isLetter()
                           && raw.at(1) == QChar(':'));
    if (driveAbs || QDir::isAbsolutePath(raw))
        candidate = QDir::cleanPath(raw);
    else
        candidate = QDir::cleanPath(base + QChar('/') + raw);

    return !isInsideRoot(candidate);
}

//----------------------------------------------

QString ProjectWorkspace::normalizeDir(QString path) const
{
    if (path.isEmpty())
        return QString();
    QFileInfo info(path);
    if (!info.exists() || !info.isDir())
        return QString();
    QString abs = info.canonicalFilePath();
    if (abs.isEmpty())
        abs = info.absoluteFilePath();
    abs = QDir::cleanPath(abs);
    return abs;
}

//----------------------------------------------

bool ProjectWorkspace::isInsideRoot(QString candidate) const
{
    const QString c = QDir::cleanPath(candidate);
    const QString r = QDir::cleanPath(_root);
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    if (c.compare(r, cs) == 0)
        return true;
    const QString prefix = r.endsWith(QChar('/')) ? r : (r + QChar('/'));
    return c.startsWith(prefix, cs);
}
