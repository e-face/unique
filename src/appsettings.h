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
 * @brief Model profile: name, API address, model, token, task class, and balance.
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
    /** API task class: llm / image / speech / audio / video / embedding / decisions / other. */
    WorkerTaskKind modality;
    /**
     * @brief Short purpose of the model (so workers know when to pick this profile).
     *        For example: "image generation" versus "object segmentation".
     */
    QString purpose;
    /**
     * @brief Balance URL, or a path relative to baseUrl.
     *        Empty — balance is not requested.
     */
    QString balanceUrl;
    /**
     * @brief Dotted path to a number in JSON (for example balance_infos.0.total_balance).
     *        Empty — typical responses (DeepSeek and others) are parsed automatically.
     */
    QString balanceJsonPath;
    /**
     * @brief Generation request path (relative to baseUrl, or a full URL).
     *        Empty — the standard OpenAI-compatible paths for the task class.
     */
    QString requestPath;
    /**
     * @brief Async task poll template with {id}, for example /tasks/{id}.
     *        Empty — the link from the provider response, or /tasks/{id}.
     */
    QString taskPath;
    /**
     * @brief Model catalog with the parameter schema (relative to baseUrl, or a full URL).
     *        Empty — /models.
     */
    QString catalogUrl;
    /**
     * @brief JSON object: fields added to every generation request
     *        when the caller did not set them. Empty — nothing.
     */
    QString extraBody;
    /** Maximum function calls per worker turn with this profile. */
    int maxToolIterations;
    /**
     * @brief Context ceiling (tokens). 0 — the full model window.
     */
    int contextCap;
};

//----------------------------------------------

/**
 * @brief Token counters for one worker role.
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
 * @brief Model usage summary: requests, tokens, and a breakdown by role.
 */
struct UsageStats
{
    UsageStats()
        : promptTokens(0)
        , completionTokens(0)
        , totalTokens(0)
        , cachedPromptTokens(0)
        , requestCount(0)
        , workMs(0)
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
    /** Total time of model requests and function calls, in milliseconds. */
    qint64 workMs;
    QMap<QString, RoleUsageStats> byRole;
};

//----------------------------------------------

//----------------------------------------------

/**
 * @brief Worker, API, and permission settings: either program defaults or the project.
 * Full model profiles live only in defaults.
 */
enum SettingsKind
{
    /** Defaults in config/ — a new project is initialized from them. */
    SettingsKindDefaults = 0,
    /** Settings of the open project in ___project___/ — independent of defaults. */
    SettingsKindProject = 1
};

//----------------------------------------------

/**
 * @brief Working settings bundle (profiles only in defaults; the project stores references).
 */
struct WorkspaceSettings
{
    WorkspaceSettings();

    QList<PermissionGrant> permissionGrants;
    QSet<QString>          alwaysCommands;
    bool                   shellControl;
    bool                   outsideFilesControl;
    bool                   systemSettingsControl;
    /** Filled only for SettingsKindDefaults (config/ai). */
    QList<LlmProfile>      profiles;
    QList<WorkerPreset>    workerPresets;
    /** Root worker preset when a project or an empty session is created. */
    QString                rootWorkerPresetId;
    /**
     * Preset ids from the program settings that were removed in this project
     * and must not be copied in again.
     */
    QSet<QString>          hiddenWorkerPresetIds;
    QString                workerProfileId[3];
    int                    maxWorkerDepth;
    int                    maxConcurrentWorkers;
    int                    llmTimeoutSec;
};

//----------------------------------------------

/**
 * @brief Application settings: defaults (config/) and the project (___project___/).
 *
 * UI appearance and full model profiles live only in defaults (config/ai).
 * The project stores profile references (workerProfileId, preset.profileId)
 * plus presets, limits, and action control. Creating a project copies presets
 * from defaults. A new preset in the program settings is also added to the
 * open project; one deleted in the project is not copied back.
 */
class AppSettings : public QObject
{
    Q_OBJECT
public:
    static AppSettings *instance();

    /**
     * @brief config directory next to the executable (created when needed).
     */
    static QString configDir();
    /**
     * @brief config/ai directory with the program's profile text files.
     */
    static QString aiProfilesDir();
    /**
     * @brief Path to config/settings.ini (defaults and appearance).
     */
    static QString settingsFilePath();

    void load();
    void save() const;
    void save(SettingsKind kind) const;
    /** Flush deferred settings to disk (before switching projects, and similar). */
    void flushPersist();
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
    /** Soft light fill of a worker card. */
    QColor  workerCardColor(WorkerType type, bool root = false) const;
    QColor  workerCardTextColor() const;
    QColor  workerCardMutedColor() const;
    QColor  selectionColor() const;
    QColor  scrollbarColor() const;
    bool    isLightTheme() const;
    /** Worker chat feed background: the window in a light theme, a slightly darker panel in a dark one. */
    QColor  chatBackgroundColor() const;

    /**
     * @brief Show context forwarded between workers in chats and the journal.
     */
    bool showForwardedContext() const;
    void setShowForwardedContext(bool enabled);

    /**
     * @brief Approve model confirmation requests without a dialog.
     */
    bool autoApprovePermissions() const;
    void setAutoApprovePermissions(bool enabled);

    /**
     * @brief The model reasons first, then answers.
     *        Off by default. Read at request time.
     */
    bool reasoningEnabled() const;
    void setReasoningEnabled(bool enabled);

    /**
     * @brief Drop only large file bodies from context,
     *        and only when the model asks for FILE_DROP.
     */
    bool dropLargeContextFiles() const;
    void setDropLargeContextFiles(bool enabled);
    /** Threshold in kilobytes for a file body that sits in context. */
    int largeContextFileKb() const;
    void setLargeContextFileKb(int kb);
    int largeContextFileBytes() const;

    QString uiLanguage() const;
    void setUiLanguage(QString code);
    void persistUiLanguage(QString code);

    /**
     * @brief Main window and dock geometry and state — program settings,
     *        not tied to a project.
     */
    QByteArray mainWindowGeometry() const;
    QByteArray mainWindowState() const;
    void setMainWindowLayout(const QByteArray &geometry, const QByteArray &state);
    /**
     * @brief Splitter sizes of the left dock column (width / heights).
     *        Zero means no saved sizes.
     */
    int leftDockWidth() const;
    int projectDockHeight() const;
    int graphThumbDockHeight() const;
    void setMainWindowDockSplit(int leftWidth, int projectHeight, int graphHeight);

    /**
     * @brief Account tokens of one model reply.
     * @param role  worker role key (manager/executor/tester/tool/root) for the breakdown.
     *
     * Always added to the application totals. If a project is open,
     * the same numbers are also added to that project's statistics.
     */
    void addUsage(int promptTokens, int completionTokens, int totalTokens,
                  int cachedPromptTokens = 0, QString role = QString());
    /**
     * @brief Start a work span (a model request or a function call).
     *        The time is added to statistics in endWork.
     */
    int beginWork();
    void endWork(int id);
    /**
     * @brief Accumulated time plus spans that are still running.
     *        @p project — the open project's counter, otherwise the global one.
     */
    qint64 liveWorkMs(bool project) const;
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
     * @brief Reset the open project's settings and statistics as if it were just created.
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
    /** Full profiles always come from the program settings (config/ai). */
    QList<LlmProfile> profiles() const;
    LlmProfile profileById(QString id) const;
    LlmProfile profileForWorkerType(WorkerType type) const;
    LlmProfile profileForWorkerType(SettingsKind kind, WorkerType type) const;
    /** Worker profile: an explicit llmProfileId, or the role's default profile. */
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
    /** Preset that matches a live worker's type and model profile. */
    WorkerPreset presetForWorker(WorkerType type, QString profileId) const;
    QString addWorkerPreset(WorkerPreset preset, QString *error = 0);
    QString addWorkerPreset(SettingsKind kind, WorkerPreset preset, QString *error = 0);
    void updateWorkerPreset(const WorkerPreset &preset, QString *error = 0);
    void updateWorkerPreset(SettingsKind kind, const WorkerPreset &preset, QString *error = 0);
    void removeWorkerPreset(QString id);
    void removeWorkerPreset(SettingsKind kind, QString id);
    /** Presets that reference a profile (both settings sets). */
    QList<WorkerPreset> presetsUsingProfile(QString profileId) const;
    QList<WorkerPreset> presetsUsingProfile(SettingsKind kind, QString profileId) const;
    /** Project presets whose profileId is missing from the profile list. */
    QList<WorkerPreset> orphanProjectPresets() const;
    /** Remove presets with this profileId from defaults and/or the project. */
    int removePresetsUsingProfile(QString profileId);
    /** Remove project presets with an unknown profileId. */
    int removeOrphanProjectPresets();
    QString workerPresetsSummary() const;
    QString workerPresetsSummary(SettingsKind kind) const;

    /**
     * @brief Top-level worker preset (chat root when a project is created).
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
    void sltFlushPersist();
    void sltEmitWorkerLimitsChanged();
    void sltEmitProfilesChanged();
    void sltEmitWorkerPresetsChanged();

private:
    explicit AppSettings(QObject *parent = 0);
    void saveStats() const;
    void scheduleStatsSave();
    void schedulePersist(SettingsKind kind);
    void scheduleWorkerLimitsSignal();
    void scheduleProfilesSignal();
    void scheduleWorkerPresetsSignal();
    static bool profilesEqual(const LlmProfile &a, const LlmProfile &b);
    static bool presetsEqual(const WorkerPreset &a, const WorkerPreset &b);
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
    bool copyMissingDefaultPresets(WorkspaceSettings *bundle);
    void syncPresetTaskKind(WorkerPreset *preset) const;
    void syncBundlePresetTaskKinds(WorkspaceSettings *bundle) const;
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
    QMap<int, qint64>      _openWork;
    int                    _nextWorkId;
    QTimer                *_statsTimer;
    QTimer                *_persistTimer;
    QTimer                *_limitsSignalTimer;
    QTimer                *_profilesSignalTimer;
    QTimer                *_presetsSignalTimer;
    bool                   _persistDefaultsPending;
    bool                   _persistProjectPending;
    bool                   _showForwardedContext;
    bool                   _autoApprovePermissions;
    bool                   _reasoningEnabled;
    bool                   _dropLargeContextFiles;
    int                    _largeContextFileKb;
    QString                _uiLanguage;
    QByteArray             _mainWindowGeometry;
    QByteArray             _mainWindowState;
    int                    _leftDockWidth;
    int                    _projectDockHeight;
    int                    _graphThumbDockHeight;
};

#endif // APPSETTINGS_H
