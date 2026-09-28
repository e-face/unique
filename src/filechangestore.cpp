#include "filechangestore.h"
//----------------------------------------------
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>

//----------------------------------------------

FileChangeStore::FileChangeStore(QObject *parent)
    : QObject(parent)
{
}

//----------------------------------------------

FileChangeStore *FileChangeStore::instance()
{
    static FileChangeStore store;
    return &store;
}

//----------------------------------------------

QString FileChangeStore::normalizeAbs(QString absPath) const
{
    QString abs = QDir::cleanPath(absPath.trimmed());
    abs.replace(QChar('\\'), QChar('/'));
#ifdef Q_OS_WIN
    if (abs.startsWith(QString::fromUtf8("//?/")))
        abs = abs.mid(4);
    abs = abs.toLower();
#endif
    return abs;
}

//----------------------------------------------

void FileChangeStore::forget()
{
    _root.clear();
    _baseline.clear();
    _delta.clear();
}

//----------------------------------------------

void FileChangeStore::setRoot(QString root, bool loadFromDisk)
{
    const QString clean = QDir::cleanPath(root.trimmed());
    if (_root == clean && loadFromDisk)
        return;
    _root = clean;
    _baseline.clear();
    _delta.clear();
    if (_root.isEmpty())
    {
        emitChanged();
        return;
    }
    if (loadFromDisk)
        loadIndex();
    emitChanged();
}

//----------------------------------------------

QString FileChangeStore::root() const
{
    return _root;
}

//----------------------------------------------

QString FileChangeStore::relativePath(QString absPath) const
{
    if (_root.isEmpty() || absPath.trimmed().isEmpty())
        return QString();

    QString abs = normalizeAbs(absPath);
    if (abs.isEmpty())
        return QString();

    const QFileInfo info(absPath);
    if (info.exists())
    {
        const QString canon = normalizeAbs(info.canonicalFilePath());
        if (!canon.isEmpty())
            abs = canon;
    }

    const QString root = normalizeAbs(_root);
    if (root.isEmpty())
        return QString();
    if (abs == root)
        return QString();

    QString rel;
    const QString prefix = root + QChar('/');
    if (abs.startsWith(prefix))
        rel = abs.mid(prefix.size());
    else
    {
        rel = QDir(QDir::fromNativeSeparators(_root))
                  .relativeFilePath(QDir::fromNativeSeparators(absPath));
        rel.replace(QChar('\\'), QChar('/'));
#ifdef Q_OS_WIN
        rel = rel.toLower();
#endif
        if (rel.isEmpty() || rel == QString::fromUtf8(".")
            || rel.startsWith(QString::fromUtf8("../"))
            || QDir::isAbsolutePath(rel))
            return QString();
    }
    if (rel.startsWith(QString::fromUtf8("___project___")))
        return QString();
    return rel;
}

//----------------------------------------------

QString FileChangeStore::indexPath() const
{
    if (_root.isEmpty())
        return QString();
    return _root + QString::fromUtf8("/___project___/filechanges.json");
}

//----------------------------------------------

QString FileChangeStore::baselineDir() const
{
    if (_root.isEmpty())
        return QString();
    return _root + QString::fromUtf8("/___project___/baselines");
}

//----------------------------------------------

QString FileChangeStore::baselineFile(QString rel) const
{
    if (rel.isEmpty() || baselineDir().isEmpty())
        return QString();
    return QDir::cleanPath(baselineDir() + QChar('/') + rel);
}

//----------------------------------------------

QString FileChangeStore::readText(QString absPath) const
{
    QFile file(absPath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(file.readAll());
}

//----------------------------------------------

bool FileChangeStore::writeText(QString absPath, QString text) const
{
    QFileInfo info(absPath);
    if (!QDir().mkpath(info.absolutePath()))
        return false;
    QFile file(absPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(text.toUtf8());
    return true;
}

//----------------------------------------------

bool FileChangeStore::loadIndex()
{
    const QString path = indexPath();
    if (path.isEmpty() || !QFileInfo(path).exists())
        return false;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return false;

    const QJsonObject files = doc.object().value(QString::fromUtf8("files")).toObject();
    if (files.isEmpty())
        return true;

    for (QJsonObject::const_iterator it = files.constBegin(); it != files.constEnd(); ++it)
    {
        QString rel = it.key();
        rel.replace(QChar('\\'), QChar('/'));
#ifdef Q_OS_WIN
        rel = rel.toLower();
#endif
        if (rel.isEmpty() || rel.startsWith(QString::fromUtf8("___project___")))
            continue;
        const QJsonObject obj = it.value().toObject();
        FileLineDelta d;
        d.added = obj.value(QString::fromUtf8("added")).toInt();
        d.removed = obj.value(QString::fromUtf8("removed")).toInt();
        if (!d.isEmpty())
            _delta.insert(rel, d);
    }
    return true;
}

//----------------------------------------------

void FileChangeStore::saveIndex() const
{
    const QString path = indexPath();
    if (path.isEmpty())
        return;

    QJsonObject files;
    for (QHash<QString, FileLineDelta>::const_iterator it = _delta.constBegin();
         it != _delta.constEnd(); ++it)
    {
        if (it.key().isEmpty() || it.value().isEmpty())
            continue;
        QJsonObject obj;
        obj.insert(QString::fromUtf8("added"), it.value().added);
        obj.insert(QString::fromUtf8("removed"), it.value().removed);
        files.insert(it.key(), obj);
    }

    QJsonObject root;
    root.insert(QString::fromUtf8("version"), 1);
    root.insert(QString::fromUtf8("files"), files);
    writeText(path, QString::fromUtf8(
                  QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

//----------------------------------------------

void FileChangeStore::ensureBaseline(QString rel, QString previous)
{
    if (rel.isEmpty() || _baseline.contains(rel))
        return;
    const QString path = baselineFile(rel);
    if (!path.isEmpty() && QFileInfo(path).exists())
        _baseline.insert(rel, readText(path));
    else
    {
        _baseline.insert(rel, previous);
        writeText(path, previous);
    }
}

//----------------------------------------------

void FileChangeStore::updateDelta(QString rel, QString current)
{
    if (rel.isEmpty() || !_baseline.contains(rel))
        return;
    const FileLineDelta d = countLineDiff(_baseline.value(rel), current);
    if (d.isEmpty())
    {
        removeEntry(rel);
        return;
    }
    _delta.insert(rel, d);
}

//----------------------------------------------

void FileChangeStore::removeEntry(QString rel)
{
    if (rel.isEmpty())
        return;
    _baseline.remove(rel);
    _delta.remove(rel);
    const QString path = baselineFile(rel);
    if (!path.isEmpty())
        QFile::remove(path);
}

//----------------------------------------------

void FileChangeStore::noteCreated(QString absPath, QString content)
{
    const QString rel = relativePath(absPath);
    if (rel.isEmpty())
        return;
    ensureBaseline(rel, QString());
    updateDelta(rel, content);
    saveIndex();
    emitChanged();
}

//----------------------------------------------

void FileChangeStore::noteWritten(QString absPath, QString previous, QString next)
{
    const QString rel = relativePath(absPath);
    if (rel.isEmpty())
        return;
    ensureBaseline(rel, previous);
    updateDelta(rel, next);
    saveIndex();
    emitChanged();
}

//----------------------------------------------

void FileChangeStore::noteDeleted(QString absPath)
{
    const QString rel = relativePath(absPath);
    if (rel.isEmpty())
        return;
    removeEntry(rel);
    saveIndex();
    emitChanged();
}

//----------------------------------------------

void FileChangeStore::noteRenamed(QString fromAbs, QString toAbs)
{
    const QString fromRel = relativePath(fromAbs);
    const QString toRel = relativePath(toAbs);
    if (fromRel.isEmpty() || toRel.isEmpty() || fromRel == toRel)
        return;
    const QString text = _baseline.contains(fromRel)
                         ? _baseline.value(fromRel)
                         : readText(baselineFile(fromRel));
    const FileLineDelta d = _delta.value(fromRel);
    removeEntry(fromRel);
    if (!text.isNull() || !d.isEmpty())
    {
        _baseline.insert(toRel, text);
        writeText(baselineFile(toRel), text);
        if (!d.isEmpty())
            _delta.insert(toRel, d);
        else
            updateDelta(toRel, readText(toAbs));
    }
    saveIndex();
    emitChanged();
}

//----------------------------------------------

void FileChangeStore::clear()
{
    const QStringList keys = _delta.keys() + _baseline.keys();
    foreach (const QString &rel, keys)
        QFile::remove(baselineFile(rel));
    if (!indexPath().isEmpty())
        QFile::remove(indexPath());
    _baseline.clear();
    _delta.clear();
    emitChanged();
}

//----------------------------------------------

FileLineDelta FileChangeStore::deltaFor(QString absPath) const
{
    const QString rel = relativePath(absPath);
    if (!rel.isEmpty() && _delta.contains(rel))
        return _delta.value(rel);

    const QFileInfo info(absPath);
    const bool isDir = info.exists() ? info.isDir() : rel.isEmpty();
    if (!isDir)
        return FileLineDelta();

    const QString prefix = rel.isEmpty() ? QString() : (rel + QChar('/'));
    FileLineDelta sum;
    for (QHash<QString, FileLineDelta>::const_iterator it = _delta.constBegin();
         it != _delta.constEnd(); ++it)
    {
        if (rel.isEmpty() || it.key().startsWith(prefix))
        {
            sum.added += it.value().added;
            sum.removed += it.value().removed;
        }
    }
    return sum;
}

//----------------------------------------------

void FileChangeStore::rebuildFromDisk()
{
    _baseline.clear();
    _delta.clear();
    const QString dir = baselineDir();
    if (dir.isEmpty() || !QDir(dir).exists())
        return;

    QStringList files;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        files.append(it.filePath());
    }

    const int total = files.size();
    for (int i = 0; i < files.size(); ++i)
    {
        const QString absBaseline = files.at(i);
        if (i == 0 || ((i + 1) % 8) == 0)
            emit loadProgress(i + 1, total, absBaseline);
        QString rel = QDir(dir).relativeFilePath(absBaseline);
        rel.replace(QChar('\\'), QChar('/'));
#ifdef Q_OS_WIN
        rel = rel.toLower();
#endif
        if (rel.isEmpty())
            continue;
        const QString original = readText(absBaseline);
        _baseline.insert(rel, original);
        const QString currentPath = QDir::cleanPath(_root + QChar('/') + rel);
        if (!QFileInfo(currentPath).exists())
        {
            removeEntry(rel);
            continue;
        }
        updateDelta(rel, readText(currentPath));
    }
    if (total > 0)
        emit loadProgress(total, total, QString());
    saveIndex();
}

//----------------------------------------------

void FileChangeStore::emitChanged()
{
    emit changed();
}

//----------------------------------------------

static QStringList splitLines(QString text)
{
    QString t = text;
    t.replace(QString::fromUtf8("\r\n"), QString::fromUtf8("\n"));
    t.replace(QChar('\r'), QChar('\n'));
    if (t.isEmpty())
        return QStringList();
    QStringList lines = t.split(QChar('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    return lines;
}

//----------------------------------------------

FileLineDelta FileChangeStore::countLineDiff(QString oldText, QString newText)
{
    const QStringList a = splitLines(oldText);
    const QStringList b = splitLines(newText);
    FileLineDelta d;
    if (a.isEmpty() && b.isEmpty())
        return d;

    QHash<QString, int> remain;
    remain.reserve(a.size());
    for (int i = 0; i < a.size(); ++i)
        remain[a.at(i)] += 1;
    for (int i = 0; i < b.size(); ++i)
    {
        const int left = remain.value(b.at(i), 0);
        if (left > 0)
            remain[b.at(i)] = left - 1;
        else
            ++d.added;
    }
    for (QHash<QString, int>::const_iterator it = remain.constBegin();
         it != remain.constEnd(); ++it)
        d.removed += it.value();
    return d;
}
