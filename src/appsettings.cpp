#include "appsettings.h"
//----------------------------------------------
#include "applocale.h"
#include "modelcapabilities.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <QtGlobal>
#include <QUuid>

//----------------------------------------------

static const int kDefaultMaxWorkerDepth       = 4;
static const int kDefaultMaxConcurrentWorkers = 6;
static const int kMinWorkerDepth              = 1;
static const int kMaxWorkerDepth              = 8;
static const int kMinConcurrentWorkers        = 1;
static const int kMaxConcurrentWorkers        = 64;
static const int kDefaultLlmTimeoutSec        = 300;
static const int kMinLlmTimeoutSec            = 30;
static const int kMaxLlmTimeoutSec            = 3600;
static const int kDefaultRoleToolIterations[4] = { 1000, 1000, 1000, 1000 };
static const int kDefaultRoleContextCap[4]     = { 0, 32000, 16000, 16000 };
static const int kMinRoleToolIterations        = 2;
static const int kMaxRoleToolIterations        = 10000;
static const int kMaxRoleContextCap            = 4000000;
static const int kAppearanceVersion = 3;

//----------------------------------------------

static QString roleBudgetKey(int index)
{
    static const char *names[4] = { "manager", "executor", "tester", "tool" };
    if (index < 0 || index > 3)
        index = 0;
    return QString::fromUtf8(names[index]);
}

//----------------------------------------------

/**
 * @brief Строка-маркер: в ini лежит полный набор настроек, а не только статистика.
 */
static QString workspaceSeededKey()
{
    return QString::fromUtf8("workspace/seeded");
}

//----------------------------------------------

WorkspaceSettings::WorkspaceSettings()
    : shellControl(true)
    , outsideFilesControl(true)
    , systemSettingsControl(true)
    , maxWorkerDepth(kDefaultMaxWorkerDepth)
    , maxConcurrentWorkers(kDefaultMaxConcurrentWorkers)
    , llmTimeoutSec(kDefaultLlmTimeoutSec)
{
    for (int i = 0; i < 3; ++i)
        workerProfileId[i].clear();
}

//----------------------------------------------

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
    , _projectOpen(false)
    , _fontPointSize(11)
    , _textColor(QString::fromUtf8("#1a1a1a"))
    , _windowColor(QString::fromUtf8("#f7f7f8"))
    , _panelColor(QString::fromUtf8("#ffffff"))
    , _accentColor(QString::fromUtf8("#262626"))
    , _chatUserColor(QString::fromUtf8("#ffffff"))
    , _chatAssistantColor(QString::fromUtf8("#ffffff"))
    , _chatOtherColor(QString::fromUtf8("#f3f4f6"))
    , _nextWorkId(0)
    , _statsTimer(0)
    , _persistTimer(0)
    , _limitsSignalTimer(0)
    , _profilesSignalTimer(0)
    , _presetsSignalTimer(0)
    , _persistDefaultsPending(false)
    , _persistProjectPending(false)
    , _showForwardedContext(true)
    , _autoApprovePermissions(false)
    , _uiLanguage(QString::fromUtf8("ru"))
    , _mainWindowGeometry()
    , _mainWindowState()
    , _leftDockWidth(0)
    , _projectDockHeight(0)
    , _graphThumbDockHeight(0)
{
}

//----------------------------------------------

AppSettings *AppSettings::instance()
{
    static AppSettings *settings = 0;
    if (!settings)
        settings = new AppSettings(QCoreApplication::instance());
    return settings;
}

//----------------------------------------------

QString AppSettings::normalizeKey(QString key)
{
    key = key.trimmed();
    if (key.isEmpty())
        return QString();
#ifdef Q_OS_WIN
    key = key.toLower();
#endif
    if (key.contains(QChar('\n')))
    {
        const QStringList lines = key.split(QChar('\n'));
        for (int i = lines.size() - 1; i >= 0; --i)
        {
            const QString line = lines.at(i).trimmed();
            if (!line.isEmpty())
            {
                key = line;
                break;
            }
        }
    }
    const int space = key.indexOf(QChar(' '));
    if (space > 0)
        key = key.left(space);
    key.replace(QChar('\\'), QChar('/'));
    const int slash = key.lastIndexOf(QChar('/'));
    if (slash >= 0)
        key = key.mid(slash + 1);
#ifdef Q_OS_WIN
    const char *suffixes[] = { ".exe", ".cmd", ".bat", ".com", 0 };
    for (int s = 0; suffixes[s]; ++s)
    {
        const QString suf = QString::fromUtf8(suffixes[s]);
        if (key.endsWith(suf))
        {
            key.chop(suf.size());
            break;
        }
    }
#endif
    return key.trimmed();
}

//----------------------------------------------

void AppSettings::migrateLegacyCommands(WorkspaceSettings *bundle, const QStringList &keys)
{
    if (!bundle)
        return;
    foreach (const QString &item, keys)
    {
        const QString key = normalizeKey(item);
        if (key.isEmpty())
            continue;
        bool found = false;
        foreach (const PermissionGrant &grant, bundle->permissionGrants)
        {
            if (grant.programKey == key)
            {
                found = true;
                break;
            }
        }
        if (found)
            continue;
        PermissionGrant grant;
        grant.programKey = key;
        grant.actionText = key;
        bundle->permissionGrants.append(grant);
        bundle->alwaysCommands.insert(key);
    }
}

//----------------------------------------------

QString AppSettings::projectStoreDir(QString projectRoot)
{
    const QString root = QDir::cleanPath(projectRoot.trimmed());
    if (root.isEmpty())
        return QString();
    return root + QString::fromUtf8("/___project___");
}

//----------------------------------------------

QString AppSettings::configDir()
{
    const QString dir = QDir::cleanPath(QCoreApplication::applicationDirPath()
                                        + QString::fromUtf8("/config"));
    QDir().mkpath(dir);
    return dir;
}

//----------------------------------------------

QString AppSettings::aiProfilesDir()
{
    const QString dir = QDir::cleanPath(configDir() + QString::fromUtf8("/ai"));
    QDir().mkpath(dir);
    return dir;
}

//----------------------------------------------

QString AppSettings::settingsFilePath()
{
    return QDir::cleanPath(configDir() + QString::fromUtf8("/settings.ini"));
}

//----------------------------------------------

QString AppSettings::sanitizeProfileFileStem(QString id)
{
    QString stem = id.trimmed();
    for (int i = 0; i < stem.size(); ++i)
    {
        const QChar ch = stem.at(i);
        const bool ok = ch.isLetterOrNumber() || ch == QChar('-') || ch == QChar('_');
        if (!ok)
            stem[i] = QChar('_');
    }
    if (stem.isEmpty())
        stem = QString::fromUtf8("profile");
    return stem;
}

//----------------------------------------------

QString AppSettings::profileFilePath(QString id) const
{
    return QDir::cleanPath(aiProfilesDir() + QChar('/')
                           + sanitizeProfileFileStem(id) + QString::fromUtf8(".ini"));
}

//----------------------------------------------

QString AppSettings::projectIniPath() const
{
    const QString dir = projectStoreDir(_projectRoot);
    if (dir.isEmpty())
        return QString();
    return dir + QString::fromUtf8("/project.ini");
}

//----------------------------------------------

QString AppSettings::projectRoot() const
{
    return _projectRoot;
}

//----------------------------------------------

bool AppSettings::hasProjectSettings() const
{
    return _projectOpen;
}

//----------------------------------------------

SettingsKind AppSettings::activeKind() const
{
    return _projectOpen ? SettingsKindProject : SettingsKindDefaults;
}

//----------------------------------------------

WorkspaceSettings &AppSettings::ws(SettingsKind kind)
{
    return (kind == SettingsKindProject) ? _project : _defaults;
}

//----------------------------------------------

const WorkspaceSettings &AppSettings::ws(SettingsKind kind) const
{
    return (kind == SettingsKindProject) ? _project : _defaults;
}

//----------------------------------------------

WorkspaceSettings &AppSettings::activeWs()
{
    return ws(activeKind());
}

//----------------------------------------------

const WorkspaceSettings &AppSettings::activeWs() const
{
    return ws(activeKind());
}

//----------------------------------------------

const WorkspaceSettings &AppSettings::workspace(SettingsKind kind) const
{
    return ws(kind);
}

//----------------------------------------------

void AppSettings::emitWorkspaceSignals(SettingsKind kind, bool limits, bool profiles,
                                       bool presets, bool permissions, bool controls)
{
    // Сигналы, влияющие на работающих воркеров, — только для активного набора.
    const bool active = (kind == activeKind());
    if (limits && active)
        emit workerLimitsChanged();
    if (profiles && active)
        emit profilesChanged();
    if (presets && active)
        emit workerPresetsChanged();
    if (permissions && active)
        emit alwaysCommandsChanged();
    if (controls && active)
        emit controlFlagsChanged();
}

//----------------------------------------------

void AppSettings::appendGrant(WorkspaceSettings *bundle, PermissionGrant grant)
{
    if (!bundle)
        return;
    grant.programKey = normalizeKey(grant.programKey);
    if (grant.programKey.isEmpty())
        grant.programKey = normalizeKey(grant.actionText);
    if (grant.programKey.isEmpty())
        return;
    if (grant.actionText.trimmed().isEmpty())
        grant.actionText = grant.programKey;
    foreach (const PermissionGrant &existing, bundle->permissionGrants)
    {
        if (existing.programKey == grant.programKey)
            return;
    }
    bundle->permissionGrants.append(grant);
    bundle->alwaysCommands.insert(grant.programKey);
}

//----------------------------------------------

void AppSettings::loadPermissions(QSettings *settings, WorkspaceSettings *bundle)
{
    if (!settings || !bundle)
        return;

    bundle->permissionGrants.clear();
    bundle->alwaysCommands.clear();

    const int count = settings->beginReadArray(QString::fromUtf8("permissionGrants"));
    for (int i = 0; i < count; ++i)
    {
        settings->setArrayIndex(i);
        PermissionGrant grant;
        grant.programKey = settings->value(QString::fromUtf8("programKey")).toString();
        grant.actionText = settings->value(QString::fromUtf8("actionText")).toString();
        appendGrant(bundle, grant);
    }
    settings->endArray();

    if (bundle->permissionGrants.isEmpty())
    {
        QByteArray grantsJson = settings->value(QString::fromUtf8("permissionGrantsJson")).toByteArray();
        if (grantsJson.isEmpty())
            grantsJson = settings->value(QString::fromUtf8("permissionGrantsJson")).toString().toUtf8();
        if (grantsJson.isEmpty())
            grantsJson = settings->value(QString::fromUtf8("permissionGrants")).toByteArray();
        if (grantsJson.isEmpty())
            grantsJson = settings->value(QString::fromUtf8("permissionGrants")).toString().toUtf8();
        if (!grantsJson.isEmpty() && grantsJson.startsWith('['))
        {
            const QJsonDocument doc = QJsonDocument::fromJson(grantsJson);
            if (doc.isArray())
            {
                foreach (const QJsonValue &value, doc.array())
                {
                    if (!value.isObject())
                        continue;
                    const QJsonObject object = value.toObject();
                    PermissionGrant grant;
                    grant.programKey = object.value(QString::fromUtf8("programKey")).toString();
                    grant.actionText = object.value(QString::fromUtf8("actionText")).toString();
                    appendGrant(bundle, grant);
                }
            }
        }
    }

    migrateLegacyCommands(bundle,
                          settings->value(QString::fromUtf8("alwaysCommands")).toStringList());
    if (settings->contains(QString::fromUtf8("control/shell")))
        bundle->shellControl = settings->value(QString::fromUtf8("control/shell"), true).toBool();
    if (settings->contains(QString::fromUtf8("control/outsideFiles")))
        bundle->outsideFilesControl =
            settings->value(QString::fromUtf8("control/outsideFiles"), true).toBool();
    if (settings->contains(QString::fromUtf8("control/systemSettings")))
        bundle->systemSettingsControl =
            settings->value(QString::fromUtf8("control/systemSettings"), true).toBool();
}

//----------------------------------------------

void AppSettings::savePermissions(QSettings *settings, const WorkspaceSettings &bundle) const
{
    if (!settings)
        return;
    settings->remove(QString::fromUtf8("permissionGrants"));
    settings->remove(QString::fromUtf8("permissionGrantsJson"));
    settings->beginWriteArray(QString::fromUtf8("permissionGrants"));
    for (int i = 0; i < bundle.permissionGrants.size(); ++i)
    {
        settings->setArrayIndex(i);
        settings->setValue(QString::fromUtf8("programKey"), bundle.permissionGrants.at(i).programKey);
        settings->setValue(QString::fromUtf8("actionText"), bundle.permissionGrants.at(i).actionText);
    }
    settings->endArray();
    settings->setValue(QString::fromUtf8("alwaysCommands"),
                       QStringList(bundle.alwaysCommands.toList()));
    settings->setValue(QString::fromUtf8("control/shell"), bundle.shellControl);
    settings->setValue(QString::fromUtf8("control/outsideFiles"), bundle.outsideFilesControl);
    settings->setValue(QString::fromUtf8("control/systemSettings"), bundle.systemSettingsControl);
}

//----------------------------------------------

void AppSettings::loadWorkspaceIni(QSettings *settings, WorkspaceSettings *bundle)
{
    if (!settings || !bundle)
        return;

    loadPermissions(settings, bundle);

    bundle->maxWorkerDepth = qBound(kMinWorkerDepth,
                                    settings->value(QString::fromUtf8("workers/maxDepth"),
                                                    kDefaultMaxWorkerDepth).toInt(),
                                    kMaxWorkerDepth);
    bundle->maxConcurrentWorkers = qBound(kMinConcurrentWorkers,
                                          settings->value(QString::fromUtf8("workers/maxConcurrent"),
                                                          kDefaultMaxConcurrentWorkers).toInt(),
                                          kMaxConcurrentWorkers);
    bundle->llmTimeoutSec = qBound(kMinLlmTimeoutSec,
                                   settings->value(QString::fromUtf8("llm/requestTimeoutSec"),
                                                   kDefaultLlmTimeoutSec).toInt(),
                                   kMaxLlmTimeoutSec);

    const char *keys[] = { "manager", "executor", "tester" };
    for (int i = 0; i < 3; ++i)
    {
        QString id = settings->value(QString::fromUtf8("workerProfile/")
                                     + QString::fromUtf8(keys[i])).toString();
        if (id.isEmpty())
            id = settings->value(QString::fromUtf8("workerProvider/")
                                 + QString::fromUtf8(keys[i])).toString();
        if (!id.isEmpty())
            bundle->workerProfileId[i] = id;
    }
    ensureWorkerProfileIds(bundle);
    loadWorkerPresets(settings, bundle);
    bundle->rootWorkerPresetId =
        settings->value(QString::fromUtf8("workers/rootPresetId")).toString().trimmed();
    bundle->hiddenWorkerPresetIds.clear();
    const QStringList hidden =
        settings->value(QString::fromUtf8("workers/hiddenPresetIds")).toStringList();
    foreach (const QString &id, hidden)
    {
        const QString clean = id.trimmed();
        if (!clean.isEmpty())
            bundle->hiddenWorkerPresetIds.insert(clean);
    }
    ensureRootWorkerPresetId(bundle);
}

//----------------------------------------------

void AppSettings::saveWorkspaceIni(QSettings *settings, const WorkspaceSettings &bundle) const
{
    if (!settings)
        return;
    settings->setValue(workspaceSeededKey(), true);
    settings->setValue(QString::fromUtf8("workers/maxDepth"), bundle.maxWorkerDepth);
    settings->setValue(QString::fromUtf8("workers/maxConcurrent"), bundle.maxConcurrentWorkers);
    settings->setValue(QString::fromUtf8("llm/requestTimeoutSec"), bundle.llmTimeoutSec);
    settings->remove(QString::fromUtf8("workers/roleBudget"));
    const char *keys[] = { "manager", "executor", "tester" };
    for (int i = 0; i < 3; ++i)
    {
        settings->setValue(QString::fromUtf8("workerProfile/") + QString::fromUtf8(keys[i]),
                           bundle.workerProfileId[i]);
    }
    settings->remove(QString::fromUtf8("llmProfiles"));
    saveWorkerPresets(settings, bundle);
    settings->setValue(QString::fromUtf8("workers/rootPresetId"), bundle.rootWorkerPresetId);
    settings->setValue(QString::fromUtf8("workers/hiddenPresetIds"),
                       QStringList(bundle.hiddenWorkerPresetIds.toList()));
    savePermissions(settings, bundle);
}

//----------------------------------------------

bool AppSettings::projectWorkspaceSeeded() const
{
    const QString path = projectIniPath();
    if (path.isEmpty() || !QFileInfo(path).exists())
        return false;
    QSettings project(path, QSettings::IniFormat);
    return project.value(workspaceSeededKey(), false).toBool();
}

//----------------------------------------------

void AppSettings::seedProjectFromDefaults()
{
    _project = _defaults;
    // Тела профилей не копируем: в проекте только ссылки (id) на config/ai.
    _project.profiles.clear();
    ensureWorkerProfileIds(&_project);
    ensureWorkerPresetsSeeded(&_project);
    ensureRootWorkerPresetId(&_project);
    saveProjectWorkspace();
}

//----------------------------------------------

void AppSettings::loadProjectWorkspace()
{
    const QString path = projectIniPath();
    if (path.isEmpty())
    {
        seedProjectFromDefaults();
        return;
    }
    _project = WorkspaceSettings();
    migrateProjectLocalProfiles();
    QSettings project(path, QSettings::IniFormat);
    loadWorkspaceIni(&project, &_project);
    _project.profiles.clear();
    ensureWorkerPresetsSeeded(&_project);
    ensureRootWorkerPresetId(&_project);
    if (copyMissingDefaultPresets(&_project))
        saveProjectWorkspace();
}

//----------------------------------------------

void AppSettings::saveProjectWorkspace() const
{
    if (!_projectOpen)
        return;
    const QString dir = projectStoreDir(_projectRoot);
    if (dir.isEmpty() || !QDir(dir).exists())
        return;
    QSettings project(projectIniPath(), QSettings::IniFormat);
    saveWorkspaceIni(&project, _project);
    project.sync();
}

//----------------------------------------------

void AppSettings::saveDefaultsWorkspace() const
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    saveWorkspaceIni(&settings, _defaults);
    settings.sync();
    saveProfiles();
}

//----------------------------------------------

void AppSettings::setProjectRoot(QString root)
{
    flushPersist();
    const QString clean = QDir::cleanPath(root.trimmed());
    if (_projectRoot == clean && _projectOpen == !clean.isEmpty())
    {
        // Тот же корень, но ___project___ пересоздан: статистика и настройки
        // проекта заново берутся из defaults.
        if (!clean.isEmpty() && !QFileInfo(projectIniPath()).exists())
        {
            _projectUsage.clear();
            seedProjectFromDefaults();
            emit projectSettingsChanged();
            emitWorkspaceSignals(SettingsKindProject, true, true, true, true, true);
            emit usageChanged();
        }
        return;
    }

    save();
    if (_statsTimer)
        _statsTimer->stop();
    _projectRoot = clean;
    _projectOpen = !clean.isEmpty();
    if (_projectOpen)
    {
        // seeded читаем до seedProjectFromDefaults — иначе свежий ini
        // посчитается «старым» и можно подтянуть чужие/пустые ключи неверно.
        const bool seeded = projectWorkspaceSeeded();
        if (seeded)
        {
            loadProjectWorkspace();
            if (QFileInfo(projectIniPath()).exists())
            {
                QSettings project(projectIniPath(), QSettings::IniFormat);
                loadUsage(&project, &_projectUsage);
            }
            else
                _projectUsage.clear();
        }
        else
        {
            seedProjectFromDefaults();
            _projectUsage.clear();
        }
        maybeSeedGlobalUsage();
    }
    else
    {
        _project = WorkspaceSettings();
        _projectUsage.clear();
    }
    emit projectSettingsChanged();
    emitWorkspaceSignals(activeKind(), true, true, true, true, true);
    emit usageChanged();
}

//----------------------------------------------

void AppSettings::load()
{
    migrateFromNativeRegistry();

    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    _defaults = WorkspaceSettings();
    loadProfiles();
    loadWorkspaceIni(&settings, &_defaults);
    migrateRoleBudgetsIntoProfiles();
    ensureWorkerPresetsSeeded(&_defaults);
    ensureRootWorkerPresetId(&_defaults);

    _fontPointSize = settings.value(QString::fromUtf8("ui/fontPointSize"), 11).toInt();
    if (_fontPointSize < 8)
        _fontPointSize = 8;
    if (_fontPointSize > 24)
        _fontPointSize = 24;
    const int appearanceVersion = settings.value(
                                      QString::fromUtf8("ui/appearanceVersion"), 0).toInt();
    if (appearanceVersion < kAppearanceVersion)
        applyLightDefaults();
    else
    {
        _textColor = QColor(settings.value(QString::fromUtf8("ui/textColor"),
                                           QString::fromUtf8("#1a1a1a")).toString());
        _windowColor = QColor(settings.value(QString::fromUtf8("ui/windowColor"),
                                             QString::fromUtf8("#f7f7f8")).toString());
        _panelColor = QColor(settings.value(QString::fromUtf8("ui/panelColor"),
                                            QString::fromUtf8("#ffffff")).toString());
        _accentColor = QColor(settings.value(QString::fromUtf8("ui/accentColor"),
                                             QString::fromUtf8("#262626")).toString());
        _chatUserColor = QColor(settings.value(QString::fromUtf8("ui/chatUserColor"),
                                               QString::fromUtf8("#ffffff")).toString());
        _chatAssistantColor = QColor(settings.value(QString::fromUtf8("ui/chatAssistantColor"),
                                                    QString::fromUtf8("#ffffff")).toString());
        _chatOtherColor = QColor(settings.value(QString::fromUtf8("ui/chatOtherColor"),
                                                QString::fromUtf8("#f3f4f6")).toString());
        if (!_textColor.isValid())
            _textColor = QColor(QString::fromUtf8("#1a1a1a"));
        if (!_windowColor.isValid())
            _windowColor = QColor(QString::fromUtf8("#f7f7f8"));
        if (!_panelColor.isValid())
            _panelColor = QColor(QString::fromUtf8("#ffffff"));
        if (!_accentColor.isValid())
            _accentColor = QColor(QString::fromUtf8("#262626"));
        if (!_chatUserColor.isValid())
            _chatUserColor = QColor(QString::fromUtf8("#ffffff"));
        if (!_chatAssistantColor.isValid())
            _chatAssistantColor = QColor(QString::fromUtf8("#ffffff"));
        if (!_chatOtherColor.isValid())
            _chatOtherColor = QColor(QString::fromUtf8("#f3f4f6"));
    }
    loadUsage(&settings, &_globalUsage);
    _projectUsage.clear();
    _showForwardedContext = settings.value(QString::fromUtf8("ui/showForwardedContext"),
                                           true).toBool();
    _autoApprovePermissions = settings.value(QString::fromUtf8("ui/autoApprovePermissions"),
                                             false).toBool();
    _uiLanguage = settings.value(QString::fromUtf8("ui/language"),
                                 QString::fromUtf8("ru")).toString().trimmed().toLower();
    if (_uiLanguage.isEmpty())
        _uiLanguage = QString::fromUtf8("ru");
    _mainWindowGeometry = QByteArray::fromBase64(
        settings.value(QString::fromUtf8("ui/mainWindowGeometry")).toString().toLatin1());
    _mainWindowState = QByteArray::fromBase64(
        settings.value(QString::fromUtf8("ui/mainWindowState")).toString().toLatin1());
    _leftDockWidth = settings.value(QString::fromUtf8("ui/leftDockWidth"), 0).toInt();
    _projectDockHeight = settings.value(QString::fromUtf8("ui/projectDockHeight"), 0).toInt();
    _graphThumbDockHeight = settings.value(QString::fromUtf8("ui/graphThumbDockHeight"), 0).toInt();

    // Проект ещё не открыт: активными остаются defaults.
    _projectOpen = false;
    _project = _defaults;
}

//----------------------------------------------

void AppSettings::save() const
{
    const_cast<AppSettings *>(this)->flushPersist();
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue(QString::fromUtf8("ui/fontPointSize"), _fontPointSize);
    settings.setValue(QString::fromUtf8("ui/appearanceVersion"), kAppearanceVersion);
    settings.setValue(QString::fromUtf8("ui/textColor"), _textColor.name());
    settings.setValue(QString::fromUtf8("ui/windowColor"), _windowColor.name());
    settings.setValue(QString::fromUtf8("ui/panelColor"), _panelColor.name());
    settings.setValue(QString::fromUtf8("ui/accentColor"), _accentColor.name());
    settings.setValue(QString::fromUtf8("ui/chatUserColor"), _chatUserColor.name());
    settings.setValue(QString::fromUtf8("ui/chatAssistantColor"), _chatAssistantColor.name());
    settings.setValue(QString::fromUtf8("ui/chatOtherColor"), _chatOtherColor.name());
    settings.setValue(QString::fromUtf8("ui/showForwardedContext"), _showForwardedContext);
    settings.setValue(QString::fromUtf8("ui/autoApprovePermissions"), _autoApprovePermissions);
    settings.setValue(QString::fromUtf8("ui/language"), _uiLanguage);
    if (!_mainWindowGeometry.isEmpty())
        settings.setValue(QString::fromUtf8("ui/mainWindowGeometry"),
                          QString::fromLatin1(_mainWindowGeometry.toBase64()));
    if (!_mainWindowState.isEmpty())
        settings.setValue(QString::fromUtf8("ui/mainWindowState"),
                          QString::fromLatin1(_mainWindowState.toBase64()));
    if (_leftDockWidth > 0)
        settings.setValue(QString::fromUtf8("ui/leftDockWidth"), _leftDockWidth);
    if (_projectDockHeight > 0)
        settings.setValue(QString::fromUtf8("ui/projectDockHeight"), _projectDockHeight);
    if (_graphThumbDockHeight > 0)
        settings.setValue(QString::fromUtf8("ui/graphThumbDockHeight"), _graphThumbDockHeight);
    saveUsage(&settings, _globalUsage);
    settings.sync();

    saveDefaultsWorkspace();

    if (_projectOpen)
    {
        const QString dir = projectStoreDir(_projectRoot);
        if (!dir.isEmpty() && QDir(dir).exists())
        {
            QSettings project(projectIniPath(), QSettings::IniFormat);
            saveUsage(&project, _projectUsage);
            project.sync();
            saveProjectWorkspace();
        }
    }
}

//----------------------------------------------

void AppSettings::save(SettingsKind kind) const
{
    if (kind == SettingsKindProject)
    {
        saveProjectWorkspace();
        return;
    }
    saveDefaultsWorkspace();
}

//----------------------------------------------

bool AppSettings::isCommandAlwaysAllowed(QString programKey) const
{
    return isCommandAlwaysAllowed(activeKind(), programKey);
}

//----------------------------------------------

bool AppSettings::isCommandAlwaysAllowed(SettingsKind kind, QString programKey) const
{
    const QString key = normalizeKey(programKey);
    if (key.isEmpty())
        return false;
    const WorkspaceSettings &bundle = ws(kind);
    if (bundle.alwaysCommands.contains(key))
        return true;
    foreach (const PermissionGrant &grant, bundle.permissionGrants)
    {
        if (normalizeKey(grant.programKey) == key)
            return true;
        if (normalizeKey(grant.actionText) == key)
            return true;
    }
    return false;
}

//----------------------------------------------

void AppSettings::addAlwaysAllowedCommand(QString programKey, QString actionText)
{
    addAlwaysAllowedCommand(activeKind(), programKey, actionText);
}

//----------------------------------------------

void AppSettings::addAlwaysAllowedCommand(SettingsKind kind, QString programKey,
                                          QString actionText)
{
    const QString key = normalizeKey(programKey);
    if (key.isEmpty())
        return;
    WorkspaceSettings &bundle = ws(kind);
    if (!actionText.trimmed().isEmpty())
    {
        for (int i = 0; i < bundle.permissionGrants.size(); ++i)
        {
            if (bundle.permissionGrants.at(i).programKey == key)
            {
                bundle.permissionGrants[i].actionText = actionText.trimmed();
                bundle.alwaysCommands.insert(key);
                schedulePersist(kind);
                emit alwaysCommandsChanged();
                return;
            }
        }
        PermissionGrant grant;
        grant.programKey = key;
        grant.actionText = actionText.trimmed();
        bundle.permissionGrants.append(grant);
    }
    else if (bundle.alwaysCommands.contains(key))
    {
        return;
    }
    else
    {
        PermissionGrant grant;
        grant.programKey = key;
        grant.actionText = key;
        bundle.permissionGrants.append(grant);
    }
    bundle.alwaysCommands.insert(key);
    schedulePersist(kind);
    emit alwaysCommandsChanged();
}

//----------------------------------------------

void AppSettings::addAlwaysAllowedCommands(const QStringList &keys)
{
    addAlwaysAllowedCommands(activeKind(), keys);
}

//----------------------------------------------

void AppSettings::addAlwaysAllowedCommands(SettingsKind kind, const QStringList &keys)
{
    WorkspaceSettings &bundle = ws(kind);
    bool changed = false;
    foreach (const QString &item, keys)
    {
        const QString key = normalizeKey(item);
        if (key.isEmpty() || bundle.alwaysCommands.contains(key))
            continue;
        PermissionGrant grant;
        grant.programKey = key;
        grant.actionText = key;
        bundle.permissionGrants.append(grant);
        bundle.alwaysCommands.insert(key);
        changed = true;
    }
    if (!changed)
        return;
    schedulePersist(kind);
    emit alwaysCommandsChanged();
}

//----------------------------------------------

void AppSettings::removeAlwaysAllowedCommand(QString programKey)
{
    removeAlwaysAllowedCommand(activeKind(), programKey);
}

//----------------------------------------------

void AppSettings::removeAlwaysAllowedCommand(SettingsKind kind, QString programKey)
{
    const QString key = normalizeKey(programKey);
    if (key.isEmpty())
        return;
    WorkspaceSettings &bundle = ws(kind);
    for (int i = bundle.permissionGrants.size() - 1; i >= 0; --i)
    {
        if (bundle.permissionGrants.at(i).programKey == key)
            bundle.permissionGrants.removeAt(i);
    }
    if (!bundle.alwaysCommands.contains(key))
        return;
    bundle.alwaysCommands.remove(key);
    schedulePersist(kind);
    emit alwaysCommandsChanged();
}

//----------------------------------------------

QStringList AppSettings::alwaysAllowedCommands() const
{
    return alwaysAllowedCommands(activeKind());
}

//----------------------------------------------

QStringList AppSettings::alwaysAllowedCommands(SettingsKind kind) const
{
    return ws(kind).alwaysCommands.toList();
}

//----------------------------------------------

QList<PermissionGrant> AppSettings::permissionGrants() const
{
    return permissionGrants(activeKind());
}

//----------------------------------------------

QList<PermissionGrant> AppSettings::permissionGrants(SettingsKind kind) const
{
    return ws(kind).permissionGrants;
}

//----------------------------------------------

bool AppSettings::shellControlEnabled() const
{
    return shellControlEnabled(activeKind());
}

//----------------------------------------------

bool AppSettings::shellControlEnabled(SettingsKind kind) const
{
    return ws(kind).shellControl;
}

//----------------------------------------------

void AppSettings::setShellControlEnabled(bool enabled)
{
    setShellControlEnabled(activeKind(), enabled);
}

//----------------------------------------------

void AppSettings::setShellControlEnabled(SettingsKind kind, bool enabled)
{
    WorkspaceSettings &bundle = ws(kind);
    if (bundle.shellControl == enabled)
        return;
    bundle.shellControl = enabled;
    schedulePersist(kind);
    emit controlFlagsChanged();
}

//----------------------------------------------

bool AppSettings::outsideFilesControlEnabled() const
{
    return outsideFilesControlEnabled(activeKind());
}

//----------------------------------------------

bool AppSettings::outsideFilesControlEnabled(SettingsKind kind) const
{
    return ws(kind).outsideFilesControl;
}

//----------------------------------------------

void AppSettings::setOutsideFilesControlEnabled(bool enabled)
{
    setOutsideFilesControlEnabled(activeKind(), enabled);
}

//----------------------------------------------

void AppSettings::setOutsideFilesControlEnabled(SettingsKind kind, bool enabled)
{
    WorkspaceSettings &bundle = ws(kind);
    if (bundle.outsideFilesControl == enabled)
        return;
    bundle.outsideFilesControl = enabled;
    schedulePersist(kind);
    emit controlFlagsChanged();
}

//----------------------------------------------

bool AppSettings::systemSettingsControlEnabled() const
{
    return systemSettingsControlEnabled(activeKind());
}

//----------------------------------------------

bool AppSettings::systemSettingsControlEnabled(SettingsKind kind) const
{
    return ws(kind).systemSettingsControl;
}

//----------------------------------------------

void AppSettings::setSystemSettingsControlEnabled(bool enabled)
{
    setSystemSettingsControlEnabled(activeKind(), enabled);
}

//----------------------------------------------

void AppSettings::setSystemSettingsControlEnabled(SettingsKind kind, bool enabled)
{
    WorkspaceSettings &bundle = ws(kind);
    if (bundle.systemSettingsControl == enabled)
        return;
    bundle.systemSettingsControl = enabled;
    schedulePersist(kind);
    emit controlFlagsChanged();
}

//----------------------------------------------

int AppSettings::fontPointSize() const
{
    return _fontPointSize;
}

//----------------------------------------------

void AppSettings::setFontPointSize(int size)
{
    _fontPointSize = qBound(8, size, 24);
}

//----------------------------------------------

QColor AppSettings::textColor() const
{
    return _textColor;
}

//----------------------------------------------

void AppSettings::setTextColor(QColor color)
{
    if (color.isValid())
        _textColor = color;
}

//----------------------------------------------

QColor AppSettings::windowColor() const
{
    return _windowColor;
}

//----------------------------------------------

void AppSettings::setWindowColor(QColor color)
{
    if (color.isValid())
        _windowColor = color;
}

//----------------------------------------------

QColor AppSettings::panelColor() const
{
    return _panelColor;
}

//----------------------------------------------

void AppSettings::setPanelColor(QColor color)
{
    if (color.isValid())
        _panelColor = color;
}

//----------------------------------------------

QColor AppSettings::accentColor() const
{
    return _accentColor;
}

//----------------------------------------------

void AppSettings::setAccentColor(QColor color)
{
    if (color.isValid())
        _accentColor = color;
}

//----------------------------------------------

QColor AppSettings::chatUserColor() const
{
    return _chatUserColor;
}

//----------------------------------------------

void AppSettings::setChatUserColor(QColor color)
{
    if (color.isValid())
        _chatUserColor = color;
}

//----------------------------------------------

QColor AppSettings::chatAssistantColor() const
{
    return _chatAssistantColor;
}

//----------------------------------------------

void AppSettings::setChatAssistantColor(QColor color)
{
    if (color.isValid())
        _chatAssistantColor = color;
}

//----------------------------------------------

QColor AppSettings::chatOtherColor() const
{
    return _chatOtherColor;
}

//----------------------------------------------

void AppSettings::setChatOtherColor(QColor color)
{
    if (color.isValid())
        _chatOtherColor = color;
}

//----------------------------------------------

QColor AppSettings::inputColor() const
{
    if (isLightTheme())
        return QColor(QString::fromUtf8("#ffffff"));
    QColor c = _panelColor.lighter(118);
    if (!c.isValid())
        c = QColor(QString::fromUtf8("#454545"));
    return c;
}

//----------------------------------------------

QColor AppSettings::toolbarColor() const
{
    // Плоский фон как у окна — без «полосы» другой яркости.
    if (isLightTheme())
        return _windowColor.isValid() ? _windowColor
                                      : QColor(QString::fromUtf8("#f7f7f8"));
    QColor c = _windowColor.lighter(185);
    if (!c.isValid())
        c = QColor(QString::fromUtf8("#4f4f4f"));
    return c;
}

//----------------------------------------------

QColor AppSettings::borderColor() const
{
    return isLightTheme() ? QColor(QString::fromUtf8("#e5e5e5"))
                          : QColor(QString::fromUtf8("#555555"));
}

//----------------------------------------------

QColor AppSettings::workerCardColor(WorkerType type, bool root) const
{
    switch (type)
    {
    case WorkerTypeExecutor:
        return QColor(QString::fromUtf8("#e4f2ea"));
    case WorkerTypeTester:
        return QColor(QString::fromUtf8("#f7f1e4"));
    case WorkerTypeTool:
        return QColor(QString::fromUtf8("#efe8f6"));
    case WorkerTypeManager:
    default:
        return QColor(QString::fromUtf8(root ? "#d5e4f4" : "#e7f0f8"));
    }
}

//----------------------------------------------

QColor AppSettings::workerCardTextColor() const
{
    return QColor(QString::fromUtf8("#243044"));
}

//----------------------------------------------

QColor AppSettings::workerCardMutedColor() const
{
    return QColor(QString::fromUtf8("#5d6d7e"));
}

//----------------------------------------------

QColor AppSettings::mutedTextColor() const
{
    return isLightTheme() ? QColor(QString::fromUtf8("#737373"))
                          : QColor(QString::fromUtf8("#8a8a8a"));
}

//----------------------------------------------

QColor AppSettings::selectionColor() const
{
    return isLightTheme() ? QColor(QString::fromUtf8("#ececec"))
                          : QColor(QString::fromUtf8("#1a5580"));
}

//----------------------------------------------

QColor AppSettings::scrollbarColor() const
{
    return isLightTheme() ? QColor(QString::fromUtf8("#d4d4d4"))
                          : QColor(QString::fromUtf8("#6a6a6a"));
}

//----------------------------------------------

bool AppSettings::isLightTheme() const
{
    return _windowColor.lightness() >= 140;
}

//----------------------------------------------

QColor AppSettings::chatBackgroundColor() const
{
    if (isLightTheme())
        return _windowColor;
    return QColor(_panelColor).darker(108);
}

//----------------------------------------------

bool AppSettings::showForwardedContext() const
{
    return _showForwardedContext;
}

//----------------------------------------------

void AppSettings::setShowForwardedContext(bool enabled)
{
    if (_showForwardedContext == enabled)
        return;
    _showForwardedContext = enabled;
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue(QString::fromUtf8("ui/showForwardedContext"), _showForwardedContext);
    settings.sync();
    emit displayOptionsChanged();
}

//----------------------------------------------

bool AppSettings::autoApprovePermissions() const
{
    return _autoApprovePermissions;
}

//----------------------------------------------

void AppSettings::setAutoApprovePermissions(bool enabled)
{
    if (_autoApprovePermissions == enabled)
        return;
    _autoApprovePermissions = enabled;
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue(QString::fromUtf8("ui/autoApprovePermissions"), _autoApprovePermissions);
    settings.sync();
    emit displayOptionsChanged();
}

//----------------------------------------------

QString AppSettings::uiLanguage() const
{
    return _uiLanguage;
}

//----------------------------------------------

void AppSettings::persistUiLanguage(QString code)
{
    code = code.trimmed().toLower();
    if (code.isEmpty())
        code = QString::fromUtf8("ru");
    _uiLanguage = code;
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue(QString::fromUtf8("ui/language"), _uiLanguage);
    settings.sync();
}

//----------------------------------------------

void AppSettings::setUiLanguage(QString code)
{
    AppLocale::instance()->setLanguage(code);
}

//----------------------------------------------

QByteArray AppSettings::mainWindowGeometry() const
{
    return _mainWindowGeometry;
}

//----------------------------------------------

QByteArray AppSettings::mainWindowState() const
{
    return _mainWindowState;
}

//----------------------------------------------

void AppSettings::setMainWindowLayout(const QByteArray &geometry, const QByteArray &state)
{
    _mainWindowGeometry = geometry;
    _mainWindowState = state;
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    if (!geometry.isEmpty())
        settings.setValue(QString::fromUtf8("ui/mainWindowGeometry"),
                          QString::fromLatin1(geometry.toBase64()));
    if (!state.isEmpty())
        settings.setValue(QString::fromUtf8("ui/mainWindowState"),
                          QString::fromLatin1(state.toBase64()));
    settings.sync();
}

//----------------------------------------------

int AppSettings::leftDockWidth() const
{
    return _leftDockWidth;
}

//----------------------------------------------

int AppSettings::projectDockHeight() const
{
    return _projectDockHeight;
}

//----------------------------------------------

int AppSettings::graphThumbDockHeight() const
{
    return _graphThumbDockHeight;
}

//----------------------------------------------

void AppSettings::setMainWindowDockSplit(int leftWidth, int projectHeight, int graphHeight)
{
    _leftDockWidth = qMax(0, leftWidth);
    _projectDockHeight = qMax(0, projectHeight);
    _graphThumbDockHeight = qMax(0, graphHeight);
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    if (_leftDockWidth > 0)
        settings.setValue(QString::fromUtf8("ui/leftDockWidth"), _leftDockWidth);
    if (_projectDockHeight > 0)
        settings.setValue(QString::fromUtf8("ui/projectDockHeight"), _projectDockHeight);
    if (_graphThumbDockHeight > 0)
        settings.setValue(QString::fromUtf8("ui/graphThumbDockHeight"), _graphThumbDockHeight);
    settings.sync();
}

//----------------------------------------------

void AppSettings::applyLightDefaults()
{
    _textColor = QColor(QString::fromUtf8("#1a1a1a"));
    _windowColor = QColor(QString::fromUtf8("#f7f7f8"));
    _panelColor = QColor(QString::fromUtf8("#ffffff"));
    _accentColor = QColor(QString::fromUtf8("#262626"));
    _chatUserColor = QColor(QString::fromUtf8("#ffffff"));
    _chatAssistantColor = QColor(QString::fromUtf8("#ffffff"));
    _chatOtherColor = QColor(QString::fromUtf8("#f3f4f6"));
}

//----------------------------------------------

void UsageStats::clear()
{
    promptTokens = 0;
    completionTokens = 0;
    totalTokens = 0;
    cachedPromptTokens = 0;
    requestCount = 0;
    workMs = 0;
    byRole.clear();
}

//----------------------------------------------

void UsageStats::add(int prompt, int completion, int total, int cached, QString role)
{
    if (prompt > 0)
        promptTokens += prompt;
    if (completion > 0)
        completionTokens += completion;
    if (total > 0)
        totalTokens += total;
    else if (prompt > 0 || completion > 0)
        totalTokens += qMax(0, prompt) + qMax(0, completion);
    if (cached > 0)
        cachedPromptTokens += cached;
    ++requestCount;

    const QString key = role.trimmed().isEmpty() ? QString::fromUtf8("other") : role.trimmed();
    RoleUsageStats &usage = byRole[key];
    ++usage.requests;
    usage.promptTokens += qMax(0, prompt);
    usage.completionTokens += qMax(0, completion);
    usage.cachedPromptTokens += qMax(0, cached);
}

//----------------------------------------------

void AppSettings::addUsage(int promptTokens, int completionTokens, int totalTokens,
                           int cachedPromptTokens, QString role)
{
    _globalUsage.add(promptTokens, completionTokens, totalTokens, cachedPromptTokens, role);
    if (_projectOpen)
        _projectUsage.add(promptTokens, completionTokens, totalTokens, cachedPromptTokens, role);
    scheduleStatsSave();
}

//----------------------------------------------

int AppSettings::beginWork()
{
    const int id = ++_nextWorkId;
    if (id <= 0)
        _nextWorkId = 1;
    const int stored = _nextWorkId;
    _openWork.insert(stored, QDateTime::currentMSecsSinceEpoch());
    return stored;
}

//----------------------------------------------

void AppSettings::endWork(int id)
{
    if (id <= 0 || !_openWork.contains(id))
        return;
    const qint64 started = _openWork.take(id);
    const qint64 ms = QDateTime::currentMSecsSinceEpoch() - started;
    if (ms <= 0)
        return;
    _globalUsage.workMs += ms;
    if (_projectOpen)
        _projectUsage.workMs += ms;
    scheduleStatsSave();
}

//----------------------------------------------

qint64 AppSettings::liveWorkMs(bool project) const
{
    if (project && !_projectOpen)
        return 0;
    qint64 total = project ? _projectUsage.workMs : _globalUsage.workMs;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (QMap<int, qint64>::const_iterator it = _openWork.constBegin();
         it != _openWork.constEnd(); ++it)
        total += qMax(qint64(0), now - it.value());
    return total;
}

//----------------------------------------------

UsageStats AppSettings::globalUsage() const
{
    return _globalUsage;
}

//----------------------------------------------

UsageStats AppSettings::projectUsage() const
{
    return _projectUsage;
}

//----------------------------------------------

qint64 AppSettings::cachedPromptTokens() const
{
    return _globalUsage.cachedPromptTokens;
}

//----------------------------------------------

QStringList AppSettings::usageRoles() const
{
    return _globalUsage.byRole.keys();
}

//----------------------------------------------

qint64 AppSettings::roleRequestCount(QString role) const
{
    return _globalUsage.byRole.value(role).requests;
}

//----------------------------------------------

qint64 AppSettings::rolePromptTokens(QString role) const
{
    return _globalUsage.byRole.value(role).promptTokens;
}

//----------------------------------------------

qint64 AppSettings::roleCompletionTokens(QString role) const
{
    return _globalUsage.byRole.value(role).completionTokens;
}

//----------------------------------------------

qint64 AppSettings::roleCachedPromptTokens(QString role) const
{
    return _globalUsage.byRole.value(role).cachedPromptTokens;
}

//----------------------------------------------

void AppSettings::loadUsage(QSettings *settings, UsageStats *stats)
{
    if (!stats)
        return;
    stats->clear();
    if (!settings)
        return;
    stats->promptTokens = settings->value(QString::fromUtf8("stats/promptTokens"), 0).toLongLong();
    stats->completionTokens = settings->value(QString::fromUtf8("stats/completionTokens"), 0).toLongLong();
    stats->totalTokens = settings->value(QString::fromUtf8("stats/totalTokens"), 0).toLongLong();
    stats->requestCount = settings->value(QString::fromUtf8("stats/requestCount"), 0).toLongLong();
    stats->cachedPromptTokens = settings->value(QString::fromUtf8("stats/cachedPromptTokens"), 0).toLongLong();
    stats->workMs = settings->value(QString::fromUtf8("stats/workMs"), 0).toLongLong();
    settings->beginGroup(QString::fromUtf8("statsByRole"));
    foreach (const QString &role, settings->childGroups())
    {
        settings->beginGroup(role);
        RoleUsageStats usage;
        usage.requests = settings->value(QString::fromUtf8("requests"), 0).toLongLong();
        usage.promptTokens = settings->value(QString::fromUtf8("promptTokens"), 0).toLongLong();
        usage.completionTokens = settings->value(QString::fromUtf8("completionTokens"), 0).toLongLong();
        usage.cachedPromptTokens = settings->value(QString::fromUtf8("cachedPromptTokens"), 0).toLongLong();
        settings->endGroup();
        stats->byRole.insert(role, usage);
    }
    settings->endGroup();
}

//----------------------------------------------

void AppSettings::saveUsage(QSettings *settings, const UsageStats &stats) const
{
    if (!settings)
        return;
    settings->setValue(QString::fromUtf8("stats/promptTokens"), stats.promptTokens);
    settings->setValue(QString::fromUtf8("stats/completionTokens"), stats.completionTokens);
    settings->setValue(QString::fromUtf8("stats/totalTokens"), stats.totalTokens);
    settings->setValue(QString::fromUtf8("stats/requestCount"), stats.requestCount);
    settings->setValue(QString::fromUtf8("stats/cachedPromptTokens"), stats.cachedPromptTokens);
    settings->setValue(QString::fromUtf8("stats/workMs"), stats.workMs);
    settings->remove(QString::fromUtf8("statsByRole"));
    saveRoleStats(settings, stats);
}

//----------------------------------------------

void AppSettings::resetUsage()
{
    _projectUsage.clear();
    if (_statsTimer)
        _statsTimer->stop();
    saveStats();
    emit usageChanged();
}

//----------------------------------------------

void AppSettings::reseedOpenProject()
{
    if (!_projectOpen)
        return;
    _projectUsage.clear();
    if (_statsTimer)
        _statsTimer->stop();
    seedProjectFromDefaults();
    saveStats();
    emit projectSettingsChanged();
    emitWorkspaceSignals(SettingsKindProject, true, true, true, true, true);
    emit usageChanged();
}

//----------------------------------------------

void AppSettings::resetGlobalUsage()
{
    _globalUsage.clear();
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue(QString::fromUtf8("stats/initialized"), true);
    saveUsage(&settings, _globalUsage);
    saveStats();
    emit usageChanged();
}

//----------------------------------------------

void AppSettings::saveRoleStats(QSettings *settings, const UsageStats &stats) const
{
    if (!settings)
        return;
    settings->beginGroup(QString::fromUtf8("statsByRole"));
    for (QMap<QString, RoleUsageStats>::const_iterator it = stats.byRole.constBegin();
         it != stats.byRole.constEnd(); ++it)
    {
        settings->beginGroup(it.key());
        settings->setValue(QString::fromUtf8("requests"), it.value().requests);
        settings->setValue(QString::fromUtf8("promptTokens"), it.value().promptTokens);
        settings->setValue(QString::fromUtf8("completionTokens"), it.value().completionTokens);
        settings->setValue(QString::fromUtf8("cachedPromptTokens"), it.value().cachedPromptTokens);
        settings->endGroup();
    }
    settings->endGroup();
}

//----------------------------------------------

void AppSettings::saveStats() const
{
    QSettings global(settingsFilePath(), QSettings::IniFormat);
    global.setValue(QString::fromUtf8("stats/initialized"), true);
    saveUsage(&global, _globalUsage);
    if (!_projectOpen)
        return;
    const QString dir = projectStoreDir(_projectRoot);
    if (dir.isEmpty() || !QDir(dir).exists())
        return;
    QSettings project(projectIniPath(), QSettings::IniFormat);
    saveUsage(&project, _projectUsage);
}

//----------------------------------------------

void AppSettings::maybeSeedGlobalUsage()
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    if (settings.contains(QString::fromUtf8("stats/initialized")))
        return;
    if (_globalUsage.requestCount <= 0 && _projectUsage.requestCount > 0)
        _globalUsage = _projectUsage;
    settings.setValue(QString::fromUtf8("stats/initialized"), true);
    saveUsage(&settings, _globalUsage);
}

//----------------------------------------------

void AppSettings::scheduleStatsSave()
{
    if (!_statsTimer)
    {
        _statsTimer = new QTimer(this);
        _statsTimer->setSingleShot(true);
        connect(_statsTimer, SIGNAL(timeout()), this, SLOT(sltFlushStats()));
    }
    if (!_statsTimer->isActive())
        _statsTimer->start(5000);
}

//----------------------------------------------

void AppSettings::schedulePersist(SettingsKind kind)
{
    if (kind == SettingsKindProject)
        _persistProjectPending = true;
    else
        _persistDefaultsPending = true;
    if (!_persistTimer)
    {
        _persistTimer = new QTimer(this);
        _persistTimer->setSingleShot(true);
        connect(_persistTimer, SIGNAL(timeout()), this, SLOT(sltFlushPersist()));
    }
    _persistTimer->start(400);
}

//----------------------------------------------

void AppSettings::flushPersist()
{
    if (_persistTimer)
        _persistTimer->stop();
    sltFlushPersist();
}

//----------------------------------------------

void AppSettings::sltFlushPersist()
{
    if (_persistDefaultsPending)
    {
        _persistDefaultsPending = false;
        saveDefaultsWorkspace();
    }
    if (_persistProjectPending)
    {
        _persistProjectPending = false;
        saveProjectWorkspace();
    }
}

//----------------------------------------------

void AppSettings::scheduleWorkerLimitsSignal()
{
    if (!_limitsSignalTimer)
    {
        _limitsSignalTimer = new QTimer(this);
        _limitsSignalTimer->setSingleShot(true);
        connect(_limitsSignalTimer, SIGNAL(timeout()),
                this, SLOT(sltEmitWorkerLimitsChanged()));
    }
    _limitsSignalTimer->start(300);
}

//----------------------------------------------

void AppSettings::sltEmitWorkerLimitsChanged()
{
    emit workerLimitsChanged();
}

//----------------------------------------------

void AppSettings::scheduleProfilesSignal()
{
    if (!_profilesSignalTimer)
    {
        _profilesSignalTimer = new QTimer(this);
        _profilesSignalTimer->setSingleShot(true);
        connect(_profilesSignalTimer, SIGNAL(timeout()),
                this, SLOT(sltEmitProfilesChanged()));
    }
    _profilesSignalTimer->start(300);
}

//----------------------------------------------

void AppSettings::sltEmitProfilesChanged()
{
    emit profilesChanged();
}

//----------------------------------------------

void AppSettings::scheduleWorkerPresetsSignal()
{
    if (!_presetsSignalTimer)
    {
        _presetsSignalTimer = new QTimer(this);
        _presetsSignalTimer->setSingleShot(true);
        connect(_presetsSignalTimer, SIGNAL(timeout()),
                this, SLOT(sltEmitWorkerPresetsChanged()));
    }
    _presetsSignalTimer->start(300);
}

//----------------------------------------------

void AppSettings::sltEmitWorkerPresetsChanged()
{
    emit workerPresetsChanged();
}

//----------------------------------------------

bool AppSettings::profilesEqual(const LlmProfile &a, const LlmProfile &b)
{
    return a.id == b.id
           && a.name == b.name
           && a.baseUrl == b.baseUrl
           && a.model == b.model
           && a.apiKey == b.apiKey
           && a.modality == b.modality
           && a.purpose == b.purpose
           && a.balanceUrl == b.balanceUrl
           && a.balanceJsonPath == b.balanceJsonPath
           && a.requestPath == b.requestPath
           && a.taskPath == b.taskPath
           && a.catalogUrl == b.catalogUrl
           && a.extraBody == b.extraBody
           && a.maxToolIterations == b.maxToolIterations
           && a.contextCap == b.contextCap;
}

//----------------------------------------------

bool AppSettings::presetsEqual(const WorkerPreset &a, const WorkerPreset &b)
{
    return a.id == b.id
           && a.type == b.type
           && a.profileId == b.profileId
           && a.taskKind == b.taskKind;
}

//----------------------------------------------

void AppSettings::sltFlushStats()
{
    saveStats();
}

//----------------------------------------------

int AppSettings::maxWorkerDepth() const
{
    return maxWorkerDepth(activeKind());
}

//----------------------------------------------

int AppSettings::maxWorkerDepth(SettingsKind kind) const
{
    return ws(kind).maxWorkerDepth;
}

//----------------------------------------------

void AppSettings::setMaxWorkerDepth(int depth)
{
    setMaxWorkerDepth(activeKind(), depth);
}

//----------------------------------------------

void AppSettings::setMaxWorkerDepth(SettingsKind kind, int depth)
{
    const int value = qBound(kMinWorkerDepth, depth, kMaxWorkerDepth);
    WorkspaceSettings &bundle = ws(kind);
    if (bundle.maxWorkerDepth == value)
        return;
    bundle.maxWorkerDepth = value;
    schedulePersist(kind);
    if (kind == activeKind())
        scheduleWorkerLimitsSignal();
}

//----------------------------------------------

int AppSettings::maxConcurrentWorkers() const
{
    return maxConcurrentWorkers(activeKind());
}

//----------------------------------------------

int AppSettings::maxConcurrentWorkers(SettingsKind kind) const
{
    return ws(kind).maxConcurrentWorkers;
}

//----------------------------------------------

void AppSettings::setMaxConcurrentWorkers(int count)
{
    setMaxConcurrentWorkers(activeKind(), count);
}

//----------------------------------------------

void AppSettings::setMaxConcurrentWorkers(SettingsKind kind, int count)
{
    const int value = qBound(kMinConcurrentWorkers, count, kMaxConcurrentWorkers);
    WorkspaceSettings &bundle = ws(kind);
    if (bundle.maxConcurrentWorkers == value)
        return;
    bundle.maxConcurrentWorkers = value;
    schedulePersist(kind);
    if (kind == activeKind())
        scheduleWorkerLimitsSignal();
}

//----------------------------------------------

int AppSettings::llmTimeoutSec() const
{
    return llmTimeoutSec(activeKind());
}

//----------------------------------------------

int AppSettings::llmTimeoutSec(SettingsKind kind) const
{
    return ws(kind).llmTimeoutSec;
}

//----------------------------------------------

void AppSettings::setLlmTimeoutSec(int seconds)
{
    setLlmTimeoutSec(activeKind(), seconds);
}

//----------------------------------------------

void AppSettings::setLlmTimeoutSec(SettingsKind kind, int seconds)
{
    const int value = qBound(kMinLlmTimeoutSec, seconds, kMaxLlmTimeoutSec);
    WorkspaceSettings &bundle = ws(kind);
    if (bundle.llmTimeoutSec == value)
        return;
    bundle.llmTimeoutSec = value;
    schedulePersist(kind);
    if (kind == activeKind())
        scheduleWorkerLimitsSignal();
}

//----------------------------------------------

int AppSettings::normalizeProfileContextCap(int tokens)
{
    int value = tokens;
    if (value < 0)
        value = 0;
    if (value > 0 && value < 4096)
        value = 4096;
    if (value > kMaxRoleContextCap)
        value = kMaxRoleContextCap;
    if (value == 128000)
        value = 0;
    return value;
}

//----------------------------------------------

void AppSettings::migrateRoleBudgetsIntoProfiles()
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    if (settings.value(QString::fromUtf8("ui/roleBudgetMigratedToProfiles"), false).toBool())
        return;

    int roleToolIterations[4];
    int roleContextCap[4];
    for (int i = 0; i < 4; ++i)
    {
        roleToolIterations[i] = kDefaultRoleToolIterations[i];
        roleContextCap[i] = kDefaultRoleContextCap[i];
    }
    settings.beginGroup(QString::fromUtf8("workers/roleBudget"));
    for (int i = 0; i < 4; ++i)
    {
        const QString key = roleBudgetKey(i);
        roleToolIterations[i] =
            qBound(kMinRoleToolIterations,
                   settings.value(key + QString::fromUtf8("/toolIterations"),
                                   kDefaultRoleToolIterations[i]).toInt(),
                   kMaxRoleToolIterations);
        roleContextCap[i] =
            normalizeProfileContextCap(
                settings.value(key + QString::fromUtf8("/contextCap"),
                               kDefaultRoleContextCap[i]).toInt());
    }
    settings.endGroup();

    static const WorkerType roles[4] = {
        WorkerTypeManager, WorkerTypeExecutor, WorkerTypeTester, WorkerTypeTool
    };
    bool changed = false;
    for (int i = 0; i < 4; ++i)
    {
        const LlmProfile mapped = profileForWorkerType(SettingsKindDefaults, roles[i]);
        const int idx = profileIndexById(mapped.id);
        if (idx < 0)
            continue;
        LlmProfile &p = _defaults.profiles[idx];
        if (p.maxToolIterations != roleToolIterations[i]
            || p.contextCap != roleContextCap[i])
        {
            p.maxToolIterations = roleToolIterations[i];
            p.contextCap = roleContextCap[i];
            changed = true;
        }
    }
    if (changed)
        saveProfiles();
    settings.setValue(QString::fromUtf8("ui/roleBudgetMigratedToProfiles"), true);
    settings.sync();
}

//----------------------------------------------

qint64 AppSettings::promptTokens() const
{
    return _globalUsage.promptTokens;
}

//----------------------------------------------

qint64 AppSettings::completionTokens() const
{
    return _globalUsage.completionTokens;
}

//----------------------------------------------

qint64 AppSettings::totalTokens() const
{
    return _globalUsage.totalTokens;
}

//----------------------------------------------

qint64 AppSettings::requestCount() const
{
    return _globalUsage.requestCount;
}

//----------------------------------------------

QString AppSettings::newProfileId()
{
    QString id = QUuid::createUuid().toString();
    if (id.startsWith(QChar('{')))
        id.remove(0, 1);
    if (id.endsWith(QChar('}')))
        id.chop(1);
    return id;
}

//----------------------------------------------

int AppSettings::profileIndexById(QString id) const
{
    const QString trimmed = id.trimmed();
    if (trimmed.isEmpty())
        return -1;
    for (int i = 0; i < _defaults.profiles.size(); ++i)
    {
        if (_defaults.profiles.at(i).id == trimmed)
            return i;
    }
    return -1;
}

//----------------------------------------------

void AppSettings::ensureWorkerProfileIds(WorkspaceSettings *bundle)
{
    if (!bundle)
        return;
    QString fallback;
    if (!_defaults.profiles.isEmpty())
        fallback = _defaults.profiles.first().id;
    for (int i = 0; i < 3; ++i)
    {
        if (profileIndexById(bundle->workerProfileId[i]) < 0)
            bundle->workerProfileId[i] = fallback;
    }
}

//----------------------------------------------

void AppSettings::migrateLegacyProviders(QSettings *settings, WorkspaceSettings *bundle)
{
    if (!settings || !bundle)
        return;

    LlmProfile deepseek;
    deepseek.id = QString::fromUtf8("deepseek");
    deepseek.name = QString::fromUtf8("DeepSeek");
    deepseek.baseUrl = QString::fromUtf8("https://api.deepseek.com");
    deepseek.model = settings->value(QString::fromUtf8("model/deepseek")).toString();
    if (deepseek.model.isEmpty())
        deepseek.model = settings->value(QString::fromUtf8("model"),
                                         QString::fromUtf8("deepseek-v4-flash")).toString();
    deepseek.apiKey = settings->value(QString::fromUtf8("apiKey/deepseek")).toString();
    if (deepseek.apiKey.isEmpty())
        deepseek.apiKey = settings->value(QString::fromUtf8("apiKey")).toString();
    if (deepseek.apiKey.isEmpty())
        deepseek.apiKey = QString::fromLocal8Bit(qgetenv("DEEPSEEK_API_KEY"));
    deepseek.balanceUrl = QString::fromUtf8("/user/balance");
    deepseek.purpose = QString::fromUtf8(
        "General LLM: coding, reasoning, agent tool use");

    LlmProfile groq;
    groq.id = QString::fromUtf8("groq");
    groq.name = QString::fromUtf8("Groq");
    groq.baseUrl = QString::fromUtf8("https://api.groq.com/openai/v1");
    groq.model = settings->value(QString::fromUtf8("model/groq"),
                                 QString::fromUtf8("llama-3.3-70b-versatile")).toString();
    groq.apiKey = settings->value(QString::fromUtf8("apiKey/groq")).toString();
    if (groq.apiKey.isEmpty())
        groq.apiKey = QString::fromLocal8Bit(qgetenv("GROQ_API_KEY"));
    groq.purpose = QString::fromUtf8(
        "Fast general LLM for routine coding and checks");

    LlmProfile qwen;
    qwen.id = QString::fromUtf8("qwencloud");
    qwen.name = QString::fromUtf8("Qwen Cloud");
    qwen.baseUrl = QString::fromUtf8(
        "https://dashscope-intl.aliyuncs.com/compatible-mode/v1");
    qwen.model = settings->value(QString::fromUtf8("model/qwencloud"),
                                 QString::fromUtf8("qwen3.7-plus")).toString();
    qwen.apiKey = settings->value(QString::fromUtf8("apiKey/qwencloud")).toString();
    if (qwen.apiKey.isEmpty())
        qwen.apiKey = QString::fromLocal8Bit(qgetenv("DASHSCOPE_API_KEY"));
    if (qwen.apiKey.isEmpty())
        qwen.apiKey = QString::fromLocal8Bit(qgetenv("QWENCLOUD_API_KEY"));
    qwen.balanceUrl = QString::fromUtf8("/api/v1/quotas");
    qwen.balanceJsonPath = QString::fromUtf8("data.credits");
    qwen.purpose = QString::fromUtf8(
        "General LLM: coding, reasoning, agent tool use");

    bundle->profiles.clear();
    bundle->profiles << deepseek << groq << qwen;

    const QString fallbackProvider = settings->value(QString::fromUtf8("provider"),
                                                     QString::fromUtf8("deepseek")).toString();
    const char *keys[] = { "manager", "executor", "tester" };
    for (int i = 0; i < 3; ++i)
    {
        QString id = settings->value(QString::fromUtf8("workerProfile/")
                                     + QString::fromUtf8(keys[i])).toString();
        if (id.isEmpty())
            id = settings->value(QString::fromUtf8("workerProvider/")
                                 + QString::fromUtf8(keys[i])).toString();
        if (id.isEmpty())
            id = fallbackProvider;
        bundle->workerProfileId[i] = id;
    }
}

//----------------------------------------------

void AppSettings::writeProfileFile(const LlmProfile &profile) const
{
    const QString path = profileFilePath(profile.id);
    if (path.isEmpty())
        return;
    QSettings file(path, QSettings::IniFormat);
    file.setIniCodec("UTF-8");
    file.setValue(QString::fromUtf8("id"), profile.id);
    file.setValue(QString::fromUtf8("name"), profile.name);
    file.setValue(QString::fromUtf8("baseUrl"), profile.baseUrl);
    file.setValue(QString::fromUtf8("model"), profile.model);
    file.setValue(QString::fromUtf8("apiKey"), profile.apiKey);
    file.setValue(QString::fromUtf8("modality"), workerTaskKindToString(profile.modality));
    file.setValue(QString::fromUtf8("purpose"), profile.purpose);
    file.setValue(QString::fromUtf8("balanceUrl"), profile.balanceUrl);
    file.setValue(QString::fromUtf8("balanceJsonPath"), profile.balanceJsonPath);
    file.setValue(QString::fromUtf8("requestPath"), profile.requestPath);
    file.setValue(QString::fromUtf8("taskPath"), profile.taskPath);
    file.setValue(QString::fromUtf8("catalogUrl"), profile.catalogUrl);
    file.setValue(QString::fromUtf8("extraBody"), profile.extraBody);
    file.setValue(QString::fromUtf8("maxToolIterations"), profile.maxToolIterations);
    file.setValue(QString::fromUtf8("contextCap"), profile.contextCap);
    file.sync();
}

//----------------------------------------------

void AppSettings::removeProfileFile(QString id) const
{
    if (id.trimmed().isEmpty())
        return;
    const QString path = profileFilePath(id);
    if (path.isEmpty())
        return;
    QFile::remove(path);
}

//----------------------------------------------

void AppSettings::migrateFromNativeRegistry()
{
    if (QFileInfo::exists(settingsFilePath()))
        return;

    // Старые установки писали в реестр / ~/.config — один раз переносим сюда.
    QSettings native;
    const QStringList keys = native.allKeys();
    if (keys.isEmpty())
        return;

    QSettings dest(settingsFilePath(), QSettings::IniFormat);
    foreach (const QString &key, keys)
    {
        if (key.startsWith(QString::fromUtf8("llmProfiles")))
            continue;
        dest.setValue(key, native.value(key));
    }
    dest.sync();

    const int count = native.beginReadArray(QString::fromUtf8("llmProfiles"));
    for (int i = 0; i < count; ++i)
    {
        native.setArrayIndex(i);
        LlmProfile profile;
        profile.id = native.value(QString::fromUtf8("id")).toString().trimmed();
        profile.name = native.value(QString::fromUtf8("name")).toString();
        profile.baseUrl = native.value(QString::fromUtf8("baseUrl")).toString();
        profile.model = native.value(QString::fromUtf8("model")).toString();
        profile.apiKey = native.value(QString::fromUtf8("apiKey")).toString();
        profile.balanceUrl = native.value(QString::fromUtf8("balanceUrl")).toString();
        profile.balanceJsonPath = native.value(QString::fromUtf8("balanceJsonPath")).toString();
        if (profile.id.isEmpty())
            profile.id = newProfileId();
        if (profile.balanceUrl.isEmpty()
            && (profile.id == QString::fromUtf8("deepseek")
                || profile.baseUrl.contains(QString::fromUtf8("deepseek"), Qt::CaseInsensitive)))
            profile.balanceUrl = QString::fromUtf8("/user/balance");
        writeProfileFile(profile);
    }
    native.endArray();
}

//----------------------------------------------

void AppSettings::migrateProjectLocalProfiles()
{
    // Старые проекты хранили полные профили в ___project___/ai — переносим
    // отсутствующие id в config/ai и удаляем локальную копию.
    const QString store = projectStoreDir(_projectRoot);
    if (store.isEmpty())
        return;
    const QString dirPath = QDir::cleanPath(store + QString::fromUtf8("/ai"));
    if (!QDir(dirPath).exists())
        return;

    bool added = false;
    const QDir dir(dirPath);
    const QStringList files = dir.entryList(QStringList() << QString::fromUtf8("*.ini"),
                                            QDir::Files, QDir::Name);
    foreach (const QString &fileName, files)
    {
        QSettings file(dir.filePath(fileName), QSettings::IniFormat);
        file.setIniCodec("UTF-8");
        LlmProfile profile;
        profile.id = file.value(QString::fromUtf8("id")).toString().trimmed();
        profile.name = file.value(QString::fromUtf8("name")).toString();
        profile.baseUrl = file.value(QString::fromUtf8("baseUrl")).toString();
        profile.model = file.value(QString::fromUtf8("model")).toString();
        profile.apiKey = file.value(QString::fromUtf8("apiKey")).toString();
        profile.modality = workerTaskKindFromString(
            file.value(QString::fromUtf8("modality")).toString());
        profile.purpose = file.value(QString::fromUtf8("purpose")).toString();
        profile.balanceUrl = file.value(QString::fromUtf8("balanceUrl")).toString();
        profile.balanceJsonPath = file.value(QString::fromUtf8("balanceJsonPath")).toString();
        if (profile.id.isEmpty())
            profile.id = QFileInfo(fileName).completeBaseName();
        if (profile.name.trimmed().isEmpty())
            profile.name = profile.id;
        if (profileIndexById(profile.id) < 0)
        {
            _defaults.profiles.append(profile);
            writeProfileFile(profile);
            added = true;
        }
        QFile::remove(dir.filePath(fileName));
    }
    dir.rmdir(dirPath);
    if (added)
    {
        ensureWorkerProfileIds(&_defaults);
        QSettings settings(settingsFilePath(), QSettings::IniFormat);
        saveWorkspaceIni(&settings, _defaults);
        settings.sync();
        emit profilesChanged();
    }
}

//----------------------------------------------

void AppSettings::loadProfiles()
{
    _defaults.profiles.clear();

    const QString dirPath = aiProfilesDir();
    {
        const QDir dir(dirPath);
        const QStringList files = dir.entryList(QStringList() << QString::fromUtf8("*.ini"),
                                                QDir::Files, QDir::Name);
        foreach (const QString &fileName, files)
        {
            QSettings file(dir.filePath(fileName), QSettings::IniFormat);
            file.setIniCodec("UTF-8");
            LlmProfile profile;
            profile.id = file.value(QString::fromUtf8("id")).toString().trimmed();
            profile.name = file.value(QString::fromUtf8("name")).toString();
            profile.baseUrl = file.value(QString::fromUtf8("baseUrl")).toString();
            profile.model = file.value(QString::fromUtf8("model")).toString();
            profile.apiKey = file.value(QString::fromUtf8("apiKey")).toString();
            profile.modality = workerTaskKindFromString(
                file.value(QString::fromUtf8("modality")).toString());
            profile.purpose = file.value(QString::fromUtf8("purpose")).toString();
            profile.balanceUrl = file.value(QString::fromUtf8("balanceUrl")).toString();
            profile.balanceJsonPath = file.value(QString::fromUtf8("balanceJsonPath")).toString();
            profile.requestPath = file.value(QString::fromUtf8("requestPath")).toString();
            profile.taskPath = file.value(QString::fromUtf8("taskPath")).toString();
            profile.catalogUrl = file.value(QString::fromUtf8("catalogUrl")).toString();
            profile.extraBody = file.value(QString::fromUtf8("extraBody")).toString();
            profile.maxToolIterations =
                qBound(kMinRoleToolIterations,
                       file.value(QString::fromUtf8("maxToolIterations"),
                                  kDefaultRoleToolIterations[0]).toInt(),
                       kMaxRoleToolIterations);
            profile.contextCap = normalizeProfileContextCap(
                file.value(QString::fromUtf8("contextCap"), 0).toInt());
            if (profile.id.isEmpty())
                profile.id = QFileInfo(fileName).completeBaseName();
            if (profile.name.trimmed().isEmpty())
                profile.name = profile.id;
            _defaults.profiles.append(profile);
        }
    }

    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    if (_defaults.profiles.isEmpty())
    {
        // Старый массив профилей в settings.ini (если миграция уже перенесла ini).
        const int count = settings.beginReadArray(QString::fromUtf8("llmProfiles"));
        for (int i = 0; i < count; ++i)
        {
            settings.setArrayIndex(i);
            LlmProfile profile;
            profile.id = settings.value(QString::fromUtf8("id")).toString().trimmed();
            profile.name = settings.value(QString::fromUtf8("name")).toString();
            profile.baseUrl = settings.value(QString::fromUtf8("baseUrl")).toString();
            profile.model = settings.value(QString::fromUtf8("model")).toString();
            profile.apiKey = settings.value(QString::fromUtf8("apiKey")).toString();
            profile.modality = workerTaskKindFromString(
                settings.value(QString::fromUtf8("modality")).toString());
            profile.purpose = settings.value(QString::fromUtf8("purpose")).toString();
            profile.balanceUrl = settings.value(QString::fromUtf8("balanceUrl")).toString();
            profile.balanceJsonPath =
                settings.value(QString::fromUtf8("balanceJsonPath")).toString();
            if (profile.id.isEmpty())
                profile.id = newProfileId();
            if (profile.balanceUrl.isEmpty()
                && (profile.id == QString::fromUtf8("deepseek")
                    || profile.baseUrl.contains(QString::fromUtf8("deepseek"),
                                                Qt::CaseInsensitive)))
                profile.balanceUrl = QString::fromUtf8("/user/balance");
            _defaults.profiles.append(profile);
        }
        settings.endArray();
        if (!_defaults.profiles.isEmpty())
        {
            settings.remove(QString::fromUtf8("llmProfiles"));
            saveProfiles();
        }
    }

    if (_defaults.profiles.isEmpty())
    {
        migrateLegacyProviders(&settings, &_defaults);
        saveProfiles();
    }

    const char *keys[] = { "manager", "executor", "tester" };
    for (int i = 0; i < 3; ++i)
    {
        QString id = settings.value(QString::fromUtf8("workerProfile/")
                                    + QString::fromUtf8(keys[i])).toString();
        if (id.isEmpty())
            id = settings.value(QString::fromUtf8("workerProvider/")
                                + QString::fromUtf8(keys[i])).toString();
        if (!id.isEmpty())
            _defaults.workerProfileId[i] = id;
    }
    ensureWorkerProfileIds(&_defaults);
}

//----------------------------------------------

void AppSettings::saveProfiles() const
{
    const QString dirPath = aiProfilesDir();
    if (dirPath.isEmpty() || !QDir(dirPath).exists())
        return;

    QSet<QString> keep;
    foreach (const LlmProfile &profile, _defaults.profiles)
    {
        writeProfileFile(profile);
        keep.insert(sanitizeProfileFileStem(profile.id) + QString::fromUtf8(".ini"));
    }

    const QDir dir(dirPath);
    foreach (const QString &fileName,
             dir.entryList(QStringList() << QString::fromUtf8("*.ini"), QDir::Files))
    {
        if (!keep.contains(fileName))
            QFile::remove(dir.filePath(fileName));
    }
}

//----------------------------------------------

QString AppSettings::addProfile(LlmProfile profile)
{
    if (profile.id.trimmed().isEmpty())
        profile.id = newProfileId();
    if (profile.name.trimmed().isEmpty())
        profile.name = QString::fromUtf8("Profile %1").arg(_defaults.profiles.size() + 1);
    _defaults.profiles.append(profile);
    ensureWorkerProfileIds(&_defaults);
    if (_projectOpen)
        ensureWorkerProfileIds(&_project);
    saveDefaultsWorkspace();
    emit profilesChanged();
    return profile.id;
}

//----------------------------------------------

void AppSettings::updateProfile(const LlmProfile &profile)
{
    const int index = profileIndexById(profile.id);
    if (index < 0)
        return;
    const LlmProfile old = _defaults.profiles.at(index);
    LlmProfile next = profile;
    next.contextCap = normalizeProfileContextCap(next.contextCap);
    next.maxToolIterations =
        qBound(kMinRoleToolIterations, next.maxToolIterations, kMaxRoleToolIterations);
    if (next.name.trimmed().isEmpty())
        next.name = QString::fromUtf8("Profile %1").arg(index + 1);
    if (profilesEqual(old, next))
        return;

    const WorkerTaskKind oldModality = old.modality;
    const QString oldStem = sanitizeProfileFileStem(old.id);
    _defaults.profiles[index] = next;
    const QString newStem = sanitizeProfileFileStem(_defaults.profiles.at(index).id);
    if (oldStem != newStem)
        QFile::remove(aiProfilesDir() + QChar('/') + oldStem + QString::fromUtf8(".ini"));

    // Только файл этого профиля — не переписываем весь config/ai и settings.ini.
    writeProfileFile(_defaults.profiles.at(index));

    const bool modalityChanged = (oldModality != next.modality);
    const bool limitsChanged = (old.maxToolIterations != next.maxToolIterations
                                || old.contextCap != next.contextCap);
    if (modalityChanged)
    {
        syncBundlePresetTaskKinds(&_defaults);
        schedulePersist(SettingsKindDefaults);
        if (_projectOpen)
        {
            syncBundlePresetTaskKinds(&_project);
            schedulePersist(SettingsKindProject);
        }
        scheduleWorkerPresetsSignal();
    }
    scheduleProfilesSignal();
    if (limitsChanged)
        scheduleWorkerLimitsSignal();
}

//----------------------------------------------

void AppSettings::removeProfile(QString id)
{
    const int index = profileIndexById(id);
    if (index < 0 || _defaults.profiles.size() <= 1)
        return;
    removeProfileFile(id);
    _defaults.profiles.removeAt(index);
    ensureWorkerProfileIds(&_defaults);
    if (_projectOpen)
        ensureWorkerProfileIds(&_project);
    saveDefaultsWorkspace();
    if (_projectOpen)
        saveProjectWorkspace();
    emit profilesChanged();
}

//----------------------------------------------

QList<LlmProfile> AppSettings::profiles() const
{
    return _defaults.profiles;
}

//----------------------------------------------

LlmProfile AppSettings::profileById(QString id) const
{
    const int index = profileIndexById(id);
    if (index < 0)
        return LlmProfile();
    return _defaults.profiles.at(index);
}

//----------------------------------------------

LlmProfile AppSettings::profileForWorker(WorkerType type, QString llmProfileId) const
{
    const QString id = llmProfileId.trimmed();
    if (!id.isEmpty())
    {
        const LlmProfile explicitProfile = profileById(id);
        if (!explicitProfile.id.isEmpty())
            return explicitProfile;
    }
    return profileForWorkerType(type);
}

//----------------------------------------------

LlmProfile AppSettings::profileForWorkerType(WorkerType type) const
{
    return profileForWorkerType(activeKind(), type);
}

//----------------------------------------------

LlmProfile AppSettings::profileForWorkerType(SettingsKind kind, WorkerType type) const
{
    const WorkerType lookup = (type == WorkerTypeManager || type == WorkerTypeTester)
                              ? WorkerTypeExecutor
                              : type;
    const WorkspaceSettings &bundle = ws(kind);
    // Профиль роли берётся из первого пресета этого типа.
    foreach (const WorkerPreset &preset, bundle.workerPresets)
    {
        if (preset.type != lookup)
            continue;
        const LlmProfile fromPreset = profileById(preset.profileId);
        if (!fromPreset.id.isEmpty())
            return fromPreset;
    }
    // Совместимость со старыми настройками без пресетов.
    LlmProfile profile = profileById(workerProfileId(kind, type));
    if (!profile.id.isEmpty())
        return profile;
    if (!_defaults.profiles.isEmpty())
        return _defaults.profiles.first();
    return LlmProfile();
}

//----------------------------------------------

QString AppSettings::workerProfileId(WorkerType type) const
{
    return workerProfileId(activeKind(), type);
}

//----------------------------------------------

QString AppSettings::workerProfileId(SettingsKind kind, WorkerType type) const
{
    // Инструмент — тот же исполнитель, использует его профиль модели.
    int row = 0;
    if (type == WorkerTypeExecutor || type == WorkerTypeTool)
        row = 1;
    else if (type == WorkerTypeTester)
        row = 2;
    return ws(kind).workerProfileId[row];
}

//----------------------------------------------

void AppSettings::setWorkerProfileId(WorkerType type, QString profileId)
{
    setWorkerProfileId(activeKind(), type, profileId);
}

//----------------------------------------------

void AppSettings::setWorkerProfileId(SettingsKind kind, WorkerType type, QString profileId)
{
    int row = 0;
    if (type == WorkerTypeExecutor || type == WorkerTypeTool)
        row = 1;
    else if (type == WorkerTypeTester)
        row = 2;
    const QString id = profileId.trimmed();
    WorkspaceSettings &bundle = ws(kind);
    if (bundle.workerProfileId[row] == id)
        return;
    bundle.workerProfileId[row] = id;
    ensureWorkerProfileIds(&bundle);
    schedulePersist(kind);
    scheduleProfilesSignal();
}

//----------------------------------------------

QList<WorkerPreset> AppSettings::workerPresets() const
{
    return workerPresets(activeKind());
}

//----------------------------------------------

QList<WorkerPreset> AppSettings::workerPresets(SettingsKind kind) const
{
    return ws(kind).workerPresets;
}

//----------------------------------------------

WorkerPreset AppSettings::workerPresetById(QString id) const
{
    return workerPresetById(activeKind(), id);
}

//----------------------------------------------

WorkerPreset AppSettings::workerPresetById(SettingsKind kind, QString id) const
{
    const QString clean = id.trimmed();
    foreach (const WorkerPreset &preset, ws(kind).workerPresets)
    {
        if (preset.id == clean)
            return preset;
    }
    return WorkerPreset();
}

//----------------------------------------------

void AppSettings::ensureWorkerPresetsSeeded(WorkspaceSettings *bundle)
{
    if (!bundle || !bundle->workerPresets.isEmpty())
        return;
    static const WorkerType types[2] = {
        WorkerTypeExecutor, WorkerTypeTool
    };
    for (int i = 0; i < 2; ++i)
    {
        const int row = 1;

        WorkerPreset preset;
        preset.id = QUuid::createUuid().toString().remove(QChar('{')).remove(QChar('}'));
        preset.type = types[i];
        preset.profileId = bundle->workerProfileId[row];
        if (preset.profileId.isEmpty() && !_defaults.profiles.isEmpty())
            preset.profileId = _defaults.profiles.first().id;
        if (preset.profileId.isEmpty())
            continue;
        syncPresetTaskKind(&preset);
        bool clash = false;
        foreach (const WorkerPreset &existing, bundle->workerPresets)
        {
            if (existing.type == preset.type
                && existing.profileId == preset.profileId)
            {
                clash = true;
                break;
            }
        }
        if (!clash)
            bundle->workerPresets.append(preset);
    }
    ensureRootWorkerPresetId(bundle);
}

//----------------------------------------------

void AppSettings::ensureRootWorkerPresetId(WorkspaceSettings *bundle)
{
    if (!bundle)
        return;
    // Корень — пресет исполнителя.
    if (!bundle->rootWorkerPresetId.trimmed().isEmpty())
    {
        foreach (const WorkerPreset &preset, bundle->workerPresets)
        {
            if (preset.id == bundle->rootWorkerPresetId
                && preset.type == WorkerTypeExecutor)
                return;
        }
    }
    foreach (const WorkerPreset &preset, bundle->workerPresets)
    {
        if (preset.type == WorkerTypeExecutor)
        {
            bundle->rootWorkerPresetId = preset.id;
            return;
        }
    }
    bundle->rootWorkerPresetId.clear();
}

//----------------------------------------------

bool AppSettings::copyMissingDefaultPresets(WorkspaceSettings *bundle)
{
    if (!bundle)
        return false;
    bool changed = false;
    foreach (const WorkerPreset &src, _defaults.workerPresets)
    {
        if (src.id.trimmed().isEmpty() || src.profileId.trimmed().isEmpty())
            continue;
        if (bundle->hiddenWorkerPresetIds.contains(src.id))
            continue;
        bool have = false;
        foreach (const WorkerPreset &existing, bundle->workerPresets)
        {
            if (existing.id == src.id
                || (existing.type == src.type && existing.profileId == src.profileId))
            {
                have = true;
                break;
            }
        }
        if (have)
            continue;
        bundle->workerPresets.append(src);
        changed = true;
    }
    if (changed)
        ensureRootWorkerPresetId(bundle);
    return changed;
}

//----------------------------------------------

void AppSettings::loadWorkerPresets(QSettings *settings, WorkspaceSettings *bundle)
{
    if (!bundle)
        return;
    bundle->workerPresets.clear();
    if (!settings)
        return;
    const int count = settings->beginReadArray(QString::fromUtf8("workerPresets"));
    for (int i = 0; i < count; ++i)
    {
        settings->setArrayIndex(i);
        WorkerPreset preset;
        preset.id = settings->value(QString::fromUtf8("id")).toString().trimmed();
        preset.type = workerTypeFromString(
            settings->value(QString::fromUtf8("type")).toString());
        preset.profileId = settings->value(QString::fromUtf8("profileId")).toString().trimmed();
        preset.taskKind = workerTaskKindFromString(
            settings->value(QString::fromUtf8("taskKind"),
                            QString::fromUtf8("llm")).toString());
        if (preset.id.isEmpty())
            preset.id = QUuid::createUuid().toString().remove(QChar('{')).remove(QChar('}'));
        if (preset.profileId.isEmpty())
            continue;
        syncPresetTaskKind(&preset);
        bundle->workerPresets.append(preset);
    }
    settings->endArray();
    dedupePresetsByTypeProfile(bundle);
    ensureWorkerPresetsSeeded(bundle);
}

//----------------------------------------------

void AppSettings::saveWorkerPresets(QSettings *settings, const WorkspaceSettings &bundle) const
{
    if (!settings)
        return;
    settings->remove(QString::fromUtf8("workerPresets"));
    settings->beginWriteArray(QString::fromUtf8("workerPresets"));
    for (int i = 0; i < bundle.workerPresets.size(); ++i)
    {
        settings->setArrayIndex(i);
        const WorkerPreset &preset = bundle.workerPresets.at(i);
        settings->setValue(QString::fromUtf8("id"), preset.id);
        settings->setValue(QString::fromUtf8("type"), workerTypeToString(preset.type));
        settings->setValue(QString::fromUtf8("profileId"), preset.profileId);
        settings->setValue(QString::fromUtf8("taskKind"),
                           workerTaskKindToString(preset.taskKind));
    }
    settings->endArray();
}

//----------------------------------------------

QString AppSettings::addWorkerPreset(WorkerPreset preset, QString *error)
{
    return addWorkerPreset(activeKind(), preset, error);
}

//----------------------------------------------

QString AppSettings::addWorkerPreset(SettingsKind kind, WorkerPreset preset, QString *error)
{
    WorkspaceSettings &bundle = ws(kind);
    if (preset.profileId.trimmed().isEmpty())
    {
        if (error)
            *error = tr("Не выбран профиль модели.");
        return QString();
    }
    syncPresetTaskKind(&preset);
    foreach (const WorkerPreset &existing, bundle.workerPresets)
    {
        if (existing.type == preset.type
            && existing.profileId == preset.profileId)
        {
            if (error)
                *error = tr("Такая комбинация тип+профиль уже есть.");
            return QString();
        }
    }
    if (preset.id.trimmed().isEmpty())
        preset.id = QUuid::createUuid().toString().remove(QChar('{')).remove(QChar('}'));
    bundle.workerPresets.append(preset);
    ensureRootWorkerPresetId(&bundle);
    if (kind == SettingsKindDefaults && _projectOpen
        && copyMissingDefaultPresets(&_project))
        schedulePersist(SettingsKindProject);
    schedulePersist(kind);
    scheduleWorkerPresetsSignal();
    return preset.id;
}

//----------------------------------------------

void AppSettings::updateWorkerPreset(const WorkerPreset &preset, QString *error)
{
    updateWorkerPreset(activeKind(), preset, error);
}

//----------------------------------------------

void AppSettings::updateWorkerPreset(SettingsKind kind, const WorkerPreset &preset, QString *error)
{
    WorkspaceSettings &bundle = ws(kind);
    int index = -1;
    for (int i = 0; i < bundle.workerPresets.size(); ++i)
    {
        if (bundle.workerPresets.at(i).id == preset.id)
        {
            index = i;
            break;
        }
    }
    if (index < 0)
    {
        if (error)
            *error = tr("Пресет не найден.");
        return;
    }
    WorkerPreset next = preset;
    syncPresetTaskKind(&next);
    if (presetsEqual(bundle.workerPresets.at(index), next))
        return;
    foreach (const WorkerPreset &existing, bundle.workerPresets)
    {
        if (existing.id == next.id)
            continue;
        if (existing.type == next.type
            && existing.profileId == next.profileId)
        {
            if (error)
                *error = tr("Такая комбинация тип+профиль уже есть.");
            return;
        }
    }
    bundle.workerPresets[index] = next;
    ensureRootWorkerPresetId(&bundle);
    schedulePersist(kind);
    scheduleWorkerPresetsSignal();
}

//----------------------------------------------

void AppSettings::syncPresetTaskKind(WorkerPreset *preset) const
{
    if (!preset)
        return;
    const LlmProfile profile = profileById(preset->profileId);
    if (!profile.id.isEmpty())
        preset->taskKind = profile.modality;
}

//----------------------------------------------

void AppSettings::syncBundlePresetTaskKinds(WorkspaceSettings *bundle) const
{
    if (!bundle)
        return;
    for (int i = 0; i < bundle->workerPresets.size(); ++i)
        syncPresetTaskKind(&bundle->workerPresets[i]);
}

//----------------------------------------------

void AppSettings::dedupePresetsByTypeProfile(WorkspaceSettings *bundle) const
{
    if (!bundle)
        return;
    QList<WorkerPreset> unique;
    foreach (const WorkerPreset &preset, bundle->workerPresets)
    {
        bool clash = false;
        foreach (const WorkerPreset &kept, unique)
        {
            if (kept.type == preset.type && kept.profileId == preset.profileId)
            {
                clash = true;
                break;
            }
        }
        if (!clash)
            unique.append(preset);
    }
    bundle->workerPresets = unique;
}

//----------------------------------------------

QList<WorkerPreset> AppSettings::presetsUsingProfile(QString profileId) const
{
    QList<WorkerPreset> out = presetsUsingProfile(SettingsKindDefaults, profileId);
    if (_projectOpen)
        out += presetsUsingProfile(SettingsKindProject, profileId);
    return out;
}

//----------------------------------------------

QList<WorkerPreset> AppSettings::presetsUsingProfile(SettingsKind kind,
                                                      QString profileId) const
{
    QList<WorkerPreset> out;
    const QString clean = profileId.trimmed();
    if (clean.isEmpty())
        return out;
    foreach (const WorkerPreset &preset, ws(kind).workerPresets)
    {
        if (preset.profileId == clean)
            out.append(preset);
    }
    return out;
}

//----------------------------------------------

QList<WorkerPreset> AppSettings::orphanProjectPresets() const
{
    QList<WorkerPreset> out;
    if (!_projectOpen)
        return out;
    foreach (const WorkerPreset &preset, _project.workerPresets)
    {
        if (preset.profileId.trimmed().isEmpty())
            continue;
        if (profileById(preset.profileId).id.isEmpty())
            out.append(preset);
    }
    return out;
}

//----------------------------------------------

int AppSettings::removePresetsUsingProfile(SettingsKind kind, QString profileId)
{
    const QString clean = profileId.trimmed();
    if (clean.isEmpty())
        return 0;
    WorkspaceSettings &bundle = ws(kind);
    int removed = 0;
    for (int i = bundle.workerPresets.size() - 1; i >= 0; --i)
    {
        if (bundle.workerPresets.at(i).profileId == clean)
        {
            bundle.workerPresets.removeAt(i);
            ++removed;
        }
    }
    if (removed > 0)
    {
        ensureRootWorkerPresetId(&bundle);
        save(kind);
    }
    return removed;
}

//----------------------------------------------

int AppSettings::removePresetsUsingProfile(QString profileId)
{
    int removed = removePresetsUsingProfile(SettingsKindDefaults, profileId);
    if (_projectOpen)
        removed += removePresetsUsingProfile(SettingsKindProject, profileId);
    if (removed > 0)
        emit workerPresetsChanged();
    return removed;
}

//----------------------------------------------

int AppSettings::removeOrphanProjectPresets()
{
    if (!_projectOpen)
        return 0;
    int removed = 0;
    for (int i = _project.workerPresets.size() - 1; i >= 0; --i)
    {
        const QString pid = _project.workerPresets.at(i).profileId.trimmed();
        if (pid.isEmpty() || profileById(pid).id.isEmpty())
        {
            _project.workerPresets.removeAt(i);
            ++removed;
        }
    }
    if (removed > 0)
    {
        ensureRootWorkerPresetId(&_project);
        saveProjectWorkspace();
        emit workerPresetsChanged();
    }
    return removed;
}

//----------------------------------------------

void AppSettings::removeWorkerPreset(QString id)
{
    removeWorkerPreset(activeKind(), id);
}

//----------------------------------------------

void AppSettings::removeWorkerPreset(SettingsKind kind, QString id)
{
    WorkspaceSettings &bundle = ws(kind);
    for (int i = 0; i < bundle.workerPresets.size(); ++i)
    {
        if (bundle.workerPresets.at(i).id == id)
        {
            if (kind == SettingsKindProject)
            {
                foreach (const WorkerPreset &src, _defaults.workerPresets)
                {
                    if (src.id == id)
                    {
                        bundle.hiddenWorkerPresetIds.insert(id);
                        break;
                    }
                }
            }
            bundle.workerPresets.removeAt(i);
            ensureRootWorkerPresetId(&bundle);
            schedulePersist(kind);
            scheduleWorkerPresetsSignal();
            return;
        }
    }
}

//----------------------------------------------

QString AppSettings::rootWorkerPresetId() const
{
    return rootWorkerPresetId(activeKind());
}

//----------------------------------------------

QString AppSettings::rootWorkerPresetId(SettingsKind kind) const
{
    return ws(kind).rootWorkerPresetId;
}

//----------------------------------------------

void AppSettings::setRootWorkerPresetId(QString presetId)
{
    setRootWorkerPresetId(activeKind(), presetId);
}

//----------------------------------------------

void AppSettings::setRootWorkerPresetId(SettingsKind kind, QString presetId)
{
    WorkspaceSettings &bundle = ws(kind);
    const QString id = presetId.trimmed();
    if (bundle.rootWorkerPresetId == id)
        return;
    bool found = false;
    foreach (const WorkerPreset &preset, bundle.workerPresets)
    {
        if (preset.id == id && preset.type == WorkerTypeExecutor)
        {
            found = true;
            break;
        }
    }
    if (!found && !id.isEmpty())
        return;
    bundle.rootWorkerPresetId = id;
    ensureRootWorkerPresetId(&bundle);
    schedulePersist(kind);
    scheduleWorkerPresetsSignal();
}

//----------------------------------------------

WorkerPreset AppSettings::rootWorkerPreset() const
{
    return rootWorkerPreset(activeKind());
}

//----------------------------------------------

WorkerPreset AppSettings::rootWorkerPreset(SettingsKind kind) const
{
    const WorkspaceSettings &bundle = ws(kind);
    foreach (const WorkerPreset &preset, bundle.workerPresets)
    {
        if (preset.id == bundle.rootWorkerPresetId && preset.type == WorkerTypeExecutor)
            return preset;
    }
    foreach (const WorkerPreset &preset, bundle.workerPresets)
    {
        if (preset.type == WorkerTypeExecutor)
            return preset;
    }
    return WorkerPreset();
}

//----------------------------------------------

QString AppSettings::workerPresetsSummary() const
{
    return workerPresetsSummary(activeKind());
}

//----------------------------------------------

QString AppSettings::workerPresetsSummary(SettingsKind kind) const
{
    const WorkspaceSettings &bundle = ws(kind);
    if (bundle.workerPresets.isEmpty())
    {
        return AppLocale::instance()->model(
            QString::fromUtf8("no_presets"),
            QString::fromUtf8("No worker presets configured."));
    }
    QStringList lines;
    lines << AppLocale::instance()->model(
        QString::fromUtf8("presets_header"),
        QString::fromUtf8(
            "Available worker presets (pick preset_id in delegate_task / add_group_member):\n"
            "modality / task_kind: inherited from the linked model profile "
            "(llm | image_generation | speech | audio_generation | video_generation | "
            "embedding | decisions | other) — pick a preset whose modality matches the "
            "subtask (image → image_generation, ASR/TTS → speech, music/SFX → "
            "audio_generation, video → video_generation, vectors → embedding, "
            "structured choices → decisions, coding/agents → llm).\n"
            "purpose: short human note on WHAT this model is for (e.g. text-to-image generation "
            "vs object segmentation on an image). Prefer the preset whose purpose best matches "
            "the subtask — do not treat all image_generation models as interchangeable.\n"
            "MODEL FIT: cheapest/fastest adequate model for routine edits and simple checks; "
            "stronger models for architecture, hard bugs, ambiguous design, group deep-dives. "
            "Prefer a fitting preset over the default type profile."));
    bool anyMedia = false;
    foreach (const WorkerPreset &preset, bundle.workerPresets)
    {
        const LlmProfile profile = profileById(preset.profileId);
        QString profileName = profile.name.trimmed();
        if (profileName.isEmpty())
            profileName = preset.profileId;
        QString model = profile.model.trimmed();
        if (model.isEmpty())
            model = QString::fromUtf8("?");
        QString purpose = profile.purpose.trimmed().simplified();
        if (purpose.isEmpty())
            purpose = QString::fromUtf8("(no purpose set)");
        else if (purpose.size() > 200)
            purpose = purpose.left(197) + QString::fromUtf8("...");
        const WorkerTaskKind modality = profile.id.isEmpty()
                                        ? preset.taskKind
                                        : profile.modality;
        LlmProfile probe = profile;
        if (probe.id.isEmpty())
            probe.modality = modality;
        const QString params = mediaParamsLine(
            modality,
            ModelCapabilityService::instance()->cachedOrHeuristic(probe));
        if (!params.isEmpty())
            anyMedia = true;
        lines << QString::fromUtf8(
                     "- preset_id=%1 type=%2 modality=%3 profile=\"%4\" "
                     "model=%5 purpose=\"%6\"%7")
                     .arg(preset.id, workerTypeToString(preset.type),
                          workerTaskKindToString(modality), profileName, model,
                          purpose,
                          params.isEmpty()
                              ? QString()
                              : QString::fromUtf8(" params: %1").arg(params));
    }
    if (anyMedia)
    {
        lines << QString::fromUtf8(
            "A media preset is not a chat model. Its task is one JSON object "
            "using that preset's params. Values in parentheses come from the "
            "provider catalog. Set every required key. Set an optional key only "
            "when the task asks for it, and only with a listed value. "
            "prompt/text is the content, not an instruction to save a file. "
            "output_path is the new file and is not sent to the provider. "
            "reference_image, reference_audio and reference_video are existing "
            "project files of that kind. Pass one only when params list that "
            "category. reference_path means reference_image.");
    }
    return lines.join(QChar('\n'));
}
