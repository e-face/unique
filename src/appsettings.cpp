#include "appsettings.h"
//----------------------------------------------
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSet>
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
static const int kAppearanceVersion = 2;

//----------------------------------------------

static int roleBudgetIndex(WorkerType type)
{
    switch (type)
    {
    case WorkerTypeExecutor:
        return 1;
    case WorkerTypeTester:
        return 2;
    case WorkerTypeTool:
        return 3;
    case WorkerTypeManager:
    default:
        return 0;
    }
}

//----------------------------------------------

static QString roleBudgetKey(int index)
{
    static const char *names[4] = { "manager", "executor", "tester", "tool" };
    if (index < 0 || index > 3)
        index = 0;
    return QString::fromUtf8(names[index]);
}

//----------------------------------------------

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
    , _shellControl(true)
    , _outsideFilesControl(true)
    , _systemSettingsControl(true)
    , _fontPointSize(11)
    , _textColor(QString::fromUtf8("#1f1f1f"))
    , _windowColor(QString::fromUtf8("#f5f5f5"))
    , _panelColor(QString::fromUtf8("#ffffff"))
    , _accentColor(QString::fromUtf8("#2563eb"))
    , _chatUserColor(QString::fromUtf8("#ffffff"))
    , _chatAssistantColor(QString::fromUtf8("#ffffff"))
    , _chatOtherColor(QString::fromUtf8("#f3f3f3"))
    , _maxWorkerDepth(kDefaultMaxWorkerDepth)
    , _maxConcurrentWorkers(kDefaultMaxConcurrentWorkers)
    , _llmTimeoutSec(kDefaultLlmTimeoutSec)
    , _statsTimer(0)
{
    for (int i = 0; i < 3; ++i)
        _workerProfileId[i].clear();
    for (int i = 0; i < 4; ++i)
    {
        _roleToolIterations[i] = kDefaultRoleToolIterations[i];
        _roleContextCap[i] = kDefaultRoleContextCap[i];
    }
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

void AppSettings::migrateLegacyCommands(const QStringList &keys)
{
    foreach (const QString &item, keys)
    {
        const QString key = normalizeKey(item);
        if (key.isEmpty())
            continue;
        bool found = false;
        foreach (const PermissionGrant &grant, _permissionGrants)
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
        _permissionGrants.append(grant);
        _alwaysCommands.insert(key);
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

QString AppSettings::profileFilePath(QString id)
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

void AppSettings::appendGrant(PermissionGrant grant)
{
    grant.programKey = normalizeKey(grant.programKey);
    if (grant.programKey.isEmpty())
        grant.programKey = normalizeKey(grant.actionText);
    if (grant.programKey.isEmpty())
        return;
    if (grant.actionText.trimmed().isEmpty())
        grant.actionText = grant.programKey;
    foreach (const PermissionGrant &existing, _permissionGrants)
    {
        if (existing.programKey == grant.programKey)
            return;
    }
    _permissionGrants.append(grant);
    _alwaysCommands.insert(grant.programKey);
}

//----------------------------------------------

void AppSettings::loadPermissions(QSettings *settings)
{
    if (!settings)
        return;

    const int count = settings->beginReadArray(QString::fromUtf8("permissionGrants"));
    for (int i = 0; i < count; ++i)
    {
        settings->setArrayIndex(i);
        PermissionGrant grant;
        grant.programKey = settings->value(QString::fromUtf8("programKey")).toString();
        grant.actionText = settings->value(QString::fromUtf8("actionText")).toString();
        appendGrant(grant);
    }
    settings->endArray();

    if (_permissionGrants.isEmpty())
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
                    appendGrant(grant);
                }
            }
        }
    }

    migrateLegacyCommands(settings->value(QString::fromUtf8("alwaysCommands")).toStringList());
    if (settings->contains(QString::fromUtf8("control/shell")))
        _shellControl = settings->value(QString::fromUtf8("control/shell"), true).toBool();
    if (settings->contains(QString::fromUtf8("control/outsideFiles")))
        _outsideFilesControl = settings->value(QString::fromUtf8("control/outsideFiles"), true).toBool();
    if (settings->contains(QString::fromUtf8("control/systemSettings")))
        _systemSettingsControl = settings->value(QString::fromUtf8("control/systemSettings"), true).toBool();
}

//----------------------------------------------

void AppSettings::savePermissions(QSettings *settings) const
{
    if (!settings)
        return;
    settings->remove(QString::fromUtf8("permissionGrants"));
    settings->remove(QString::fromUtf8("permissionGrantsJson"));
    settings->beginWriteArray(QString::fromUtf8("permissionGrants"));
    for (int i = 0; i < _permissionGrants.size(); ++i)
    {
        settings->setArrayIndex(i);
        settings->setValue(QString::fromUtf8("programKey"), _permissionGrants.at(i).programKey);
        settings->setValue(QString::fromUtf8("actionText"), _permissionGrants.at(i).actionText);
    }
    settings->endArray();
    settings->setValue(QString::fromUtf8("alwaysCommands"), alwaysAllowedCommands());
    settings->setValue(QString::fromUtf8("control/shell"), _shellControl);
    settings->setValue(QString::fromUtf8("control/outsideFiles"), _outsideFilesControl);
    settings->setValue(QString::fromUtf8("control/systemSettings"), _systemSettingsControl);
    settings->sync();
}

//----------------------------------------------

void AppSettings::migrateProjectPermissions(QString projectIni)
{
    if (projectIni.isEmpty() || !QFileInfo(projectIni).exists())
        return;

    QSettings project(projectIni, QSettings::IniFormat);
    const int before = _permissionGrants.size();
    // Только список «всегда»: флаги контроля уже глобальные и не должны
    // перезаписываться старым project.ini.
    const int count = project.beginReadArray(QString::fromUtf8("permissionGrants"));
    for (int i = 0; i < count; ++i)
    {
        project.setArrayIndex(i);
        PermissionGrant grant;
        grant.programKey = project.value(QString::fromUtf8("programKey")).toString();
        grant.actionText = project.value(QString::fromUtf8("actionText")).toString();
        appendGrant(grant);
    }
    project.endArray();
    migrateLegacyCommands(project.value(QString::fromUtf8("alwaysCommands")).toStringList());

    // Убрать из проектного ini: дальше разрешения живут только в глобальных.
    project.remove(QString::fromUtf8("permissionGrants"));
    project.remove(QString::fromUtf8("permissionGrantsJson"));
    project.remove(QString::fromUtf8("alwaysCommands"));
    project.remove(QString::fromUtf8("control/shell"));
    project.remove(QString::fromUtf8("control/outsideFiles"));
    project.remove(QString::fromUtf8("control/systemSettings"));
    project.sync();

    if (_permissionGrants.size() != before)
    {
        QSettings global(settingsFilePath(), QSettings::IniFormat);
        savePermissions(&global);
        emit alwaysCommandsChanged();
    }
}

//----------------------------------------------

void AppSettings::setProjectRoot(QString root)
{
    const QString clean = QDir::cleanPath(root.trimmed());
    if (_projectRoot == clean)
    {
        // Тот же корень, но ___project___ пересоздан: статистика проекта
        // сбрасывается. Список «всегда» и флаги контроля — общие для всех
        // проектов, их не трогаем.
        if (!clean.isEmpty() && !QFileInfo(projectIniPath()).exists())
        {
            _projectUsage.clear();
            emit usageChanged();
        }
        return;
    }
    save();
    _projectRoot = clean;
    if (!_projectRoot.isEmpty())
    {
        if (QFileInfo(projectIniPath()).exists())
        {
            QSettings project(projectIniPath(), QSettings::IniFormat);
            loadUsage(&project, &_projectUsage);
            migrateProjectPermissions(projectIniPath());
        }
        else
            _projectUsage.clear();
        maybeSeedGlobalUsage();
    }
    else
        _projectUsage.clear();
    emit usageChanged();
}

//----------------------------------------------

void AppSettings::load()
{
    migrateFromNativeRegistry();

    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    _permissionGrants.clear();
    _alwaysCommands.clear();
    loadPermissions(&settings);

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
                                           QString::fromUtf8("#1f1f1f")).toString());
        _windowColor = QColor(settings.value(QString::fromUtf8("ui/windowColor"),
                                             QString::fromUtf8("#f5f5f5")).toString());
        _panelColor = QColor(settings.value(QString::fromUtf8("ui/panelColor"),
                                            QString::fromUtf8("#ffffff")).toString());
        _accentColor = QColor(settings.value(QString::fromUtf8("ui/accentColor"),
                                             QString::fromUtf8("#2563eb")).toString());
        _chatUserColor = QColor(settings.value(QString::fromUtf8("ui/chatUserColor"),
                                               QString::fromUtf8("#ffffff")).toString());
        _chatAssistantColor = QColor(settings.value(QString::fromUtf8("ui/chatAssistantColor"),
                                                    QString::fromUtf8("#ffffff")).toString());
        _chatOtherColor = QColor(settings.value(QString::fromUtf8("ui/chatOtherColor"),
                                                QString::fromUtf8("#f3f3f3")).toString());
        if (!_textColor.isValid())
            _textColor = QColor(QString::fromUtf8("#1f1f1f"));
        if (!_windowColor.isValid())
            _windowColor = QColor(QString::fromUtf8("#f5f5f5"));
        if (!_panelColor.isValid())
            _panelColor = QColor(QString::fromUtf8("#ffffff"));
        if (!_accentColor.isValid())
            _accentColor = QColor(QString::fromUtf8("#2563eb"));
        if (!_chatUserColor.isValid())
            _chatUserColor = QColor(QString::fromUtf8("#ffffff"));
        if (!_chatAssistantColor.isValid())
            _chatAssistantColor = QColor(QString::fromUtf8("#ffffff"));
        if (!_chatOtherColor.isValid())
            _chatOtherColor = QColor(QString::fromUtf8("#f3f3f3"));
    }
    loadUsage(&settings, &_globalUsage);
    _projectUsage.clear();
    _maxWorkerDepth = qBound(kMinWorkerDepth,
                             settings.value(QString::fromUtf8("workers/maxDepth"),
                                            kDefaultMaxWorkerDepth).toInt(),
                             kMaxWorkerDepth);
    _maxConcurrentWorkers = qBound(kMinConcurrentWorkers,
                                   settings.value(QString::fromUtf8("workers/maxConcurrent"),
                                                  kDefaultMaxConcurrentWorkers).toInt(),
                                   kMaxConcurrentWorkers);
    _llmTimeoutSec = qBound(kMinLlmTimeoutSec,
                            settings.value(QString::fromUtf8("llm/requestTimeoutSec"),
                                           kDefaultLlmTimeoutSec).toInt(),
                            kMaxLlmTimeoutSec);
    settings.beginGroup(QString::fromUtf8("workers/roleBudget"));
    for (int i = 0; i < 4; ++i)
    {
        const QString key = roleBudgetKey(i);
        _roleToolIterations[i] = qBound(kMinRoleToolIterations,
                                        settings.value(key + QString::fromUtf8("/toolIterations"),
                                                       kDefaultRoleToolIterations[i]).toInt(),
                                        kMaxRoleToolIterations);
        _roleContextCap[i] = qBound(0,
                                    settings.value(key + QString::fromUtf8("/contextCap"),
                                                   kDefaultRoleContextCap[i]).toInt(),
                                    kMaxRoleContextCap);
        if (_roleContextCap[i] > 0 && _roleContextCap[i] < 4096)
            _roleContextCap[i] = 4096;
        // Старая кнопка «окно модели» записывала 128k для DeepSeek V4 (реально 1M).
        if (_roleContextCap[i] == 128000)
            _roleContextCap[i] = 0;
    }
    settings.endGroup();
    loadProfiles();
}

//----------------------------------------------

void AppSettings::save() const
{
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
    settings.setValue(QString::fromUtf8("workers/maxDepth"), _maxWorkerDepth);
    settings.setValue(QString::fromUtf8("workers/maxConcurrent"), _maxConcurrentWorkers);
    settings.setValue(QString::fromUtf8("llm/requestTimeoutSec"), _llmTimeoutSec);
    settings.beginGroup(QString::fromUtf8("workers/roleBudget"));
    for (int i = 0; i < 4; ++i)
    {
        const QString key = roleBudgetKey(i);
        settings.setValue(key + QString::fromUtf8("/toolIterations"), _roleToolIterations[i]);
        settings.setValue(key + QString::fromUtf8("/contextCap"), _roleContextCap[i]);
    }
    settings.endGroup();
    saveProfiles();
    saveUsage(&settings, _globalUsage);
    // Разрешения «всегда» и флаги контроля — общие для всех проектов.
    savePermissions(&settings);

    if (!_projectRoot.isEmpty())
    {
        const QString dir = projectStoreDir(_projectRoot);
        if (!dir.isEmpty() && QDir(dir).exists())
        {
            QSettings project(projectIniPath(), QSettings::IniFormat);
            saveUsage(&project, _projectUsage);
        }
    }
}

//----------------------------------------------

bool AppSettings::isCommandAlwaysAllowed(QString programKey) const
{
    const QString key = normalizeKey(programKey);
    if (key.isEmpty())
        return false;
    if (_alwaysCommands.contains(key))
        return true;
    foreach (const PermissionGrant &grant, _permissionGrants)
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
    const QString key = normalizeKey(programKey);
    if (key.isEmpty())
        return;
    if (!actionText.trimmed().isEmpty())
    {
        for (int i = 0; i < _permissionGrants.size(); ++i)
        {
            if (_permissionGrants.at(i).programKey == key)
            {
                _permissionGrants[i].actionText = actionText.trimmed();
                _alwaysCommands.insert(key);
                save();
                emit alwaysCommandsChanged();
                return;
            }
        }
        PermissionGrant grant;
        grant.programKey = key;
        grant.actionText = actionText.trimmed();
        _permissionGrants.append(grant);
    }
    else if (_alwaysCommands.contains(key))
    {
        return;
    }
    else
    {
        PermissionGrant grant;
        grant.programKey = key;
        grant.actionText = key;
        _permissionGrants.append(grant);
    }
    _alwaysCommands.insert(key);
    save();
    emit alwaysCommandsChanged();
}

//----------------------------------------------

void AppSettings::addAlwaysAllowedCommands(const QStringList &keys)
{
    bool changed = false;
    foreach (const QString &item, keys)
    {
        const QString key = normalizeKey(item);
        if (key.isEmpty() || _alwaysCommands.contains(key))
            continue;
        PermissionGrant grant;
        grant.programKey = key;
        grant.actionText = key;
        _permissionGrants.append(grant);
        _alwaysCommands.insert(key);
        changed = true;
    }
    if (!changed)
        return;
    save();
    emit alwaysCommandsChanged();
}

//----------------------------------------------

void AppSettings::removeAlwaysAllowedCommand(QString programKey)
{
    const QString key = normalizeKey(programKey);
    if (key.isEmpty())
        return;
    for (int i = _permissionGrants.size() - 1; i >= 0; --i)
    {
        if (_permissionGrants.at(i).programKey == key)
            _permissionGrants.removeAt(i);
    }
    if (!_alwaysCommands.contains(key))
        return;
    _alwaysCommands.remove(key);
    save();
    emit alwaysCommandsChanged();
}

//----------------------------------------------

QStringList AppSettings::alwaysAllowedCommands() const
{
    return _alwaysCommands.toList();
}

//----------------------------------------------

QList<PermissionGrant> AppSettings::permissionGrants() const
{
    return _permissionGrants;
}

//----------------------------------------------

bool AppSettings::shellControlEnabled() const
{
    return _shellControl;
}

//----------------------------------------------

void AppSettings::setShellControlEnabled(bool enabled)
{
    if (_shellControl == enabled)
        return;
    _shellControl = enabled;
    save();
    emit controlFlagsChanged();
}

//----------------------------------------------

bool AppSettings::outsideFilesControlEnabled() const
{
    return _outsideFilesControl;
}

//----------------------------------------------

void AppSettings::setOutsideFilesControlEnabled(bool enabled)
{
    if (_outsideFilesControl == enabled)
        return;
    _outsideFilesControl = enabled;
    save();
    emit controlFlagsChanged();
}

//----------------------------------------------

bool AppSettings::systemSettingsControlEnabled() const
{
    return _systemSettingsControl;
}

//----------------------------------------------

void AppSettings::setSystemSettingsControlEnabled(bool enabled)
{
    if (_systemSettingsControl == enabled)
        return;
    _systemSettingsControl = enabled;
    save();
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
    if (isLightTheme())
        return QColor(QString::fromUtf8("#ececec"));
    QColor c = _windowColor.lighter(185);
    if (!c.isValid())
        c = QColor(QString::fromUtf8("#4f4f4f"));
    return c;
}

//----------------------------------------------

QColor AppSettings::borderColor() const
{
    return isLightTheme() ? QColor(QString::fromUtf8("#d4d4d4"))
                          : QColor(QString::fromUtf8("#555555"));
}

//----------------------------------------------

QColor AppSettings::mutedTextColor() const
{
    return isLightTheme() ? QColor(QString::fromUtf8("#6b6b6b"))
                          : QColor(QString::fromUtf8("#8a8a8a"));
}

//----------------------------------------------

QColor AppSettings::selectionColor() const
{
    return isLightTheme() ? QColor(QString::fromUtf8("#d6e4ff"))
                          : QColor(QString::fromUtf8("#1a5580"));
}

//----------------------------------------------

QColor AppSettings::scrollbarColor() const
{
    return isLightTheme() ? QColor(QString::fromUtf8("#c4c4c4"))
                          : QColor(QString::fromUtf8("#6a6a6a"));
}

//----------------------------------------------

bool AppSettings::isLightTheme() const
{
    return _windowColor.lightness() >= 140;
}

//----------------------------------------------

void AppSettings::applyLightDefaults()
{
    _textColor = QColor(QString::fromUtf8("#1f1f1f"));
    _windowColor = QColor(QString::fromUtf8("#f5f5f5"));
    _panelColor = QColor(QString::fromUtf8("#ffffff"));
    _accentColor = QColor(QString::fromUtf8("#2563eb"));
    _chatUserColor = QColor(QString::fromUtf8("#ffffff"));
    _chatAssistantColor = QColor(QString::fromUtf8("#ffffff"));
    _chatOtherColor = QColor(QString::fromUtf8("#f3f3f3"));
}

//----------------------------------------------

void UsageStats::clear()
{
    promptTokens = 0;
    completionTokens = 0;
    totalTokens = 0;
    cachedPromptTokens = 0;
    requestCount = 0;
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
    if (!_projectRoot.isEmpty())
        _projectUsage.add(promptTokens, completionTokens, totalTokens, cachedPromptTokens, role);
    scheduleStatsSave();
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
    settings->remove(QString::fromUtf8("statsByRole"));
    saveRoleStats(settings, stats);
}

//----------------------------------------------

void AppSettings::resetUsage()
{
    _projectUsage.clear();
    saveStats();
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
    if (_projectRoot.isEmpty())
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

void AppSettings::sltFlushStats()
{
    saveStats();
}

//----------------------------------------------

int AppSettings::maxWorkerDepth() const
{
    return _maxWorkerDepth;
}

//----------------------------------------------

void AppSettings::setMaxWorkerDepth(int depth)
{
    const int value = qBound(kMinWorkerDepth, depth, kMaxWorkerDepth);
    if (_maxWorkerDepth == value)
        return;
    _maxWorkerDepth = value;
    save();
    emit workerLimitsChanged();
}

//----------------------------------------------

int AppSettings::maxConcurrentWorkers() const
{
    return _maxConcurrentWorkers;
}

//----------------------------------------------

int AppSettings::roleToolIterations(WorkerType type) const
{
    return _roleToolIterations[roleBudgetIndex(type)];
}

//----------------------------------------------

void AppSettings::setRoleToolIterations(WorkerType type, int count)
{
    const int index = roleBudgetIndex(type);
    const int value = qBound(kMinRoleToolIterations, count, kMaxRoleToolIterations);
    if (_roleToolIterations[index] == value)
        return;
    _roleToolIterations[index] = value;
    save();
    emit workerLimitsChanged();
}

//----------------------------------------------

int AppSettings::roleContextCap(WorkerType type) const
{
    return _roleContextCap[roleBudgetIndex(type)];
}

//----------------------------------------------

void AppSettings::setRoleContextCap(WorkerType type, int tokens)
{
    const int index = roleBudgetIndex(type);
    int value = tokens;
    if (value < 0)
        value = 0;
    if (value > 0 && value < 4096)
        value = 4096;
    if (value > kMaxRoleContextCap)
        value = kMaxRoleContextCap;
    if (_roleContextCap[index] == value)
        return;
    _roleContextCap[index] = value;
    save();
    emit workerLimitsChanged();
}

//----------------------------------------------

void AppSettings::setMaxConcurrentWorkers(int count)
{
    const int value = qBound(kMinConcurrentWorkers, count, kMaxConcurrentWorkers);
    if (_maxConcurrentWorkers == value)
        return;
    _maxConcurrentWorkers = value;
    save();
    emit workerLimitsChanged();
}

//----------------------------------------------

int AppSettings::llmTimeoutSec() const
{
    return _llmTimeoutSec;
}

//----------------------------------------------

void AppSettings::setLlmTimeoutSec(int seconds)
{
    const int value = qBound(kMinLlmTimeoutSec, seconds, kMaxLlmTimeoutSec);
    if (_llmTimeoutSec == value)
        return;
    _llmTimeoutSec = value;
    save();
    emit workerLimitsChanged();
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
    for (int i = 0; i < _profiles.size(); ++i)
    {
        if (_profiles.at(i).id == trimmed)
            return i;
    }
    return -1;
}

//----------------------------------------------

void AppSettings::ensureWorkerProfileIds()
{
    QString fallback;
    if (!_profiles.isEmpty())
        fallback = _profiles.first().id;
    for (int i = 0; i < 3; ++i)
    {
        if (profileIndexById(_workerProfileId[i]) < 0)
            _workerProfileId[i] = fallback;
    }
}

//----------------------------------------------

void AppSettings::migrateLegacyProviders(QSettings *settings)
{
    if (!settings)
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

    LlmProfile groq;
    groq.id = QString::fromUtf8("groq");
    groq.name = QString::fromUtf8("Groq");
    groq.baseUrl = QString::fromUtf8("https://api.groq.com/openai/v1");
    groq.model = settings->value(QString::fromUtf8("model/groq"),
                                 QString::fromUtf8("llama-3.3-70b-versatile")).toString();
    groq.apiKey = settings->value(QString::fromUtf8("apiKey/groq")).toString();
    if (groq.apiKey.isEmpty())
        groq.apiKey = QString::fromLocal8Bit(qgetenv("GROQ_API_KEY"));

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

    _profiles.clear();
    _profiles << deepseek << groq << qwen;

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
        _workerProfileId[i] = id;
    }
}

//----------------------------------------------

void AppSettings::writeProfileFile(const LlmProfile &profile) const
{
    QSettings file(profileFilePath(profile.id), QSettings::IniFormat);
    file.setIniCodec("UTF-8");
    file.setValue(QString::fromUtf8("id"), profile.id);
    file.setValue(QString::fromUtf8("name"), profile.name);
    file.setValue(QString::fromUtf8("baseUrl"), profile.baseUrl);
    file.setValue(QString::fromUtf8("model"), profile.model);
    file.setValue(QString::fromUtf8("apiKey"), profile.apiKey);
    file.setValue(QString::fromUtf8("balanceUrl"), profile.balanceUrl);
    file.setValue(QString::fromUtf8("balanceJsonPath"), profile.balanceJsonPath);
    file.sync();
}

//----------------------------------------------

void AppSettings::removeProfileFile(QString id) const
{
    if (id.trimmed().isEmpty())
        return;
    QFile::remove(profileFilePath(id));
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

void AppSettings::loadProfiles()
{
    _profiles.clear();
    const QDir dir(aiProfilesDir());
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
        profile.balanceUrl = file.value(QString::fromUtf8("balanceUrl")).toString();
        profile.balanceJsonPath = file.value(QString::fromUtf8("balanceJsonPath")).toString();
        if (profile.id.isEmpty())
            profile.id = QFileInfo(fileName).completeBaseName();
        if (profile.name.trimmed().isEmpty())
            profile.name = profile.id;
        _profiles.append(profile);
    }

    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    if (_profiles.isEmpty())
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
            _profiles.append(profile);
        }
        settings.endArray();
        if (!_profiles.isEmpty())
        {
            settings.remove(QString::fromUtf8("llmProfiles"));
            saveProfiles();
        }
    }

    if (_profiles.isEmpty())
    {
        migrateLegacyProviders(&settings);
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
            _workerProfileId[i] = id;
    }
    ensureWorkerProfileIds();
}

//----------------------------------------------

void AppSettings::saveProfiles() const
{
    QSet<QString> keep;
    foreach (const LlmProfile &profile, _profiles)
    {
        writeProfileFile(profile);
        keep.insert(sanitizeProfileFileStem(profile.id) + QString::fromUtf8(".ini"));
    }

    const QDir dir(aiProfilesDir());
    foreach (const QString &fileName,
             dir.entryList(QStringList() << QString::fromUtf8("*.ini"), QDir::Files))
    {
        if (!keep.contains(fileName))
            QFile::remove(dir.filePath(fileName));
    }

    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.remove(QString::fromUtf8("llmProfiles"));
    const char *keys[] = { "manager", "executor", "tester" };
    for (int i = 0; i < 3; ++i)
    {
        settings.setValue(QString::fromUtf8("workerProfile/") + QString::fromUtf8(keys[i]),
                           _workerProfileId[i]);
    }
}

//----------------------------------------------

QString AppSettings::addProfile(LlmProfile profile)
{
    if (profile.id.trimmed().isEmpty())
        profile.id = newProfileId();
    if (profile.name.trimmed().isEmpty())
        profile.name = QString::fromUtf8("Profile %1").arg(_profiles.size() + 1);
    _profiles.append(profile);
    ensureWorkerProfileIds();
    save();
    emit profilesChanged();
    return profile.id;
}

//----------------------------------------------

void AppSettings::updateProfile(const LlmProfile &profile)
{
    const int index = profileIndexById(profile.id);
    if (index < 0)
        return;
    const QString oldStem = sanitizeProfileFileStem(_profiles.at(index).id);
    _profiles[index] = profile;
    if (_profiles[index].name.trimmed().isEmpty())
        _profiles[index].name = QString::fromUtf8("Profile %1").arg(index + 1);
    const QString newStem = sanitizeProfileFileStem(_profiles.at(index).id);
    if (oldStem != newStem)
        QFile::remove(aiProfilesDir() + QChar('/') + oldStem + QString::fromUtf8(".ini"));
    save();
    emit profilesChanged();
}

//----------------------------------------------

void AppSettings::removeProfile(QString id)
{
    const int index = profileIndexById(id);
    if (index < 0 || _profiles.size() <= 1)
        return;
    removeProfileFile(id);
    _profiles.removeAt(index);
    ensureWorkerProfileIds();
    save();
    emit profilesChanged();
}

//----------------------------------------------

QList<LlmProfile> AppSettings::profiles() const
{
    return _profiles;
}

//----------------------------------------------

LlmProfile AppSettings::profileById(QString id) const
{
    const int index = profileIndexById(id);
    if (index < 0)
        return LlmProfile();
    return _profiles.at(index);
}

//----------------------------------------------

LlmProfile AppSettings::profileForWorkerType(WorkerType type) const
{
    LlmProfile profile = profileById(workerProfileId(type));
    if (!profile.id.isEmpty())
        return profile;
    if (!_profiles.isEmpty())
        return _profiles.first();
    return LlmProfile();
}

//----------------------------------------------

QString AppSettings::workerProfileId(WorkerType type) const
{
    // Инструмент — тот же исполнитель, использует его профиль модели.
    int row = 0;
    if (type == WorkerTypeExecutor || type == WorkerTypeTool)
        row = 1;
    else if (type == WorkerTypeTester)
        row = 2;
    return _workerProfileId[row];
}

//----------------------------------------------

void AppSettings::setWorkerProfileId(WorkerType type, QString profileId)
{
    int row = 0;
    if (type == WorkerTypeExecutor || type == WorkerTypeTool)
        row = 1;
    else if (type == WorkerTypeTester)
        row = 2;
    const QString id = profileId.trimmed();
    if (_workerProfileId[row] == id)
        return;
    _workerProfileId[row] = id;
    ensureWorkerProfileIds();
    save();
    emit profilesChanged();
}
