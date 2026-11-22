#include "settingstools.h"
//----------------------------------------------
#include "appsettings.h"
#include "mcphub.h"
#include "worker.h"
#include "workertypes.h"
//----------------------------------------------
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringList>

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

static bool resolveScope(const QJsonObject &args, SettingsKind *kind, QString *error)
{
    const QString scope = args.value(QString::fromUtf8("scope")).toString().trimmed().toLower();
    AppSettings *s = AppSettings::instance();
    if (scope.isEmpty())
    {
        *kind = s->hasProjectSettings() ? SettingsKindProject : SettingsKindDefaults;
        return true;
    }
    if (scope == QString::fromUtf8("program") || scope == QString::fromUtf8("app")
        || scope == QString::fromUtf8("defaults") || scope == QString::fromUtf8("global"))
    {
        *kind = SettingsKindDefaults;
        return true;
    }
    if (scope == QString::fromUtf8("project"))
    {
        if (!s->hasProjectSettings())
        {
            if (error)
                *error = QString::fromUtf8("No project is open; use scope=\"program\".");
            return false;
        }
        *kind = SettingsKindProject;
        return true;
    }
    if (error)
        *error = QString::fromUtf8("scope must be \"program\" or \"project\".");
    return false;
}

//----------------------------------------------

static QString scopeTitle(SettingsKind kind)
{
    return kind == SettingsKindProject ? QObject::tr("настройки проекта")
                                       : QObject::tr("настройки программы");
}

//----------------------------------------------

static QString scopeKey(SettingsKind kind)
{
    return kind == SettingsKindProject ? QString::fromUtf8("project")
                                       : QString::fromUtf8("program");
}

//----------------------------------------------

/** Value by a snake_case or camelCase key. */
static QJsonValue fieldValue(const QJsonObject &obj, const char *snake, const char *camel)
{
    if (obj.contains(QString::fromUtf8(snake)))
        return obj.value(QString::fromUtf8(snake));
    if (camel && obj.contains(QString::fromUtf8(camel)))
        return obj.value(QString::fromUtf8(camel));
    return QJsonValue(QJsonValue::Undefined);
}

//----------------------------------------------

static bool mentionsApiKey(const QJsonObject &obj)
{
    foreach (const QString &key, obj.keys())
    {
        const QString low = key.toLower();
        if (low == QString::fromUtf8("apikey") || low == QString::fromUtf8("api_key")
            || low == QString::fromUtf8("token") || low == QString::fromUtf8("key"))
            return true;
    }
    return false;
}

//----------------------------------------------

static QJsonObject profileToJson(const LlmProfile &p)
{
    QJsonObject o;
    o.insert(QString::fromUtf8("id"), p.id);
    o.insert(QString::fromUtf8("name"), p.name);
    o.insert(QString::fromUtf8("base_url"), p.baseUrl);
    o.insert(QString::fromUtf8("model"), p.model);
    o.insert(QString::fromUtf8("api_key_set"), !p.apiKey.trimmed().isEmpty());
    o.insert(QString::fromUtf8("modality"), workerTaskKindToString(p.modality));
    o.insert(QString::fromUtf8("purpose"), p.purpose);
    o.insert(QString::fromUtf8("balance_url"), p.balanceUrl);
    o.insert(QString::fromUtf8("balance_json_path"), p.balanceJsonPath);
    o.insert(QString::fromUtf8("request_path"), p.requestPath);
    o.insert(QString::fromUtf8("task_path"), p.taskPath);
    o.insert(QString::fromUtf8("catalog_url"), p.catalogUrl);
    o.insert(QString::fromUtf8("extra_body"), p.extraBody);
    o.insert(QString::fromUtf8("max_tool_iterations"), p.maxToolIterations);
    o.insert(QString::fromUtf8("context_cap"), p.contextCap);
    return o;
}

//----------------------------------------------

static QJsonObject presetToJson(const WorkerPreset &preset, const QString &rootId)
{
    QJsonObject o;
    o.insert(QString::fromUtf8("id"), preset.id);
    o.insert(QString::fromUtf8("type"), workerTypeToString(preset.type));
    o.insert(QString::fromUtf8("profile_id"), preset.profileId);
    o.insert(QString::fromUtf8("modality"), workerTaskKindToString(preset.taskKind));
    const LlmProfile profile = AppSettings::instance()->profileById(preset.profileId);
    o.insert(QString::fromUtf8("profile_name"), profile.name);
    o.insert(QString::fromUtf8("model"), profile.model);
    o.insert(QString::fromUtf8("is_root"), preset.id == rootId);
    return o;
}

//----------------------------------------------

static QJsonObject generalToJson(SettingsKind kind)
{
    AppSettings *s = AppSettings::instance();
    QJsonObject o;
    o.insert(QString::fromUtf8("max_worker_depth"), s->maxWorkerDepth(kind));
    o.insert(QString::fromUtf8("max_concurrent_workers"), s->maxConcurrentWorkers(kind));
    o.insert(QString::fromUtf8("llm_timeout_sec"), s->llmTimeoutSec(kind));
    o.insert(QString::fromUtf8("root_worker_preset_id"), s->rootWorkerPresetId(kind));
    o.insert(QString::fromUtf8("shell_control"), s->shellControlEnabled(kind));
    o.insert(QString::fromUtf8("outside_files_control"), s->outsideFilesControlEnabled(kind));
    o.insert(QString::fromUtf8("system_settings_control"),
             s->systemSettingsControlEnabled(kind));
    QJsonArray always;
    foreach (const QString &cmd, s->alwaysAllowedCommands(kind))
        always.append(cmd);
    o.insert(QString::fromUtf8("always_allowed_commands"), always);
    return o;
}

//----------------------------------------------

static QString compactJson(const QJsonObject &obj)
{
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

//----------------------------------------------

static QJsonObject mcpToJson();

//----------------------------------------------

ReadSettingsTool::ReadSettingsTool(Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _owner(owner)
{
}

//----------------------------------------------

QString ReadSettingsTool::name() const
{
    return QString::fromUtf8("read_settings");
}

//----------------------------------------------

QString ReadSettingsTool::description() const
{
    return QString::fromUtf8(
        "Read program or project settings: worker limits, action-control flags, "
        "always-allowed commands, model profiles (API keys are never returned, only "
        "whether one is set), worker presets, and MCP servers. "
        "MCP servers (a user may write NCP) are program-wide tool servers. "
        "They are not model profiles and not a file inside the project. "
        "section \"mcp\" or \"all\" lists them, plus local_config_files: configs already "
        "on this computer (Claude, Cursor) that name MCP servers. "
        "Do not invent a server and do not scan ports. If local_config_files is empty, "
        "ask the user for the command or the http address. "
        "The user confirms every read. Model profiles are program-wide; presets and "
        "limits exist per scope; MCP servers are always program-wide.");
}

//----------------------------------------------

QJsonObject ReadSettingsTool::parametersSchema() const
{
    QJsonObject scope;
    scope.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    scope.insert(QString::fromUtf8("description"),
                 QString::fromUtf8("\"project\" (default when a project is open) or "
                                   "\"program\"."));
    QJsonObject section;
    section.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    section.insert(QString::fromUtf8("description"),
                   QString::fromUtf8("\"all\" (default), \"general\", \"profiles\", "
                                     "\"presets\" or \"mcp\"."));
    QJsonObject reason;
    reason.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    reason.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("Why you need these settings; shown to the user."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("scope"), scope);
    properties.insert(QString::fromUtf8("section"), section);
    properties.insert(QString::fromUtf8("reason"), reason);

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    return schema;
}

//----------------------------------------------

QString ReadSettingsTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseArgs(argumentsJson, &args, &error))
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
    SettingsKind kind = SettingsKindDefaults;
    if (!resolveScope(args, &kind, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    QString section = args.value(QString::fromUtf8("section")).toString().trimmed().toLower();
    if (section.isEmpty())
        section = QString::fromUtf8("all");
    if (section != QString::fromUtf8("all") && section != QString::fromUtf8("general")
        && section != QString::fromUtf8("profiles") && section != QString::fromUtf8("presets")
        && section != QString::fromUtf8("mcp"))
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("section must be all, general, profiles, presets or mcp.");
    }

    QString sectionTitle;
    if (section == QString::fromUtf8("general"))
        sectionTitle = QObject::tr("лимиты и контроль действий");
    else if (section == QString::fromUtf8("profiles"))
        sectionTitle = QObject::tr("профили моделей (без ключей API)");
    else if (section == QString::fromUtf8("presets"))
        sectionTitle = QObject::tr("пресеты воркеров");
    else if (section == QString::fromUtf8("mcp"))
        sectionTitle = QObject::tr("серверы MCP");
    else
        sectionTitle = QObject::tr("все разделы (ключи API не раскрываются)");

    QString action = QObject::tr("Прочитать %1: %2").arg(scopeTitle(kind), sectionTitle);
    const QString reason = args.value(QString::fromUtf8("reason")).toString().trimmed();
    if (!reason.isEmpty())
        action += QObject::tr("\nЗачем: %1").arg(reason);

    const QString decision = _owner->askPermission(action, false);
    if (!decision.startsWith(QString::fromUtf8("approved")))
    {
        if (ok)
            *ok = true;
        return QString::fromUtf8("Settings were not read. User answer: %1").arg(decision);
    }

    AppSettings *s = AppSettings::instance();
    QJsonObject out;
    out.insert(QString::fromUtf8("scope"), scopeKey(kind));
    if (section == QString::fromUtf8("all") || section == QString::fromUtf8("general"))
        out.insert(QString::fromUtf8("general"), generalToJson(kind));
    if (section == QString::fromUtf8("all") || section == QString::fromUtf8("profiles"))
    {
        QJsonArray list;
        foreach (const LlmProfile &p, s->profiles())
            list.append(profileToJson(p));
        out.insert(QString::fromUtf8("profiles"), list);
    }
    if (section == QString::fromUtf8("all") || section == QString::fromUtf8("presets"))
    {
        QJsonArray list;
        const QString rootId = s->rootWorkerPresetId(kind);
        foreach (const WorkerPreset &preset, s->workerPresets(kind))
            list.append(presetToJson(preset, rootId));
        out.insert(QString::fromUtf8("worker_presets"), list);
    }
    if (section == QString::fromUtf8("all") || section == QString::fromUtf8("mcp"))
        out.insert(QString::fromUtf8("mcp"), mcpToJson());
    if (ok)
        *ok = true;
    return compactJson(out);
}

//----------------------------------------------

static QJsonArray localMcpConfigFiles()
{
    QStringList candidates;
    const QString appData = QString::fromLocal8Bit(qgetenv("APPDATA"));
    const QString home = QDir::homePath();
    if (!appData.isEmpty())
        candidates << QDir(appData).filePath(QString::fromUtf8("Claude/claude_desktop_config.json"));
    candidates << QDir(home).filePath(QString::fromUtf8(".cursor/mcp.json"));
    candidates << QDir(home).filePath(QString::fromUtf8(".config/Code/User/mcp.json"));

    QJsonArray files;
    foreach (const QString &path, candidates)
    {
        if (QFileInfo(path).isFile())
            files.append(QDir::fromNativeSeparators(path));
    }
    return files;
}

//----------------------------------------------

static QJsonObject mcpToJson()
{
    QJsonArray list;
    McpHub *hub = McpHub::instance();
    foreach (const McpServerConfig &server, hub->servers())
    {
        QJsonObject one;
        one.insert(QString::fromUtf8("id"), server.id);
        one.insert(QString::fromUtf8("name"), server.name);
        one.insert(QString::fromUtf8("transport"), server.transport);
        one.insert(QString::fromUtf8("command"), server.command);
        one.insert(QString::fromUtf8("arguments"), server.arguments);
        one.insert(QString::fromUtf8("work_dir"), server.workDir);
        one.insert(QString::fromUtf8("environment"), server.environment);
        one.insert(QString::fromUtf8("url"), server.url);
        one.insert(QString::fromUtf8("headers"), server.headers);
        one.insert(QString::fromUtf8("enabled"), server.enabled);
        one.insert(QString::fromUtf8("status"), hub->statusText(server.id));
        list.append(one);
    }
    QJsonObject mcp;
    mcp.insert(QString::fromUtf8("scope"), QString::fromUtf8("program"));
    mcp.insert(QString::fromUtf8("servers"), list);
    mcp.insert(QString::fromUtf8("local_config_files"), localMcpConfigFiles());
    mcp.insert(QString::fromUtf8("note"),
               QString::fromUtf8(
                   "MCP servers are program settings. A user may write NCP and mean these. "
                   "They are not model profiles and not a file in the project. "
                   "Add with edit_settings scope=program, changes.mcp = "
                   "{add:[{name, transport:stdio|http, command, arguments, url, work_dir, "
                   "environment, headers, enabled}], update:[{id, ...same fields}], "
                   "remove:[id]}. Do not invent a server and do not scan ports. "
                   "If local_config_files is not empty, read those files and copy the "
                   "servers they name. If it is empty, ask the user for the command or "
                   "the http address."));
    return mcp;
}

//----------------------------------------------

/** One planned change: text for the user, and the apply step. */
struct SettingsChange
{
    enum Kind
    {
        ProfileAdd,
        ProfileUpdate,
        ProfileRemove,
        PresetAdd,
        PresetUpdate,
        PresetRemove,
        McpAdd,
        McpUpdate,
        McpRemove,
        General
    };

    SettingsChange()
        : kind(General)
        , intValue(0)
        , boolValue(false)
    {
    }

    Kind            kind;
    QString         text;      // line shown in the confirmation dialog
    LlmProfile      profile;   // Profile*
    WorkerPreset    preset;    // Preset*
    McpServerConfig mcp;       // Mcp*
    QString         id;        // id to remove, or the general field name
    int          intValue;
    bool         boolValue;
    QStringList  listValue;
};

//----------------------------------------------

static QString describeProfileFields(const LlmProfile &p, const QStringList &fields)
{
    QStringList parts;
    foreach (const QString &f, fields)
    {
        if (f == QString::fromUtf8("name"))
            parts << QObject::tr("имя=%1").arg(p.name);
        else if (f == QString::fromUtf8("base_url"))
            parts << QObject::tr("адрес API=%1").arg(p.baseUrl);
        else if (f == QString::fromUtf8("model"))
            parts << QObject::tr("модель=%1").arg(p.model);
        else if (f == QString::fromUtf8("modality"))
            parts << QObject::tr("класс задач=%1").arg(workerTaskKindToString(p.modality));
        else if (f == QString::fromUtf8("purpose"))
            parts << QObject::tr("назначение=%1").arg(p.purpose);
        else if (f == QString::fromUtf8("balance_url"))
            parts << QObject::tr("URL баланса=%1").arg(p.balanceUrl);
        else if (f == QString::fromUtf8("balance_json_path"))
            parts << QObject::tr("JSON путь баланса=%1").arg(p.balanceJsonPath);
        else if (f == QString::fromUtf8("request_path"))
            parts << QObject::tr("путь запроса=%1").arg(p.requestPath);
        else if (f == QString::fromUtf8("task_path"))
            parts << QObject::tr("путь статуса задачи=%1").arg(p.taskPath);
        else if (f == QString::fromUtf8("catalog_url"))
            parts << QObject::tr("каталог моделей=%1").arg(p.catalogUrl);
        else if (f == QString::fromUtf8("extra_body"))
            parts << QObject::tr("доп. поля запроса=%1").arg(p.extraBody);
        else if (f == QString::fromUtf8("max_tool_iterations"))
            parts << QObject::tr("лимит вызовов функций=%1").arg(p.maxToolIterations);
        else if (f == QString::fromUtf8("context_cap"))
            parts << QObject::tr("потолок контекста=%1").arg(p.contextCap);
    }
    return parts.join(QString::fromUtf8("; "));
}

//----------------------------------------------

static QString profileFieldLabel(const QString &field)
{
    if (field == QString::fromUtf8("name"))
        return QObject::tr("имя");
    if (field == QString::fromUtf8("base_url"))
        return QObject::tr("адрес API");
    if (field == QString::fromUtf8("model"))
        return QObject::tr("модель");
    if (field == QString::fromUtf8("modality"))
        return QObject::tr("класс задач");
    if (field == QString::fromUtf8("purpose"))
        return QObject::tr("назначение");
    if (field == QString::fromUtf8("balance_url"))
        return QObject::tr("URL баланса");
    if (field == QString::fromUtf8("balance_json_path"))
        return QObject::tr("JSON путь баланса");
    if (field == QString::fromUtf8("request_path"))
        return QObject::tr("путь запроса");
    if (field == QString::fromUtf8("task_path"))
        return QObject::tr("путь статуса задачи");
    if (field == QString::fromUtf8("catalog_url"))
        return QObject::tr("каталог моделей");
    if (field == QString::fromUtf8("extra_body"))
        return QObject::tr("доп. поля запроса");
    if (field == QString::fromUtf8("max_tool_iterations"))
        return QObject::tr("лимит вызовов функций");
    if (field == QString::fromUtf8("context_cap"))
        return QObject::tr("потолок контекста");
    return field;
}

//----------------------------------------------

static QString profileFieldText(const LlmProfile &p, const QString &field)
{
    if (field == QString::fromUtf8("name"))
        return p.name;
    if (field == QString::fromUtf8("base_url"))
        return p.baseUrl;
    if (field == QString::fromUtf8("model"))
        return p.model;
    if (field == QString::fromUtf8("modality"))
        return workerTaskKindToString(p.modality);
    if (field == QString::fromUtf8("purpose"))
        return p.purpose;
    if (field == QString::fromUtf8("balance_url"))
        return p.balanceUrl;
    if (field == QString::fromUtf8("balance_json_path"))
        return p.balanceJsonPath;
    if (field == QString::fromUtf8("request_path"))
        return p.requestPath;
    if (field == QString::fromUtf8("task_path"))
        return p.taskPath;
    if (field == QString::fromUtf8("catalog_url"))
        return p.catalogUrl;
    if (field == QString::fromUtf8("extra_body"))
        return p.extraBody;
    if (field == QString::fromUtf8("max_tool_iterations"))
        return QString::number(p.maxToolIterations);
    if (field == QString::fromUtf8("context_cap"))
        return QString::number(p.contextCap);
    return QString();
}

//----------------------------------------------

static QString shownProfileValue(const QString &value)
{
    if (value.trimmed().isEmpty())
        return QObject::tr("(пусто)");
    return value;
}

//----------------------------------------------

/** Profile diff: each field as "was, then becomes". Equal values are skipped. */
static QString describeProfileDiff(const LlmProfile &before, const LlmProfile &after,
                                   const QStringList &fields)
{
    QStringList parts;
    foreach (const QString &field, fields)
    {
        const QString oldValue = profileFieldText(before, field);
        const QString newValue = profileFieldText(after, field);
        if (oldValue == newValue)
            continue;
        parts << QObject::tr("%1: %2 → %3")
                     .arg(profileFieldLabel(field),
                          shownProfileValue(oldValue),
                          shownProfileValue(newValue));
    }
    return parts.join(QString::fromUtf8("\n"));
}

//----------------------------------------------

/**
 * @brief Apply fields from JSON to a profile. Returns the names of the changed fields.
 */
static bool applyProfileFields(const QJsonObject &obj, LlmProfile *p, QStringList *touched,
                               QString *error)
{
    if (mentionsApiKey(obj))
    {
        if (error)
            *error = QString::fromUtf8(
                "API keys cannot be read or changed by workers. Ask the user to enter the "
                "key in the model profile settings.");
        return false;
    }
    QJsonValue v;
    v = fieldValue(obj, "name", "name");
    if (!v.isUndefined())
    {
        p->name = v.toString().trimmed();
        *touched << QString::fromUtf8("name");
    }
    v = fieldValue(obj, "base_url", "baseUrl");
    if (!v.isUndefined())
    {
        p->baseUrl = v.toString().trimmed();
        *touched << QString::fromUtf8("base_url");
    }
    v = fieldValue(obj, "model", "model");
    if (!v.isUndefined())
    {
        p->model = v.toString().trimmed();
        *touched << QString::fromUtf8("model");
    }
    v = fieldValue(obj, "modality", "modality");
    if (!v.isUndefined())
    {
        const QString text = v.toString().trimmed();
        const QString low = text.toLower();
        const WorkerTaskKind kind = workerTaskKindFromString(low);
        const bool alias = low == QString::fromUtf8("image")
                           || low == QString::fromUtf8("img")
                           || low == QString::fromUtf8("tts")
                           || low == QString::fromUtf8("stt")
                           || low == QString::fromUtf8("asr")
                           || low == QString::fromUtf8("audio")
                           || low == QString::fromUtf8("music")
                           || low == QString::fromUtf8("sound")
                           || low == QString::fromUtf8("video")
                           || low == QString::fromUtf8("embeddings")
                           || low == QString::fromUtf8("decision");
        if (low != workerTaskKindToString(kind) && !alias)
        {
            if (error)
                *error = QString::fromUtf8(
                    "Unknown modality \"%1\". Use llm, image_generation, speech, "
                    "audio_generation, video_generation, embedding, decisions or other.")
                    .arg(text);
            return false;
        }
        p->modality = kind;
        *touched << QString::fromUtf8("modality");
    }
    v = fieldValue(obj, "purpose", "purpose");
    if (!v.isUndefined())
    {
        p->purpose = v.toString().trimmed();
        *touched << QString::fromUtf8("purpose");
    }
    v = fieldValue(obj, "balance_url", "balanceUrl");
    if (!v.isUndefined())
    {
        p->balanceUrl = v.toString().trimmed();
        *touched << QString::fromUtf8("balance_url");
    }
    v = fieldValue(obj, "balance_json_path", "balanceJsonPath");
    if (!v.isUndefined())
    {
        p->balanceJsonPath = v.toString().trimmed();
        *touched << QString::fromUtf8("balance_json_path");
    }
    v = fieldValue(obj, "request_path", "requestPath");
    if (!v.isUndefined())
    {
        p->requestPath = v.toString().trimmed();
        *touched << QString::fromUtf8("request_path");
    }
    v = fieldValue(obj, "task_path", "taskPath");
    if (!v.isUndefined())
    {
        p->taskPath = v.toString().trimmed();
        *touched << QString::fromUtf8("task_path");
    }
    v = fieldValue(obj, "catalog_url", "catalogUrl");
    if (!v.isUndefined())
    {
        p->catalogUrl = v.toString().trimmed();
        *touched << QString::fromUtf8("catalog_url");
    }
    v = fieldValue(obj, "extra_body", "extraBody");
    if (!v.isUndefined())
    {
        QString text;
        if (v.isObject())
            text = QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
        else
            text = v.toString().trimmed();
        if (!text.isEmpty())
        {
            const QJsonDocument check = QJsonDocument::fromJson(text.toUtf8());
            if (!check.isObject())
            {
                if (error)
                    *error = QString::fromUtf8("extra_body must be a JSON object.");
                return false;
            }
        }
        p->extraBody = text;
        *touched << QString::fromUtf8("extra_body");
    }
    v = fieldValue(obj, "max_tool_iterations", "maxToolIterations");
    if (!v.isUndefined())
    {
        p->maxToolIterations = v.toInt(p->maxToolIterations);
        *touched << QString::fromUtf8("max_tool_iterations");
    }
    v = fieldValue(obj, "context_cap", "contextCap");
    if (!v.isUndefined())
    {
        p->contextCap = AppSettings::normalizeProfileContextCap(v.toInt(p->contextCap));
        *touched << QString::fromUtf8("context_cap");
    }
    return true;
}

//----------------------------------------------

static bool collectProfileChanges(const QJsonObject &profiles, QList<SettingsChange> *out,
                                  QString *error)
{
    AppSettings *s = AppSettings::instance();

    const QJsonArray adds = profiles.value(QString::fromUtf8("add")).toArray();
    for (int i = 0; i < adds.size(); ++i)
    {
        const QJsonObject obj = adds.at(i).toObject();
        LlmProfile p;
        QStringList touched;
        if (!applyProfileFields(obj, &p, &touched, error))
            return false;
        if (p.baseUrl.isEmpty() || p.model.isEmpty())
        {
            if (error)
                *error = QString::fromUtf8("A new profile needs base_url and model.");
            return false;
        }
        if (p.name.isEmpty())
            p.name = p.model;
        SettingsChange change;
        change.kind = SettingsChange::ProfileAdd;
        change.profile = p;
        change.text = QObject::tr("Добавить профиль модели: %1 (ключ API останется пустым)")
                          .arg(describeProfileFields(p, touched));
        out->append(change);
    }

    const QJsonArray updates = profiles.value(QString::fromUtf8("update")).toArray();
    for (int i = 0; i < updates.size(); ++i)
    {
        const QJsonObject obj = updates.at(i).toObject();
        const QString id = obj.value(QString::fromUtf8("id")).toString().trimmed();
        const LlmProfile before = s->profileById(id);
        if (before.id.isEmpty())
        {
            if (error)
                *error = QString::fromUtf8("Profile \"%1\" was not found.").arg(id);
            return false;
        }
        LlmProfile p = before;
        QJsonObject fields = obj;
        fields.remove(QString::fromUtf8("id"));
        QStringList touched;
        if (!applyProfileFields(fields, &p, &touched, error))
            return false;
        const QString diff = describeProfileDiff(before, p, touched);
        if (diff.isEmpty())
            continue;
        SettingsChange change;
        change.kind = SettingsChange::ProfileUpdate;
        change.profile = p;
        const QString profileName = before.name.isEmpty() ? before.id : before.name;
        change.text = QObject::tr("Изменить профиль модели «%1»: %2")
                          .arg(profileName, QString::fromUtf8("\n") + diff);
        out->append(change);
    }

    const QJsonArray removes = profiles.value(QString::fromUtf8("remove")).toArray();
    for (int i = 0; i < removes.size(); ++i)
    {
        const QString id = removes.at(i).toString().trimmed();
        const LlmProfile p = s->profileById(id);
        if (p.id.isEmpty())
        {
            if (error)
                *error = QString::fromUtf8("Profile \"%1\" was not found.").arg(id);
            return false;
        }
        SettingsChange change;
        change.kind = SettingsChange::ProfileRemove;
        change.id = id;
        change.text = QObject::tr("Удалить профиль модели «%1» (%2) и пресеты на нём")
                          .arg(p.name, p.model);
        out->append(change);
    }
    return true;
}

//----------------------------------------------

static bool presetFromJson(const QJsonObject &obj, WorkerPreset *preset, QStringList *touched,
                           QString *error)
{
    QJsonValue v = fieldValue(obj, "type", "type");
    if (!v.isUndefined())
    {
        const QString text = v.toString().trimmed().toLower();
        if (text != QString::fromUtf8("executor") && text != QString::fromUtf8("tool"))
        {
            if (error)
                *error = QString::fromUtf8("Preset type must be \"executor\" or \"tool\".");
            return false;
        }
        preset->type = workerTypeFromString(text);
        *touched << QString::fromUtf8("type");
    }
    v = fieldValue(obj, "profile_id", "profileId");
    if (!v.isUndefined())
    {
        const QString id = v.toString().trimmed();
        if (AppSettings::instance()->profileById(id).id.isEmpty())
        {
            if (error)
                *error = QString::fromUtf8("Profile \"%1\" was not found.").arg(id);
            return false;
        }
        preset->profileId = id;
        *touched << QString::fromUtf8("profile_id");
    }
    return true;
}

//----------------------------------------------

static QString presetTitle(const WorkerPreset &preset)
{
    const LlmProfile profile = AppSettings::instance()->profileById(preset.profileId);
    return QString::fromUtf8("%1 / %2")
        .arg(workerTypeToString(preset.type) == QString::fromUtf8("tool")
                 ? QObject::tr("инструмент")
                 : QObject::tr("исполнитель"),
             profile.name.isEmpty() ? preset.profileId : profile.name);
}

//----------------------------------------------

static bool collectPresetChanges(SettingsKind kind, const QJsonObject &presets,
                                 QList<SettingsChange> *out, QString *error)
{
    AppSettings *s = AppSettings::instance();

    const QJsonArray adds = presets.value(QString::fromUtf8("add")).toArray();
    for (int i = 0; i < adds.size(); ++i)
    {
        WorkerPreset preset;
        QStringList touched;
        if (!presetFromJson(adds.at(i).toObject(), &preset, &touched, error))
            return false;
        if (preset.profileId.isEmpty())
        {
            if (error)
                *error = QString::fromUtf8("A new preset needs profile_id (and type).");
            return false;
        }
        SettingsChange change;
        change.kind = SettingsChange::PresetAdd;
        change.preset = preset;
        change.text = QObject::tr("Добавить пресет воркера: %1").arg(presetTitle(preset));
        out->append(change);
    }

    const QJsonArray updates = presets.value(QString::fromUtf8("update")).toArray();
    for (int i = 0; i < updates.size(); ++i)
    {
        const QJsonObject obj = updates.at(i).toObject();
        const QString id = obj.value(QString::fromUtf8("id")).toString().trimmed();
        WorkerPreset preset = s->workerPresetById(kind, id);
        if (preset.id.isEmpty())
        {
            if (error)
                *error = QString::fromUtf8("Preset \"%1\" was not found in %2 settings.")
                             .arg(id, scopeKey(kind));
            return false;
        }
        const QString before = presetTitle(preset);
        QStringList touched;
        if (!presetFromJson(obj, &preset, &touched, error))
            return false;
        if (touched.isEmpty())
            continue;
        SettingsChange change;
        change.kind = SettingsChange::PresetUpdate;
        change.preset = preset;
        change.text = QObject::tr("Изменить пресет воркера %1 → %2")
                          .arg(before, presetTitle(preset));
        out->append(change);
    }

    const QJsonArray removes = presets.value(QString::fromUtf8("remove")).toArray();
    for (int i = 0; i < removes.size(); ++i)
    {
        const QString id = removes.at(i).toString().trimmed();
        const WorkerPreset preset = s->workerPresetById(kind, id);
        if (preset.id.isEmpty())
        {
            if (error)
                *error = QString::fromUtf8("Preset \"%1\" was not found in %2 settings.")
                             .arg(id, scopeKey(kind));
            return false;
        }
        SettingsChange change;
        change.kind = SettingsChange::PresetRemove;
        change.id = id;
        change.text = QObject::tr("Удалить пресет воркера %1").arg(presetTitle(preset));
        out->append(change);
    }
    return true;
}

//----------------------------------------------

static bool collectGeneralChanges(SettingsKind kind, const QJsonObject &general,
                                  QList<SettingsChange> *out, QString *error)
{
    AppSettings *s = AppSettings::instance();
    struct IntField
    {
        const char *snake;
        const char *camel;
        const char *title;
        int         current;
    };
    const IntField ints[] = {
        { "max_worker_depth", "maxWorkerDepth",
          QT_TR_NOOP("глубина дерева воркеров"), s->maxWorkerDepth(kind) },
        { "max_concurrent_workers", "maxConcurrentWorkers",
          QT_TR_NOOP("одновременных воркеров"), s->maxConcurrentWorkers(kind) },
        { "llm_timeout_sec", "llmTimeoutSec",
          QT_TR_NOOP("таймаут запроса к модели, с"), s->llmTimeoutSec(kind) },
    };
    for (size_t i = 0; i < sizeof(ints) / sizeof(ints[0]); ++i)
    {
        const QJsonValue v = fieldValue(general, ints[i].snake, ints[i].camel);
        if (v.isUndefined())
            continue;
        if (!v.isDouble())
        {
            if (error)
                *error = QString::fromUtf8("%1 must be a number.").arg(QString::fromUtf8(ints[i].snake));
            return false;
        }
        SettingsChange change;
        change.kind = SettingsChange::General;
        change.id = QString::fromUtf8(ints[i].snake);
        change.intValue = v.toInt();
        change.text = QObject::tr("%1: %2 → %3")
                          .arg(QObject::tr(ints[i].title))
                          .arg(ints[i].current)
                          .arg(change.intValue);
        out->append(change);
    }

    struct BoolField
    {
        const char *snake;
        const char *camel;
        const char *title;
        bool        current;
    };
    const BoolField bools[] = {
        { "shell_control", "shellControl",
          QT_TR_NOOP("подтверждать команды shell"), s->shellControlEnabled(kind) },
        { "outside_files_control", "outsideFilesControl",
          QT_TR_NOOP("подтверждать файлы вне проекта"), s->outsideFilesControlEnabled(kind) },
        { "system_settings_control", "systemSettingsControl",
          QT_TR_NOOP("подтверждать системные настройки"),
          s->systemSettingsControlEnabled(kind) },
    };
    for (size_t i = 0; i < sizeof(bools) / sizeof(bools[0]); ++i)
    {
        const QJsonValue v = fieldValue(general, bools[i].snake, bools[i].camel);
        if (v.isUndefined())
            continue;
        if (!v.isBool())
        {
            if (error)
                *error = QString::fromUtf8("%1 must be true or false.")
                             .arg(QString::fromUtf8(bools[i].snake));
            return false;
        }
        SettingsChange change;
        change.kind = SettingsChange::General;
        change.id = QString::fromUtf8(bools[i].snake);
        change.boolValue = v.toBool();
        change.text = QObject::tr("%1: %2 → %3")
                          .arg(QObject::tr(bools[i].title),
                               bools[i].current ? QObject::tr("да") : QObject::tr("нет"),
                               change.boolValue ? QObject::tr("да") : QObject::tr("нет"));
        out->append(change);
    }

    const QJsonValue root = fieldValue(general, "root_worker_preset_id", "rootWorkerPresetId");
    if (!root.isUndefined())
    {
        const QString id = root.toString().trimmed();
        const WorkerPreset preset = s->workerPresetById(kind, id);
        if (preset.id.isEmpty() || preset.type != WorkerTypeExecutor)
        {
            if (error)
                *error = QString::fromUtf8(
                    "root_worker_preset_id must be an existing executor preset in %1 settings.")
                    .arg(scopeKey(kind));
            return false;
        }
        SettingsChange change;
        change.kind = SettingsChange::General;
        change.id = QString::fromUtf8("root_worker_preset_id");
        change.listValue << id;
        change.text = QObject::tr("Корневой пресет: %1").arg(presetTitle(preset));
        out->append(change);
    }

    const QJsonValue always = fieldValue(general, "always_allowed_commands", "alwaysAllowedCommands");
    if (!always.isUndefined())
    {
        const QJsonObject ops = always.toObject();
        const QJsonArray add = ops.value(QString::fromUtf8("add")).toArray();
        const QJsonArray remove = ops.value(QString::fromUtf8("remove")).toArray();
        if (!add.isEmpty())
        {
            SettingsChange change;
            change.kind = SettingsChange::General;
            change.id = QString::fromUtf8("always_add");
            for (int i = 0; i < add.size(); ++i)
            {
                const QString key = add.at(i).toString().trimmed();
                if (!key.isEmpty())
                    change.listValue << key;
            }
            if (!change.listValue.isEmpty())
            {
                change.text = QObject::tr("Разрешить без подтверждения команды: %1")
                                  .arg(change.listValue.join(QString::fromUtf8(", ")));
                out->append(change);
            }
        }
        if (!remove.isEmpty())
        {
            SettingsChange change;
            change.kind = SettingsChange::General;
            change.id = QString::fromUtf8("always_remove");
            for (int i = 0; i < remove.size(); ++i)
            {
                const QString key = remove.at(i).toString().trimmed();
                if (!key.isEmpty())
                    change.listValue << key;
            }
            if (!change.listValue.isEmpty())
            {
                change.text = QObject::tr("Снова требовать подтверждение для команд: %1")
                                  .arg(change.listValue.join(QString::fromUtf8(", ")));
                out->append(change);
            }
        }
    }
    return true;
}

//----------------------------------------------

static QString mcpBrief(const McpServerConfig &server)
{
    QStringList parts;
    if (server.transport == QString::fromUtf8("http"))
        parts << QString::fromUtf8("http %1").arg(server.url.trimmed());
    else
    {
        const QString command = (server.command + QLatin1Char(' ') + server.arguments).trimmed();
        parts << QString::fromUtf8("stdio %1").arg(command);
    }
    if (!server.environment.trimmed().isEmpty())
        parts << QObject::tr("среда задана");
    if (!server.headers.trimmed().isEmpty())
        parts << QObject::tr("заголовки заданы");
    if (!server.enabled)
        parts << QObject::tr("выключен");
    return parts.join(QString::fromUtf8(", "));
}

//----------------------------------------------

static bool fillMcpServer(const QJsonObject &obj, McpServerConfig *server, QString *error)
{
    QJsonValue value = fieldValue(obj, "name", "name");
    if (!value.isUndefined())
        server->name = value.toString().trimmed();
    value = fieldValue(obj, "transport", "transport");
    if (!value.isUndefined())
    {
        const QString transport = value.toString().trimmed().toLower();
        if (transport != QString::fromUtf8("stdio") && transport != QString::fromUtf8("http"))
        {
            if (error)
                *error = QString::fromUtf8("mcp transport must be stdio or http.");
            return false;
        }
        server->transport = transport;
    }
    value = fieldValue(obj, "command", "command");
    if (!value.isUndefined())
        server->command = value.toString().trimmed();
    value = fieldValue(obj, "arguments", "arguments");
    if (!value.isUndefined())
        server->arguments = value.toString();
    value = fieldValue(obj, "work_dir", "workDir");
    if (!value.isUndefined())
        server->workDir = value.toString().trimmed();
    value = fieldValue(obj, "environment", "environment");
    if (!value.isUndefined())
        server->environment = value.toString();
    value = fieldValue(obj, "url", "url");
    if (!value.isUndefined())
        server->url = value.toString().trimmed();
    value = fieldValue(obj, "headers", "headers");
    if (!value.isUndefined())
        server->headers = value.toString();
    value = fieldValue(obj, "enabled", "enabled");
    if (value.isBool())
        server->enabled = value.toBool();
    else if (value.isDouble())
        server->enabled = value.toInt() != 0;
    else if (value.isString())
    {
        const QString flag = value.toString().trimmed().toLower();
        server->enabled = flag != QString::fromUtf8("false")
                          && flag != QString::fromUtf8("0")
                          && flag != QString::fromUtf8("no")
                          && flag != QString::fromUtf8("off");
    }
    if (server->transport != QString::fromUtf8("http"))
        server->transport = QString::fromUtf8("stdio");
    if (server->name.isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("mcp server requires a name.");
        return false;
    }
    if (server->transport == QString::fromUtf8("http"))
    {
        const QString url = server->url.toLower();
        if (!url.startsWith(QString::fromUtf8("http://"))
            && !url.startsWith(QString::fromUtf8("https://")))
        {
            if (error)
                *error = QString::fromUtf8("mcp http server requires an http or https url.");
            return false;
        }
    }
    else if (server->command.isEmpty())
    {
        if (error)
            *error = QString::fromUtf8("mcp stdio server requires a command.");
        return false;
    }
    return true;
}

//----------------------------------------------

static McpServerConfig mcpById(const QString &id)
{
    foreach (const McpServerConfig &server, McpHub::instance()->servers())
    {
        if (server.id == id)
            return server;
    }
    return McpServerConfig();
}

//----------------------------------------------

static bool collectMcpChanges(const QJsonObject &mcp, QList<SettingsChange> *out, QString *error)
{
    const QJsonArray add = mcp.value(QString::fromUtf8("add")).toArray();
    for (int i = 0; i < add.size(); ++i)
    {
        if (!add.at(i).isObject())
        {
            if (error)
                *error = QString::fromUtf8("mcp add entries must be objects.");
            return false;
        }
        SettingsChange change;
        change.kind = SettingsChange::McpAdd;
        change.mcp.transport = QString::fromUtf8("stdio");
        change.mcp.enabled = true;
        if (!fillMcpServer(add.at(i).toObject(), &change.mcp, error))
            return false;
        change.text = QObject::tr("Добавить сервер MCP «%1»: %2")
                          .arg(change.mcp.name, mcpBrief(change.mcp));
        out->append(change);
    }

    const QJsonArray update = mcp.value(QString::fromUtf8("update")).toArray();
    for (int i = 0; i < update.size(); ++i)
    {
        if (!update.at(i).isObject())
        {
            if (error)
                *error = QString::fromUtf8("mcp update entries must be objects.");
            return false;
        }
        const QJsonObject obj = update.at(i).toObject();
        const QString id = fieldValue(obj, "id", "id").toString().trimmed();
        McpServerConfig existing = mcpById(id);
        if (existing.id.isEmpty())
        {
            if (error)
                *error = QString::fromUtf8("mcp update id was not found: %1").arg(id);
            return false;
        }
        if (!fillMcpServer(obj, &existing, error))
            return false;
        SettingsChange change;
        change.kind = SettingsChange::McpUpdate;
        change.mcp = existing;
        change.text = QObject::tr("Изменить сервер MCP «%1»: %2")
                          .arg(change.mcp.name, mcpBrief(change.mcp));
        out->append(change);
    }

    const QJsonArray remove = mcp.value(QString::fromUtf8("remove")).toArray();
    for (int i = 0; i < remove.size(); ++i)
    {
        const QString id = remove.at(i).toString().trimmed();
        const McpServerConfig existing = mcpById(id);
        if (existing.id.isEmpty())
        {
            if (error)
                *error = QString::fromUtf8("mcp remove id was not found: %1").arg(id);
            return false;
        }
        SettingsChange change;
        change.kind = SettingsChange::McpRemove;
        change.id = id;
        change.text = QObject::tr("Удалить сервер MCP «%1»").arg(existing.name);
        out->append(change);
    }
    return true;
}

//----------------------------------------------

static QString applyChange(SettingsKind kind, const SettingsChange &change)
{
    AppSettings *s = AppSettings::instance();
    QString error;
    switch (change.kind)
    {
    case SettingsChange::ProfileAdd:
    {
        const QString id = s->addProfile(change.profile);
        return QString::fromUtf8("profile added: id=%1 (API key is empty; the user must "
                                 "enter it in settings before use)")
            .arg(id);
    }
    case SettingsChange::ProfileUpdate:
        s->updateProfile(change.profile);
        return QString::fromUtf8("profile updated: id=%1").arg(change.profile.id);
    case SettingsChange::ProfileRemove:
        s->removePresetsUsingProfile(change.id);
        s->removeProfile(change.id);
        if (!s->profileById(change.id).id.isEmpty())
            return QString::fromUtf8("profile %1 was NOT removed (the last profile cannot "
                                     "be deleted)")
                .arg(change.id);
        return QString::fromUtf8("profile removed: id=%1").arg(change.id);
    case SettingsChange::PresetAdd:
    {
        const QString id = s->addWorkerPreset(kind, change.preset, &error);
        if (id.isEmpty())
            return QString::fromUtf8("preset not added: %1").arg(error);
        return QString::fromUtf8("preset added: id=%1").arg(id);
    }
    case SettingsChange::PresetUpdate:
        s->updateWorkerPreset(kind, change.preset, &error);
        if (!error.isEmpty())
            return QString::fromUtf8("preset %1 not updated: %2").arg(change.preset.id, error);
        return QString::fromUtf8("preset updated: id=%1").arg(change.preset.id);
    case SettingsChange::PresetRemove:
        s->removeWorkerPreset(kind, change.id);
        return QString::fromUtf8("preset removed: id=%1").arg(change.id);
    case SettingsChange::McpAdd:
    {
        const QString id = McpHub::instance()->addServer(change.mcp);
        return QString::fromUtf8("mcp server added: id=%1 name=%2").arg(id, change.mcp.name);
    }
    case SettingsChange::McpUpdate:
        McpHub::instance()->updateServer(change.mcp);
        return QString::fromUtf8("mcp server updated: id=%1").arg(change.mcp.id);
    case SettingsChange::McpRemove:
        McpHub::instance()->removeServer(change.id);
        return QString::fromUtf8("mcp server removed: id=%1").arg(change.id);
    case SettingsChange::General:
        break;
    }

    if (change.id == QString::fromUtf8("max_worker_depth"))
    {
        s->setMaxWorkerDepth(kind, change.intValue);
        return QString::fromUtf8("max_worker_depth=%1").arg(s->maxWorkerDepth(kind));
    }
    if (change.id == QString::fromUtf8("max_concurrent_workers"))
    {
        s->setMaxConcurrentWorkers(kind, change.intValue);
        return QString::fromUtf8("max_concurrent_workers=%1").arg(s->maxConcurrentWorkers(kind));
    }
    if (change.id == QString::fromUtf8("llm_timeout_sec"))
    {
        s->setLlmTimeoutSec(kind, change.intValue);
        return QString::fromUtf8("llm_timeout_sec=%1").arg(s->llmTimeoutSec(kind));
    }
    if (change.id == QString::fromUtf8("shell_control"))
    {
        s->setShellControlEnabled(kind, change.boolValue);
        return QString::fromUtf8("shell_control=%1").arg(change.boolValue);
    }
    if (change.id == QString::fromUtf8("outside_files_control"))
    {
        s->setOutsideFilesControlEnabled(kind, change.boolValue);
        return QString::fromUtf8("outside_files_control=%1").arg(change.boolValue);
    }
    if (change.id == QString::fromUtf8("system_settings_control"))
    {
        s->setSystemSettingsControlEnabled(kind, change.boolValue);
        return QString::fromUtf8("system_settings_control=%1").arg(change.boolValue);
    }
    if (change.id == QString::fromUtf8("root_worker_preset_id"))
    {
        s->setRootWorkerPresetId(kind, change.listValue.value(0));
        return QString::fromUtf8("root_worker_preset_id=%1").arg(s->rootWorkerPresetId(kind));
    }
    if (change.id == QString::fromUtf8("always_add"))
    {
        s->addAlwaysAllowedCommands(kind, change.listValue);
        return QString::fromUtf8("always allowed added: %1")
            .arg(change.listValue.join(QString::fromUtf8(", ")));
    }
    if (change.id == QString::fromUtf8("always_remove"))
    {
        foreach (const QString &key, change.listValue)
            s->removeAlwaysAllowedCommand(kind, key);
        return QString::fromUtf8("always allowed removed: %1")
            .arg(change.listValue.join(QString::fromUtf8(", ")));
    }
    return QString::fromUtf8("unknown change %1").arg(change.id);
}

//----------------------------------------------

EditSettingsTool::EditSettingsTool(Worker *owner, QObject *parent)
    : AgentTool(parent)
    , _owner(owner)
{
}

//----------------------------------------------

QString EditSettingsTool::name() const
{
    return QString::fromUtf8("edit_settings");
}

//----------------------------------------------

QString EditSettingsTool::description() const
{
    return QString::fromUtf8(
        "Change program or project settings when a profile or a setting is wrong for "
        "the task. Do not invent a workaround around a bad path, modality, or purpose. "
        "Read with read_settings first. reason must say what is wrong and what each "
        "new value fixes. The user always sees every field as old → new and must "
        "confirm; nothing changes without that confirmation. "
        "changes = {\"general\":{max_worker_depth, max_concurrent_workers, llm_timeout_sec, "
        "root_worker_preset_id, shell_control, outside_files_control, "
        "system_settings_control, always_allowed_commands:{add:[],remove:[]}}, "
        "\"profiles\":{add:[{name, base_url, model, modality, purpose, request_path, "
        "task_path, catalog_url, extra_body, max_tool_iterations, context_cap}], "
        "update:[{id, ...same fields}], remove:[id]}, "
        "\"presets\":{add:[{type: executor|tool, profile_id}], update:[{id, type, "
        "profile_id}], remove:[id]}, "
        "\"mcp\":{add:[{name, transport:stdio|http, command, arguments, url, work_dir, "
        "environment, headers, enabled}], update:[{id, ...same fields}], remove:[id]}}. "
        "MCP servers (a user may write NCP) are program-wide tool servers, not model "
        "profiles and not a project file: call this with scope=program. "
        "Do not invent a server and do not scan ports. Read with read_settings first. "
        "If local_config_files lists paths, read those files and copy the servers. "
        "If that list is empty, ask the user for the command or the http address. "
        "API keys cannot be read or set: a new profile is created without a key and the "
        "user enters it. Model profiles are program-wide; presets and general values "
        "belong to the chosen scope.");
}

//----------------------------------------------

QJsonObject EditSettingsTool::parametersSchema() const
{
    QJsonObject scope;
    scope.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    scope.insert(QString::fromUtf8("description"),
                 QString::fromUtf8("\"project\" (default when a project is open) or "
                                   "\"program\"."));
    QJsonObject changes;
    changes.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    changes.insert(QString::fromUtf8("description"),
                   QString::fromUtf8("Object with optional keys general, profiles, presets, "
                                     "mcp (see tool description)."));
    QJsonObject reason;
    reason.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    reason.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("Why these changes are needed; shown to the user."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("scope"), scope);
    properties.insert(QString::fromUtf8("changes"), changes);
    properties.insert(QString::fromUtf8("reason"), reason);

    QJsonArray required;
    required.append(QString::fromUtf8("changes"));

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    schema.insert(QString::fromUtf8("required"), required);
    return schema;
}

//----------------------------------------------

QString EditSettingsTool::invoke(QString argumentsJson, bool *ok)
{
    QJsonObject args;
    QString error;
    if (!parseArgs(argumentsJson, &args, &error))
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
    SettingsKind kind = SettingsKindDefaults;
    if (!resolveScope(args, &kind, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    QJsonValue changesValue = args.value(QString::fromUtf8("changes"));
    if (changesValue.isString())
    {
        const QJsonDocument doc = QJsonDocument::fromJson(changesValue.toString().toUtf8());
        if (doc.isObject())
            changesValue = doc.object();
    }
    if (!changesValue.isObject())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("changes must be a JSON object.");
    }
    const QJsonObject changes = changesValue.toObject();

    QList<SettingsChange> planned;
    if (changes.contains(QString::fromUtf8("profiles"))
        && !collectProfileChanges(changes.value(QString::fromUtf8("profiles")).toObject(),
                                  &planned, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (changes.contains(QString::fromUtf8("presets"))
        && !collectPresetChanges(kind, changes.value(QString::fromUtf8("presets")).toObject(),
                                 &planned, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (changes.contains(QString::fromUtf8("general"))
        && !collectGeneralChanges(kind, changes.value(QString::fromUtf8("general")).toObject(),
                                  &planned, &error))
    {
        if (ok)
            *ok = false;
        return error;
    }
    if (changes.contains(QString::fromUtf8("mcp")))
    {
        if (kind != SettingsKindDefaults)
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8(
                "MCP servers are program settings. Call edit_settings with scope=program.");
        }
        const QJsonValue mcpValue = changes.value(QString::fromUtf8("mcp"));
        if (!mcpValue.isObject())
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8("changes.mcp must be a JSON object.");
        }
        if (!collectMcpChanges(mcpValue.toObject(), &planned, &error))
        {
            if (ok)
                *ok = false;
            return error;
        }
    }
    if (planned.isEmpty())
    {
        if (ok)
            *ok = false;
        return QString::fromUtf8("No recognized changes in the request.");
    }

    QString action = QObject::tr("Изменить %1:").arg(scopeTitle(kind));
    foreach (const SettingsChange &change, planned)
        action += QString::fromUtf8("\n• ") + change.text;
    const QString reason = args.value(QString::fromUtf8("reason")).toString().trimmed();
    if (!reason.isEmpty())
        action += QObject::tr("\nЗачем: %1").arg(reason);

    const QString decision = _owner->askPermission(action, false, QString(), true);
    if (!decision.startsWith(QString::fromUtf8("approved")))
    {
        if (ok)
            *ok = true;
        return QString::fromUtf8("Settings were not changed. User answer: %1").arg(decision);
    }

    QStringList results;
    foreach (const SettingsChange &change, planned)
        results << applyChange(kind, change);
    AppSettings::instance()->flushPersist();
    if (ok)
        *ok = true;
    return QString::fromUtf8("Applied to %1 settings:\n- %2")
        .arg(scopeKey(kind), results.join(QString::fromUtf8("\n- ")));
}
