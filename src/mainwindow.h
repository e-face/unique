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
class QMenuBar;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QShowEvent;
class QTabWidget;
class QThread;
class QTimer;
class QToolButton;
class SessionTabBar;
class SettingsPage;
class TeamSchemaDialog;
class UpdateChecker;
class Worker;
class WorkerGraphWidget;
class WorkerManager;

//----------------------------------------------

/**
 * @brief Main window: chats and the worker schema of the current project.
 *
 * Work happens only inside an opened or created project.
 * Chats are saved in that directory's ___project___. At startup
 * no project is opened; it is chosen from the File menu.
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = 0);
    virtual ~MainWindow();

    void beginStatusProgress(QString title, int total);
    void updateStatusProgress(int current, int total, QString detail = QString(),
                              bool showCounts = true);
    void endStatusProgress(QString readyMessage = QString());

protected:
    virtual void closeEvent(QCloseEvent *event);
    virtual void showEvent(QShowEvent *event);
    virtual void changeEvent(QEvent *event);
    virtual bool eventFilter(QObject *watched, QEvent *event);
#ifdef Q_OS_WIN
    virtual bool nativeEvent(const QByteArray &eventType, void *message, long *result);
#endif

private slots:
    void sltClear();
    void sltResetWorkspace();
    void sltOpenProject();
    void sltNewProjectDirectory();
    void sltNewProjectExisting();
    void sltOpenLastProject();
    void sltOpenRecentProject();
    void sltRebuildRecentMenu();
    void sltSendRequested(const QString &text);
    void sltCompactRequested();
    void sltSessionContextUsage(int usedTokens, int limitTokens);
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
    void sltComposerLiveStatus(const QString &text);
    void sltComposerLiveFade(int alpha);
    void sltShowSettings();
    void sltShowProjectSettings();
    void sltCreateTeamSchema();
    void sltShowMySchema();
    void sltShowStatistics();
    void sltShowAbout();
    void sltCheckUpdates();
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
    void sltComposerOpenAttachment(QString relativePath);
    void sltComposerFilesDropped(const QStringList &absolutePaths);
    void sltComposerPasteHint(QString text, bool force);
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
    void sltComposerVcsAction(QString actionId);
    void sltVcsCommit();
    void sltVcsPush();
    void sltVcsPull();
    void sltVcsNewBranch();
    void sltVcsSwitchBranch();
    void sltVcsAdd();
    void sltCaptionMin();
    void sltCaptionMax();
    void sltCaptionClose();
    void sltCaptionIcon();

private:
    /**
     * @brief Switch projects when no tool is running
     *        inside a nested event loop: deleting sessions from under
     *        a live invoke() stack crashes the program.
     */
    void requestProjectSwitch(QString root, bool loadExisting, QString retireStore);
    void beginNewProject(QString workspace);
    bool anyAgentInsideToolCall() const;
    void performProjectSwitch();
    void scheduleUiRefresh();
    void refreshBusyTabs();
    void rebuildGraphIfVisible();
    void updateGraphThumb();
    void setupUi();
    void setupMenus();
    void setupCaption();
    void updateCaptionButtons();
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
    bool projectAwaitingFirstQuery() const;
    QString teamSchemaPath() const;
    QString teamSchemaRelativePath() const;
    bool hasSavedTeamSchema() const;
    void storeTeamSchema(TeamSchemaDialog &dialog, bool insertIntoQuery);
    void writeTeamSchemaText(const QString &text);
    void attachSavedTeamSchema();
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
    void refreshLiveStatusBar();
    void updateComposerState();
    void refreshVersionControl();
    void showVcsResult(bool ok, QString text, bool canceled);
    QString projectRootPath() const;
    bool askVcsText(QString title, QString label, QString okText, bool multiline,
                    QString *value);
    bool askVcsChoice(QString title, QString label, QString okText,
                      const QStringList &options, QString *value);
    void updateComposerAttachmentsUi();
    int addComposerAbsoluteFiles(const QStringList &absolutePaths);
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
    void updateWorkerMenu();
    void refreshContextMeter();

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
    QMenu               *_workerMenu;
    QMenu               *_projectMenu;
    QMenu               *_vcsMenu;
    QMenu               *_helpMenu;
    QMenu               *_recentMenu;
    QAction             *_balanceAction;
    QAction             *_quitAction;
    QAction             *_windowChatAction;
    QAction             *_windowGraphAction;
    QAction             *_windowJournalAction;
    QAction             *_windowSettingsAction;
    QAction             *_workerCompactAction;
    QAction             *_workerInterruptAction;
    QAction             *_helpStatsAction;
    QAction             *_helpUpdateAction;
    QAction             *_helpAboutAction;
    QAction             *_openProjectAction;
    QMenu               *_newProjectMenu;
    QAction             *_newProjectDirectoryAction;
    QAction             *_newProjectExistingAction;
    QAction             *_openLastAction;
    QAction             *_fileClearAction;
    QAction             *_clearAction;
    QAction             *_showChatAction;
    QAction             *_showGraphAction;
    QAction             *_showJournalAction;
    QAction             *_prevConfirmAction;
    QAction             *_nextConfirmAction;
    QAction             *_projectSettingsAction;
    QAction             *_projectSchemaAction;
    QAction             *_projectMySchemaAction;
    QAction             *_vcsCommitAction;
    QAction             *_vcsPushAction;
    QAction             *_vcsPullAction;
    QAction             *_vcsNewBranchAction;
    QAction             *_vcsSwitchBranchAction;
    QAction             *_vcsAddAction;
    QCheckBox           *_inspectCheck;
    ComposerInput       *_composerBar;
    GraphThumb          *_graphThumb;
    QPlainTextEdit      *_composer;
    QStringList          _composerAttachments;
    QString              _composerLiveStatus;
    int                  _composerLiveAlpha;
    QLabel              *_liveStatusLabel;
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
    UpdateChecker       *_updateChecker;
    bool                 _updateCheckScheduled;
    bool                 _vcsBusy;
    QWidget             *_captionBar;
    QMenuBar            *_menuBar;
    QToolButton         *_captionMin;
    QToolButton         *_captionMax;
    QToolButton         *_captionClose;
    QToolButton         *_captionIcon;
    int                  _resizeEdge;
    int                  _hoverEdge;
    bool                 _edgeCursorOn;
    bool                 _linuxMaximized;
    bool                 _winFrameReady;
    QPoint               _edgeAnchor;
    QRect                _edgeGeom;
    QRect                _linuxRestore;

#ifndef Q_OS_WIN
    int frameEdgeAt(const QPoint &global) const;
    void updateEdgeCursor(int edge);
    void applyEdgeResize(const QPoint &global);
    bool handleLinuxFrameEvent(QObject *watched, QEvent *event);
#endif
#ifdef Q_OS_WIN
    void polishWindowsFrame();
    int captionHitTest(int x, int y) const;
#endif
};

#endif // MAINWINDOW_H
