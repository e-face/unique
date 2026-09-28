#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include "workertypes.h"
//----------------------------------------------
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
 * @brief Профиль модели: имя, адрес API, модель, токен и опциональный баланс.
 */
struct LlmProfile
{
    QString id;
    QString name;
    QString baseUrl;
    QString model;
    QString apiKey;
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

/**
 * @brief Глобальные настройки: разрешения команд, тема, статистика.
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
     * @brief Каталог config/ai с текстовыми файлами профилей.
     */
    static QString aiProfilesDir();
    /**
     * @brief Путь к config/settings.ini (глобальные настройки, не реестр).
     */
    static QString settingsFilePath();

    void load();
    void save() const;
    void setProjectRoot(QString root);
    QString projectRoot() const;
    static QString projectStoreDir(QString projectRoot);

    bool isCommandAlwaysAllowed(QString programKey) const;
    void addAlwaysAllowedCommand(QString programKey, QString actionText = QString());
    void addAlwaysAllowedCommands(const QStringList &keys);
    void removeAlwaysAllowedCommand(QString programKey);
    QStringList alwaysAllowedCommands() const;
    QList<PermissionGrant> permissionGrants() const;

    bool shellControlEnabled() const;
    void setShellControlEnabled(bool enabled);
    bool outsideFilesControlEnabled() const;
    void setOutsideFilesControlEnabled(bool enabled);
    bool systemSettingsControlEnabled() const;
    void setSystemSettingsControlEnabled(bool enabled);

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
     * @brief Максимальная глубина дерева воркеров (корень — глубина 0).
     */
    int  maxWorkerDepth() const;
    void setMaxWorkerDepth(int depth);
    /**
     * @brief Сколько воркеров могут одновременно вести диалог с моделью.
     */
    int  maxConcurrentWorkers() const;
    void setMaxConcurrentWorkers(int count);
    /**
     * @brief Сколько секунд ждать ответ модели на один HTTP-запрос,
     *        после чего запрос прерывается с ошибкой таймаута.
     */
    int  llmTimeoutSec() const;
    void setLlmTimeoutSec(int seconds);
    /**
     * @brief Бюджет вызовов функций за один ход для роли воркера.
     */
    int  roleToolIterations(WorkerType type) const;
    void setRoleToolIterations(WorkerType type, int count);
    /**
     * @brief Потолок контекста в токенах (0 — окно модели).
     */
    int  roleContextCap(WorkerType type) const;
    void setRoleContextCap(WorkerType type, int tokens);

    QList<LlmProfile> profiles() const;
    LlmProfile profileById(QString id) const;
    LlmProfile profileForWorkerType(WorkerType type) const;
    QString workerProfileId(WorkerType type) const;
    void setWorkerProfileId(WorkerType type, QString profileId);
    QString addProfile(LlmProfile profile);
    void updateProfile(const LlmProfile &profile);
    void removeProfile(QString id);

signals:
    void appearanceChanged();
    void alwaysCommandsChanged();
    void controlFlagsChanged();
    void profilesChanged();
    void workerLimitsChanged();
    void usageChanged();

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
    void migrateLegacyCommands(const QStringList &keys);

    QString projectIniPath() const;
    /**
     * @brief Перенести список «всегда» из старого project.ini в глобальные
     *        настройки и убрать его из файла проекта.
     */
    void migrateProjectPermissions(QString projectIni);
    void appendGrant(PermissionGrant grant);
    void loadPermissions(QSettings *settings);
    void savePermissions(QSettings *settings) const;
    void loadProfiles();
    void saveProfiles() const;
    void migrateLegacyProviders(QSettings *settings);
    void migrateFromNativeRegistry();
    void writeProfileFile(const LlmProfile &profile) const;
    void removeProfileFile(QString id) const;
    static QString profileFilePath(QString id);
    static QString sanitizeProfileFileStem(QString id);
    int profileIndexById(QString id) const;
    void ensureWorkerProfileIds();
    static QString newProfileId();
    void applyLightDefaults();

    QString                _projectRoot;
    QList<PermissionGrant> _permissionGrants;
    QSet<QString>          _alwaysCommands;
    bool                   _shellControl;
    bool                   _outsideFilesControl;
    bool                   _systemSettingsControl;
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
    QList<LlmProfile>      _profiles;
    QString                _workerProfileId[3];
    int                    _maxWorkerDepth;
    int                    _maxConcurrentWorkers;
    int                    _llmTimeoutSec;
    int                    _roleToolIterations[4];
    int                    _roleContextCap[4];
    QTimer                *_statsTimer;
};

#endif // APPSETTINGS_H
