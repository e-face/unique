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
#include <QElapsedTimer>
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

static bool isWebLookupProgram(QString key)
{
    key = key.toLower();
    static const char *names[] = {
        "curl", "wget", "w3m", "lynx", "links", "elinks",
        "aria2c", "httpie", "xh",
        "iwr", "irm", "invoke-webrequest", "invoke-restmethod",
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

static bool isNonSearchNetProgram(QString key)
{
    key = key.toLower();
    static const char *names[] = {
        "git", "svn", "hg",
        "npm", "npx", "yarn", "pnpm", "composer", "bundle", "gem",
        "pip", "pip3", "conda",
        "cargo", "go", "dotnet", "nuget", "mvn", "gradle",
        "docker", "podman",
        "winget", "choco", "chocolatey", "scoop",
        "cmake", "msbuild", "vcpkg",
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

static QString httpUrlHost(QString token)
{
    const QString lower = token.toLower();
    int at = lower.indexOf(QString::fromUtf8("https://"));
    int skip = 8;
    const int httpAt = lower.indexOf(QString::fromUtf8("http://"));
    if (at < 0 || (httpAt >= 0 && httpAt < at))
    {
        at = httpAt;
        skip = 7;
    }
    if (at < 0)
        return QString();
    QString host = lower.mid(at + skip);
    for (int i = 0; i < host.size(); ++i)
    {
        const QChar c = host.at(i);
        if (c == QChar('/') || c == QChar('\\') || c == QChar('?')
            || c == QChar('#') || c == QChar(':'))
        {
            host = host.left(i);
            break;
        }
    }
    if (host.startsWith(QChar('[')))
    {
        const int end = host.indexOf(QChar(']'));
        if (end > 0)
            host = host.mid(1, end - 1);
    }
    return host;
}

//----------------------------------------------

static bool isLocalWebHost(QString host)
{
    host = host.trimmed().toLower();
    if (host.isEmpty())
        return true;
    if (host == QString::fromUtf8("localhost")
        || host == QString::fromUtf8("127.0.0.1")
        || host == QString::fromUtf8("::1")
        || host == QString::fromUtf8("0.0.0.0"))
        return true;
    if (host.startsWith(QString::fromUtf8("127.")))
        return true;
    if (host.endsWith(QString::fromUtf8(".localhost")))
        return true;
    return false;
}

//----------------------------------------------

static bool tokenHasHttpUrl(QString token)
{
    const QString lower = token.toLower();
    return lower.contains(QString::fromUtf8("https://"))
        || lower.contains(QString::fromUtf8("http://"));
}

//----------------------------------------------

static bool tokenHasRemoteHttpUrl(QString token)
{
    const QString host = httpUrlHost(token);
    if (host.isEmpty())
        return false;
    return !isLocalWebHost(host);
}

//----------------------------------------------

static bool commandSearchesInternet(QString command)
{
    const QStringList segments = commandSegments(command);
    foreach (const QString &segment, segments)
    {
        const QString key = commandProgramKey(segment).toLower();
        if (isNonSearchNetProgram(key))
            continue;
        const QStringList tokens = splitCommandTokens(segment);
        bool remote = false;
        bool anyUrl = false;
        foreach (const QString &token, tokens)
        {
            if (!tokenHasHttpUrl(token))
                continue;
            anyUrl = true;
            if (tokenHasRemoteHttpUrl(token))
                remote = true;
        }
        if (isWebLookupProgram(key))
        {
            if (anyUrl && !remote)
                continue;
            return true;
        }
        if (remote)
            return true;
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
        "Record YOUR plan, then carry it out. Call list_tools and list_worker_presets first. "
        "Save tokens and time. Default every part to how=self and do it yourself. "
        "how=delegate or how=tool only for a complex task where you judge that a child "
        "or a tool will cost less than doing the work here. You decide; a short or "
        "ordinary task is one how=self subtask. After the plan is accepted, do the work "
        "in this same turn.");
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
    QJsonObject how;
    how.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    how.insert(QString::fromUtf8("description"),
               QString::fromUtf8(
                   "self — you do this part (default; saves tokens and time). "
                   "delegate — a child, only if you judge that split cheaper. "
                   "tool — a repeated utilitarian step, only if a tool is cheaper "
                   "than doing it inline."));
    QJsonObject reason;
    reason.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    reason.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("Why this is simple/atomic or still complex."));

    QJsonObject subtaskProps;
    subtaskProps.insert(QString::fromUtf8("name"), name);
    subtaskProps.insert(QString::fromUtf8("task"), task);
    subtaskProps.insert(QString::fromUtf8("how"), how);
    subtaskProps.insert(QString::fromUtf8("reason"), reason);
    QJsonArray subtaskRequired;
    subtaskRequired.append(QString::fromUtf8("name"));
    subtaskRequired.append(QString::fromUtf8("task"));
    subtaskRequired.append(QString::fromUtf8("how"));
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
    if (!_owner->listedTools() || !_owner->listedPresets())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8(
            "Call list_tools and list_worker_presets first, then plan_work.");
    }

    const QJsonArray raw = args.value(QString::fromUtf8("subtasks")).toArray();
    QList<WorkSubtask> items;
    for (int i = 0; i < raw.size(); ++i)
    {
        const QJsonObject obj = raw.at(i).toObject();
        WorkSubtask item;
        item.name = obj.value(QString::fromUtf8("name")).toString();
        item.task = obj.value(QString::fromUtf8("task")).toString();
        item.how = obj.value(QString::fromUtf8("how")).toString();
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
    QString report = QString::fromUtf8(
        "Plan accepted. Carry it out now, in this turn. "
        "Children and tools only where you marked them; do the rest yourself. "
        "Do not stop with a status message.\n");
    if (manager)
        report += manager->loadSummary() + QString::fromUtf8("\n");
    foreach (const WorkSubtask &item, _owner->workPlan())
    {
        report += QString::fromUtf8("- %1 -> %2\n")
                      .arg(item.name.trimmed(), item.how);
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
        "Optional. You decide whether a child is worth the tokens and time. "
        "Use it only for a complex part that is cheaper as its own chat: "
        "why=independent (large piece that does not need the other parts' text; "
        "because says what it owns and returns), why=context (this part does not "
        "fit one chat; pass because), why=other_modality (another model API; pass "
        "preset_id), why=check (the user asked for an independent check; "
        "check_only=true). A lookup, a list, or one piece stays here. "
        "Prefer worker_id of a child you already have, including COMPLETED children, for "
        "follow-ups of the same part. A repeated command is create_tool/run_tool, not "
        "another executor. Pass preset_id to match modality AND purpose. Several calls "
        "run in parallel; you continue after ALL children of the batch answer.");
}

//----------------------------------------------

QJsonObject DelegateTaskTool::parametersSchema() const
{
    QJsonObject type;
    type.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    type.insert(QString::fromUtf8("description"),
                QString::fromUtf8("Must be executor."));

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
                   QString::fromUtf8("Optional label. Not required."));

    QJsonObject why;
    why.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    why.insert(QString::fromUtf8("description"),
               QString::fromUtf8(
                   "Required for a new child: other_modality, context, independent, or check. "
                   "Omit only when reusing worker_id."));

    QJsonObject because;
    because.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    because.insert(QString::fromUtf8("description"),
                   QString::fromUtf8(
                       "For context or independent: what this part owns, what it must return, "
                       "and why it does not fit this chat or does not need the other parts."));

    QJsonObject checkOnly;
    checkOnly.insert(QString::fromUtf8("type"), QString::fromUtf8("boolean"));
    checkOnly.insert(QString::fromUtf8("description"),
                     QString::fromUtf8(
                         "True only for why=check. The child cannot edit files or spawn."));

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
    properties.insert(QString::fromUtf8("why"), why);
    properties.insert(QString::fromUtf8("because"), because);
    properties.insert(QString::fromUtf8("check_only"), checkOnly);

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
    const QString blocked = _owner->openingBlockReason();
    if (!blocked.isEmpty())
    {
        if (ok)
            *ok = false;
        return blocked;
    }

    const QString typeName = args.value(QString::fromUtf8("type")).toString().trimmed().toLower();
    if (typeName == QString::fromUtf8("tool"))
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8(
            "Tools are not children. Use create_tool and run_tool.");
    }
    if (!typeName.isEmpty() && !isKnownWorkerTypeName(typeName))
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Invalid worker type. Use executor.");
    }
    const WorkerType type = WorkerTypeExecutor;
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
        if (preset.type == WorkerTypeTool)
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8(
                "Tool presets are for create_tool / run_tool, not delegate_task.");
        }
        if (normalizeWorkerType(preset.type) != type)
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8(
                "preset_id=%1 is type=%2, but delegate_task needs an executor.")
                .arg(presetId, workerTypeToString(preset.type));
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
        _owner->noteWorkAction();
        _owner->markSubtaskDelegated(subtaskName);
        _owner->markSubtaskDelegated(title);
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

    QString why = args.value(QString::fromUtf8("why")).toString().trimmed().toLower();
    const QString because = args.value(QString::fromUtf8("because")).toString().trimmed();
    bool checkOnly = args.value(QString::fromUtf8("check_only")).toBool(false);
    if (checkOnly && why.isEmpty())
        why = QString::fromUtf8("check");
    if (why == QString::fromUtf8("check"))
        checkOnly = true;
    if (why != QString::fromUtf8("other_modality")
        && why != QString::fromUtf8("context")
        && why != QString::fromUtf8("independent")
        && why != QString::fromUtf8("check"))
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8(
            "A new child needs why=other_modality, context, independent, or check. "
            "Do the work yourself when none of these apply. "
            "Reuse worker_id for a follow-up of a child you already have.");
    }
    if ((why == QString::fromUtf8("context") || why == QString::fromUtf8("independent"))
        && because.size() < 20)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8(
            "why=%1 needs because: what this part owns, what it must return, "
            "and why it does not fit this chat or does not need the other parts.")
            .arg(why);
    }
    if (why == QString::fromUtf8("other_modality"))
    {
        if (presetId.isEmpty())
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8(
                "other_modality needs preset_id of the other model "
                "(image, audio, speech, video, embeddings, decisions, ...).");
        }
        const LlmProfile childProfile = AppSettings::instance()->profileById(llmProfileId);
        const WorkerTaskKind childKind = childProfile.id.isEmpty()
                                         ? WorkerTaskKindLlm
                                         : childProfile.modality;
        if (childKind == _manager->modalityOf(_owner))
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8(
                "That preset has the same modality as you. "
                "Use why=context or why=independent, or pick a preset of another modality.");
        }
    }
    if (checkOnly && !presetId.isEmpty())
    {
        const LlmProfile childProfile = AppSettings::instance()->profileById(llmProfileId);
        if (childProfile.id.isEmpty()
            || workerTaskKindIsMediaExecutor(childProfile.modality))
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8(
                "check_only needs an LLM preset, not a media generator.");
        }
    }

    Worker *spawned = _manager->spawn(_owner, type, task, &error, llmProfileId, checkOnly);
    if (!spawned)
    {
        if (ok)
            *ok = false;
        return error;
    }
    spawned->setTitle(title);

    _owner->registerDelegated(spawned);
    _owner->noteWorkAction();
    _owner->markSubtaskDelegated(subtaskName);
    _owner->markSubtaskDelegated(title);
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
        "Report YOUR finished part to the parent. Only a child has this tool. "
        "The top executor answers the user and does not call it. "
        "Call it when your part is done, not to ask the parent to finish it. "
        "A check-only executor must pass verified plus structured evidence "
        "{command, output, artifact?, digest?} and must not have edited the project. "
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
                        "true if the checks you ran passed; false if they failed. Required "
                        "for testers. Optional for executors."));
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
                        "Structured proof. Required with verified for testers. Optional "
                        "for an executor smoke check."));
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

static bool reportHandsWorkUpward(const QString &result)
{
    const QString text = result.toLower();
    const QStringList needles = QStringList()
        << QString::fromUtf8("рекомендую родител")
        << QString::fromUtf8("рекомендую пользовател")
        << QString::fromUtf8("recommend the parent")
        << QString::fromUtf8("recommend the user")
        << QString::fromUtf8("ask the parent")
        << QString::fromUtf8("ask the user to")
        << QString::fromUtf8("the parent should")
        << QString::fromUtf8("the user should")
        << QString::fromUtf8("родитель должен")
        << QString::fromUtf8("пользователь должен");
    foreach (const QString &needle, needles)
    {
        if (text.contains(needle))
            return true;
    }
    return false;
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
    const QString blocked = _owner->openingBlockReason();
    if (!blocked.isEmpty())
    {
        if (ok)
            *ok = false;
        return blocked;
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

    // Сдача своей части — не просьба родителю или пользователю доделать её.
    if (!_owner->isCheckOnly() && reportHandsWorkUpward(result))
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8(
            "REFUSED: your part is not finished. Report the outcome of the part "
            "you were given. Do not tell the parent or the user to do the rest. "
            "The top executor answers the user; a child only reports its own "
            "finished part.");
    }

    // Независимая проверка закрывается только со структурированным свидетельством.
    if (_owner->isCheckOnly())
    {
        if (!hasVerified || !evidenceOk)
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8(
                "REFUSED (verification gate): a check-only executor must pass verified "
                "and structured evidence {command, output} (output >= 20 chars). "
                "Optional artifact/digest.");
        }
    }
    bool success = true;
    if (_owner->isCheckOnly() || hasVerified)
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
        "Required before any internet search when you will not run that search "
        "yourself. The dialog offers Always, same as a shell command: if the user "
        "grants it, later searches with that program are not asked again. "
        "Also use for delete, rename, overwrite, or other destructive file changes. "
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
        "Optional. You decide whether a tool is worth the tokens and time. "
        "Create one only for a narrow utilitarian job that will repeat enough to be "
        "cheaper than doing it inline (fetch, parse, build, lint, generate, convert, "
        "batch edits). Do not create a tool for a one-off step and do not spawn an "
        "executor for a repetition. Pass preset_id to bind the model: match "
        "modality AND purpose from the "
        "presets list (e.g. text-to-image vs object segmentation). Without preset_id the "
        "default Tool LLM profile is used (cannot generate images). Call list_tools first. "
        "Only the user can delete tools.");
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
    const QString blocked = _owner->openingBlockReason();
    if (!blocked.isEmpty())
    {
        if (ok)
            *ok = false;
        return blocked;
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

    _owner->noteWorkAction();
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
    , _owner(0)
{
}

//----------------------------------------------

void ListToolsTool::setOwner(Worker *owner)
{
    _owner = owner;
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
        "List shared tools: tool_id, name, busy/free, queue, purpose. "
        "Call this before plan_work. A repeated utilitarian step may become a tool "
        "when you judge that cheaper; you decide.");
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
    if (_owner)
        _owner->noteListedTools();
    if (ok)
        *ok = true;
    return _manager->toolsSummary();
}

//----------------------------------------------

ListWorkerPresetsTool::ListWorkerPresetsTool(WorkerManager *manager, Worker *owner,
                                             QObject *parent)
    : AgentTool(parent)
    , _manager(manager)
    , _owner(owner)
{
}

//----------------------------------------------

QString ListWorkerPresetsTool::name() const
{
    return QString::fromUtf8("list_worker_presets");
}

//----------------------------------------------

QString ListWorkerPresetsTool::description() const
{
    return QString::fromUtf8(
        "List the worker presets you can create: preset_id, type, modality, model, purpose. "
        "Call this before plan_work, together with list_tools. "
        "Creating a child or a tool is your decision and only worth it when it "
        "saves tokens and time. Pass preset_id if you do.");
}

//----------------------------------------------

QJsonObject ListWorkerPresetsTool::parametersSchema() const
{
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), QJsonObject());
    return schema;
}

//----------------------------------------------

QString ListWorkerPresetsTool::invoke(QString, bool *ok)
{
    if (!_manager)
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("Worker manager is missing.");
    }
    if (_owner)
        _owner->noteListedPresets();
    if (ok)
        *ok = true;
    return AppSettings::instance()->workerPresetsSummary();
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
        "An llm tool gets a plain-language task. "
        "An image, audio, video, speech or embedding tool is not a chat model. "
        "request is one JSON object. The keys are that tool's params from list_tools "
        "(the same list is on the preset line). Set required keys. Set any other key "
        "only when the task asks for that setting. prompt or text is the content, "
        "not an instruction to save a file. output_path is the new file. "
        "reference_path is an existing file and only when params include it.");
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
                       "For an llm tool: what to do. For an image/audio/video/speech/"
                       "embedding tool: one JSON object whose keys are that tool's "
                       "params from list_tools. Not a chat instruction."));

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
    const QString blocked = _owner->openingBlockReason();
    if (!blocked.isEmpty())
    {
        if (ok)
            *ok = false;
        return blocked;
    }
    _owner->noteWorkAction();
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
        "the project). Any internet search asks the user every time, with no 'always' "
        "option. working_directory is relative to the project; timeout_sec default 120, "
        "max 300. If the program waits for a line (Y/N, pause, set /p), the last lines "
        "are shown to the user and their answer is written to stdin. Do not pipe through "
        "more. For GUI/servers set background=true. Scratch output in ___project___/tmp, "
        "not project-root tmp/.");
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

    // Стоп прошлого вызова не должен отменять эту команду, не запустив её.
    _interrupted = false;

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

static bool looksLikeConsolePrompt(const QString &output)
{
    if (output.isEmpty())
        return false;
    // Вопрос почти всегда без завершающего перевода строки.
    // «file.cpp:» в конце полной строки — обычный вывод компилятора.
    const bool incomplete = !output.endsWith(QChar('\n'))
                            && !output.endsWith(QChar('\r'));
    QString trimmed = output;
    while (trimmed.endsWith(QChar('\n')) || trimmed.endsWith(QChar('\r')))
        trimmed.chop(1);
    trimmed = trimmed.trimmed();
    if (trimmed.isEmpty())
        return false;
    const int nl = trimmed.lastIndexOf(QChar('\n'));
    const QString last = (nl >= 0 ? trimmed.mid(nl + 1) : trimmed).trimmed();
    if (last.size() < 1 || last.size() > 240)
        return false;
    const QString low = last.toLower();
    if (low.contains(QString::fromUtf8("y/n"))
        || low.contains(QString::fromUtf8("yes/no"))
        || low.contains(QString::fromUtf8("press any key"))
        || low.contains(QString::fromUtf8("нажмите"))
        || low.contains(QString::fromUtf8("overwrite"))
        || low.contains(QString::fromUtf8("перезапис"))
        || low.contains(QString::fromUtf8("введите"))
        || low.contains(QString::fromUtf8("password"))
        || low.contains(QString::fromUtf8("парол")))
        return true;
    const QChar tail = last.at(last.size() - 1);
    if (tail == QChar('?') || tail == QChar('>'))
        return true;
    if (incomplete && (tail == QChar(':')
                       || low.contains(QString::fromUtf8("enter"))))
        return true;
    return false;
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
    const QString blocked = _owner->openingBlockReason();
    if (!blocked.isEmpty())
    {
        if (ok)
            *ok = false;
        return blocked;
    }
    _owner->noteWorkAction();

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
    const bool webSearch = commandSearchesInternet(command);
    const QString action = webSearch
        ? QString::fromUtf8("Internet search in directory %1:\n%2").arg(relDir, command)
        : QString::fromUtf8("Run command in directory %1:\n%2").arg(relDir, command);

    AppSettings *appSettings = AppSettings::instance();
    const bool rememberable = !programKey.isEmpty();
    QString decision;
    if (rememberable && appSettings->isCommandAlwaysAllowed(programKey))
        decision = QString::fromUtf8("approved: always");
    else if (webSearch)
        decision = _owner->askPermission(action, rememberable, programKey);
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
    // Фоновый запуск не читает ввод. У обычной команды stdin — канал:
    // вопрос в консоли показывается пользователю, ответ пишется сюда.
    if (background)
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
    QTimer poll;
    poll.setInterval(400);
    QObject::connect(&process, SIGNAL(finished(int,QProcess::ExitStatus)),
                     &loop, SLOT(quit()));
    QObject::connect(&process, SIGNAL(error(QProcess::ProcessError)),
                     &loop, SLOT(quit()));
    QObject::connect(&timeout, SIGNAL(timeout()), &loop, SLOT(quit()));
    QObject::connect(&poll, SIGNAL(timeout()), &loop, SLOT(quit()));
    timeout.start(timeoutSec * 1000);
    poll.start();
    _runningLoop = &loop;

    QByteArray bytes;
    QElapsedTimer idleClock;
    idleClock.start();
    bool promptArmed = true;
    while (process.state() != QProcess::NotRunning && !_interrupted)
    {
        loop.exec();
        if (_interrupted)
            break;
        const QByteArray chunk = process.readAll();
        if (!chunk.isEmpty())
        {
            bytes += chunk;
            idleClock.restart();
            promptArmed = true;
        }
        if (!timeout.isActive())
            break;
        if (background || !promptArmed || idleClock.elapsed() < 1200)
            continue;
        if (!_owner)
            continue;
        const QString soFar = ProjectWorkspace::decodeConsoleOutput(bytes);
        if (!looksLikeConsolePrompt(soFar))
            continue;
        promptArmed = false;
        timeout.stop();
        QString line;
        const bool sent = _owner->askConsoleInput(soFar, &line);
        if (_interrupted)
            break;
        if (sent && process.state() != QProcess::NotRunning)
        {
            QByteArray encoded = line.toLocal8Bit();
            encoded.append("\r\n");
            process.write(encoded);
            idleClock.restart();
        }
        timeout.start(timeoutSec * 1000);
    }
    poll.stop();
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

    bytes += process.readAll();
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
    if (code != 0)
    {
        const QString lowered = output.toLower();
        const bool notFound = (code == 9009)
            || lowered.contains(QString::fromUtf8("is not recognized"))
            || lowered.contains(QString::fromUtf8("не является внутренней"))
            || lowered.contains(QString::fromUtf8("command not found"))
            || lowered.contains(QString::fromUtf8("not recognized as the name"));
        if (notFound)
        {
            report += QString::fromUtf8(
                "\nThe program is not on PATH. Look for it on the machine and "
                "rerun by full path before you decide it is absent. "
                "Do not hand the rest of your part to someone else.");
        }
    }
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
