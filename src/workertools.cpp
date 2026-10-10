#include "workertools.h"
//----------------------------------------------
#include "agent.h"
#include "appsettings.h"
#include "chatsession.h"
#include "knowledgestore.h"
#include "projectworkspace.h"
#include "worker.h"
#include "workermanager.h"
//----------------------------------------------
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QEventLoop>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegExp>
#include <QStringList>
#include <QTimer>
#include <QtGlobal>

//----------------------------------------------

static bool parseObject(QString argumentsJson, QJsonObject *object, QString *error)
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

static QString expandEnvVars(QString text, bool *unknown)
{
    if (unknown)
        *unknown = false;
#ifdef Q_OS_WIN
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QRegExp re(QString::fromUtf8("%([^%]+)%"));
    QString result;
    int last = 0;
    int pos = 0;
    while ((pos = re.indexIn(text, last)) != -1)
    {
        result += text.mid(last, pos - last);
        const QString name = re.cap(1);
        if (env.contains(name))
            result += env.value(name);
        else if (unknown)
            *unknown = true;
        last = pos + re.matchedLength();
    }
    result += text.mid(last);
    return result;
#else
    return text;
#endif
}

//----------------------------------------------

static QStringList splitCommandTokens(QString command)
{
    QStringList tokens;
    QString cur;
    bool inQuotes = false;
    for (int i = 0; i < command.size(); ++i)
    {
        const QChar c = command.at(i);
        if (c == QChar('"'))
        {
            inQuotes = !inQuotes;
            continue;
        }
        if (!inQuotes && c.isSpace())
        {
            if (!cur.isEmpty())
            {
                tokens.append(cur);
                cur.clear();
            }
            continue;
        }
        cur += c;
    }
    if (!cur.isEmpty())
        tokens.append(cur);
    return tokens;
}

//----------------------------------------------

static QStringList commandSegments(QString command)
{
    QStringList parts;
    QString cur;
    bool inQuotes = false;
    const int n = command.size();
    for (int i = 0; i < n; ++i)
    {
        const QChar c = command.at(i);
        if (c == QChar('"'))
        {
            inQuotes = !inQuotes;
            cur += c;
            continue;
        }
        if (!inQuotes)
        {
            if (c == QChar('&') && cur.endsWith(QChar('>')))
            {
                cur += c;
                continue;
            }
            bool split = false;
            int skip = 0;
            if (c == QChar(';'))
            {
                split = true;
            }
            else if (c == QChar('&') || c == QChar('|'))
            {
                split = true;
                if (i + 1 < n && command.at(i + 1) == c)
                    skip = 1;
            }
            if (split)
            {
                if (!cur.trimmed().isEmpty())
                    parts.append(cur.trimmed());
                cur.clear();
                i += skip;
                continue;
            }
        }
        cur += c;
    }
    if (!cur.trimmed().isEmpty())
        parts.append(cur.trimmed());
    return parts;
}

//----------------------------------------------

static QString commandProgramKey(QString command)
{
    const QStringList tokens = splitCommandTokens(command);
    if (tokens.isEmpty())
        return QString();
    QString first = tokens.first().trimmed();
    first.replace(QChar('\\'), QChar('/'));
    const int slash = first.lastIndexOf(QChar('/'));
    if (slash >= 0)
        first = first.mid(slash + 1);
    if (first.isEmpty())
        return QString();
#ifdef Q_OS_WIN
    first = first.toLower();
    const char *suffixes[] = { ".exe", ".cmd", ".bat", ".com", 0 };
    for (int s = 0; suffixes[s]; ++s)
    {
        const QString suf = QString::fromUtf8(suffixes[s]);
        if (first.endsWith(suf))
        {
            first.chop(suf.size());
            break;
        }
    }
    if (tokens.size() >= 3
        && (first == QString::fromUtf8("cmd")))
    {
        for (int i = 1; i < tokens.size(); ++i)
        {
            const QString a = tokens.at(i).toLower();
            if (a == QString::fromUtf8("/c") || a == QString::fromUtf8("/k"))
            {
                QString rest;
                for (int j = i + 1; j < tokens.size(); ++j)
                {
                    if (!rest.isEmpty())
                        rest += QChar(' ');
                    rest += tokens.at(j);
                }
                if (!rest.isEmpty())
                    return commandProgramKey(rest);
                break;
            }
        }
    }
    if (tokens.size() >= 3
        && (first == QString::fromUtf8("powershell") || first == QString::fromUtf8("pwsh")))
    {
        for (int i = 1; i < tokens.size(); ++i)
        {
            const QString a = tokens.at(i).toLower();
            if (a == QString::fromUtf8("-command") || a == QString::fromUtf8("-c")
                || a == QString::fromUtf8("-file"))
            {
                QString rest;
                for (int j = i + 1; j < tokens.size(); ++j)
                {
                    if (!rest.isEmpty())
                        rest += QChar(' ');
                    rest += tokens.at(j);
                }
                if (!rest.isEmpty())
                    return commandProgramKey(rest);
                break;
            }
        }
    }
#endif
    return first;
}

//----------------------------------------------

static bool looksLikePath(QString token)
{
    const QString t = token.trimmed();
    if (t.isEmpty())
        return false;
    const QString lower = t.toLower();
    if (lower.startsWith(QString::fromUtf8("http://"))
        || lower.startsWith(QString::fromUtf8("https://"))
        || lower.startsWith(QString::fromUtf8("git@")))
        return false;
    if (lower == QString::fromUtf8("nul") || lower == QString::fromUtf8("con")
        || lower == QString::fromUtf8("prn") || lower == QString::fromUtf8("aux")
        || lower == QString::fromUtf8("nul:") || lower.startsWith(QString::fromUtf8("/dev/")))
        return false;
    if (t.startsWith(QString::fromUtf8("\\\\")) || t.startsWith(QString::fromUtf8("//")))
        return true;
    if (t.size() >= 2 && t.at(0).isLetter() && t.at(1) == QChar(':'))
        return true;
    if (t.contains(QChar('\\')) || t.contains(QChar('/')))
        return true;
    if (t == QString::fromUtf8(".") || t == QString::fromUtf8("..")
        || t.startsWith(QString::fromUtf8("./")) || t.startsWith(QString::fromUtf8(".\\"))
        || t.startsWith(QString::fromUtf8("../")) || t.startsWith(QString::fromUtf8("..\\")))
        return true;
    if (t.startsWith(QChar('~')) || t.contains(QChar('%')))
        return true;
    return false;
}

//----------------------------------------------

static QStringList pathCandidatesFromToken(QString token)
{
    QStringList out;
    QString t = token.trimmed();
    if (t.isEmpty())
        return out;

    if (t.startsWith(QChar('>')) || t.startsWith(QChar('<')))
        t.remove(0, t.startsWith(QString::fromUtf8(">>")) ? 2 : 1);
    else if (t.size() >= 2 && t.at(0).isDigit()
             && (t.at(1) == QChar('>') || t.at(1) == QChar('<')))
        t.remove(0, t.contains(QString::fromUtf8(">>")) ? 3 : 2);

    const int eq = t.indexOf(QChar('='));
    if (eq > 0 && !t.startsWith(QChar('-')))
        t = t.mid(eq + 1);

    if (t.startsWith(QString::fromUtf8("--")))
    {
        const int flagEq = t.indexOf(QChar('='));
        if (flagEq > 0)
            t = t.mid(flagEq + 1);
        else
            return out;
    }
    else if (t.startsWith(QChar('-')) && t.size() >= 3
             && t.at(1).isLetter())
    {
        QString rest = t.mid(2);
        if (rest.startsWith(QChar('=')))
            rest = rest.mid(1);
        if (looksLikePath(rest))
            t = rest;
    }
    else if (t.startsWith(QChar('/')) && t.size() >= 3
             && t.at(1).isLetter())
    {
        QString rest = t.mid(2);
        if (rest.startsWith(QChar(':')) || rest.startsWith(QChar('=')))
            rest = rest.mid(1);
        if (looksLikePath(rest))
            t = rest;
    }

    if (looksLikePath(t))
        out.append(t);
    return out;
}

//----------------------------------------------

static QStringList redirectTargets(QString segment)
{
    QStringList out;
    bool inQuotes = false;
    QString cur;
    bool capture = false;
    for (int i = 0; i < segment.size(); ++i)
    {
        const QChar c = segment.at(i);
        if (c == QChar('"'))
        {
            inQuotes = !inQuotes;
            if (capture && !inQuotes && !cur.isEmpty())
            {
                out.append(cur);
                cur.clear();
                capture = false;
            }
            continue;
        }
        if (capture)
        {
            if (!inQuotes && c.isSpace())
            {
                if (!cur.isEmpty())
                    out.append(cur);
                cur.clear();
                capture = false;
            }
            else
                cur += c;
            continue;
        }
        if (inQuotes)
            continue;
        if (c == QChar('>') || c == QChar('<'))
        {
            if (c == QChar('>') && i + 1 < segment.size()
                && segment.at(i + 1) == QChar('>'))
                ++i;
            if (i + 1 < segment.size() && segment.at(i + 1) == QChar('&'))
                continue;
            capture = true;
            cur.clear();
        }
    }
    if (capture && !cur.isEmpty())
        out.append(cur);
    return out;
}

//----------------------------------------------

static bool isSystemWideProgram(QString programKey)
{
    const QString key = programKey.toLower();
    static const char *names[] = {
        "reg", "regedit", "regedt32",
        "setx",
        "net", "netsh",
        "sc",
        "schtasks",
        "bcdedit", "bcdboot", "bootrec",
        "diskpart", "format",
        "shutdown", "logoff",
        "wmic",
        "assoc", "ftype",
        "rundll32", "regsvr32",
        "msiexec",
        "powercfg",
        "gpupdate", "secedit",
        "takeown", "icacls", "cipher",
        "wscript", "cscript", "mshta",
        "bitsadmin", "certutil", "wevtutil",
        "wbadmin", "vssadmin",
        "pnputil",
        "winget", "choco", "chocolatey", "scoop",
        0
    };
    for (int i = 0; names[i]; ++i)
    {
        if (key == QString::fromUtf8(names[i]))
            return true;
    }
    return false;
}

//----------------------------------------------

static bool argsMayAffectOutside(QString programKey, const QStringList &tokens)
{
    const QString key = programKey.toLower();
    QStringList args;
    for (int i = 1; i < tokens.size(); ++i)
        args.append(tokens.at(i).toLower());

    foreach (const QString &arg, args)
    {
        if (arg == QString::fromUtf8("--global") || arg == QString::fromUtf8("--system")
            || arg == QString::fromUtf8("--user"))
            return true;
    }

    if (key == QString::fromUtf8("npm") || key == QString::fromUtf8("npx")
        || key == QString::fromUtf8("yarn") || key == QString::fromUtf8("pnpm")
        || key == QString::fromUtf8("composer"))
    {
        foreach (const QString &arg, args)
        {
            if (arg == QString::fromUtf8("-g"))
                return true;
        }
    }
    if (key == QString::fromUtf8("cargo") || key == QString::fromUtf8("go"))
    {
        if (!args.isEmpty() && args.first() == QString::fromUtf8("install"))
            return true;
    }
    if (key == QString::fromUtf8("git"))
    {
        for (int i = 0; i < args.size(); ++i)
        {
            if (args.at(i) == QString::fromUtf8("config") && i + 1 < args.size())
            {
                const QString next = args.at(i + 1);
                if (next == QString::fromUtf8("--global") || next == QString::fromUtf8("--system"))
                    return true;
            }
        }
    }
    return false;
}

//----------------------------------------------

static bool tokenRefersOutside(QString token, QString workDir, ProjectWorkspace *workspace)
{
    bool unknownEnv = false;
    const QString expanded = expandEnvVars(token, &unknownEnv);
    if (unknownEnv)
        return true;
    if (expanded.trimmed().startsWith(QChar('~')))
        return true;
    return workspace->refersOutside(expanded, workDir);
}

//----------------------------------------------

static bool commandMayAffectOutside(QString command, QString workDir,
                                    ProjectWorkspace *workspace)
{
    if (!workspace)
        return true;
    if (!workspace->contains(workDir))
        return true;

    const QStringList segments = commandSegments(command);
    QString programKey;
    foreach (const QString &segment, segments)
    {
        const QString key = commandProgramKey(segment);
        if (key.isEmpty())
            continue;
        if (programKey.isEmpty())
            programKey = key;
        else if (programKey.compare(key, Qt::CaseInsensitive) != 0)
            return true;
        if (isSystemWideProgram(key))
            continue;

        const QStringList tokens = splitCommandTokens(segment);
        if (argsMayAffectOutside(key, tokens))
            return true;

        for (int i = 1; i < tokens.size(); ++i)
        {
            foreach (const QString &path, pathCandidatesFromToken(tokens.at(i)))
            {
                if (tokenRefersOutside(path, workDir, workspace))
                    return true;
            }
        }
        foreach (const QString &target, redirectTargets(segment))
        {
            if (tokenRefersOutside(target, workDir, workspace))
                return true;
        }
    }
    return false;
}

//----------------------------------------------

static bool commandAffectsSystemSettings(QString command)
{
    const QStringList segments = commandSegments(command);
    foreach (const QString &segment, segments)
    {
        const QString key = commandProgramKey(segment);
        if (key.isEmpty())
            continue;
        if (isSystemWideProgram(key))
            return true;
        const QStringList tokens = splitCommandTokens(segment);
        if (argsMayAffectOutside(key, tokens))
            return true;
    }
    return false;
}

//----------------------------------------------

//----------------------------------------------

PlanWorkTool::PlanWorkTool(Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _owner(owner)
{
}

//----------------------------------------------

QString PlanWorkTool::name() const
{
    return QString::fromUtf8("plan_work");
}

//----------------------------------------------

QString PlanWorkTool::description() const
{
    return QString::fromUtf8(
        "Required before the first delegate_task (skip if you finish a small task yourself). "
        "simple/cannot_split -> executor; complex needs expected_parts>=3 or stay "
        "in YOUR plan as simple items. Do not give the whole job to one executor. "
        "When planning, note which preset_id (model) fits each subtask for later "
        "delegate_task — match modality and purpose, minimize cost, maximize fit.");
}

//----------------------------------------------

QJsonObject PlanWorkTool::parametersSchema() const
{
    QJsonObject architecture;
    architecture.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    architecture.insert(QString::fromUtf8("description"),
                        QString::fromUtf8(
                            "Overall design: components, boundaries, data flow, file ownership."));

    QJsonObject name;
    name.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    name.insert(QString::fromUtf8("description"),
                QString::fromUtf8("Short specific name of the subtask, 2-6 words in the user's "
                                    "language; it becomes the child worker's name in the UI."));
    QJsonObject task;
    task.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    QJsonObject complexity;
    complexity.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    complexity.insert(QString::fromUtf8("description"),
                      QString::fromUtf8("simple or complex"));
    QJsonObject cannotSplit;
    cannotSplit.insert(QString::fromUtf8("type"), QString::fromUtf8("boolean"));
    QJsonObject expectedParts;
    expectedParts.insert(QString::fromUtf8("type"), QString::fromUtf8("integer"));
    expectedParts.insert(QString::fromUtf8("description"),
                         QString::fromUtf8(
                             "For complex subtasks only: how many independent executor-sized "
                             "parts the child manager would create. Must be >= 3; with 1-2 "
                             "parts do not create a manager, list the parts here as simple "
                             "subtasks instead."));
    QJsonObject reason;
    reason.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    reason.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("Why this is simple/atomic or still complex."));

    QJsonObject subtaskProps;
    subtaskProps.insert(QString::fromUtf8("name"), name);
    subtaskProps.insert(QString::fromUtf8("task"), task);
    subtaskProps.insert(QString::fromUtf8("complexity"), complexity);
    subtaskProps.insert(QString::fromUtf8("cannot_split"), cannotSplit);
    subtaskProps.insert(QString::fromUtf8("expected_parts"), expectedParts);
    subtaskProps.insert(QString::fromUtf8("reason"), reason);
    QJsonArray subtaskRequired;
    subtaskRequired.append(QString::fromUtf8("name"));
    subtaskRequired.append(QString::fromUtf8("task"));
    subtaskRequired.append(QString::fromUtf8("complexity"));
    QJsonObject subtaskItem;
    subtaskItem.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    subtaskItem.insert(QString::fromUtf8("properties"), subtaskProps);
    subtaskItem.insert(QString::fromUtf8("required"), subtaskRequired);

    QJsonObject subtasks;
    subtasks.insert(QString::fromUtf8("type"), QString::fromUtf8("array"));
    subtasks.insert(QString::fromUtf8("items"), subtaskItem);

    QJsonObject title;
    title.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    title.insert(QString::fromUtf8("description"),
                 QString::fromUtf8("Short name of your whole assignment, 2-6 words in the "
                                     "user's language. Becomes the chat/worker title in the UI."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("title"), title);
    properties.insert(QString::fromUtf8("architecture"), architecture);
    properties.insert(QString::fromUtf8("subtasks"), subtasks);
    QJsonArray required;
    required.append(QString::fromUtf8("title"));
    required.append(QString::fromUtf8("architecture"));
    required.append(QString::fromUtf8("subtasks"));

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString PlanWorkTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!_owner)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Worker is missing.");
    }

    const QJsonArray raw = args.value(QString::fromUtf8("subtasks")).toArray();
    QList<WorkSubtask> items;
    for (int i = 0; i < raw.size(); ++i)
    {
        const QJsonObject obj = raw.at(i).toObject();
        WorkSubtask item;
        item.name = obj.value(QString::fromUtf8("name")).toString();
        item.task = obj.value(QString::fromUtf8("task")).toString();
        item.complexity = obj.value(QString::fromUtf8("complexity")).toString();
        item.cannotSplit = obj.value(QString::fromUtf8("cannot_split")).toBool();
        item.expectedParts = obj.value(QString::fromUtf8("expected_parts")).toInt();
        item.reason = obj.value(QString::fromUtf8("reason")).toString();
        items.append(item);
    }

    error = _owner->applyWorkPlan(
        args.value(QString::fromUtf8("architecture")).toString(), items);
    if (!error.isEmpty())
    {
        if (ok)
            *ok = false;
        return error;
    }
    const QString title = args.value(QString::fromUtf8("title")).toString().simplified();
    if (!title.isEmpty())
        _owner->setTitle(title);

    WorkerManager *manager = qobject_cast<WorkerManager *>(_owner->parent());
    const bool managersAllowed = !manager || manager->canSpawnManagerUnder(_owner);
    QString report = QString::fromUtf8(
        "Plan accepted. Now call delegate_task once per subtask in this turn.\n");
    if (!managersAllowed)
        report += QString::fromUtf8(
            "Depth limit: type=manager is not allowed at your level, "
            "so every subtask goes to an executor.\n");
    if (manager)
        report += manager->loadSummary() + QString::fromUtf8("\n");
    foreach (const WorkSubtask &item, _owner->workPlan())
    {
        const bool atomic = (item.complexity.toLower() == QString::fromUtf8("simple")
                             || item.cannotSplit);
        report += QString::fromUtf8("- %1 -> type %2\n")
                      .arg(item.name.trimmed(),
                           (atomic || !managersAllowed) ? QString::fromUtf8("executor")
                                                        : QString::fromUtf8("manager"));
    }
    if (ok)
        *ok = true;
    return report;
}

//----------------------------------------------

DelegateTaskTool::DelegateTaskTool(WorkerManager *manager, Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _manager(manager)
    , _owner(owner)
{
}

//----------------------------------------------

QString DelegateTaskTool::name() const
{
    return QString::fromUtf8("delegate_task");
}

//----------------------------------------------

QString DelegateTaskTool::description() const
{
    return QString::fromUtf8(
        "Give work to a direct child. plan_work first (not needed if you do a small task yourself). "
        "simple -> executor, complex -> manager (depth limits are in your first message). "
        "Pass preset_id from available worker presets to bind the child's model "
        "(match modality AND purpose: e.g. text-to-image vs object segmentation; "
        "cheapest adequate for routine work; stronger models for hard reasoning). "
        "Reuse an existing child via worker_id — including COMPLETED children, which can "
        "be restarted many times with new instructions. Several calls run in parallel; "
        "you continue after ALL children of the batch answer. type: manager/executor/tester.");
}

//----------------------------------------------

QJsonObject DelegateTaskTool::parametersSchema() const
{
    QJsonObject type;
    type.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    type.insert(QString::fromUtf8("description"),
                QString::fromUtf8("Worker type: manager, executor, or tester."));

    QJsonObject task;
    task.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    task.insert(QString::fromUtf8("description"),
                QString::fromUtf8("Self-contained task, or follow-up instructions for an existing child."));

    QJsonObject workerId;
    workerId.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    workerId.insert(QString::fromUtf8("description"),
                    QString::fromUtf8("Optional id of an existing direct child to reuse."));

    QJsonObject subtask;
    subtask.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    subtask.insert(QString::fromUtf8("description"),
                   QString::fromUtf8("Name of the subtask from plan_work (required for managers)."));

    QJsonObject title;
    title.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    title.insert(QString::fromUtf8("description"),
                 QString::fromUtf8("Short human-readable name of THIS child's job, 2-6 words in "
                                     "the user's language (e.g. 'Парсер конфигурации'). Shown as "
                                     "the worker's name in the UI, so make it specific, not the "
                                     "project name."));

    QJsonObject presetId;
    presetId.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    presetId.insert(QString::fromUtf8("description"),
                    QString::fromUtf8(
                        "Optional worker preset_id (from first-message list). Selects the "
                        "child's model by modality and purpose. type must match the "
                        "preset's type. Omit only if reusing worker_id or accepting the "
                        "default for type."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("type"), type);
    properties.insert(QString::fromUtf8("title"), title);
    properties.insert(QString::fromUtf8("task"), task);
    properties.insert(QString::fromUtf8("worker_id"), workerId);
    properties.insert(QString::fromUtf8("subtask"), subtask);
    properties.insert(QString::fromUtf8("preset_id"), presetId);

    QJsonArray required;
    required.append(QString::fromUtf8("type"));
    required.append(QString::fromUtf8("title"));
    required.append(QString::fromUtf8("task"));

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString DelegateTaskTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!_manager || !_owner)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Worker manager is not available.");
    }

    const QString typeName = args.value(QString::fromUtf8("type")).toString().trimmed().toLower();
    if (!isKnownWorkerTypeName(typeName))
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Invalid worker type. Use manager, executor, or tester.");
    }
    const WorkerType type = workerTypeFromString(typeName);
    const QString task = args.value(QString::fromUtf8("task")).toString();
    const QString workerId = args.value(QString::fromUtf8("worker_id")).toString();
    const QString subtaskName = args.value(QString::fromUtf8("subtask")).toString();
    const QString presetId = args.value(QString::fromUtf8("preset_id")).toString().trimmed();
    // Имя воркера: явный title, иначе имя подзадачи из плана, иначе суть из текста.
    QString title = args.value(QString::fromUtf8("title")).toString().simplified();
    if (title.isEmpty())
        title = subtaskName.simplified();
    if (task.trimmed().isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Task text is empty.");
    }

    QString llmProfileId;
    if (!presetId.isEmpty())
    {
        const WorkerPreset preset = AppSettings::instance()->workerPresetById(presetId);
        if (preset.id.isEmpty())
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8(
                "Unknown preset_id=%1.\n%2")
                .arg(presetId, AppSettings::instance()->workerPresetsSummary());
        }
        if (preset.type != type)
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8(
                "preset_id=%1 is type=%2, but delegate_task type=%3. "
                "Pick a preset whose type matches, or change type.")
                .arg(presetId, workerTypeToString(preset.type), workerTypeToString(type));
        }
        if (preset.type == WorkerTypeTool)
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8(
                "Tool presets are for create_tool / run_tool, not delegate_task.");
        }
        llmProfileId = preset.profileId.trimmed();
        if (llmProfileId.isEmpty())
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8("Preset %1 has an empty profileId.").arg(presetId);
        }
        if (AppSettings::instance()->profileById(llmProfileId).id.isEmpty())
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8(
                "preset_id=%1 points to unknown profileId=%2.")
                .arg(presetId, llmProfileId);
        }
    }

    if (_owner->type() == WorkerTypeManager)
    {
        if (!_owner->hasWorkPlan())
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8(
                "Call plan_work first: architecture, then subtasks with complexity. "
                "Do not spawn an executor for the whole assignment.");
        }
        WorkSubtask *item = _owner->findWorkSubtask(subtaskName, task);
        if (!item)
        {
            if (ok)
                *ok = false;
            QString names;
            foreach (const WorkSubtask &planned, _owner->workPlan())
            {
                if (!names.isEmpty())
                    names += QString::fromUtf8(", ");
                names += planned.name;
            }
            return QString::fromUtf8(
                "subtask must match a plan_work item. Planned names: %1")
                .arg(names);
        }
        if (type != WorkerTypeTester)
        {
            const bool atomic = (item->complexity.toLower() == QString::fromUtf8("simple")
                                 || item->cannotSplit);
            const bool managersAllowed = _manager && _manager->canSpawnManagerUnder(_owner);
            if (type == WorkerTypeExecutor && !atomic && managersAllowed)
            {
                if (ok)
                    *ok = false;
                return QString::fromUtf8(
                    "Subtask '%1' is still complex. Spawn type=manager so it can be split further.")
                    .arg(item->name);
            }
            if (type == WorkerTypeManager && atomic)
            {
                if (ok)
                    *ok = false;
                return QString::fromUtf8(
                    "Subtask '%1' is simple/atomic. Spawn type=executor, not manager.")
                    .arg(item->name);
            }
        }
        _owner->markSubtaskDelegated(item->name);
    }

    Worker *child = _owner->findChildForAssignment(type, workerId, task);
    if (!workerId.trimmed().isEmpty() && !child)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8(
            "No direct child with worker_id=%1.\n%2")
            .arg(workerId.trimmed(), _owner->childrenSummary());
    }
    if (child)
    {
        if (child->type() != type)
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8(
                "Child id=%1 is type %2, not %3. Use the matching child or omit worker_id.")
                .arg(child->id(),
                     workerTypeToString(child->type()),
                     workerTypeToString(type));
        }
        _owner->registerDelegated(child);
        if (!title.isEmpty() && child->title().isEmpty())
            child->setTitle(title);
        child->markUsed();
        const QString previousState = workerStateToString(child->state());
        // Завершённый ребёнок снова берёт задачу — родитель ждёт новый отчёт.
        child->beginFollowUp(task);
        if (ok)
            *ok = true;
        QString note;
        if (!llmProfileId.isEmpty() && child->llmProfileId() != llmProfileId)
        {
            note = QString::fromUtf8(
                " (preset_id ignored: child's model is already fixed)");
        }
        return QString::fromUtf8(
            "Reused existing child worker id=%1 type=%2 previous_state=%3%4. "
            "Completed workers MAY be restarted many times with new instructions. "
            "Further instructions were sent. It will answer after ALL children "
            "of this batch have finished.")
            .arg(child->id(),
                 workerTypeToString(child->type()),
                 previousState,
                 note);
    }

    Worker *spawned = _manager->spawn(_owner, type, task, &error, llmProfileId);
    if (!spawned)
    {
        if (ok)
            *ok = false;
        return error;
    }
    spawned->setTitle(title);

    _owner->registerDelegated(spawned);
    _manager->requestStart(spawned);
    if (ok)
        *ok = true;
    const bool queued = spawned->state() == WorkerStateQueued;
    const int usedBatches = _owner->subtreeToolBatches();
    const int usedMin = int(_owner->subtreeElapsedMs() / 60000);
    QString profileNote;
    if (!llmProfileId.isEmpty())
    {
        const LlmProfile profile = AppSettings::instance()->profileById(llmProfileId);
        const QString pname = profile.name.trimmed().isEmpty() ? llmProfileId
                                                               : profile.name.trimmed();
        const QString model = profile.model.trimmed().isEmpty()
            ? QString::fromUtf8("?")
            : profile.model.trimmed();
        profileNote = QString::fromUtf8(
                          " Model: profile=\"%1\" model=%2 purpose=\"%3\" (preset_id=%4).")
                          .arg(pname, model,
                               profile.purpose.trimmed().isEmpty()
                                   ? QString::fromUtf8("-")
                                   : profile.purpose.trimmed().simplified(),
                               presetId);
    }
    return QString::fromUtf8(
        "%1 NEW child worker id=%2 type=%3 (depth %4 of max %5).%6 "
        "Subtree usage: tool-batches=%7, wall≈%8 min. %9 "
        "Reuse this id for later instructions on the same assignment. "
        "It will answer you after ALL children in this batch have finished. "
        "Do not assume success yet.")
        .arg(queued ? QString::fromUtf8("Queued") : QString::fromUtf8("Started"))
        .arg(spawned->id(), workerTypeToString(spawned->type()))
        .arg(spawned->depth())
        .arg(_manager->maxDepth())
        .arg(profileNote)
        .arg(usedBatches)
        .arg(usedMin)
        .arg(_manager->loadSummary());
}

//----------------------------------------------

CompleteTaskTool::CompleteTaskTool(Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _owner(owner)
{
}

//----------------------------------------------

QString CompleteTaskTool::name() const
{
    return QString::fromUtf8("complete_task");
}

//----------------------------------------------

QString CompleteTaskTool::description() const
{
    return QString::fromUtf8(
        "Finish and send YOUR report to the parent. Evidence-over-claims / fail-closed: "
        "executors who changed files and ALL testers must pass verified plus structured "
        "evidence {command, output, artifact?, digest?}. Managers cannot accept work that "
        "changed files until a tester child completed with verified=true. "
        "lessons format: lines CMD:/ERR:/FIX:/PATH:/TAGS:.");
}

//----------------------------------------------

QJsonObject CompleteTaskTool::parametersSchema() const
{
    QJsonObject result;
    result.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    result.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("Result text for the parent worker."));
    QJsonObject lessons;
    lessons.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    lessons.insert(QString::fromUtf8("description"),
                   QString::fromUtf8(
                       "Structured reusable knowledge, prefer lines:\n"
                       "CMD: <exact command>\nERR: <error text>\nFIX: <what fixed it>\n"
                       "PATH: <file paths>\nTAGS: build,lint,project-token\n"
                       "Plus any short quirks. No generic advice."));
    QJsonObject verified;
    verified.insert(QString::fromUtf8("type"), QString::fromUtf8("boolean"));
    verified.insert(QString::fromUtf8("description"),
                    QString::fromUtf8(
                        "true if build/checks passed; false if they failed. Required for "
                        "testers and for executors who changed files."));
    QJsonObject command;
    command.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    command.insert(QString::fromUtf8("description"),
                   QString::fromUtf8("Exact verification command that was run."));
    QJsonObject output;
    output.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    output.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("Raw/key stdout+stderr (errors with file:line)."));
    QJsonObject artifact;
    artifact.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    artifact.insert(QString::fromUtf8("description"),
                    QString::fromUtf8("Optional path to produced artifact (exe, log, report)."));
    QJsonObject digest;
    digest.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    digest.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("Optional short hash/size/fingerprint of the artifact."));
    QJsonObject evidenceProps;
    evidenceProps.insert(QString::fromUtf8("command"), command);
    evidenceProps.insert(QString::fromUtf8("output"), output);
    evidenceProps.insert(QString::fromUtf8("artifact"), artifact);
    evidenceProps.insert(QString::fromUtf8("digest"), digest);
    QJsonArray evidenceRequired;
    evidenceRequired.append(QString::fromUtf8("command"));
    evidenceRequired.append(QString::fromUtf8("output"));
    QJsonObject evidence;
    evidence.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    evidence.insert(QString::fromUtf8("description"),
                    QString::fromUtf8(
                        "Structured proof. Required with verified for testers and for "
                        "executors who changed files."));
    evidence.insert(QString::fromUtf8("properties"), evidenceProps);
    evidence.insert(QString::fromUtf8("required"), evidenceRequired);

    QJsonObject properties;
    properties.insert(QString::fromUtf8("result"), result);
    properties.insert(QString::fromUtf8("lessons"), lessons);
    properties.insert(QString::fromUtf8("verified"), verified);
    properties.insert(QString::fromUtf8("evidence"), evidence);

    QJsonArray required;
    required.append(QString::fromUtf8("result"));

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString CompleteTaskTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!_owner)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Worker is missing.");
    }

    if (_owner->hasUnfinishedChildren())
    {
        if (ok)
            *ok = false;
        QString waiting = QString::fromUtf8(
            "Cannot complete yet. Still waiting for child workers:");
        foreach (Worker *child, _owner->unfinishedChildren())
        {
            waiting += QString::fromUtf8("\n- id=%1 type=%2 state=%3")
                           .arg(child->id(),
                                workerTypeToString(child->type()),
                                workerStateToString(child->state()));
        }
        waiting += QString::fromUtf8(
            "\nYou will receive their results automatically, then call complete_task.");
        return waiting;
    }

    QString result = args.value(QString::fromUtf8("result")).toString();
    const QString lessons = args.value(QString::fromUtf8("lessons")).toString().trimmed();
    const bool hasVerified = args.contains(QString::fromUtf8("verified"));
    const bool verified = args.value(QString::fromUtf8("verified")).toBool();
    const WorkerType type = _owner->type();
    const bool changedFiles = !_owner->changedFiles().isEmpty();

    QString evCommand;
    QString evOutput;
    QString evArtifact;
    QString evDigest;
    const QJsonValue evidenceVal = args.value(QString::fromUtf8("evidence"));
    if (evidenceVal.isObject())
    {
        const QJsonObject ev = evidenceVal.toObject();
        evCommand = ev.value(QString::fromUtf8("command")).toString().trimmed();
        evOutput = ev.value(QString::fromUtf8("output")).toString().trimmed();
        evArtifact = ev.value(QString::fromUtf8("artifact")).toString().trimmed();
        evDigest = ev.value(QString::fromUtf8("digest")).toString().trimmed();
    }
    else if (evidenceVal.isString())
    {
        // Совместимость: свободный текст → output, command помечаем legacy.
        evOutput = evidenceVal.toString().trimmed();
        if (!evOutput.isEmpty())
            evCommand = QString::fromUtf8("(unstructured evidence)");
    }
    const bool evidenceOk = (evCommand.size() >= 3 && evOutput.size() >= 20);

    // A: verification gate — fail-closed + structured evidence.
    if (type == WorkerTypeTester)
    {
        if (!hasVerified || !evidenceOk)
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8(
                "REFUSED (verification gate): tester must pass verified and structured "
                "evidence {command, output} (output >= 20 chars). Optional artifact/digest.");
        }
    }
    else if (type == WorkerTypeExecutor && changedFiles)
    {
        if (!hasVerified || !evidenceOk)
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8(
                "REFUSED (verification gate): after changing files, executor must pass "
                "verified and evidence {command, output} for build/compile/syntax. "
                "Test suites are tester-only.");
        }
    }
    else if (type == WorkerTypeManager)
    {
        bool needsTester = false;
        bool havePassingTester = false;
        foreach (Worker *child, _owner->children())
        {
            if (!child || !child->hasAnswered())
                continue;
            if (child->type() == WorkerTypeExecutor && !child->changedFiles().isEmpty())
                needsTester = true;
            if (child->type() == WorkerTypeTester && child->verificationPassed())
                havePassingTester = true;
        }
        if (needsTester && !havePassingTester)
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8(
                "REFUSED (acceptance gate): executors changed files but no tester child "
                "completed with verified=true + evidence. Spawn/await an independent tester "
                "reproduction before complete_task.");
        }
    }

    bool success = true;
    if (type == WorkerTypeTester || (type == WorkerTypeExecutor && changedFiles))
        success = verified;

    if (hasVerified || evidenceOk)
        _owner->setVerification(true, verified && evidenceOk, evCommand, evOutput,
                                evArtifact, evDigest);

    if (evidenceOk)
    {
        result += QString::fromUtf8(
                      "\n\n[verification] verified=%1\ncommand: %2\noutput:\n%3")
                      .arg(verified ? QString::fromUtf8("true")
                                    : QString::fromUtf8("false"),
                           evCommand, evOutput);
        if (!evArtifact.isEmpty())
            result += QString::fromUtf8("\nartifact: ") + evArtifact;
        if (!evDigest.isEmpty())
            result += QString::fromUtf8("\ndigest: ") + evDigest;
    }

    if (!lessons.isEmpty())
        KnowledgeStore::instance()->remember(_owner->type(), _owner->task(), lessons, success);

    WorkerManager *manager = qobject_cast<WorkerManager *>(_owner->parent());
    if (manager)
    {
        manager->releaseFilePaths(_owner);
        manager->noteTaskCompleted();
    }
    _owner->complete(result, success);
    if (_owner->session() && _owner->session()->agent())
        _owner->session()->agent()->requestStopAfterTools();
    if (ok)
        *ok = true;
    if (success)
        return QString::fromUtf8("Task completed and returned to the parent worker.");
    return QString::fromUtf8(
        "Task finished as FAILED (verified=false). Report returned to the parent.");
}

//----------------------------------------------

RequestPermissionTool::RequestPermissionTool(Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _owner(owner)
{
}

//----------------------------------------------

QString RequestPermissionTool::name() const
{
    return QString::fromUtf8("request_permission");
}

//----------------------------------------------

QString RequestPermissionTool::description() const
{
    return QString::fromUtf8(
        "Ask the user to approve an action before doing it. "
        "Use for delete, rename, overwrite, or other destructive file changes. "
        "The function returns approved or rejected.");
}

//----------------------------------------------

QJsonObject RequestPermissionTool::parametersSchema() const
{
    QJsonObject action;
    action.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    action.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("What you want to do, in clear language for the user."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("action"), action);

    QJsonArray required;
    required.append(QString::fromUtf8("action"));

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString RequestPermissionTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!_owner)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Worker is missing.");
    }

    const QString action = args.value(QString::fromUtf8("action")).toString();
    const QString decision = _owner->askPermission(action, true);
    if (ok)
        *ok = true;
    return decision;
}

//----------------------------------------------

ListChildWorkersTool::ListChildWorkersTool(Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _owner(owner)
{
}

//----------------------------------------------

QString ListChildWorkersTool::name() const
{
    return QString::fromUtf8("list_child_workers");
}

//----------------------------------------------

QString ListChildWorkersTool::description() const
{
    return QString::fromUtf8(
        "List direct children (id, type, state, task). Use worker_id in delegate_task to "
        "reuse — completed children can be restarted with new instructions repeatedly.");
}

//----------------------------------------------

QJsonObject ListChildWorkersTool::parametersSchema() const
{
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), QJsonObject());
    return schema;
}

//----------------------------------------------

QString ListChildWorkersTool::invoke(QString, bool *ok)
{
    if (!_owner)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Worker is missing.");
    }
    if (ok)
        *ok = true;
    return _owner->childrenSummary();
}

//----------------------------------------------

GetChildResultTool::GetChildResultTool(Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _owner(owner)
{
}

//----------------------------------------------

QString GetChildResultTool::name() const
{
    return QString::fromUtf8("get_child_result");
}

//----------------------------------------------

QString GetChildResultTool::description() const
{
    return QString::fromUtf8(
        "Full report of one direct child. Auto-delivered reports are clipped; call only if needed.");
}

//----------------------------------------------

QJsonObject GetChildResultTool::parametersSchema() const
{
    QJsonObject workerId;
    workerId.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    workerId.insert(QString::fromUtf8("description"),
                    QString::fromUtf8("Id of the direct child (from the report or list_child_workers)."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("worker_id"), workerId);

    QJsonArray required;
    required.append(QString::fromUtf8("worker_id"));

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString GetChildResultTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!_owner)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Worker is missing.");
    }
    const QString childId = args.value(QString::fromUtf8("worker_id")).toString().trimmed();
    Worker *child = _owner->childById(childId);
    if (!child)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("No direct child with worker_id=%1.\n%2")
            .arg(childId, _owner->childrenSummary());
    }
    if (!child->hasAnswered())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Child %1 has not answered yet (state=%2).")
            .arg(childId, workerStateToString(child->state()));
    }
    if (ok)
        *ok = true;
    QString result = child->result().trimmed();
    if (result.isEmpty())
        result = QString::fromUtf8("(empty)");
    return QString::fromUtf8("Full report of %1 (%2):\n%3")
        .arg(childId, workerTypeToString(child->type()), result);
}

//----------------------------------------------

DismissChildWorkerTool::DismissChildWorkerTool(WorkerManager *manager, Worker *owner,
                                               QObject *parent)
    : AgentTool(parent)
    , _manager(manager)
    , _owner(owner)
{
}

//----------------------------------------------

QString DismissChildWorkerTool::name() const
{
    return QString::fromUtf8("dismiss_child_worker");
}

//----------------------------------------------

QString DismissChildWorkerTool::description() const
{
    return QString::fromUtf8(
        "Remove a finished/temporary direct child and its descendants. Do not dismiss an active assignment.");
}

//----------------------------------------------

CreateToolTool::CreateToolTool(WorkerManager *manager, Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _manager(manager)
    , _owner(owner)
{
}

//----------------------------------------------

QString CreateToolTool::name() const
{
    return QString::fromUtf8("create_tool");
}

//----------------------------------------------

QString CreateToolTool::description() const
{
    return QString::fromUtf8(
        "Create a shared reusable TOOL for one narrow job. Pass preset_id to bind the "
        "model: match modality AND purpose from the presets list (e.g. text-to-image vs "
        "object segmentation). Without preset_id the default Tool LLM profile is used "
        "(cannot generate images). Call list_tools first. Only the user can delete tools.");
}

//----------------------------------------------

QJsonObject CreateToolTool::parametersSchema() const
{
    QJsonObject name;
    name.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    name.insert(QString::fromUtf8("description"),
                QString::fromUtf8("Short unique name, e.g. 'build', 'gen-cover', 'unit-tests'."));

    QJsonObject purpose;
    purpose.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    purpose.insert(QString::fromUtf8("description"),
                   QString::fromUtf8(
                       "What the tool does on every run and what it must report."));

    QJsonObject presetId;
    presetId.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    presetId.insert(QString::fromUtf8("description"),
                    QString::fromUtf8(
                        "Worker preset_id from the available presets list. REQUIRED for "
                        "non-LLM work. Binds the tool to that profile's modality and "
                        "purpose — pick the purpose that fits the job. Prefer a Tool-type "
                        "preset; executor/manager presets are accepted for their profile."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("name"), name);
    properties.insert(QString::fromUtf8("purpose"), purpose);
    properties.insert(QString::fromUtf8("preset_id"), presetId);

    QJsonArray required;
    required.append(QString::fromUtf8("name"));
    required.append(QString::fromUtf8("purpose"));

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString CreateToolTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!_manager || !_owner)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Worker manager is missing.");
    }
    const QString name = args.value(QString::fromUtf8("name")).toString();
    const QString purpose = args.value(QString::fromUtf8("purpose")).toString();
    const QString presetId = args.value(QString::fromUtf8("preset_id")).toString().trimmed();

    QString llmProfileId;
    QString modalityNote;
    if (!presetId.isEmpty())
    {
        const WorkerPreset preset = AppSettings::instance()->workerPresetById(presetId);
        if (preset.id.isEmpty())
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8("Unknown preset_id=%1.\n%2")
                .arg(presetId, AppSettings::instance()->workerPresetsSummary());
        }
        llmProfileId = preset.profileId.trimmed();
        if (llmProfileId.isEmpty())
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8("Preset %1 has an empty profileId.").arg(presetId);
        }
        const LlmProfile profile = AppSettings::instance()->profileById(llmProfileId);
        if (profile.id.isEmpty())
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8("Preset %1 points to unknown profileId=%2.")
                .arg(presetId, llmProfileId);
        }
        modalityNote = QString::fromUtf8(
                           " modality=%1 profile=\"%2\" model=%3 purpose=\"%4\" (preset_id=%5)")
                           .arg(workerTaskKindToString(profile.modality),
                                profile.name.trimmed().isEmpty() ? profile.id
                                                                 : profile.name.trimmed(),
                                profile.model.trimmed().isEmpty() ? QString::fromUtf8("?")
                                                                  : profile.model.trimmed(),
                                profile.purpose.trimmed().isEmpty()
                                    ? QString::fromUtf8("-")
                                    : profile.purpose.trimmed().simplified(),
                                presetId);
    }
    else
    {
        modalityNote = QString::fromUtf8(
            " (no preset_id → default Tool LLM profile; cannot do image/speech/video)");
    }

    Worker *tool = _manager->createTool(_owner, name, purpose, &error, llmProfileId);
    if (!tool)
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (ok)
        *ok = true;
    return QString::fromUtf8(
        "Tool created: tool_id=%1 name=\"%2\"%3. Idle until run_tool. "
        "Every worker can use it.\n%4")
        .arg(tool->id(), tool->toolName(), modalityNote, _manager->toolsSummary());
}

//----------------------------------------------

ListToolsTool::ListToolsTool(WorkerManager *manager, QObject *parent)
    : AgentTool(parent)
    , _manager(manager)
{
}

//----------------------------------------------

QString ListToolsTool::name() const
{
    return QString::fromUtf8("list_tools");
}

//----------------------------------------------

QString ListToolsTool::description() const
{
    return QString::fromUtf8(
        "List shared tools: tool_id, name, busy/free, queue, purpose. Call before create_tool or a build/run.");
}

//----------------------------------------------

QJsonObject ListToolsTool::parametersSchema() const
{
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), QJsonObject());
    return schema;
}

//----------------------------------------------

QString ListToolsTool::invoke(QString, bool *ok)
{
    if (!_manager)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Worker manager is missing.");
    }
    if (ok)
        *ok = true;
    return _manager->toolsSummary();
}

//----------------------------------------------

RunToolTool::RunToolTool(WorkerManager *manager, Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _manager(manager)
    , _owner(owner)
{
}

//----------------------------------------------

QString RunToolTool::name() const
{
    return QString::fromUtf8("run_tool");
}

//----------------------------------------------

QString RunToolTool::description() const
{
    return QString::fromUtf8(
        "Run a shared tool and wait for its report. One caller at a time (others queue). "
        "Prefer this over re-discovering build/run commands.");
}

//----------------------------------------------

QJsonObject RunToolTool::parametersSchema() const
{
    QJsonObject toolId;
    toolId.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    toolId.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("tool_id (or exact name) from list_tools."));

    QJsonObject request;
    request.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    request.insert(QString::fromUtf8("description"),
                   QString::fromUtf8(
                       "The run request: what exactly to do this time and what to report."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("tool_id"), toolId);
    properties.insert(QString::fromUtf8("request"), request);

    QJsonArray required;
    required.append(QString::fromUtf8("tool_id"));
    required.append(QString::fromUtf8("request"));

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString RunToolTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!_manager || !_owner)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Worker manager is missing.");
    }
    const QString toolId = args.value(QString::fromUtf8("tool_id")).toString();
    const QString request = args.value(QString::fromUtf8("request")).toString();
    Worker *tool = _manager->toolByIdOrName(toolId);
    const QString toolName = tool ? tool->toolName() : toolId.trimmed();
    const QString result = _manager->runTool(_owner, toolId, request, &error);
    if (!error.isEmpty())
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (ok)
        *ok = true;
    return QString::fromUtf8("Tool \"%1\" answered:\n%2").arg(toolName, result);
}

//----------------------------------------------

QJsonObject DismissChildWorkerTool::parametersSchema() const
{
    QJsonObject workerId;
    workerId.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    workerId.insert(QString::fromUtf8("description"),
                    QString::fromUtf8("Id of the direct child to remove."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("worker_id"), workerId);

    QJsonArray required;
    required.append(QString::fromUtf8("worker_id"));

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString DismissChildWorkerTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (!_manager || !_owner)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Worker manager is not available.");
    }

    const QString workerId = args.value(QString::fromUtf8("worker_id")).toString();
    if (!_manager->dismissChild(_owner, workerId, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (ok)
        *ok = true;
    return QString::fromUtf8(
        "Removed child worker id=%1 and its descendants from the graph.")
        .arg(workerId.trimmed());
}

//----------------------------------------------

RunCommandAgentTool::RunCommandAgentTool(ProjectWorkspace *workspace, Worker *owner,
                                         QObject *parent)
    : AgentTool(parent)
    , _workspace(workspace)
    , _owner(owner)
    , _runningProcess(0)
    , _runningLoop(0)
    , _interrupted(false)
{
}

//----------------------------------------------

void RunCommandAgentTool::interrupt()
{
    _interrupted = true;
    if (_runningProcess)
        killProcessTree(_runningProcess);
    if (_runningLoop)
        _runningLoop->quit();
}

//----------------------------------------------

void RunCommandAgentTool::killProcessTree(QProcess *process)
{
    if (!process || process->state() == QProcess::NotRunning)
        return;
#ifdef Q_OS_WIN
    // kill() снимает только cmd.exe; сама программа (компилятор, сервер,
    // запущенный exe) остаётся жить и держит файлы. Валим всё дерево.
    const qint64 pid = process->processId();
    if (pid > 0)
    {
        QProcess::startDetached(QString::fromUtf8("taskkill"),
                                QStringList()
                                    << QString::fromUtf8("/F")
                                    << QString::fromUtf8("/T")
                                    << QString::fromUtf8("/PID")
                                    << QString::number(pid));
    }
#endif
    process->kill();
}

//----------------------------------------------

QString RunCommandAgentTool::name() const
{
    return QString::fromUtf8("run_command");
}

//----------------------------------------------

QString RunCommandAgentTool::description() const
{
    return QString::fromUtf8(
        "Run shell command(s) (cmd). Prefer commands=[...] for several independent commands "
        "in one call (or multiple run_command tool_calls in the same turn). Single command= "
        "still works. User approves the program (or every time if it touches paths outside "
        "the project). working_directory is relative to the project; timeout_sec default 120, "
        "max 300. NON-INTERACTIVE: stdin is empty - pass /y, -y, --yes, /q. For GUI/servers "
        "set background=true. Scratch output in ___project___/tmp, not project-root tmp/.");
}

//----------------------------------------------

QJsonObject RunCommandAgentTool::parametersSchema() const
{
    QJsonObject command;
    command.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    command.insert(QString::fromUtf8("description"),
                   QString::fromUtf8("One command line to run."));

    QJsonObject commandItem;
    commandItem.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    QJsonObject commands;
    commands.insert(QString::fromUtf8("type"), QString::fromUtf8("array"));
    commands.insert(QString::fromUtf8("items"), commandItem);
    commands.insert(QString::fromUtf8("description"),
                    QString::fromUtf8("Several independent command lines (max 8). "
                                      "Prefer this over many separate run_command calls."));

    QJsonObject workingDirectory;
    workingDirectory.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    workingDirectory.insert(QString::fromUtf8("description"),
                            QString::fromUtf8("Optional directory relative to the project root "
                                              "(shared by command= / commands=)."));

    QJsonObject timeoutSec;
    timeoutSec.insert(QString::fromUtf8("type"), QString::fromUtf8("integer"));
    timeoutSec.insert(QString::fromUtf8("description"),
                      QString::fromUtf8("Optional timeout in seconds, 1 to 300. Default 120."));

    QJsonObject background;
    background.insert(QString::fromUtf8("type"), QString::fromUtf8("boolean"));
    background.insert(QString::fromUtf8("description"),
                      QString::fromUtf8("Launch detached and return immediately (GUI apps, "
                                          "servers, watchers). Output is not captured."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("command"), command);
    properties.insert(QString::fromUtf8("commands"), commands);
    properties.insert(QString::fromUtf8("working_directory"), workingDirectory);
    properties.insert(QString::fromUtf8("timeout_sec"), timeoutSec);
    properties.insert(QString::fromUtf8("background"), background);

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    return schema;
}

//----------------------------------------------

QString RunCommandAgentTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }

    QStringList commandList;
    const QJsonArray arr = args.value(QString::fromUtf8("commands")).toArray();
    for (int i = 0; i < arr.size(); ++i)
    {
        const QString c = arr.at(i).toString().trimmed();
        if (!c.isEmpty())
            commandList.append(c);
    }
    if (commandList.isEmpty())
    {
        const QString single = args.value(QString::fromUtf8("command")).toString().trimmed();
        if (!single.isEmpty())
            commandList.append(single);
    }

    if (commandList.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Provide command or commands=[...].");
    }
    if (commandList.size() > 8)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Too many commands (%1); max 8 per run_command call.")
            .arg(commandList.size());
    }

    if (commandList.size() == 1)
    {
        QJsonObject one = args;
        one.insert(QString::fromUtf8("command"), commandList.first());
        one.remove(QString::fromUtf8("commands"));
        return runOne(one, ok);
    }

    QString report;
    int okCount = 0;
    for (int i = 0; i < commandList.size(); ++i)
    {
        if (_interrupted)
        {
            if (ok)
                *ok = false;
            report += QString::fromUtf8("STOPPED: the user interrupted the command.");
            return report;
        }
        QJsonObject one = args;
        one.insert(QString::fromUtf8("command"), commandList.at(i));
        one.remove(QString::fromUtf8("commands"));
        bool oneOk = false;
        const QString body = runOne(one, &oneOk);
        if (oneOk)
            ++okCount;
        report += QString::fromUtf8("===== [%1/%2] %3 =====\n%4\n")
                      .arg(i + 1)
                      .arg(commandList.size())
                      .arg(commandList.at(i), body);
        if (i + 1 < commandList.size())
            report += QChar('\n');
    }
    if (ok)
        *ok = (okCount == commandList.size());
    return report;
}

//----------------------------------------------

QString RunCommandAgentTool::runOne(QJsonObject args, bool *ok)
{
    if (!_workspace || !_owner)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Workspace or worker is missing.");
    }

    const QString command = args.value(QString::fromUtf8("command")).toString().trimmed();
    if (command.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Command is empty.");
    }

    QString workDir;
    QString error;
    const QString requestedDir = args.value(QString::fromUtf8("working_directory")).toString();
    if (!_workspace->resolveDirectory(requestedDir, &workDir, &error, true))
    {
        if (ok)
            *ok = false;
        return error;
    }

    int timeoutSec = 120;
    if (args.contains(QString::fromUtf8("timeout_sec")))
        timeoutSec = args.value(QString::fromUtf8("timeout_sec")).toInt();
    if (timeoutSec < 1)
        timeoutSec = 1;
    if (timeoutSec > 300)
        timeoutSec = 300;
    const bool background = args.value(QString::fromUtf8("background")).toBool(false);

    const bool outside = commandMayAffectOutside(command, workDir, _workspace);
    const bool system = commandAffectsSystemSettings(command);
    QString relDir = QDir(_workspace->root()).relativeFilePath(workDir);
    if (relDir.isEmpty() || relDir == QString::fromUtf8(".") || !_workspace->contains(workDir))
        relDir = workDir;

    const QString programKey = commandProgramKey(command);
    const QString action = QString::fromUtf8(
        "Run command in directory %1:\n%2")
                               .arg(relDir, command);

    AppSettings *appSettings = AppSettings::instance();
    const bool rememberable = !programKey.isEmpty();
    QString decision;
    if (rememberable && appSettings->isCommandAlwaysAllowed(programKey))
        decision = QString::fromUtf8("approved: always");
    else if (system && appSettings->systemSettingsControlEnabled())
        decision = _owner->askPermission(action, rememberable, programKey);
    else if (outside && appSettings->outsideFilesControlEnabled()
             && appSettings->shellControlEnabled())
    {
        // «Вне проекта» для shell имеет смысл только когда включён общий
        // контроль командной строки; иначе срабатывало на конвейеры и env.
        decision = _owner->askPermission(action, rememberable, programKey);
    }
    else if (appSettings->shellControlEnabled())
        decision = _owner->askPermission(action, rememberable, programKey);
    else
        decision = QString::fromUtf8("approved");
    if (!decision.startsWith(QString::fromUtf8("approved")))
    {
        if (ok)
            *ok = true;
        return QString::fromUtf8("Command was not executed. User answer: %1")
            .arg(decision);
    }

    _interrupted = false;
    QProcess process;
    process.setWorkingDirectory(workDir);
    process.setProcessChannelMode(QProcess::MergedChannels);
    // Пустой stdin: иначе любой вопрос «Overwrite? (Yes/No)», pause, set /p,
    // запрос пароля и т.п. молча ждут ввода до самого таймаута.
    process.setStandardInputFile(QProcess::nullDevice());
    _runningProcess = &process;
    // Фоновый запуск: cmd отдаёт программу системе через start и сразу выходит,
    // а мы возвращаемся к модели, не дожидаясь завершения GUI/сервера.
    QString shellCommand = command;
#ifdef Q_OS_WIN
    if (background)
        shellCommand = QString::fromUtf8("start \"\" %1").arg(command);
    process.start(QString::fromUtf8("cmd.exe"),
                  QStringList()
                      << QString::fromUtf8("/s")
                      << QString::fromUtf8("/c")
                      << shellCommand);
#else
    if (background)
        shellCommand = QString::fromUtf8("(%1) >/dev/null 2>&1 &").arg(command);
    process.start(QString::fromUtf8("/bin/sh"),
                  QStringList()
                      << QString::fromUtf8("-c")
                      << shellCommand);
#endif
    if (process.state() != QProcess::Running)
    {
        QEventLoop startLoop;
        QTimer startTimeout;
        startTimeout.setSingleShot(true);
        QObject::connect(&process, SIGNAL(started()), &startLoop, SLOT(quit()));
        QObject::connect(&process, SIGNAL(error(QProcess::ProcessError)),
                         &startLoop, SLOT(quit()));
        QObject::connect(&startTimeout, SIGNAL(timeout()), &startLoop, SLOT(quit()));
        startTimeout.start(15000);
        _runningLoop = &startLoop;
        if (process.state() == QProcess::Starting && !_interrupted)
            startLoop.exec();
        _runningLoop = 0;
    }
    if (_interrupted)
    {
        _runningProcess = 0;
        if (ok)
            *ok = false;
        return QString::fromUtf8("STOPPED: the user interrupted the command.");
    }
    if (process.state() != QProcess::Running)
    {
        _runningProcess = 0;
        if (ok)
            *ok = false;
        return QString::fromUtf8("Failed to start the command: %1")
            .arg(process.errorString());
    }

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&process, SIGNAL(finished(int,QProcess::ExitStatus)),
                     &loop, SLOT(quit()));
    QObject::connect(&process, SIGNAL(error(QProcess::ProcessError)),
                     &loop, SLOT(quit()));
    QObject::connect(&timeout, SIGNAL(timeout()), &loop, SLOT(quit()));
    timeout.start(timeoutSec * 1000);
    _runningLoop = &loop;
    if (process.state() != QProcess::NotRunning && !_interrupted)
        loop.exec();
    _runningLoop = 0;
    _runningProcess = 0;
    if (_interrupted)
    {
        if (process.state() != QProcess::NotRunning)
            process.kill();
        if (ok)
            *ok = false;
        return QString::fromUtf8("STOPPED: the user interrupted the command.");
    }
    const bool finished = (process.state() == QProcess::NotRunning);
    if (!finished)
    {
        killProcessTree(&process);
        QEventLoop killLoop;
        QObject::connect(&process, SIGNAL(finished(int,QProcess::ExitStatus)),
                         &killLoop, SLOT(quit()));
        QTimer::singleShot(5000, &killLoop, SLOT(quit()));
        if (process.state() != QProcess::NotRunning)
            killLoop.exec();
    }

    QByteArray bytes = process.readAll();
    QString output = ProjectWorkspace::decodeConsoleOutput(bytes);

    // Полный лог — в ___project___/tmp; в ответ модели — exit + file:line и ссылка.
    QString logRel;
    if (_workspace && _workspace->ensureTempDir() && !output.isEmpty())
    {
        const QString stamp = QDateTime::currentDateTime().toString(
            QString::fromUtf8("yyyyMMdd_hhmmss_zzz"));
        const QString fileName = QString::fromUtf8("cmd_%1.log").arg(stamp);
        const QString absLog = QDir(_workspace->tempDir()).filePath(fileName);
        QFile logFile(absLog);
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            logFile.write(output.toUtf8());
            logFile.close();
            logRel = QString::fromUtf8("___project___/tmp/%1").arg(fileName);
        }
    }

    QString replyBody;
    {
        const QStringList allLines = output.split(QChar('\n'));
        QStringList hits;
        QRegExp msvc(QString::fromUtf8("\\([^\\)]*\\d+[^\\)]*\\)"));
        QRegExp unixLike(QString::fromUtf8(":[0-9]+(?::[0-9]+)?"));
        foreach (const QString &line, allLines)
        {
            const QString t = line.trimmed();
            if (t.isEmpty())
                continue;
            if (msvc.indexIn(t) >= 0 || unixLike.indexIn(t) >= 0)
                hits.append(t);
        }
        if (!hits.isEmpty())
        {
            replyBody = QString::fromUtf8("file:line hits (%1):\n").arg(hits.size());
            int used = replyBody.size();
            const int budget = ProjectWorkspace::maxCommandReplyChars;
            for (int i = 0; i < hits.size(); ++i)
            {
                const QString row = hits.at(i) + QChar('\n');
                if (used + row.size() > budget)
                {
                    replyBody += QString::fromUtf8("... [%1 more hits in full log]\n")
                                     .arg(hits.size() - i);
                    break;
                }
                replyBody += row;
                used += row.size();
            }
        }
        else
        {
            // Нет file:line — короткий превью начала/конца.
            const int budget = ProjectWorkspace::maxCommandReplyChars;
            if (output.size() <= budget)
                replyBody = output;
            else
            {
                const int head = budget * 5 / 8;
                const int tail = budget - head;
                replyBody = output.left(head)
                            + QString::fromUtf8("\n...[%1 chars omitted]...\n")
                                  .arg(output.size() - head - tail)
                            + output.right(tail);
            }
        }
    }
    if (replyBody.trimmed().isEmpty())
        replyBody = QString::fromUtf8("(no output)");
    if (!logRel.isEmpty())
    {
        replyBody += QString::fromUtf8(
                         "\nfull log: %1\n"
                         "Continue with ONE call: read_file path=\"%1\" offset=1")
                         .arg(logRel);
    }

    QString report;
    if (!finished)
    {
        report = QString::fromUtf8("TIMEOUT after %1 seconds; the process tree was killed. "
                                     "The command did not finish on its own: it is either a "
                                     "long-running program (GUI, server, watcher — rerun with "
                                     "background=true), or it waited for input (stdin is empty; "
                                     "add flags like /y, -y, --yes). Partial output:\n%2")
                     .arg(timeoutSec)
                     .arg(replyBody);
        if (ok)
            *ok = false;
        return report;
    }

    const int code = process.exitCode();
    if (background && code == 0)
    {
        report = QString::fromUtf8("Started in background (detached), not waiting for it "
                                     "to finish. exit_code=0\n%1")
                     .arg(replyBody);
        if (ok)
            *ok = true;
        return report;
    }
    report = QString::fromUtf8("exit_code=%1\n%2").arg(code).arg(replyBody);
    if (ok)
        *ok = (code == 0);
    return report;
}

//----------------------------------------------

AddGroupMemberTool::AddGroupMemberTool(WorkerManager *manager, Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _manager(manager)
    , _owner(owner)
{
}

//----------------------------------------------

QString AddGroupMemberTool::name() const
{
    return QString::fromUtf8("add_group_member");
}

//----------------------------------------------

QString AddGroupMemberTool::description() const
{
    return QString::fromUtf8(
        "Add an interlocutor to your discussion group (creates the group if needed). "
        "Managers may form a group at their discretion for thorough study of a hard/"
        "ambiguous question before coding. "
        "Pick preset_id from available presets — prefer diverse, capable models for "
        "debate; match each member's purpose to the question. "
        "deep discussion; avoid duplicate profiles. "
        "Each group member MUST use a different model profile. "
        "The group uses turn-based dialogue (group_speak / group_await / vote); "
        "peers cannot delegate_task.");
}

//----------------------------------------------

QJsonObject AddGroupMemberTool::parametersSchema() const
{
    QJsonObject presetId;
    presetId.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    presetId.insert(QString::fromUtf8("description"),
                    QString::fromUtf8("Id of a worker preset from settings."));
    QJsonObject properties;
    properties.insert(QString::fromUtf8("preset_id"), presetId);
    QJsonArray required;
    required.append(QString::fromUtf8("preset_id"));
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString AddGroupMemberTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    const QString presetId = args.value(QString::fromUtf8("preset_id")).toString();
    Worker *peer = _manager ? _manager->addGroupMember(_owner, presetId, &error) : 0;
    if (!peer)
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (ok)
        *ok = true;
    return QString::fromUtf8(
        "Added group member %1 (type=%2, worker_id=%3, group_id=%4). "
        "Turn-based dialogue started: narrator uses group_speak/group_end_turn; "
        "others group_await; finish with group_propose + unanimous group_vote.")
        .arg(peer->personName(), workerTypeToString(peer->type()), peer->id(),
             peer->groupId());
}

//----------------------------------------------

TalkToTool::TalkToTool(WorkerManager *manager, Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _manager(manager)
    , _owner(owner)
{
}

//----------------------------------------------

QString TalkToTool::name() const
{
    return QString::fromUtf8("talk_to");
}

//----------------------------------------------

QString TalkToTool::description() const
{
    return QString::fromUtf8(
        "DISABLED. Do not call. Outside a group workers do not discuss peers "
        "(parent assignment / complete_task / delegate_task only). "
        "Inside a group use group_speak / group_await / group_propose / group_vote.");
}

//----------------------------------------------

QJsonObject TalkToTool::parametersSchema() const
{
    QJsonObject workerId;
    workerId.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    workerId.insert(QString::fromUtf8("description"), QString::fromUtf8("Target worker_id."));
    QJsonObject message;
    message.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    message.insert(QString::fromUtf8("description"), QString::fromUtf8("Message text."));
    QJsonObject properties;
    properties.insert(QString::fromUtf8("worker_id"), workerId);
    properties.insert(QString::fromUtf8("message"), message);
    QJsonArray required;
    required.append(QString::fromUtf8("worker_id"));
    required.append(QString::fromUtf8("message"));
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString TalkToTool::invoke(QString argumentsJson, bool *ok)
{
    Q_UNUSED(argumentsJson);
    if (ok)
        *ok = false;
    return QString::fromUtf8(
        "REFUSED: talk_to is disabled. Outside a group do not discuss peers — "
        "receive the assignment from the parent and report via complete_task; "
        "managers use delegate_task. Group members use group_speak / group_await / "
        "group_end_turn / group_propose / group_vote.");
}

//----------------------------------------------

GroupAwaitTool::GroupAwaitTool(WorkerManager *manager, Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _manager(manager)
    , _owner(owner)
{
}

//----------------------------------------------

QString GroupAwaitTool::name() const
{
    return QString::fromUtf8("group_await");
}

//----------------------------------------------

QString GroupAwaitTool::description() const
{
    return QString::fromUtf8(
        "If you are NOT the narrator and do not need to vote: call once to park until a "
        "follow-up wakes you for your turn. Non-blocking (does not freeze other members). "
        "If you ARE the narrator or must vote, returns ACTION REQUIRED — speak/vote now, "
        "do NOT call group_await again to park.");
}

//----------------------------------------------

QJsonObject GroupAwaitTool::parametersSchema() const
{
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), QJsonObject());
    return schema;
}

//----------------------------------------------

QString GroupAwaitTool::invoke(QString argumentsJson, bool *ok)
{
    Q_UNUSED(argumentsJson);
    QString error;
    const QString report = _manager ? _manager->groupAwait(_owner, &error) : QString();
    if (report.isEmpty())
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (ok)
        *ok = true;
    return report;
}

//----------------------------------------------

GroupSpeakTool::GroupSpeakTool(WorkerManager *manager, Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _manager(manager)
    , _owner(owner)
{
}

//----------------------------------------------

QString GroupSpeakTool::name() const
{
    return QString::fromUtf8("group_speak");
}

//----------------------------------------------

QString GroupSpeakTool::description() const
{
    return QString::fromUtf8(
        "As the current narrator, broadcast a replica to ALL group members. "
        "Then call group_end_turn() or group_propose(result).");
}

//----------------------------------------------

QJsonObject GroupSpeakTool::parametersSchema() const
{
    QJsonObject message;
    message.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    message.insert(QString::fromUtf8("description"), QString::fromUtf8("What you say to the group."));
    QJsonObject properties;
    properties.insert(QString::fromUtf8("message"), message);
    QJsonArray required;
    required.append(QString::fromUtf8("message"));
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString GroupSpeakTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    const QString message = args.value(QString::fromUtf8("message")).toString();
    const QString report = _manager ? _manager->groupSpeak(_owner, message, &error)
                                    : QString();
    if (report.isEmpty())
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (ok)
        *ok = true;
    return report;
}

//----------------------------------------------

GroupEndTurnTool::GroupEndTurnTool(WorkerManager *manager, Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _manager(manager)
    , _owner(owner)
{
}

//----------------------------------------------

QString GroupEndTurnTool::name() const
{
    return QString::fromUtf8("group_end_turn");
}

//----------------------------------------------

QString GroupEndTurnTool::description() const
{
    return QString::fromUtf8(
        "Pass the narrator role to the next group member after group_speak.");
}

//----------------------------------------------

QJsonObject GroupEndTurnTool::parametersSchema() const
{
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), QJsonObject());
    return schema;
}

//----------------------------------------------

QString GroupEndTurnTool::invoke(QString argumentsJson, bool *ok)
{
    Q_UNUSED(argumentsJson);
    QString error;
    const QString report = _manager ? _manager->groupEndTurn(_owner, &error) : QString();
    if (report.isEmpty())
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (ok)
        *ok = true;
    return report;
}

//----------------------------------------------

GroupProposeTool::GroupProposeTool(WorkerManager *manager, Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _manager(manager)
    , _owner(owner)
{
}

//----------------------------------------------

QString GroupProposeTool::name() const
{
    return QString::fromUtf8("group_propose");
}

//----------------------------------------------

QString GroupProposeTool::description() const
{
    return QString::fromUtf8(
        "Propose the group's shared result for a unanimous vote. "
        "Current narrator or chief. Others must group_vote.");
}

//----------------------------------------------

QJsonObject GroupProposeTool::parametersSchema() const
{
    QJsonObject result;
    result.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    result.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("Concrete shared conclusion for the group to approve."));
    QJsonObject properties;
    properties.insert(QString::fromUtf8("result"), result);
    QJsonArray required;
    required.append(QString::fromUtf8("result"));
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString GroupProposeTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    const QString result = args.value(QString::fromUtf8("result")).toString();
    const QString report = _manager ? _manager->groupPropose(_owner, result, &error)
                                    : QString();
    if (report.isEmpty())
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (ok)
        *ok = true;
    return report;
}

//----------------------------------------------

GroupVoteTool::GroupVoteTool(WorkerManager *manager, Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _manager(manager)
    , _owner(owner)
{
}

//----------------------------------------------

QString GroupVoteTool::name() const
{
    return QString::fromUtf8("group_vote");
}

//----------------------------------------------

QString GroupVoteTool::description() const
{
    return QString::fromUtf8(
        "Vote on the current group_propose. approve=true to accept; false rejects and "
        "continues the turn-based discussion. Unanimous approval ends the dialogue.");
}

//----------------------------------------------

QJsonObject GroupVoteTool::parametersSchema() const
{
    QJsonObject approve;
    approve.insert(QString::fromUtf8("type"), QString::fromUtf8("boolean"));
    approve.insert(QString::fromUtf8("description"),
                   QString::fromUtf8("true = approve proposal; false = reject."));
    QJsonObject comment;
    comment.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    comment.insert(QString::fromUtf8("description"),
                   QString::fromUtf8("Short reason, especially on reject."));
    QJsonObject properties;
    properties.insert(QString::fromUtf8("approve"), approve);
    properties.insert(QString::fromUtf8("comment"), comment);
    QJsonArray required;
    required.append(QString::fromUtf8("approve"));
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString GroupVoteTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseObject(argumentsJson, &args, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    const bool approve = args.value(QString::fromUtf8("approve")).toBool();
    const QString comment = args.value(QString::fromUtf8("comment")).toString();
    const QString report = _manager ? _manager->groupVote(_owner, approve, comment, &error)
                                    : QString();
    if (report.isEmpty())
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (ok)
        *ok = true;
    return report;
}

//----------------------------------------------

ListGroupMembersTool::ListGroupMembersTool(WorkerManager *manager, Worker *owner,
                                           QObject *parent)
    : AgentTool(parent)
    , _manager(manager)
    , _owner(owner)
{
}

//----------------------------------------------

QString ListGroupMembersTool::name() const
{
    return QString::fromUtf8("list_group_members");
}

//----------------------------------------------

QString ListGroupMembersTool::description() const
{
    return QString::fromUtf8(
        "List members of your discussion group (name, type, role, worker_id, state).");
}

//----------------------------------------------

QJsonObject ListGroupMembersTool::parametersSchema() const
{
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), QJsonObject());
    return schema;
}

//----------------------------------------------

QString ListGroupMembersTool::invoke(QString argumentsJson, bool *ok)
{
    Q_UNUSED(argumentsJson);
    if (!_owner || _owner->groupId().isEmpty())
    {
        if (ok)
            *ok = true;
        return QString::fromUtf8("You are not in a group yet. Managers: add_group_member.");
    }
    QStringList lines;
    lines << QString::fromUtf8("group_id=%1").arg(_owner->groupId());
    foreach (Worker *peer, _manager->groupMembers(_owner->groupId()))
    {
        if (!peer)
            continue;
        lines << QString::fromUtf8("- %1 | type=%2 | role=%3 | worker_id=%4 | state=%5")
                     .arg(peer->personName().isEmpty() ? QString::fromUtf8("(unnamed)")
                                                       : peer->personName(),
                          workerTypeToString(peer->type()),
                          groupRoleToString(peer->groupRole()), peer->id(),
                          workerStateToString(peer->state()));
    }
    if (ok)
        *ok = true;
    return lines.join(QChar('\n'));
}
