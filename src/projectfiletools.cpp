#include "projectfiletools.h"
//----------------------------------------------
#include "appsettings.h"
#include "filechangestore.h"
#include "projectworkspace.h"
#include "worker.h"
#include "workermanager.h"
//----------------------------------------------
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPair>

//----------------------------------------------

static bool refuseUntilPlanned(Worker *owner, bool *ok, QString *error)
{
    if (!owner)
        return false;
    const QString block = owner->openingBlockReason();
    if (block.isEmpty())
        return false;
    if (ok)
        *ok = false;
    if (error)
        *error = block;
    return true;
}

//----------------------------------------------

static QString nextFileContextId()
{
    static int seq = 0;
    ++seq;
    return QString::fromUtf8("fc%1").arg(seq);
}

//----------------------------------------------

static bool claimPathForOwner(Worker *owner, ProjectWorkspace *workspace, QString absolute,
                              QString *error)
{
    if (!owner || !workspace)
        return true;
    WorkerManager *manager = qobject_cast<WorkerManager *>(owner->parent());
    if (!manager)
        return true;
    QString rel = workspace->toRelative(absolute);
    if (rel.isEmpty())
        rel = absolute;
    return manager->claimFilePath(owner, rel, error);
}

//----------------------------------------------

static bool parseArgs(QString argumentsJson, QJsonObject *object, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(argumentsJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (error)
            *error = QString::fromUtf8("Invalid JSON arguments.");
        return false;
    }
    if (object)
        *object = document.object();
    return true;
}

//----------------------------------------------

static QJsonObject pathOnlySchema()
{
    QJsonObject path;
    path.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    path.insert(QString::fromUtf8("description"),
                QString::fromUtf8("File path relative to the project directory."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("path"), path);

    QJsonArray required;
    required.append(QString::fromUtf8("path"));

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

static bool ensureParentDir(QString absolutePath, QString *error)
{
    const QString parent = QFileInfo(absolutePath).absolutePath();
    QDir dir;
    if (dir.exists(parent))
        return true;
    if (!dir.mkpath(parent))
    {
        if (error)
            *error = QString::fromUtf8("Failed to create directory: %1").arg(parent);
        return false;
    }
    return true;
}

//----------------------------------------------

static QString readAllText(QString path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return ProjectWorkspace::decodeText(file.readAll());
}

//----------------------------------------------

static bool confirmIfOutside(Worker *owner, ProjectWorkspace *workspace,
                             QString absolutePath, QString actionText, QString *error)
{
    if (!workspace || workspace->contains(absolutePath))
        return true;
    if (!AppSettings::instance()->outsideFilesControlEnabled())
        return true;
    if (!owner)
    {
        if (error)
            *error = QString::fromUtf8(
                "Path is outside the project directory. User approval is required.");
        return false;
    }
    const QString decision = owner->askPermission(actionText, true);
    if (!decision.startsWith(QString::fromUtf8("approved")))
    {
        if (error)
            *error = QString::fromUtf8("User rejected the change outside the project: %1")
                         .arg(decision);
        return false;
    }
    return true;
}

//----------------------------------------------

CreateFileAgentTool::CreateFileAgentTool(ProjectWorkspace *workspace, Worker *owner,
                                         QObject *parent)
    : AgentTool(parent)
    , _workspace(workspace)
    , _owner(owner)
{
}

//----------------------------------------------

QString CreateFileAgentTool::name() const
{
    return QString::fromUtf8("create_file");
}

//----------------------------------------------

static QJsonObject filesBatchSchema(bool contentRequired)
{
    QJsonObject path;
    path.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    path.insert(QString::fromUtf8("description"),
                QString::fromUtf8("File path relative to the project directory."));

    QJsonObject content;
    content.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    content.insert(QString::fromUtf8("description"),
                   contentRequired
                       ? QString::fromUtf8("Full UTF-8 text content of the file.")
                       : QString::fromUtf8("Optional initial UTF-8 content."));

    QJsonObject fileItemProps;
    fileItemProps.insert(QString::fromUtf8("path"), path);
    fileItemProps.insert(QString::fromUtf8("content"), content);
    QJsonArray fileItemRequired;
    fileItemRequired.append(QString::fromUtf8("path"));
    if (contentRequired)
        fileItemRequired.append(QString::fromUtf8("content"));
    QJsonObject fileItem;
    fileItem.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    fileItem.insert(QString::fromUtf8("properties"), fileItemProps);
    fileItem.insert(QString::fromUtf8("required"), fileItemRequired);

    QJsonObject files;
    files.insert(QString::fromUtf8("type"), QString::fromUtf8("array"));
    files.insert(QString::fromUtf8("items"), fileItem);
    files.insert(QString::fromUtf8("description"),
                 QString::fromUtf8("Several files (max 16). Prefer this over many separate "
                                   "calls when creating/writing independent files."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("path"), path);
    properties.insert(QString::fromUtf8("content"), content);
    properties.insert(QString::fromUtf8("files"), files);

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    return schema;
}

//----------------------------------------------

static bool collectFileEntries(const QJsonObject &args, bool contentRequired,
                               QList<QPair<QString, QString> > *out, QString *error)
{
    if (!out)
        return false;
    out->clear();
    const QJsonArray filesArr = args.value(QString::fromUtf8("files")).toArray();
    for (int i = 0; i < filesArr.size(); ++i)
    {
        const QJsonObject item = filesArr.at(i).toObject();
        const QString p = item.value(QString::fromUtf8("path")).toString().trimmed();
        if (p.isEmpty())
            continue;
        const QString c = item.value(QString::fromUtf8("content")).toString();
        if (contentRequired && !item.contains(QString::fromUtf8("content")))
        {
            if (error)
                *error = QString::fromUtf8("files[%1]: content is required.").arg(i);
            return false;
        }
        out->append(qMakePair(p, c));
    }
    if (out->isEmpty())
    {
        const QString p = args.value(QString::fromUtf8("path")).toString().trimmed();
        if (p.isEmpty())
        {
            if (error)
                *error = QString::fromUtf8("Provide path+content or files=[{path,content},...].");
            return false;
        }
        if (contentRequired && !args.contains(QString::fromUtf8("content")))
        {
            if (error)
                *error = QString::fromUtf8("content is required.");
            return false;
        }
        out->append(qMakePair(p, args.value(QString::fromUtf8("content")).toString()));
    }
    if (out->size() > 16)
    {
        if (error)
            *error = QString::fromUtf8("Too many files (%1); max 16 per call.")
                         .arg(out->size());
        return false;
    }
    return true;
}

//----------------------------------------------

QString CreateFileAgentTool::description() const
{
    return QString::fromUtf8(
        "Create new file(s) in the project. Prefer files=[{path,content},...] for several "
        "independent files in one call. Single path=/content= still works. Paths outside "
        "need user approval. Put scratch files in ___project___/tmp, not project-root tmp/.");
}

//----------------------------------------------

QJsonObject CreateFileAgentTool::parametersSchema() const
{
    return filesBatchSchema(false);
}

//----------------------------------------------

QString CreateFileAgentTool::createOne(QString relPath, QString content, bool *ok)
{
    QString error;
    if (content.toUtf8().size() > ProjectWorkspace::maxWriteBytes)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Content is too large (limit %1 bytes).")
               .arg(ProjectWorkspace::maxWriteBytes);
    }

    QString absolute;
    if (!_workspace || !_workspace->resolvePath(relPath, false, true, &absolute, &error, true))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!confirmIfOutside(_owner, _workspace, absolute,
                          QString::fromUtf8("Создать файл вне проекта:\n%1").arg(absolute),
                          &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!claimPathForOwner(_owner, _workspace, absolute, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (QFileInfo(absolute).exists())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("File already exists: %1").arg(relPath);
    }
    if (!ensureParentDir(absolute, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }

    QFile file(absolute);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Failed to create file: %1").arg(file.errorString());
    }
    if (file.write(content.toUtf8()) < 0)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Write error: %1").arg(file.errorString());
    }
    file.close();
    FileChangeStore::instance()->noteCreated(absolute, content);
    const QString shown = _workspace->toRelative(absolute);
    if (_owner)
        _owner->noteFileChanged(shown);
    if (ok)
        *ok = true;
    return QString::fromUtf8("Created file: %1 (%2 bytes)")
           .arg(shown)
           .arg(QFileInfo(absolute).size());
}

//----------------------------------------------

QString CreateFileAgentTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (refuseUntilPlanned(_owner, ok, &error))
        return error;
    if (_owner)
        _owner->noteWorkAction();
    if (!parseArgs(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }

    QList<QPair<QString, QString> > entries;
    if (!collectFileEntries(args, false, &entries, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }

    if (entries.size() == 1)
        return createOne(entries.first().first, entries.first().second, ok);

    QString report;
    int okCount = 0;
    for (int i = 0; i < entries.size(); ++i)
    {
        bool oneOk = false;
        const QString body = createOne(entries.at(i).first, entries.at(i).second, &oneOk);
        if (oneOk)
            ++okCount;
        report += QString::fromUtf8("===== %1 =====\n%2\n")
                      .arg(entries.at(i).first, body);
        if (i + 1 < entries.size())
            report += QChar('\n');
    }
    if (ok)
        *ok = (okCount == entries.size());
    return report;
}

//----------------------------------------------

WriteFileAgentTool::WriteFileAgentTool(ProjectWorkspace *workspace, Worker *owner,
                                       QObject *parent)
    : AgentTool(parent)
    , _workspace(workspace)
    , _owner(owner)
{
}

//----------------------------------------------

QString WriteFileAgentTool::name() const
{
    return QString::fromUtf8("write_file");
}

//----------------------------------------------

QString WriteFileAgentTool::description() const
{
    return QString::fromUtf8(
        "Write or overwrite text file(s) in the project. Prefer files=[{path,content},...] "
        "for several independent files in one call. Single path=/content= still works. "
        "Paths outside need user approval. Put scratch in ___project___/tmp, not "
        "project-root tmp/.");
}

//----------------------------------------------

QJsonObject WriteFileAgentTool::parametersSchema() const
{
    return filesBatchSchema(true);
}

//----------------------------------------------

QString WriteFileAgentTool::writeOne(QString relPath, QString content, bool *ok)
{
    QString error;
    if (content.toUtf8().size() > ProjectWorkspace::maxWriteBytes)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Content is too large (limit %1 bytes).")
               .arg(ProjectWorkspace::maxWriteBytes);
    }

    QString absolute;
    if (!_workspace || !_workspace->resolvePath(relPath, false, true, &absolute, &error, true))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!confirmIfOutside(_owner, _workspace, absolute,
                          QString::fromUtf8("Изменить файл вне проекта:\n%1").arg(absolute),
                          &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!claimPathForOwner(_owner, _workspace, absolute, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!ensureParentDir(absolute, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }

    const QString previous = QFileInfo(absolute).exists() ? readAllText(absolute) : QString();
    QFile file(absolute);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Failed to write file: %1").arg(file.errorString());
    }
    if (file.write(content.toUtf8()) < 0)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Write error: %1").arg(file.errorString());
    }
    file.close();
    FileChangeStore::instance()->noteWritten(absolute, previous, content);
    const QString shown = _workspace->toRelative(absolute);
    if (_owner)
        _owner->noteFileChanged(shown);
    if (ok)
        *ok = true;
    return QString::fromUtf8("Wrote file: %1 (%2 bytes)")
           .arg(shown)
           .arg(QFileInfo(absolute).size());
}

//----------------------------------------------

QString WriteFileAgentTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (refuseUntilPlanned(_owner, ok, &error))
        return error;
    if (_owner)
        _owner->noteWorkAction();
    if (!parseArgs(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }

    QList<QPair<QString, QString> > entries;
    if (!collectFileEntries(args, true, &entries, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }

    if (entries.size() == 1)
        return writeOne(entries.first().first, entries.first().second, ok);

    QString report;
    int okCount = 0;
    for (int i = 0; i < entries.size(); ++i)
    {
        bool oneOk = false;
        const QString body = writeOne(entries.at(i).first, entries.at(i).second, &oneOk);
        if (oneOk)
            ++okCount;
        report += QString::fromUtf8("===== %1 =====\n%2\n")
                      .arg(entries.at(i).first, body);
        if (i + 1 < entries.size())
            report += QChar('\n');
    }
    if (ok)
        *ok = (okCount == entries.size());
    return report;
}

//----------------------------------------------

ReadFileAgentTool::ReadFileAgentTool(ProjectWorkspace *workspace, QObject *parent)
    : AgentTool(parent)
    , _workspace(workspace)
{
}

//----------------------------------------------

QString ReadFileAgentTool::name() const
{
    return QString::fromUtf8("read_file");
}

//----------------------------------------------

QString ReadFileAgentTool::description() const
{
    return QString::fromUtf8(
        "Read project text file(s) (~8 KB/file, ~12 KB/call). Prefer paths=[...] for several "
        "independent files. Single path= still works. Use offset (1-based line) and limit to "
        "continue a long file — truncated replies include a ready continuation call. "
        "Each file body is tagged [[filectx id=fcN ...]] and stays in context until you "
        "list that id in FILE_DROP.");
}

//----------------------------------------------

QJsonObject ReadFileAgentTool::parametersSchema() const
{
    QJsonObject path;
    path.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    path.insert(QString::fromUtf8("description"),
                QString::fromUtf8("One file path relative to the project directory."));

    QJsonObject paths;
    paths.insert(QString::fromUtf8("type"), QString::fromUtf8("array"));
    QJsonObject pathItem;
    pathItem.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    paths.insert(QString::fromUtf8("items"), pathItem);
    paths.insert(QString::fromUtf8("description"),
                 QString::fromUtf8("Several relative paths to read in one call (max 32). "
                                   "Prefer this over many separate read_file calls."));

    QJsonObject offset;
    offset.insert(QString::fromUtf8("type"), QString::fromUtf8("integer"));
    offset.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("First line to return, 1-based. Default 1. "
                                    "Applies to path= or to every entry in paths=."));

    QJsonObject limit;
    limit.insert(QString::fromUtf8("type"), QString::fromUtf8("integer"));
    limit.insert(QString::fromUtf8("description"),
                 QString::fromUtf8("Maximum number of lines to return per file. "
                                   "Default: as many as fit in one chunk."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("path"), path);
    properties.insert(QString::fromUtf8("paths"), paths);
    properties.insert(QString::fromUtf8("offset"), offset);
    properties.insert(QString::fromUtf8("limit"), limit);

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    return schema;
}

//----------------------------------------------

QString ReadFileAgentTool::readOne(QString relPath, int offset, int limit, bool *ok) const
{
    QString absolute;
    QString error;
    if (!_workspace || !_workspace->resolvePath(relPath, true, false, &absolute, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }

    QFile file(absolute);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Failed to read file: %1").arg(file.errorString());
    }

    const qint64 size = file.size();
    if (size > ProjectWorkspace::maxReadBytes)
    {
        file.close();
        if (ok)
            *ok = false;
        return QString::fromUtf8(
            "File is too large to read as text (%1 bytes, limit %2). "
            "Use run_command with a filter (e.g. findstr/grep) instead.")
            .arg(size)
            .arg(ProjectWorkspace::maxReadBytes);
    }
    const QString text = ProjectWorkspace::decodeText(file.readAll());
    file.close();

    // Куски с готовой командой продолжения: модель не должна перечитывать файл
    // целиком — один следующий read_file с offset восстанавливает хвост.
    const int startOffset = qMax(1, offset);
    const int lineLimit = qMax(0, limit);
    const QStringList lines = text.split(QChar('\n'));
    const int total = lines.size();
    const int start = startOffset - 1;
    if (start >= total)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("offset %1 is beyond the end: the file has %2 lines.")
            .arg(startOffset)
            .arg(total);
    }

    const int chunkBudget = ProjectWorkspace::maxReadChunkChars;
    QString body;
    int end = start;
    while (end < total)
    {
        if (lineLimit > 0 && end - start >= lineLimit)
            break;
        const QString &line = lines.at(end);
        if (end > start && body.size() + line.size() + 1 > chunkBudget)
            break;
        if (end > start)
            body += QChar('\n');
        if (line.size() > chunkBudget)
            body += line.left(chunkBudget) + QString::fromUtf8("...[line clipped]");
        else
            body += line;
        ++end;
    }

    if (ok)
        *ok = true;

    const QString digest = QString::number(qHash(body), 16);
    const QString fileId = nextFileContextId();
    const QString openTag = QString::fromUtf8("[[filectx id=%1 path=%2 lines=%3-%4]]\n")
                                .arg(fileId, relPath)
                                .arg(start + 1)
                                .arg(end);
    const QString closeTag = QString::fromUtf8("\n[[/filectx]]");
    if (start == 0 && end >= total)
    {
        QString full = openTag;
        full += QString::fromUtf8("[path=%1 lines=1-%2 of %2 digest=%3]\n")
                    .arg(relPath)
                    .arg(total)
                    .arg(digest);
        full += body;
        full += closeTag;
        return full;
    }

    QString result = openTag;
    result += QString::fromUtf8("[path=%1 lines=%2-%3 of %4 digest=%5]\n")
                  .arg(relPath)
                  .arg(start + 1)
                  .arg(end)
                  .arg(total)
                  .arg(digest);
    result += body;
    if (end < total)
    {
        result += QString::fromUtf8(
                      "\n... [%1 more lines]\n"
                      "Continue with ONE call (do not re-read from line 1):\n"
                      "read_file path=\"%2\" offset=%3")
                      .arg(total - end)
                      .arg(relPath)
                      .arg(end + 1);
    }
    result += closeTag;
    return result;
}

//----------------------------------------------

QString ReadFileAgentTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseArgs(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }

    const int offset = qMax(1, args.value(QString::fromUtf8("offset")).toInt(1));
    const int limit = qMax(0, args.value(QString::fromUtf8("limit")).toInt(0));

    QStringList relPaths;
    const QJsonArray pathsArr = args.value(QString::fromUtf8("paths")).toArray();
    for (int i = 0; i < pathsArr.size(); ++i)
    {
        const QString p = pathsArr.at(i).toString().trimmed();
        if (!p.isEmpty())
            relPaths.append(p);
    }
    if (relPaths.isEmpty())
    {
        const QString single = args.value(QString::fromUtf8("path")).toString().trimmed();
        if (!single.isEmpty())
            relPaths.append(single);
    }

    if (relPaths.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Provide path or paths=[...].");
    }
    if (relPaths.size() > 32)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Too many paths (%1); max 32 per read_file call.")
            .arg(relPaths.size());
    }

    if (relPaths.size() == 1)
        return readOne(relPaths.first(), offset, limit, ok);

    QString report;
    int okCount = 0;
    const int callBudget = ProjectWorkspace::maxReadCallChars;
    for (int i = 0; i < relPaths.size(); ++i)
    {
        if (report.size() >= callBudget)
        {
            report += QString::fromUtf8(
                          "\n[call budget exhausted; remaining paths not read]\n");
            for (int j = i; j < relPaths.size(); ++j)
            {
                report += QString::fromUtf8(
                              "Continue with ONE call: read_file path=\"%1\" offset=1\n")
                              .arg(relPaths.at(j));
            }
            break;
        }
        bool oneOk = false;
        const QString body = readOne(relPaths.at(i), offset, limit, &oneOk);
        if (oneOk)
            ++okCount;
        QString chunk = QString::fromUtf8("===== %1 =====\n%2\n")
                            .arg(relPaths.at(i), body);
        if (report.size() + chunk.size() > callBudget && !report.isEmpty())
        {
            report += QString::fromUtf8(
                          "\n[call budget exhausted; remaining paths not read]\n");
            for (int j = i; j < relPaths.size(); ++j)
            {
                report += QString::fromUtf8(
                              "Continue with ONE call: read_file path=\"%1\" offset=1\n")
                              .arg(relPaths.at(j));
            }
            break;
        }
        report += chunk;
        if (i + 1 < relPaths.size())
            report += QChar('\n');
    }
    if (ok)
        *ok = (okCount == relPaths.size());
    return report;
}

//----------------------------------------------

DeleteFileAgentTool::DeleteFileAgentTool(ProjectWorkspace *workspace, Worker *owner,
                                         QObject *parent)
    : AgentTool(parent)
    , _workspace(workspace)
    , _owner(owner)
{
}

//----------------------------------------------

QString DeleteFileAgentTool::name() const
{
    return QString::fromUtf8("delete_file");
}

//----------------------------------------------

QString DeleteFileAgentTool::description() const
{
    return QString::fromUtf8(
        "Delete a file (not a directory). Paths outside need user approval.");
}

//----------------------------------------------

QJsonObject DeleteFileAgentTool::parametersSchema() const
{
    return pathOnlySchema();
}

//----------------------------------------------

QString DeleteFileAgentTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (refuseUntilPlanned(_owner, ok, &error))
        return error;
    if (_owner)
        _owner->noteWorkAction();
    if (!parseArgs(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }

    const QString rel = args.value(QString::fromUtf8("path")).toString();
    QString absolute;
    if (!_workspace || !_workspace->resolvePath(rel, true, false, &absolute, &error, true))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!confirmIfOutside(_owner, _workspace, absolute,
                          QString::fromUtf8("Удалить файл вне проекта:\n%1").arg(absolute),
                          &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!claimPathForOwner(_owner, _workspace, absolute, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }

    QFile file(absolute);
    if (!file.remove())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Failed to delete file: %1").arg(file.errorString());
    }
    FileChangeStore::instance()->noteDeleted(absolute);
    if (_owner)
        _owner->noteFileChanged(rel);
    if (ok)
        *ok = true;
    return QString::fromUtf8("Deleted file: %1").arg(rel);
}

//----------------------------------------------

RenameFileAgentTool::RenameFileAgentTool(ProjectWorkspace *workspace, Worker *owner,
                                         QObject *parent)
    : AgentTool(parent)
    , _workspace(workspace)
    , _owner(owner)
{
}

//----------------------------------------------

QString RenameFileAgentTool::name() const
{
    return QString::fromUtf8("rename_file");
}

//----------------------------------------------

QString RenameFileAgentTool::description() const
{
    return QString::fromUtf8(
        "Rename or move a file. Does not overwrite. Directories cannot be renamed.");
}

//----------------------------------------------

QJsonObject RenameFileAgentTool::parametersSchema() const
{
    QJsonObject path;
    path.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    path.insert(QString::fromUtf8("description"),
                QString::fromUtf8("Current file path relative to the project directory."));

    QJsonObject newPath;
    newPath.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    newPath.insert(QString::fromUtf8("description"),
                   QString::fromUtf8("New file path relative to the project directory."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("path"), path);
    properties.insert(QString::fromUtf8("new_path"), newPath);

    QJsonArray required;
    required.append(QString::fromUtf8("path"));
    required.append(QString::fromUtf8("new_path"));

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString RenameFileAgentTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (refuseUntilPlanned(_owner, ok, &error))
        return error;
    if (_owner)
        _owner->noteWorkAction();
    if (!parseArgs(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }

    const QString fromRel = args.value(QString::fromUtf8("path")).toString();
    const QString toRel = args.value(QString::fromUtf8("new_path")).toString();

    QString fromAbs;
    if (!_workspace || !_workspace->resolvePath(fromRel, true, false, &fromAbs, &error, true))
    {
        if (ok)
            *ok = false;
        return error;
    }

    QString toAbs;
    if (!_workspace->resolvePath(toRel, false, true, &toAbs, &error, true))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!confirmIfOutside(_owner, _workspace, fromAbs,
                          QString::fromUtf8("Переименовать или переместить вне проекта:\n%1\n→ %2")
                              .arg(fromAbs, toAbs),
                          &error)
        || !confirmIfOutside(_owner, _workspace, toAbs,
                             QString::fromUtf8("Создать файл вне проекта:\n%1").arg(toAbs),
                             &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!claimPathForOwner(_owner, _workspace, fromAbs, &error)
        || !claimPathForOwner(_owner, _workspace, toAbs, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (QFileInfo(toAbs).exists())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Destination already exists: %1").arg(toRel);
    }
    if (!ensureParentDir(toAbs, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }

    if (!QFile::rename(fromAbs, toAbs))
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Failed to rename %1 to %2").arg(fromRel, toRel);
    }
    FileChangeStore::instance()->noteRenamed(fromAbs, toAbs);
    if (_owner)
    {
        _owner->noteFileChanged(fromRel);
        _owner->noteFileChanged(toRel);
    }
    if (ok)
        *ok = true;
    return QString::fromUtf8("Renamed file: %1 -> %2").arg(fromRel, toRel);
}

//----------------------------------------------

ListDirectoryAgentTool::ListDirectoryAgentTool(ProjectWorkspace *workspace, QObject *parent)
    : AgentTool(parent)
    , _workspace(workspace)
{
}

//----------------------------------------------

QString ListDirectoryAgentTool::name() const
{
    return QString::fromUtf8("list_directory");
}

//----------------------------------------------

QString ListDirectoryAgentTool::description() const
{
    return QString::fromUtf8(
        "List files in a project folder (not recursive). Empty path or \".\" is the project root.");
}

//----------------------------------------------

QJsonObject ListDirectoryAgentTool::parametersSchema() const
{
    QJsonObject path;
    path.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    path.insert(QString::fromUtf8("description"),
                QString::fromUtf8("Directory path relative to the project. "
                                    "Empty or \".\" means the project root."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("path"), path);

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    return schema;
}

//----------------------------------------------

QString ListDirectoryAgentTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!argumentsJson.trimmed().isEmpty())
    {
        if (!parseArgs(argumentsJson, &args, &error))
        {
            if (ok)
                *ok = false;
            return error;
        }
    }

    const QString rel = args.value(QString::fromUtf8("path")).toString();
    QString absolute;
    if (!_workspace || !_workspace->resolveDirectory(rel, &absolute, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }

    QDir dir(absolute);
    const QFileInfoList infos = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                                                  QDir::DirsFirst | QDir::Name);
    QJsonArray entries;
    int skipped = 0;
    for (int i = 0; i < infos.size(); ++i)
    {
        if (entries.size() >= ProjectWorkspace::maxListEntries)
        {
            skipped = infos.size() - i;
            break;
        }

        const QFileInfo info = infos.at(i);
        QString canon = QDir::cleanPath(info.canonicalFilePath());
        if (canon.isEmpty())
            canon = QDir::cleanPath(info.absoluteFilePath());
        if (canon.isEmpty() || !_workspace->contains(canon))
            continue;

        QJsonObject item;
        item.insert(QString::fromUtf8("name"), info.fileName());
        if (info.isDir())
            item.insert(QString::fromUtf8("type"), QString::fromUtf8("dir"));
        else
        {
            item.insert(QString::fromUtf8("type"), QString::fromUtf8("file"));
            item.insert(QString::fromUtf8("size"), info.size());
        }
        entries.append(item);
    }

    QDir rootDir(_workspace->root());
    QString shown = rootDir.relativeFilePath(absolute);
    if (shown.isEmpty() || shown == QString::fromUtf8("."))
        shown = QString::fromUtf8(".");

    QJsonObject result;
    result.insert(QString::fromUtf8("directory"), shown);
    result.insert(QString::fromUtf8("count"), entries.size());
    result.insert(QString::fromUtf8("truncated"), skipped > 0);
    if (skipped > 0)
        result.insert(QString::fromUtf8("omitted"), skipped);
    result.insert(QString::fromUtf8("entries"), entries);

    if (ok)
        *ok = true;
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented));
}
