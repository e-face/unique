#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include "workertypes.h"
//----------------------------------------------
#include <QByteArray>
#include <QColor>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QSettings;
class QTimer;

//----------------------------------------------

struct PermissionGrant
{
    QString programKey;
    QString actionText;
};

//----------------------------------------------

/**
 * @brief Профиль модели: имя, адрес API, модель, токен, класс задач и баланс.
 */
struct LlmProfile
{
    LlmProfile()
        : modality(WorkerTaskKindLlm)
        , maxToolIterations(1000)
        , contextCap(0)
    {
    }

    QString id;
    QString name;
    QString baseUrl;
    QString model;
    QString apiKey;
    /** Класс задач API: llm / image / speech / audio / video / embedding / decisions / other. */
    WorkerTaskKind modality;
    /**
     * @brief Краткое назначение модели (для воркеров: когда выбирать этот профиль).
     *        Например: «генерация картинок» vs «сегментация объектов на изображении».
     */
    QString purpose;
    /**
     * @brief URL или путь относительно baseUrl для запроса баланса.
     *        Пусто — баланс не запрашивается.
     */
    QString balanceUrl;
    /**
     * @brief Точечный путь к числу в JSON (например balance_infos.0.total_balance).
     *        Пусто — авторазбор типичных ответов (DeepSeek и др.).
     */
    QString balanceJsonPath;
    /** Максимум вызовов функций за ход воркера с этим профилем. */
    int maxToolIterations;
    /**
     * @brief Потолок контекста (токены). 0 — полное окно модели.
     */
    int contextCap;
};

//----------------------------------------------

/**
 * @brief Счётчики токенов одной роли воркера.
 */
struct RoleUsageStats
{
    RoleUsageStats()
        : requests(0)
        , promptTokens(0)
        , completionTokens(0)
        , cachedPromptTokens(0)
    {
    }

    qint64 requests;
    qint64 promptTokens;
    qint64 completionTokens;
    qint64 cachedPromptTokens;
};

//----------------------------------------------

/**
 * @brief Сводка использования модели: запросы, токены и разбивка по ролям.
 */
struct UsageStats
{
    UsageStats()
        : promptTokens(0)
        , completionTokens(0)
        , totalTokens(0)
        , cachedPromptTokens(0)
        , requestCount(0)
    {
    }

    void clear();
    void add(int promptTokens, int completionTokens, int totalTokens,
             int cachedPromptTokens, QString role);

    qint64 promptTokens;
    qint64 completionTokens;
    qint64 totalTokens;
    qint64 cachedPromptTokens;
    qint64 requestCount;
    QMap<QString, RoleUsageStats> byRole;
};

//----------------------------------------------

//----------------------------------------------

/**
 * @brief Настройки воркеров/API/доступов: либо defaults программы, либо проекта.
 * Полные профили моделей живут только в defaults.
 */
enum SettingsKind
{
    /** Значения по умолчанию в config/ — ими инициализируется новый проект. */
    SettingsKindDefaults = 0,
    /** Настройки открытого проекта в ___project___/ — независимы от defaults. */
    SettingsKindProject = 1
};

//----------------------------------------------

/**
 * @brief Рабочий набор настроек (профили только в defaults; в проекте — ссылки).
 */
struct WorkspaceSettings
{
    WorkspaceSettings();

    QList<PermissionGrant> permissionGrants;
    QSet<QString>          alwaysCommands;
    bool                   shellControl;
    bool                   outsideFilesControl;
    bool                   systemSettingsControl;
    /** Заполняется только для SettingsKindDefaults (config/ai). */
    QList<LlmProfile>      profiles;
    QList<WorkerPreset>    workerPresets;
    /** Пресет корневого воркера при создании проекта / пустого сеанса. */
    QString                rootWorkerPresetId;
    QString                workerProfileId[3];
    int                    maxWorkerDepth;
    int                    maxConcurrentWorkers;
    int                    llmTimeoutSec;
};

//----------------------------------------------

/**
 * @brief Настройки приложения: defaults (config/) и проект (___project___/).
 *
 * Оформление UI и полные профили моделей — только в defaults (config/ai).
 * В проекте хранятся ссылки на профили (workerProfileId, preset.profileId)
 * плюс пресеты, лимиты и контроль действий; при создании проекта копируются
 * из defaults, дальше правятся независимо (кроме тел профилей).
 */
class AppSettings : public QObject
{
    Q_OBJECT
public:
    static AppSettings *instance();

    /**
     * @brief Каталог config рядом с exe (создаётся при необходимости).
     */
    static QString configDir();
    /**
     * @brief Каталог config/ai с текстовыми файлами профилей программы.
     */
    static QString aiProfilesDir();
    /**
     * @brief Путь к config/settings.ini (defaults и оформление).
     */
    static QString settingsFilePath();

    void load();
    void save() const;
    void save(SettingsKind kind) const;
    void setProjectRoot(QString root);
    QString projectRoot() const;
    bool hasProjectSettings() const;
    static QString projectStoreDir(QString projectRoot);

    SettingsKind activeKind() const;
    const WorkspaceSettings &workspace(SettingsKind kind) const;

    bool isCommandAlwaysAllowed(QString programKey) const;
    bool isCommandAlwaysAllowed(SettingsKind kind, QString programKey) const;
    void addAlwaysAllowedCommand(QString programKey, QString actionText = QString());
    void addAlwaysAllowedCommand(SettingsKind kind, QString programKey,
                                 QString actionText = QString());
    void addAlwaysAllowedCommands(const QStringList &keys);
    void addAlwaysAllowedCommands(SettingsKind kind, const QStringList &keys);
    void removeAlwaysAllowedCommand(QString programKey);
    void removeAlwaysAllowedCommand(SettingsKind kind, QString programKey);
    QStringList alwaysAllowedCommands() const;
    QStringList alwaysAllowedCommands(SettingsKind kind) const;
    QList<PermissionGrant> permissionGrants() const;
    QList<PermissionGrant> permissionGrants(SettingsKind kind) const;

    bool shellControlEnabled() const;
    bool shellControlEnabled(SettingsKind kind) const;
    void setShellControlEnabled(bool enabled);
    void setShellControlEnabled(SettingsKind kind, bool enabled);
    bool outsideFilesControlEnabled() const;
    bool outsideFilesControlEnabled(SettingsKind kind) const;
    void setOutsideFilesControlEnabled(bool enabled);
    void setOutsideFilesControlEnabled(SettingsKind kind, bool enabled);
    bool systemSettingsControlEnabled() const;
    bool systemSettingsControlEnabled(SettingsKind kind) const;
    void setSystemSettingsControlEnabled(bool enabled);
    void setSystemSettingsControlEnabled(SettingsKind kind, bool enabled);

    int     fontPointSize() const;
    void    setFontPointSize(int size);
    QColor  textColor() const;
    void    setTextColor(QColor color);
    QColor  windowColor() const;
    void    setWindowColor(QColor color);
    QColor  panelColor() const;
    void    setPanelColor(QColor color);
    QColor  accentColor() const;
    void    setAccentColor(QColor color);
    QColor  chatUserColor() const;
    void    setChatUserColor(QColor color);
    QColor  chatAssistantColor() const;
    void    setChatAssistantColor(QColor color);
    QColor  chatOtherColor() const;
    void    setChatOtherColor(QColor color);
    QColor  inputColor() const;
    QColor  toolbarColor() const;
    QColor  borderColor() const;
    QColor  mutedTextColor() const;
    QColor  selectionColor() const;
    QColor  scrollbarColor() const;
    bool    isLightTheme() const;

    /**
     * @brief Показывать пересылаемый между воркерами контекст в чатах и журнале.
     */
    bool showForwardedContext() const;
    void setShowForwardedContext(bool enabled);

    QString uiLanguage() const;
    void setUiLanguage(QString code);
    void persistUiLanguage(QString code);

    /**
     * @brief Геометрия и состояние главного окна/доков — настройки программы,
     *        не привязаны к проекту.
     */
    QByteArray mainWindowGeometry() const;
    QByteArray mainWindowState() const;
    void setMainWindowLayout(const QByteArray &geometry, const QByteArray &state);
    /**
     * @brief Размеры сплиттеров левой колонки доков (ширина / высоты).
     *        Нулевые значения — нет сохранённых размеров.
     */
    int leftDockWidth() const;
    int projectDockHeight() const;
    int graphThumbDockHeight() const;
    void setMainWindowDockSplit(int leftWidth, int projectHeight, int graphHeight);

    /**
     * @brief Учёт токенов одного ответа модели.
     * @param role  ключ роли воркера (manager/executor/tester/tool/root) для разбивки.
     *
     * Всегда добавляется в общую статистику приложения. Если открыт проект —
     * дублируется в статистику этого проекта.
     */
    void addUsage(int promptTokens, int completionTokens, int totalTokens,
                  int cachedPromptTokens = 0, QString role = QString());
    UsageStats globalUsage() const;
    UsageStats projectUsage() const;
    qint64 promptTokens() const;
    qint64 completionTokens() const;
    qint64 totalTokens() const;
    qint64 cachedPromptTokens() const;
    qint64 requestCount() const;
    QStringList usageRoles() const;
    qint64 roleRequestCount(QString role) const;
    qint64 rolePromptTokens(QString role) const;
    qint64 roleCompletionTokens(QString role) const;
    qint64 roleCachedPromptTokens(QString role) const;
    void resetUsage();
    void resetGlobalUsage();
    /**
     * @brief Сбросить настройки и статистику открытого проекта как при создании.
     */
    void reseedOpenProject();

    int  maxWorkerDepth() const;
    int  maxWorkerDepth(SettingsKind kind) const;
    void setMaxWorkerDepth(int depth);
    void setMaxWorkerDepth(SettingsKind kind, int depth);
    int  maxConcurrentWorkers() const;
    int  maxConcurrentWorkers(SettingsKind kind) const;
    void setMaxConcurrentWorkers(int count);
    void setMaxConcurrentWorkers(SettingsKind kind, int count);
    int  llmTimeoutSec() const;
    int  llmTimeoutSec(SettingsKind kind) const;
    void setLlmTimeoutSec(int seconds);
    void setLlmTimeoutSec(SettingsKind kind, int seconds);
    /** Полные профили — всегда из настроек программы (config/ai). */
    QList<LlmProfile> profiles() const;
    LlmProfile profileById(QString id) const;
    LlmProfile profileForWorkerType(WorkerType type) const;
    LlmProfile profileForWorkerType(SettingsKind kind, WorkerType type) const;
    /** Профиль воркера: явный llmProfileId или профиль роли по умолчанию. */
    LlmProfile profileForWorker(WorkerType type, QString llmProfileId) const;
    QString workerProfileId(WorkerType type) const;
    QString workerProfileId(SettingsKind kind, WorkerType type) const;
    void setWorkerProfileId(WorkerType type, QString profileId);
    void setWorkerProfileId(SettingsKind kind, WorkerType type, QString profileId);
    QString addProfile(LlmProfile profile);
    void updateProfile(const LlmProfile &profile);
    void removeProfile(QString id);
    static int normalizeProfileContextCap(int tokens);

    QList<WorkerPreset> workerPresets() const;
    QList<WorkerPreset> workerPresets(SettingsKind kind) const;
    WorkerPreset workerPresetById(QString id) const;
    WorkerPreset workerPresetById(SettingsKind kind, QString id) const;
    QString addWorkerPreset(WorkerPreset preset, QString *error = 0);
    QString addWorkerPreset(SettingsKind kind, WorkerPreset preset, QString *error = 0);
    void updateWorkerPreset(const WorkerPreset &preset, QString *error = 0);
    void updateWorkerPreset(SettingsKind kind, const WorkerPreset &preset, QString *error = 0);
    void removeWorkerPreset(QString id);
    void removeWorkerPreset(SettingsKind kind, QString id);
    /** Пресеты, ссылающиеся на профиль (оба набора настроек). */
    QList<WorkerPreset> presetsUsingProfile(QString profileId) const;
    QList<WorkerPreset> presetsUsingProfile(SettingsKind kind, QString profileId) const;
    /** Пресеты проекта с profileId, которого нет в списке профилей. */
    QList<WorkerPreset> orphanProjectPresets() const;
    /** Удалить пресеты с данным profileId из defaults и/или проекта. */
    int removePresetsUsingProfile(QString profileId);
    /** Удалить пресеты проекта с неизвестным profileId. */
    int removeOrphanProjectPresets();
    QString workerPresetsSummary() const;
    QString workerPresetsSummary(SettingsKind kind) const;

    /**
     * @brief Пресет воркера верхнего уровня (корень чата при создании проекта).
     */
    QString rootWorkerPresetId() const;
    QString rootWorkerPresetId(SettingsKind kind) const;
    void setRootWorkerPresetId(QString presetId);
    void setRootWorkerPresetId(SettingsKind kind, QString presetId);
    WorkerPreset rootWorkerPreset() const;
    WorkerPreset rootWorkerPreset(SettingsKind kind) const;

signals:
    void appearanceChanged();
    void alwaysCommandsChanged();
    void controlFlagsChanged();
    void profilesChanged();
    void workerPresetsChanged();
    void workerLimitsChanged();
    void usageChanged();
    void projectSettingsChanged();
    void displayOptionsChanged();

private slots:
    void sltFlushStats();

private:
    explicit AppSettings(QObject *parent = 0);
    void saveStats() const;
    void scheduleStatsSave();
    void maybeSeedGlobalUsage();
    void loadUsage(QSettings *settings, UsageStats *stats);
    void saveUsage(QSettings *settings, const UsageStats &stats) const;
    void saveRoleStats(QSettings *settings, const UsageStats &stats) const;
    static QString normalizeKey(QString key);
    void migrateLegacyCommands(WorkspaceSettings *ws, const QStringList &keys);

    QString projectIniPath() const;
    WorkspaceSettings &ws(SettingsKind kind);
    const WorkspaceSettings &ws(SettingsKind kind) const;
    WorkspaceSettings &activeWs();
    const WorkspaceSettings &activeWs() const;
    void appendGrant(WorkspaceSettings *bundle, PermissionGrant grant);
    void loadPermissions(QSettings *settings, WorkspaceSettings *bundle);
    void savePermissions(QSettings *settings, const WorkspaceSettings &bundle) const;
    void loadWorkspaceIni(QSettings *settings, WorkspaceSettings *bundle);
    void saveWorkspaceIni(QSettings *settings, const WorkspaceSettings &bundle) const;
    void loadProfiles();
    void saveProfiles() const;
    void loadWorkerPresets(QSettings *settings, WorkspaceSettings *bundle);
    void saveWorkerPresets(QSettings *settings, const WorkspaceSettings &bundle) const;
    void ensureWorkerPresetsSeeded(WorkspaceSettings *bundle);
    void ensureRootWorkerPresetId(WorkspaceSettings *bundle);
    void syncPresetTaskKind(WorkerPreset *preset) const;
    void syncBundlePresetTaskKinds(WorkspaceSettings *bundle) const;
    void dedupePresetsByTypeProfile(WorkspaceSettings *bundle) const;
    int removePresetsUsingProfile(SettingsKind kind, QString profileId);
    void migrateLegacyProviders(QSettings *settings, WorkspaceSettings *bundle);
    void migrateFromNativeRegistry();
    void migrateProjectLocalProfiles();
    void migrateRoleBudgetsIntoProfiles();
    void writeProfileFile(const LlmProfile &profile) const;
    void removeProfileFile(QString id) const;
    QString profileFilePath(QString id) const;
    static QString sanitizeProfileFileStem(QString id);
    int profileIndexById(QString id) const;
    void ensureWorkerProfileIds(WorkspaceSettings *bundle);
    static QString newProfileId();
    void applyLightDefaults();
    bool projectWorkspaceSeeded() const;
    void seedProjectFromDefaults();
    void loadProjectWorkspace();
    void saveProjectWorkspace() const;
    void saveDefaultsWorkspace() const;
    void emitWorkspaceSignals(SettingsKind kind, bool limits, bool profiles,
                              bool presets, bool permissions, bool controls);

    QString                _projectRoot;
    bool                   _projectOpen;
    WorkspaceSettings      _defaults;
    WorkspaceSettings      _project;
    int                    _fontPointSize;
    QColor                 _textColor;
    QColor                 _windowColor;
    QColor                 _panelColor;
    QColor                 _accentColor;
    QColor                 _chatUserColor;
    QColor                 _chatAssistantColor;
    QColor                 _chatOtherColor;
    UsageStats             _globalUsage;
    UsageStats             _projectUsage;
    QTimer                *_statsTimer;
    bool                   _showForwardedContext;
    QString                _uiLanguage;
    QByteArray             _mainWindowGeometry;
    QByteArray             _mainWindowState;
    int                    _leftDockWidth;
    int                    _projectDockHeight;
    int                    _graphThumbDockHeight;
};

#endif // APPSETTINGS_H
