#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "appsettings.h"
#include "workertypes.h"
//----------------------------------------------
#include <QIcon>
#include <QJsonArray>
#include <QList>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QtGlobal>

class ChatSession;
class ComposerInput;
class ConversationWidget;
class GraphThumb;
class QDockWidget;
class JournalWidget;
class ProjectTreeDock;
class ProjectWorkspace;
class QAction;
class QCheckBox;
class QCloseEvent;
class QEvent;
class QMenu;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QShowEvent;
class QTabWidget;
class QThread;
class QTimer;
class SessionTabBar;
class SettingsPage;
class Worker;
class WorkerGraphWidget;
class WorkerManager;

//----------------------------------------------

/**
 * @brief Главное окно: чаты и схема воркеров текущего проекта.
 *
 * Работа ведётся только внутри открытого или созданного проекта.
 * Чаты сохраняются в ___project___ этого каталога. При старте
 * проект не открывается — его выбирают из меню «Файл».
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = 0);
    virtual ~MainWindow();

protected:
    virtual void closeEvent(QCloseEvent *event);
    virtual void showEvent(QShowEvent *event);
    virtual bool eventFilter(QObject *watched, QEvent *event);

private slots:
    void sltClear();
    void sltResetWorkspace();
    void sltOpenProject();
    void sltNewProject();
    void sltOpenLastProject();
    void sltOpenRecentProject();
    void sltRebuildRecentMenu();
    void sltSendRequested(const QString &text);
    void sltCompactRequested();
    void sltBusyChanged(bool busy);
    void sltUsageUpdated(int promptTokens, int completionTokens, int totalTokens,
                         int cachedPromptTokens);
    void sltResetStatistics();
    void sltResetGlobalStatistics();
    void sltUsageChanged();
    void sltRefreshProjectMeters();
    void sltSaveSettings();
    void sltShowChat();
    void sltNewChat();
    void sltChatUserMessage(const QString &text);
    void sltInspectToggled(bool enabled);
    void sltInspectRequested(const QString &title, const QString &text);
    void sltWorkerCreated(Worker *worker);
    void sltWorkerFinished();
    void sltWorkerAboutToBeRemoved(Worker *worker);
    void sltWorkerTreeChanged();
    void sltWorkerActivated(const QString &id);
    void sltToolRemoveRequested(const QString &id);
    void sltWorkerRemoveRequested(const QString &id);
    void sltWorkerForceStop();
    void sltWorkerForceReport();
    void sltWorkerForceStopById(const QString &id);
    void sltWorkerForceReportById(const QString &id);
    void sltWorkerProfileChangeRequested(const QString &workerId, const QString &profileId);
    void sltPermissionRequested(const QString &action);
    void sltPermissionAnswered(bool approved, bool always, const QString &comment);
    void sltConsoleInputRequested(const QString &transcript);
    void sltConsoleInputAnswered(const QString &line, bool sent);
    void sltSubtreeBudgetRequested(const QString &text);
    void sltSubtreeBudgetAnswered(bool granted);
    void sltToolBudgetExhausted(int chunk);
    void sltContinuationRequested(const QString &error);
    void sltContinuationAnswered(bool retry);
    void sltShowGraph();
    void sltShowJournal();
    void sltJournalWorkerChatRequested(const QString &workerId);
    void sltShowSettings();
    void sltShowProjectSettings();
    void sltShowStatistics();
    void sltShowAbout();
    void sltShowBalance();
    void sltAppearanceApplied();
    void sltProfilesChanged();
    void sltDefaultProfilesChanged();
    void sltApplyClientsFromProfiles();
    void sltRemoveWorkersForProfile(const QString &profileId);
    void sltPrevConfirmation();
    void sltNextConfirmation();
    void sltComposerSend();
    void sltComposerStop();
    void sltComposerAttachPlus();
    void sltComposerAttachImage();
    void sltComposerAttachAudio();
    void sltComposerAttachVideo();
    void sltComposerAttachFile();
    void sltComposerClearAttachments();
    void sltComposerRemoveAttachment(int index);
    void sltComposerFilesDropped(const QStringList &absolutePaths);
    void sltStopAll();
    void sltFlushSaveChats();
    void sltChatSaveFinished();
    void sltFileChangeLoadProgress(int current, int total, const QString &path);
    void sltAdvanceConfirmation();
    void sltTabBlink();
    void sltTabCloseRequested(int index);
    void sltCurrentTabChanged(int index);
    void updateBusyTabs();
    void sltRefreshWorkerUi();
    void sltResumeProjectSwitch();
    void sltLocaleChanged(const QString &code);

private:
    /**
     * @brief Переключить проект, когда ни один инструмент не выполняется
     *        во вложенном цикле событий: иначе удаление сессий из-под
     *        живого стека invoke() роняет программу.
     */
    void requestProjectSwitch(QString root, bool loadExisting, QString retireStore);
    bool anyAgentInsideToolCall() const;
    void performProjectSwitch();
    void scheduleUiRefresh();
    void refreshBusyTabs();
    void rebuildGraphIfVisible();
    void updateGraphThumb();
    void setupUi();
    void setupMenus();
    void retranslateUi();
    ChatSession *createSession(QString id, QString title);
    void attachSessionUi(ChatSession *session);
    void createGraphWindow();
    void createJournalWindow();
    QString workerWindowTitle(Worker *worker) const;
    void applyWorkerUi(Worker *worker);
    void applyClientToSession(ChatSession *session);
    void applyClientToAllSessions();
    void applyProjectToAllSessions();
    ChatSession *currentSession() const;
    ChatSession *rootSession() const;
    ChatSession *sessionByWidget(ConversationWidget *widget) const;
    ChatSession *sessionByAgent(QObject *agent) const;
    ChatSession *sessionByTabWidget(QWidget *widget) const;
    void loadOpenChats();
    void resumeWorkers();
    void saveOpenChats(bool wait = false);
    void scheduleSaveOpenChats();
    void waitForChatSave();
    void saveAppWindowLayout();
    void restoreAppWindowLayout();
    void saveProjectUiState();
    void restoreProjectUiState(bool preferJournalIfEmpty);
    QString tabKeyForWidget(QWidget *widget) const;
    void openTabByKey(const QString &key, bool makeCurrent);
    void pumpUi(bool force = false);
    void beginStatusProgress(QString title, int total);
    void updateStatusProgress(int current, int total, QString detail = QString());
    void endStatusProgress(QString readyMessage = QString());
    void updateWindowTitle();
    void applySessionGeometries();
    void rememberChatNumber(QString title);
    QString nextChatTitle();
    void loadSettings();
    void applyProjectDirectory(bool loadFileChanges = true, bool bindTree = true);
    void promptOrphanProjectPresets();
    bool retireProjectStore(QString storeDir);
    void ensureProjectStore();
    void persistAndStopCurrentProject();
    void discardSessionUi();
    void switchToProject(QString workspaceRoot, bool loadExisting);
    void openProjectAt(QString workspace);
    void rememberRecentProject(QString root);
    void forgetRecentProject(QString root);
    void pruneRecentProjects();
    void rebuildRecentProjectsMenu();
    void updateProjectActions();
    bool hasOpenProject() const;
    QString projectDialogStartPath() const;
    QString workspaceRootFromSelection(QString selected) const;
    int askStyledQuestion(QString title, QString text, QString yesText, QString noText);
    void ensureTabVisible(QWidget *widget, QString title, QIcon icon = QIcon());
    void ensureSessionVisible(ChatSession *session);
    void focusWorkerSession(Worker *worker);
    void restoreSessionContent(ChatSession *session);
    void updateSessionTabTitle(ChatSession *session);
    QIcon iconForSession(ChatSession *session) const;
    void applyAppStyle();
    void styleStatusPills();
    void updateComposerState();
    void updateComposerAttachmentsUi();
    void addComposerAbsoluteFiles(const QStringList &absolutePaths);
    QStringList pickComposerAttachments(QString filter, QString title);
    QString importAttachmentIntoProject(QString absolutePath, QString *error);
    QPixmap composerThumbForPath(QString relativePath) const;
    QString encodeProjectImageDataUrl(QString relativePath, QString *error) const;
    QString encodeProjectFileDataUrl(QString relativePath, QString *error) const;
    QString encodeProjectAudioBase64(QString relativePath, QString *formatOut,
                                     QString *error) const;
    QString readProjectTextAttachment(QString relativePath, int maxBytes,
                                     QString *error) const;
    static bool isImageAttachmentPath(QString path);
    static bool isAudioAttachmentPath(QString path);
    static bool isPdfAttachmentPath(QString path);
    static bool isTextLikeAttachmentPath(QString path);
    static QString audioFormatForPath(QString path);
    void sendRootComposerMessage(QString text, QJsonArray contentParts = QJsonArray());
    LlmProfile profileForComposerTarget() const;
    void stopAllWork();
    bool anyWorkActive() const;
    void setAllStopEnabled(bool enabled);
    bool anySessionBusy() const;
    void applyInspectToAllSessions();
    void showInspectDialog(QString title, QString text);
    QList<Worker *> pendingConfirmationWorkers() const;
    void cycleConfirmation(int delta);
    void updatePermissionHighlights();

    ProjectWorkspace    *_workspace;
    WorkerManager       *_workers;
    QList<ChatSession *> _sessions;
    QTabWidget          *_tabs;
    SessionTabBar       *_tabBar;
    QTimer              *_tabBlinkTimer;
    QTimer              *_saveChatsTimer;
    QTimer              *_uiRefreshTimer;
    QTimer              *_profilesApplyTimer;
    bool                 _uiRefreshTree;
    bool                 _graphDirty;
    bool                 _tabBlinkOn;
    WorkerGraphWidget   *_graph;
    JournalWidget       *_journal;
    SettingsPage        *_settingsPage;
    SettingsPage        *_projectSettingsPage;
    ProjectTreeDock     *_projectDock;
    QDockWidget         *_graphThumbDock;
    QMenu               *_fileMenu;
    QMenu               *_windowMenu;
    QMenu               *_projectMenu;
    QMenu               *_helpMenu;
    QMenu               *_recentMenu;
    QAction             *_balanceAction;
    QAction             *_quitAction;
    QAction             *_windowChatAction;
    QAction             *_windowGraphAction;
    QAction             *_windowJournalAction;
    QAction             *_windowSettingsAction;
    QAction             *_helpStatsAction;
    QAction             *_helpAboutAction;
    QAction             *_openProjectAction;
    QAction             *_newProjectAction;
    QAction             *_openLastAction;
    QAction             *_fileClearAction;
    QAction             *_clearAction;
    QAction             *_showChatAction;
    QAction             *_showGraphAction;
    QAction             *_showJournalAction;
    QAction             *_prevConfirmAction;
    QAction             *_nextConfirmAction;
    QAction             *_projectSettingsAction;
    QCheckBox           *_inspectCheck;
    ComposerInput       *_composerBar;
    GraphThumb          *_graphThumb;
    QPlainTextEdit      *_composer;
    QStringList          _composerAttachments;
    QProgressBar        *_progressBar;
    QLabel              *_workTimeLabel;
    QLabel              *_changedFilesLabel;
    QLabel              *_addedLinesLabel;
    QLabel              *_removedLinesLabel;
    QTimer              *_projectMeterTimer;
    QThread             *_chatSaveThread;
    QString              _pendingActiveId;
    QString              _progressTitle;
    qint64               _lastPumpMs;
    int                  _progressDepth;
    int                  _nextChatNumber;
    bool                 _chatsShown;
    bool                 _closing;
    bool                 _resetting;
    bool                 _loadingProject;
    bool                 _projectStoreEnabled;
    bool                 _appWindowLayoutRestored;
    bool                 _chatSavePending;
    QStringList          _recentProjects;
    QString              _pendingSwitchRoot;
    QString              _pendingRetireStore;
    bool                 _pendingSwitchLoad;
    int                  _pendingSwitchTries;
};

#endif // MAINWINDOW_H
