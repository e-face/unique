#include "mainwindow.h"
//----------------------------------------------
#include "agent.h"
#include "appicons.h"
#include "appsettings.h"
#include "balancedialog.h"
#include "chatsession.h"
#include "conversationwidget.h"
#include "filechangestore.h"
#include "journalwidget.h"
#include "llmclient.h"
#include "openaicompatibleclient.h"
#include "projecttreedock.h"
#include "projectworkspace.h"
#include "settingspage.h"
#include "usagestatswidget.h"
#include "worker.h"
#include "workergraphwidget.h"
#include "workermanager.h"
#include "workertypes.h"
//----------------------------------------------
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegExp>
#include <QSettings>
#include <QShowEvent>
#include <QSize>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QThread>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

//----------------------------------------------

class ProjectStoreTrashThread : public QThread
{
public:
    explicit ProjectStoreTrashThread(QString path, QObject *parent = 0)
        : QThread(parent)
        , _path(path)
    {
        setPriority(QThread::LowPriority);
        connect(this, SIGNAL(finished()), this, SLOT(deleteLater()));
    }

protected:
    virtual void run()
    {
        if (_path.isEmpty())
            return;
        QDir folder(_path);
        folder.removeRecursively();
    }

private:
    QString _path;
};

//----------------------------------------------

struct ChatSaveJob
{
    QList<ChatSnapshot> dirty;
    QList<ChatSnapshot> index;
    QString activeId;
};

//----------------------------------------------

static void writeChatSaveJob(const ChatSaveJob &job)
{
    for (int i = 0; i < job.dirty.size(); ++i)
        ChatStore::saveChat(job.dirty.at(i));
    ChatStore::save(job.index, job.activeId);
}

//----------------------------------------------

class ChatSaveThread : public QThread
{
public:
    explicit ChatSaveThread(const ChatSaveJob &job, QObject *parent = 0)
        : QThread(parent)
        , _job(job)
    {
        setPriority(QThread::LowPriority);
    }

protected:
    virtual void run()
    {
        writeChatSaveJob(_job);
    }

private:
    ChatSaveJob _job;
};

//----------------------------------------------

class SessionTabBar : public QTabBar
{
public:
    explicit SessionTabBar(QWidget *parent = 0)
        : QTabBar(parent)
        , _graphPage(0)
        , _journalPage(0)
        , _blinkOn(false)
    {
        setExpanding(false);
        setElideMode(Qt::ElideRight);
        setUsesScrollButtons(true);
    }

    void setGraphPage(QWidget *page)
    {
        _graphPage = page;
        update();
    }

    void setJournalPage(QWidget *page)
    {
        _journalPage = page;
        update();
    }

    void setBusyIndexes(const QList<int> &indexes)
    {
        if (_busy == indexes)
            return;
        _busy = indexes;
        update();
    }

    void setPermissionIndexes(const QList<int> &indexes)
    {
        if (_permission == indexes)
            return;
        _permission = indexes;
        for (int i = 0; i < count(); ++i)
        {
            if (_permission.contains(i))
                setTabTextColor(i, QColor(250, 250, 250));
            else
                setTabTextColor(i, QColor(210, 210, 210));
        }
        update();
    }

    void setBlinkOn(bool on)
    {
        if (_blinkOn == on)
            return;
        _blinkOn = on;
        if (!_busy.isEmpty())
            update();
    }

protected:
    virtual void paintEvent(QPaintEvent *event)
    {
        QTabBar::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        AppSettings *s = AppSettings::instance();
        const QColor tabBg = s->toolbarColor();
        const QColor tabActive = tabBg.lighter(118);
        const QColor textColor = s->textColor();

        for (int i = 0; i < count(); ++i)
        {
            const QRect r = tabRect(i).adjusted(1, 1, -2, 0);
            QColor bg = (i == currentIndex()) ? tabActive : tabBg;
            if (_permission.contains(i))
                bg = QColor(199, 93, 66, 235);
            painter.setPen(Qt::NoPen);
            painter.setBrush(bg);
            painter.drawRoundedRect(r, 4, 4);

            const QRect textRect = tabRect(i).adjusted(8, 0, -22, 0);
            const QString shown = fontMetrics().elidedText(tabText(i), Qt::ElideRight,
                                                          qMax(8, textRect.width()));
            painter.setPen(_permission.contains(i) ? QColor(250, 250, 250) : textColor);
            painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, shown);
        }

        if (_busy.isEmpty())
            return;
        const int alpha = _blinkOn ? 210 : 75;
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(130, 205, 140, alpha), 1.8));
        foreach (int index, _busy)
        {
            if (index < 0 || index >= count())
                continue;
            const QRect r = tabRect(index).adjusted(2, 2, -3, -1);
            painter.drawRoundedRect(r, 4, 4);
        }
    }

private:
    QWidget    *_graphPage;
    QWidget    *_journalPage;
    QList<int>  _busy;
    QList<int>  _permission;
    bool        _blinkOn;
};

//----------------------------------------------

class SessionTabWidget : public QTabWidget
{
public:
    explicit SessionTabWidget(QWidget *parent = 0)
        : QTabWidget(parent)
        , _bar(new SessionTabBar(this))
    {
        setTabBar(_bar);
    }

    SessionTabBar *sessionBar() const
    {
        return _bar;
    }

private:
    SessionTabBar *_bar;
};

//----------------------------------------------

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , _workspace(new ProjectWorkspace(this))
    , _workers(new WorkerManager(_workspace, this))
    , _tabs(0)
    , _tabBar(0)
    , _tabBlinkTimer(0)
    , _saveChatsTimer(0)
    , _uiRefreshTimer(0)
    , _uiRefreshTree(false)
    , _graphDirty(true)
    , _tabBlinkOn(false)
    , _graph(0)
    , _journal(0)
    , _settingsPage(0)
    , _projectDock(0)
    , _recentMenu(0)
    , _openProjectAction(0)
    , _newProjectAction(0)
    , _openLastAction(0)
    , _fileClearAction(0)
    , _clearAction(0)
    , _showChatAction(0)
    , _showGraphAction(0)
    , _showJournalAction(0)
    , _prevConfirmAction(0)
    , _nextConfirmAction(0)
    , _inspectCheck(0)
    , _composer(0)
    , _composerSend(0)
    , _composerStop(0)
    , _progressBar(0)
    , _chatSaveThread(0)
    , _lastPumpMs(0)
    , _progressDepth(0)
    , _nextChatNumber(0)
    , _chatsShown(false)
    , _closing(false)
    , _resetting(false)
    , _loadingProject(false)
    , _projectStoreEnabled(false)
    , _chatSavePending(false)
    , _pendingSwitchLoad(false)
    , _pendingSwitchTries(0)
{
    setWindowTitle(QString::fromUtf8("dsagent"));
    setWindowIcon(AppIcons::mainChat());
    resize(960, 720);

    setupUi();
    setupMenus();
    connect(_workers, SIGNAL(workerCreated(Worker*)),
            this, SLOT(sltWorkerCreated(Worker*)));
    connect(_workers, SIGNAL(workerAboutToBeRemoved(Worker*)),
            this, SLOT(sltWorkerAboutToBeRemoved(Worker*)));
    connect(_workers, SIGNAL(treeChanged()),
            this, SLOT(sltWorkerTreeChanged()));
    connect(AppSettings::instance(), SIGNAL(usageChanged()),
            this, SLOT(sltUsageChanged()));
    connect(FileChangeStore::instance(), SIGNAL(loadProgress(int,int,QString)),
            this, SLOT(sltFileChangeLoadProgress(int,int,QString)));
    loadSettings();
    applyProjectDirectory();

    sltBusyChanged(false);
    statusBar()->showMessage(tr("Откройте или создайте проект"));
}

//----------------------------------------------

MainWindow::~MainWindow()
{
    _closing = true;
    saveOpenChats(true);
    sltSaveSettings();
}

//----------------------------------------------

void MainWindow::closeEvent(QCloseEvent *event)
{
    _closing = true;
    if (statusBar())
        statusBar()->showMessage(tr("Сохранение…"));
    saveOpenChats(true);
    sltSaveSettings();
    QMainWindow::closeEvent(event);
}

//----------------------------------------------

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (_chatsShown)
        return;
    _chatsShown = true;
    if (!hasOpenProject())
    {
        updateComposerState();
        updateProjectActions();
        statusBar()->showMessage(tr("Откройте или создайте проект"));
        return;
    }
    beginStatusProgress(tr("Загрузка проекта"), 0);
    loadOpenChats();
    applySessionGeometries();
    sltShowGraph();
    sltWorkerTreeChanged();
    resumeWorkers();
    endStatusProgress(tr("Готово"));
}

//----------------------------------------------

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == _composer && event && event->type() == QEvent::KeyPress)
    {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
        {
            if (!(keyEvent->modifiers() & Qt::ShiftModifier))
            {
                sltComposerSend();
                return true;
            }
        }
        return QMainWindow::eventFilter(watched, event);
    }

    return QMainWindow::eventFilter(watched, event);
}

//----------------------------------------------

void MainWindow::sltClear()
{
    if (_resetting || !hasOpenProject())
        return;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Очистить всё"));
    box.setText(tr("Удалить все чаты и воркеров и начать заново?\n"
                   "Папка проекта и ключи API не изменятся."));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    box.setButtonText(QMessageBox::Yes, tr("Очистить"));
    box.setButtonText(QMessageBox::No, tr("Отмена"));
    AppSettings *s = AppSettings::instance();
    box.setStyleSheet(QString::fromUtf8(
                          "QMessageBox { background:%1; color:%2; }"
                          "QMessageBox QLabel { color:%2; background:transparent; }"
                          "QPushButton { background:%3; color:%2; border:1px solid %4;"
                          "  border-radius:4px; padding:5px 12px; }")
                          .arg(s->panelColor().name(),
                               s->textColor().name(),
                               s->inputColor().name(),
                               s->borderColor().name()));
    if (box.exec() != QMessageBox::Yes)
        return;

    _resetting = true;
    foreach (ChatSession *session, _sessions)
    {
        if (session && session->agent())
            session->agent()->abort();
    }
    if (_workers)
    {
        foreach (Worker *worker, _workers->workers())
        {
            if (worker)
                worker->quitBlockingLoops();
        }
    }
    QMetaObject::invokeMethod(this, "sltResetWorkspace", Qt::QueuedConnection);
}

//----------------------------------------------

void MainWindow::sltResetWorkspace()
{
    _resetting = true;
    if (!hasOpenProject())
    {
        discardSessionUi();
        sltShowGraph();
        _resetting = false;
        updateComposerState();
        updateProjectActions();
        statusBar()->showMessage(tr("Откройте или создайте проект"));
        return;
    }

    if (_projectStoreEnabled)
        ChatStore::save(QList<ChatSnapshot>(), QString());

    discardSessionUi();

    sltNewChat();
    sltShowGraph();
    sltShowChat();
    _resetting = false;
    if (rootSession() && rootSession()->agent())
        rootSession()->agent()->refreshContextUsage();
    saveOpenChats();
    sltBusyChanged(false);
    updateComposerState();
    statusBar()->showMessage(tr("Начат новый пустой сеанс."));
}

//----------------------------------------------

//----------------------------------------------

int MainWindow::askStyledQuestion(QString title, QString text, QString yesText, QString noText)
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(title);
    box.setText(text);
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    box.setButtonText(QMessageBox::Yes, yesText);
    box.setButtonText(QMessageBox::No, noText);
    AppSettings *s = AppSettings::instance();
    box.setStyleSheet(QString::fromUtf8(
                          "QMessageBox { background:%1; color:%2; }"
                          "QMessageBox QLabel { color:%2; background:transparent; }"
                          "QPushButton { background:%3; color:%2; border:1px solid %4;"
                          "  border-radius:4px; padding:5px 12px; }")
                          .arg(s->panelColor().name(),
                               s->textColor().name(),
                               s->inputColor().name(),
                               s->borderColor().name()));
    return box.exec();
}

//----------------------------------------------

QString MainWindow::workspaceRootFromSelection(QString selected) const
{
    const QString path = QDir::cleanPath(selected.trimmed());
    if (path.isEmpty())
        return QString();
    const QFileInfo info(path);
    if (!info.exists() || !info.isDir())
        return QString();
    if (info.fileName() == QString::fromUtf8("___project___"))
        return QDir::cleanPath(info.absolutePath());
    if (QDir(AppSettings::projectStoreDir(path)).exists())
        return path;
    return QString();
}

//----------------------------------------------

void MainWindow::persistAndStopCurrentProject()
{
    foreach (ChatSession *session, _sessions)
    {
        if (session && session->agent())
            session->agent()->abort();
    }
    if (_workers)
    {
        _workers->stopAll();
        foreach (Worker *worker, _workers->workers())
        {
            if (worker)
                worker->quitBlockingLoops();
        }
    }
    foreach (ChatSession *session, _sessions)
    {
        if (session && session->widget())
            session->widget()->hidePermissionRequest();
    }
    if (_projectStoreEnabled)
        saveOpenChats(true);
    AppSettings::instance()->save();
}

//----------------------------------------------

void MainWindow::discardSessionUi()
{
    _resetting = true;
    if (_uiRefreshTimer)
        _uiRefreshTimer->stop();
    _uiRefreshTree = false;
    if (_graph)
        _graph->rebuild(0);
    foreach (ChatSession *session, _sessions)
    {
        if (!session)
            continue;
        if (_journal && session->agent())
            _journal->detachAgent(session->agent());
        if (session->agent())
        {
            session->agent()->abort();
            session->agent()->clearHistory();
        }
        if (session->widget())
            session->widget()->clearMessages();
    }
    if (_workers)
        _workers->clear();

    const QList<ChatSession *> sessions = _sessions;
    _sessions.clear();
    foreach (ChatSession *session, sessions)
    {
        if (!session)
            continue;
        ConversationWidget *widget = session->widget();
        session->setWidget(0);
        session->setWorker(0);
        if (_tabs && widget)
        {
            const int index = _tabs->indexOf(widget);
            if (index >= 0)
                _tabs->removeTab(index);
            delete widget;
        }
        delete session;
    }

    if (_tabs && _graph)
    {
        const int index = _tabs->indexOf(_graph);
        if (index >= 0)
            _tabs->removeTab(index);
    }

    _nextChatNumber = 0;
    _pendingActiveId.clear();
}

//----------------------------------------------

void MainWindow::switchToProject(QString workspaceRoot, bool loadExisting)
{
    beginStatusProgress(tr("Открытие проекта"), 0);
    pumpUi(true);

    _resetting = true;
    persistAndStopCurrentProject();
    discardSessionUi();
    _resetting = false;
    _loadingProject = true;

    const QString clean = QDir::cleanPath(workspaceRoot);
    if (_workspace)
        _workspace->setRoot(clean);
    applyProjectDirectory(loadExisting, false);
    if (!loadExisting)
        ensureProjectStore();
    rememberRecentProject(hasOpenProject() ? _workspace->root() : clean);
    sltSaveSettings();
    pumpUi(true);

    if (loadExisting)
        loadOpenChats();
    else
        sltNewChat();

    updateStatusProgress(0, 0, tr("Дерево файлов"));
    if (_projectDock)
        _projectDock->setProjectRoot(hasOpenProject() ? _workspace->root() : QString());
    pumpUi(true);

    _loadingProject = false;
    // Дерево уже построено. Дальше идёт восстановление воркеров (чтение их
    // чатов с диска, пересборка графа) — показываем это отдельной подписью,
    // иначе «Дерево файлов» висит на экране всё время восстановления.
    updateStatusProgress(0, 0, tr("Восстановление воркеров"));
    sltShowGraph();
    sltShowChat();
    if (rootSession() && rootSession()->agent())
        rootSession()->agent()->refreshContextUsage();
    if (_chatsShown)
        resumeWorkers();
    sltBusyChanged(false);
    updateComposerState();
    updateProjectActions();
    const QString native = QDir::toNativeSeparators(
        hasOpenProject() ? _workspace->root() : clean);
    endStatusProgress(tr("Проект: %1").arg(native));
}

//----------------------------------------------

void MainWindow::openProjectAt(QString workspace)
{
    const QString path = QDir::cleanPath(workspace.trimmed());
    if (path.isEmpty())
        return;

    if (!QDir(path).exists()
        || !QDir(AppSettings::projectStoreDir(path)).exists())
    {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Открыть проект"));
        box.setText(tr("Проект больше недоступен:\n%1\n"
                       "Папка удалена или в ней нет ___project___.")
                        .arg(QDir::toNativeSeparators(path)));
        box.setStandardButtons(QMessageBox::Ok);
        AppSettings *s = AppSettings::instance();
        box.setStyleSheet(QString::fromUtf8(
                              "QMessageBox { background:%1; color:%2; }"
                              "QMessageBox QLabel { color:%2; background:transparent; }"
                              "QPushButton { background:%3; color:%2; border:1px solid %4;"
                              "  border-radius:4px; padding:5px 12px; }")
                              .arg(s->panelColor().name(),
                                   s->textColor().name(),
                                   s->inputColor().name(),
                                   s->borderColor().name()));
        box.exec();
        forgetRecentProject(path);
        rebuildRecentProjectsMenu();
        updateProjectActions();
        sltSaveSettings();
        return;
    }

    if (hasOpenProject()
        && QDir::cleanPath(_workspace->root()).compare(path, Qt::CaseInsensitive) == 0)
        return;

    requestProjectSwitch(path, true, QString());
}

//----------------------------------------------

void MainWindow::sltOpenProject()
{
    const QString selected = QFileDialog::getExistingDirectory(
                                 this,
                                 tr("Открыть проект — папка ___project___"),
                                 projectDialogStartPath());
    if (selected.isEmpty())
        return;

    const QString workspace = workspaceRootFromSelection(selected);
    if (workspace.isEmpty())
    {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Открыть проект"));
        box.setText(tr("Выберите папку ___project___ или каталог, в котором она уже есть."));
        box.setStandardButtons(QMessageBox::Ok);
        AppSettings *s = AppSettings::instance();
        box.setStyleSheet(QString::fromUtf8(
                              "QMessageBox { background:%1; color:%2; }"
                              "QMessageBox QLabel { color:%2; background:transparent; }"
                              "QPushButton { background:%3; color:%2; border:1px solid %4;"
                              "  border-radius:4px; padding:5px 12px; }")
                              .arg(s->panelColor().name(),
                                   s->textColor().name(),
                                   s->inputColor().name(),
                                   s->borderColor().name()));
        box.exec();
        return;
    }

    openProjectAt(workspace);
}

//----------------------------------------------

void MainWindow::sltNewProject()
{
    QString selected = QFileDialog::getExistingDirectory(
                           this,
                           tr("Новый проект — рабочая папка"),
                           projectDialogStartPath());
    if (selected.isEmpty())
        return;

    QString workspace = QDir::cleanPath(selected);
    const QFileInfo info(workspace);
    if (info.fileName() == QString::fromUtf8("___project___"))
        workspace = QDir::cleanPath(info.absolutePath());

    const QString store = AppSettings::projectStoreDir(workspace);
    QString retire;
    if (QDir(store).exists())
    {
        const int answer = askStyledQuestion(
            tr("Новый проект"),
            tr("В этой папке уже есть проект (___project___).\n"
               "Удалить его и начать заново? Чаты и настройки проекта будут потеряны."),
            tr("Удалить"),
            tr("Отмена"));
        if (answer != QMessageBox::Yes)
            return;
        retire = store;
    }
    requestProjectSwitch(workspace, false, retire);
}

//----------------------------------------------

bool MainWindow::anyAgentInsideToolCall() const
{
    foreach (ChatSession *session, _sessions)
    {
        if (session && session->agent() && session->agent()->isInsideToolCall())
            return true;
    }
    return false;
}

//----------------------------------------------

void MainWindow::requestProjectSwitch(QString root, bool loadExisting, QString retireStore)
{
    _pendingSwitchRoot = root;
    _pendingSwitchLoad = loadExisting;
    _pendingRetireStore = retireStore;
    _pendingSwitchTries = 0;
    if (anyAgentInsideToolCall())
    {
        // Мы сами вызваны из вложенного цикла (команда, ожидание разрешения).
        // Останавливаем воркеров и даём стеку размотаться, потом переключаем.
        statusBar()->showMessage(tr("Остановка воркеров перед сменой проекта…"));
        if (_workers)
            _workers->stopAll();
        QTimer::singleShot(50, this, SLOT(sltResumeProjectSwitch()));
        return;
    }
    performProjectSwitch();
}

//----------------------------------------------

void MainWindow::sltResumeProjectSwitch()
{
    if (_closing || _pendingSwitchRoot.isEmpty())
        return;
    if (anyAgentInsideToolCall() && ++_pendingSwitchTries < 200)
    {
        QTimer::singleShot(50, this, SLOT(sltResumeProjectSwitch()));
        return;
    }
    performProjectSwitch();
}

//----------------------------------------------

void MainWindow::performProjectSwitch()
{
    const QString workspace = _pendingSwitchRoot;
    const bool loadExisting = _pendingSwitchLoad;
    const QString store = _pendingRetireStore;
    _pendingSwitchRoot.clear();
    _pendingRetireStore.clear();
    if (workspace.isEmpty())
        return;

    if (!store.isEmpty() && QDir(store).exists())
    {
        if (hasOpenProject()
            && QDir::cleanPath(_workspace->root()).compare(workspace, Qt::CaseInsensitive) == 0)
        {
            _resetting = true;
            persistAndStopCurrentProject();
            discardSessionUi();
            _resetting = false;
            _projectStoreEnabled = false;
        }
        FileChangeStore::instance()->forget();
        if (_projectDock)
            _projectDock->releaseFileModel();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (!retireProjectStore(store))
        {
            QMessageBox box(this);
            box.setIcon(QMessageBox::Warning);
            box.setWindowTitle(tr("Новый проект"));
            box.setText(tr("Не удалось убрать папку ___project___.\n"
                           "Закройте программы, которые держат файлы проекта, и повторите."));
            box.setStandardButtons(QMessageBox::Ok);
            AppSettings *s = AppSettings::instance();
            box.setStyleSheet(QString::fromUtf8(
                                  "QMessageBox { background:%1; color:%2; }"
                                  "QMessageBox QLabel { color:%2; background:transparent; }"
                                  "QPushButton { background:%3; color:%2; border:1px solid %4;"
                                  "  border-radius:4px; padding:5px 12px; }")
                                  .arg(s->panelColor().name(),
                                       s->textColor().name(),
                                       s->inputColor().name(),
                                       s->borderColor().name()));
            box.exec();
            return;
        }
    }

    switchToProject(workspace, loadExisting);
    if (loadExisting)
        statusBar()->showMessage(tr("Открыт проект: %1").arg(
                                     QDir::toNativeSeparators(workspace)));
    else
        statusBar()->showMessage(tr("Новый проект: %1").arg(
                                     QDir::toNativeSeparators(workspace)));
}

//----------------------------------------------

void MainWindow::sltOpenLastProject()
{
    pruneRecentProjects();
    rebuildRecentProjectsMenu();
    if (_recentProjects.isEmpty())
        return;
    openProjectAt(_recentProjects.first());
}

//----------------------------------------------

void MainWindow::sltOpenRecentProject()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;
    openProjectAt(action->data().toString());
}

//----------------------------------------------

void MainWindow::sltRebuildRecentMenu()
{
    pruneRecentProjects();
    rebuildRecentProjectsMenu();
    updateProjectActions();
}

//----------------------------------------------

bool MainWindow::retireProjectStore(QString storeDir)
{
    const QString path = QDir::cleanPath(storeDir);
    if (path.isEmpty() || !QDir(path).exists())
        return true;

    const QString parent = QFileInfo(path).absolutePath();
    for (int attempt = 0; attempt < 40; ++attempt)
    {
        const QString trash = parent
            + QString::fromUtf8("/___project___.trash-%1")
                  .arg(QDateTime::currentMSecsSinceEpoch() + attempt);
        if (QDir(trash).exists() || QFileInfo(trash).exists())
            continue;

        bool renamed = false;
#ifdef Q_OS_WIN
        const QString fromNative = QDir::toNativeSeparators(path);
        const QString toNative = QDir::toNativeSeparators(trash);
        renamed = MoveFileW(reinterpret_cast<LPCWSTR>(fromNative.utf16()),
                            reinterpret_cast<LPCWSTR>(toNative.utf16())) != 0;
#endif
        if (!renamed)
            renamed = QDir().rename(path, trash);
        if (renamed)
        {
            ProjectStoreTrashThread *worker = new ProjectStoreTrashThread(trash);
            worker->start();
            return true;
        }
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QThread::msleep(50);
    }
    return !QDir(path).exists();
}

//----------------------------------------------

void MainWindow::ensureProjectStore()
{
    if (!_workspace || !_workspace->isSet())
        return;
    const QString dir = AppSettings::projectStoreDir(_workspace->root());
    if (dir.isEmpty())
        return;
    if (!QDir(dir).exists() && !QDir().mkpath(dir))
        return;
    QDir().mkpath(dir + QString::fromUtf8("/tmp"));
    _projectStoreEnabled = true;
    AppSettings::instance()->save();
    saveOpenChats();
}

//----------------------------------------------

void MainWindow::sltSendRequested(const QString &text)
{
    if (!hasOpenProject())
        return;
    ChatSession *session = rootSession();
    if (!session)
        return;
    if (session->worker() && !session->worker()->isRoot())
        return;

    if (_settingsPage)
        _settingsPage->commitCurrentProfile();
    applyClientToAllSessions();
    sltSaveSettings();
    applyProjectDirectory();
    ensureProjectStore();
    applyClientToSession(session);
    ensureSessionVisible(session);
    if (session->worker() && session->worker()->isRoot())
    {
        session->worker()->clearWorkPlan();
        session->worker()->setTask(text);
        const QString prefix = session->worker()->composeStartPrefix();
        if (!prefix.isEmpty())
            session->agent()->injectContext(prefix);
    }
    session->agent()->sendUserMessage(text);
    updateComposerState();
}

//----------------------------------------------

void MainWindow::sltCompactRequested()
{
    ConversationWidget *widget = qobject_cast<ConversationWidget *>(sender());
    ChatSession *session = sessionByWidget(widget);
    if (!session)
        session = currentSession();
    if (!session)
        return;

    if (_settingsPage)
        _settingsPage->commitCurrentProfile();
    applyClientToSession(session);
    session->agent()->compactContext();
}

//----------------------------------------------

void MainWindow::sltBusyChanged(bool)
{
    const bool busy = anySessionBusy();
    updateProjectActions();
    if (busy)
        statusBar()->showMessage(tr("Запрос к модели..."));
    else if (statusBar()->currentMessage() == tr("Запрос к модели..."))
        statusBar()->showMessage(tr("Готово"));

    if (!busy && !_closing && !_resetting && _chatsShown)
        scheduleSaveOpenChats();
    updateComposerState();
    updateBusyTabs();
}

//----------------------------------------------

void MainWindow::sltUsageUpdated(int promptTokens, int completionTokens, int totalTokens,
                                 int cachedPromptTokens)
{
    // Разбивка по ролям: видно, где именно тратятся токены
    // (менеджеры vs исполнители) и работает ли кеш префикса у провайдера.
    QString role = QString::fromUtf8("other");
    ChatSession *session = sessionByAgent(sender());
    if (session && session->worker())
    {
        Worker *worker = session->worker();
        role = worker->isRoot() ? QString::fromUtf8("root")
                                : workerTypeToString(worker->type());
    }
    AppSettings::instance()->addUsage(promptTokens, completionTokens, totalTokens,
                                      cachedPromptTokens, role);
    QString message = tr("Токены ответа: prompt %1, completion %2, всего %3")
                          .arg(promptTokens)
                          .arg(completionTokens)
                          .arg(totalTokens);
    if (cachedPromptTokens > 0)
        message += tr(" (из кеша %1)").arg(cachedPromptTokens);
    statusBar()->showMessage(message);
}

//----------------------------------------------

void MainWindow::sltResetStatistics()
{
    if (!hasOpenProject())
        return;
    const int answer = askStyledQuestion(tr("Статистика"),
                                         tr("Обнулить счётчики токенов и запросов этого проекта?\n"
                                            "Общая статистика приложения не изменится."),
                                         tr("Обнулить"), tr("Отмена"));
    if (answer != QMessageBox::Yes)
        return;
    AppSettings::instance()->resetUsage();
}

//----------------------------------------------

void MainWindow::sltResetGlobalStatistics()
{
    const int answer = askStyledQuestion(tr("Статистика"),
                                         tr("Обнулить общую статистику по всем проектам?\n"
                                            "Счётчики открытого проекта не изменятся."),
                                         tr("Обнулить"), tr("Отмена"));
    if (answer != QMessageBox::Yes)
        return;
    AppSettings::instance()->resetGlobalUsage();
}

//----------------------------------------------

void MainWindow::sltUsageChanged()
{
    AppSettings *s = AppSettings::instance();
    if (hasOpenProject())
    {
        const UsageStats u = s->projectUsage();
        if (u.requestCount <= 0)
        {
            statusBar()->showMessage(tr("Статистика проекта обнулена"));
            return;
        }
        statusBar()->showMessage(tr("Проект: запросов %1, prompt %2, completion %3, всего %4")
                                     .arg(u.requestCount)
                                     .arg(u.promptTokens)
                                     .arg(u.completionTokens)
                                     .arg(u.totalTokens));
        return;
    }
    const UsageStats u = s->globalUsage();
    if (u.requestCount <= 0)
    {
        statusBar()->showMessage(tr("Общая статистика обнулена"));
        return;
    }
    statusBar()->showMessage(tr("Всего: запросов %1, prompt %2, completion %3, всего %4")
                                 .arg(u.requestCount)
                                 .arg(u.promptTokens)
                                 .arg(u.completionTokens)
                                 .arg(u.totalTokens));
}

//----------------------------------------------

void MainWindow::sltSaveSettings()
{
    QSettings settings(AppSettings::settingsFilePath(), QSettings::IniFormat);
    if (hasOpenProject())
        settings.setValue(QString::fromUtf8("projectDir"),
                          QDir::toNativeSeparators(_workspace->root()));
    else if (!_recentProjects.isEmpty())
        settings.setValue(QString::fromUtf8("projectDir"),
                          QDir::toNativeSeparators(_recentProjects.first()));
    settings.setValue(QString::fromUtf8("recentProjects"), _recentProjects);
    if (_inspectCheck)
        settings.setValue(QString::fromUtf8("inspectIo"), _inspectCheck->isChecked());
}

//----------------------------------------------

void MainWindow::sltShowChat()
{
    if (!hasOpenProject())
        return;
    ChatSession *root = 0;
    foreach (ChatSession *session, _sessions)
    {
        if (session && session->worker() && session->worker()->isRoot())
            root = session;
    }
    if (!root)
    {
        sltNewChat();
        return;
    }
    ensureSessionVisible(root);
}

//----------------------------------------------

void MainWindow::sltNewChat()
{
    if (!hasOpenProject())
        return;
    ChatSession *session = createSession(QString(), nextChatTitle());
    if (!session)
        return;
    _workers->createRoot(session);
    if (_chatsShown)
    {
        ensureSessionVisible(session);
        saveOpenChats();
    }
}

//----------------------------------------------

void MainWindow::sltTabCloseRequested(int index)
{
    if (!_tabs || index < 0 || index >= _tabs->count())
        return;
    QWidget *page = _tabs->widget(index);
    _tabs->removeTab(index);
    if (page == _graph || page == _settingsPage || page == _journal)
        return;
    if (page)
        page->hide();
}

//----------------------------------------------

void MainWindow::sltCurrentTabChanged(int index)
{
    if (_resetting || _loadingProject || !_tabs || index < 0)
        return;
    QWidget *page = _tabs->widget(index);
    if (page && page == _graph)
    {
        if (_graphDirty)
            rebuildGraphIfVisible();
        return;
    }
    ChatSession *session = sessionByTabWidget(page);
    if (session)
        restoreSessionContent(session);
}

//----------------------------------------------

void MainWindow::sltChatUserMessage(const QString &text)
{
    ChatSession *session = sessionByAgent(sender());
    if (!session || !session->autoTitle())
        return;
    if (session->worker() && !session->worker()->isRoot())
        return;

    QString title = Worker::shortLabelFromTask(text);
    if (title.isEmpty())
        return;
    if (title.size() > 48)
        title = title.left(45) + QString::fromUtf8("…");
    session->setTitle(title);
    session->setAutoTitle(false);
    updateSessionTabTitle(session);
}

//----------------------------------------------

void MainWindow::sltInspectToggled(bool)
{
    applyInspectToAllSessions();
    sltSaveSettings();
}

//----------------------------------------------

void MainWindow::sltInspectRequested(const QString &title, const QString &text)
{
    showInspectDialog(title, text);
}

//----------------------------------------------

void MainWindow::showInspectDialog(QString title, QString text)
{
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.resize(720, 520);

    QPlainTextEdit *edit = new QPlainTextEdit(&dialog);
    edit->setReadOnly(true);
    edit->setPlainText(text);
    edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Продолжить"));
    connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->addWidget(edit, 1);
    layout->addWidget(buttons);

    dialog.exec();
}

//----------------------------------------------

void MainWindow::createGraphWindow()
{
    if (_graph)
        return;
    _graph = new WorkerGraphWidget();
    connect(_graph, SIGNAL(workerActivated(QString)),
            this, SLOT(sltWorkerActivated(QString)));
    connect(_graph, SIGNAL(toolRemoveRequested(QString)),
            this, SLOT(sltToolRemoveRequested(QString)));
    connect(_graph, SIGNAL(workerInterruptRequested(QString)),
            this, SLOT(sltWorkerForceStopById(QString)));
    connect(_graph, SIGNAL(reportToParentRequested(QString)),
            this, SLOT(sltWorkerForceReportById(QString)));
    if (_tabBar)
        _tabBar->setGraphPage(_graph);
}

//----------------------------------------------

void MainWindow::createJournalWindow()
{
    if (_journal)
        return;
    _journal = new JournalWidget();
    if (_tabBar)
        _tabBar->setJournalPage(_journal);
}

//----------------------------------------------

void MainWindow::sltShowJournal()
{
    createJournalWindow();
    ensureTabVisible(_journal, tr("Журнал"), AppIcons::journal());
}

//----------------------------------------------

QString MainWindow::workerWindowTitle(Worker *worker) const
{
    if (!worker)
        return tr("Чат");
    QString base;
    switch (worker->type())
    {
    case WorkerTypeExecutor:
        base = tr("Исполнитель");
        break;
    case WorkerTypeTester:
        base = tr("Тестировщик");
        break;
    case WorkerTypeTool:
        if (!worker->toolName().isEmpty())
            return tr("Инструмент: %1").arg(worker->toolName());
        base = tr("Инструмент");
        break;
    case WorkerTypeManager:
    default:
        if (worker->isRoot())
            return worker->session() ? worker->session()->title() : tr("Главный чат");
        base = tr("Менеджер");
        break;
    }
    QString task = worker->displayLabel();
    if (task.isEmpty())
        return base;
    if (task.size() > 48)
        task = task.left(45) + QString::fromUtf8("…");
    return base + QString::fromUtf8(" — ") + task;
}

//----------------------------------------------

void MainWindow::applyWorkerUi(Worker *worker)
{
    if (!worker || !worker->session() || !worker->session()->widget())
        return;
    const bool root = worker->isRoot();
    worker->session()->widget()->setUserInputEnabled(root);
    worker->session()->widget()->setReportToParentVisible(!root);
    // Главный чат называем так, как модель сама озаглавила задание в plan_work:
    // это точнее первых 48 символов запроса пользователя.
    if (root && !worker->title().isEmpty()
        && worker->session()->title() != worker->title())
    {
        worker->session()->setTitle(worker->title());
        worker->session()->setAutoTitle(false);
    }
    // Паузы, о которых агент не знает: пользователь видит, чего именно ждёт чат.
    QString external;
    switch (worker->state())
    {
    case WorkerStateWaitingChildren:
        external = tr("Ожидание дочерних воркеров");
        break;
    case WorkerStateWaitingApproval:
        external = tr("Ожидание разрешения пользователя");
        break;
    case WorkerStateWaitingContinuation:
        external = tr("Ожидание решения пользователя");
        break;
    case WorkerStateQueued:
        external = tr("В очереди: заняты все слоты воркеров");
        break;
    default:
        break;
    }
    worker->session()->widget()->setExternalActivity(external);
    updateSessionTabTitle(worker->session());
    if (_journal && worker->session()->agent())
        _journal->setAgentLabel(worker->session()->agent(), workerWindowTitle(worker));
    if (root)
        updateComposerState();
}

//----------------------------------------------

void MainWindow::sltWorkerCreated(Worker *worker)
{
    if (!worker)
        return;

    if (!worker->session())
    {
        ChatSession *session = createSession(worker->id(), workerWindowTitle(worker));
        worker->setId(session->id());
        worker->setSession(session);
        session->setWorker(worker);
    }

    _workers->installTools(worker);
    applyClientToSession(worker->session());
    if (worker->session() && worker->session()->agent())
        worker->session()->agent()->setProjectDirectory(_workspace->root());
    if (_inspectCheck && worker->session() && worker->session()->agent())
        worker->session()->agent()->setInspectIo(_inspectCheck->isChecked());
    applyWorkerUi(worker);

    connect(worker, SIGNAL(stateChanged()),
            this, SLOT(sltWorkerTreeChanged()));
    connect(worker, SIGNAL(permissionRequested(QString)),
            this, SLOT(sltPermissionRequested(QString)));
    connect(worker, SIGNAL(continuationRequested(QString)),
            this, SLOT(sltContinuationRequested(QString)));
    connect(worker, SIGNAL(finished()),
            this, SLOT(sltWorkerFinished()));
    if (_journal && worker->session() && worker->session()->agent())
    {
        _journal->attachAgent(worker->session()->agent(), workerWindowTitle(worker));
        if (!_resetting && !_loadingProject && !worker->isRoot())
            _journal->addTransient(workerWindowTitle(worker), tr("Создан"),
                                   worker->task().isEmpty() ? tr("Новый воркер") : worker->task(),
                                   QString::fromUtf8("noticeMsg"));
    }
    sltWorkerTreeChanged();
}

//----------------------------------------------

void MainWindow::sltWorkerFinished()
{
    if (_resetting || _closing)
        return;
    Worker *worker = qobject_cast<Worker *>(sender());
    if (!worker || !worker->isRoot())
        return;
    focusWorkerSession(worker);
}

//----------------------------------------------

void MainWindow::sltWorkerTreeChanged()
{
    // Изменения состояний приходят пачками от десятков воркеров;
    // тяжёлую перерисовку делаем один раз на итерацию цикла событий.
    _uiRefreshTree = true;
    _graphDirty = true;
    scheduleUiRefresh();
    if (!_closing && !_resetting && !_loadingProject && _chatsShown)
        scheduleSaveOpenChats();
}

//----------------------------------------------

void MainWindow::scheduleUiRefresh()
{
    if (_closing || _resetting)
        return;
    if (!_uiRefreshTimer)
    {
        _uiRefreshTimer = new QTimer(this);
        _uiRefreshTimer->setSingleShot(true);
        _uiRefreshTimer->setInterval(40);
        connect(_uiRefreshTimer, SIGNAL(timeout()), this, SLOT(sltRefreshWorkerUi()));
    }
    if (!_uiRefreshTimer->isActive())
        _uiRefreshTimer->start();
}

//----------------------------------------------

void MainWindow::sltRefreshWorkerUi()
{
    if (_closing || _resetting)
        return;
    if (_uiRefreshTree)
    {
        _uiRefreshTree = false;
        rebuildGraphIfVisible();
        if (_workers)
        {
            foreach (Worker *worker, _workers->workers())
                applyWorkerUi(worker);
        }
        updateComposerState();
    }
    refreshBusyTabs();
}

//----------------------------------------------

void MainWindow::rebuildGraphIfVisible()
{
    if (!_graph)
        return;
    if (!_tabs || _tabs->currentWidget() != _graph)
        return;
    _graph->rebuild(_workers);
    _graphDirty = false;
}

//----------------------------------------------

void MainWindow::sltWorkerAboutToBeRemoved(Worker *worker)
{
    if (!worker || _resetting)
        return;

    ChatSession *session = worker->session();
    worker->setSession(0);
    if (!session)
        return;

    session->setWorker(0);
    if (_journal)
        _journal->detachAgent(session->agent());
    ConversationWidget *widget = session->widget();
    session->setWidget(0);
    _sessions.removeAll(session);
    if (widget)
    {
        widget->setAgent(0);
        if (_tabs)
        {
            const int index = _tabs->indexOf(widget);
            if (index >= 0)
                _tabs->removeTab(index);
        }
        widget->deleteLater();
    }
    session->deleteLater();
}

//----------------------------------------------

void MainWindow::sltWorkerActivated(const QString &id)
{
    Worker *worker = _workers ? _workers->workerById(id) : 0;
    if (!worker || !worker->session())
        return;
    ensureSessionVisible(worker->session());
}

//----------------------------------------------

void MainWindow::sltWorkerForceStop()
{
    ChatSession *session = currentSession();
    if (session && session->worker())
        sltWorkerForceStopById(session->worker()->id());
}

//----------------------------------------------

void MainWindow::sltWorkerForceReport()
{
    ChatSession *session = currentSession();
    if (session && session->worker())
        sltWorkerForceReportById(session->worker()->id());
}

//----------------------------------------------

void MainWindow::sltWorkerForceStopById(const QString &id)
{
    Worker *worker = _workers ? _workers->workerById(id) : 0;
    if (!worker)
        return;
    worker->forceStop();
    if (statusBar())
        statusBar()->showMessage(tr("Воркер остановлен: %1").arg(workerWindowTitle(worker)));
}

//----------------------------------------------

void MainWindow::sltWorkerForceReportById(const QString &id)
{
    Worker *worker = _workers ? _workers->workerById(id) : 0;
    if (!worker || worker->isRoot())
        return;
    worker->forceReportToParent();
    if (statusBar())
        statusBar()->showMessage(tr("Отчёт отправлен родителю: %1")
                                 .arg(workerWindowTitle(worker)));
}

//----------------------------------------------

void MainWindow::sltToolRemoveRequested(const QString &id)
{
    Worker *tool = _workers ? _workers->workerById(id) : 0;
    if (!tool || !tool->isTool())
        return;
    QString text = tr("Удалить инструмент «%1»?").arg(tool->toolName());
    if (_workers->toolBusy(tool) || _workers->toolQueueLength(tool) > 0)
        text += tr("\n\nИнструмент сейчас занят или к нему есть очередь: "
                   "ожидающие воркеры получат отказ.");
    const int answer = askStyledQuestion(tr("Удаление инструмента"), text,
                                         tr("Удалить"), tr("Отмена"));
    if (answer != QMessageBox::Yes)
        return;
    if (_journal)
        _journal->addTransient(workerWindowTitle(tool), tr("Удалён"),
                               tr("Инструмент удалён пользователем"),
                               QString::fromUtf8("noticeMsg"));
    _workers->destroyTool(id);
    if (!_closing && !_resetting)
        scheduleSaveOpenChats();
}


//----------------------------------------------

void MainWindow::sltPermissionRequested(const QString &action)
{
    Worker *worker = qobject_cast<Worker *>(sender());
    if (!worker || !worker->session())
        return;
    focusWorkerSession(worker);
    if (worker->session()->widget())
        worker->session()->widget()->showPermissionRequest(
            action, worker->permissionOfferAlways());
    if (_journal)
        _journal->addTransient(workerWindowTitle(worker), tr("Запрос подтверждения"),
                               action, QString::fromUtf8("noticeMsg"));
}

//----------------------------------------------

void MainWindow::sltPermissionAnswered(bool approved, bool always, const QString &comment)
{
    ConversationWidget *widget = qobject_cast<ConversationWidget *>(sender());
    ChatSession *session = sessionByWidget(widget);
    if (!session || !session->worker())
        return;
    session->worker()->resolvePermission(approved, always, comment);
    updatePermissionHighlights();
    QTimer::singleShot(0, this, SLOT(sltAdvanceConfirmation()));
}

//----------------------------------------------

void MainWindow::sltContinuationRequested(const QString &error)
{
    Worker *worker = qobject_cast<Worker *>(sender());
    if (!worker || !worker->session())
        return;
    focusWorkerSession(worker);
    if (worker->session()->widget())
        worker->session()->widget()->showContinuationRequest(error);
}

//----------------------------------------------

void MainWindow::sltContinuationAnswered(bool retry)
{
    ConversationWidget *widget = qobject_cast<ConversationWidget *>(sender());
    ChatSession *session = sessionByWidget(widget);
    if (!session || !session->worker())
        return;
    session->worker()->resolveContinuation(retry);
    updatePermissionHighlights();
    QTimer::singleShot(0, this, SLOT(sltAdvanceConfirmation()));
}

//----------------------------------------------

void MainWindow::sltShowGraph()
{
    createGraphWindow();
    ensureTabVisible(_graph, tr("Команда"), AppIcons::workerGraph());
    if (_graph)
    {
        _graph->rebuild(_workers);
        _graphDirty = false;
    }
}

//----------------------------------------------

void MainWindow::sltShowSettings()
{
    if (!_settingsPage)
        return;
    _settingsPage->loadControlFlags();
    _settingsPage->refreshPermissionsList();
    ensureTabVisible(_settingsPage, tr("Настройки"));
}

//----------------------------------------------

//----------------------------------------------

void MainWindow::sltShowBalance()
{
    BalanceDialog dialog(this);
    dialog.exec();
}

//----------------------------------------------

void MainWindow::sltShowStatistics()
{
    AppSettings *s = AppSettings::instance();
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Статистика"));
    dialog.resize(640, 720);

    QGroupBox *globalBox = new QGroupBox(tr("Всего"), &dialog);
    QVBoxLayout *globalLay = new QVBoxLayout(globalBox);
    UsageStatsWidget *globalView = new UsageStatsWidget(globalBox);
    globalView->setStats(s->globalUsage());
    globalLay->addWidget(globalView);

    QGroupBox *projectBox = new QGroupBox(tr("Текущий проект"), &dialog);
    QVBoxLayout *projectLay = new QVBoxLayout(projectBox);
    if (hasOpenProject())
    {
        UsageStatsWidget *projectView = new UsageStatsWidget(projectBox);
        projectView->setStats(s->projectUsage());
        const int workers = _workers ? _workers->workers().size() : 0;
        projectView->setExtraRows(
            QStringList()
                << tr("Открытых чатов")
                << tr("Воркеров")
                << tr("Программ в «всегда»"),
            QStringList()
                << QString::number(_sessions.size())
                << QString::number(workers)
                << QString::number(s->alwaysAllowedCommands().size()));
        projectLay->addWidget(projectView);
    }
    else
    {
        QLabel *empty = new QLabel(tr("Проект не открыт"), projectBox);
        empty->setWordWrap(true);
        projectLay->addWidget(empty);
    }

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    QPushButton *resetProject = buttons->addButton(tr("Обнулить проект"),
                                                   QDialogButtonBox::ResetRole);
    QPushButton *resetGlobal = buttons->addButton(tr("Обнулить общую"),
                                                  QDialogButtonBox::ResetRole);
    resetProject->setEnabled(hasOpenProject());
    connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
    connect(resetProject, SIGNAL(clicked()), this, SLOT(sltResetStatistics()));
    connect(resetProject, SIGNAL(clicked()), &dialog, SLOT(reject()));
    connect(resetGlobal, SIGNAL(clicked()), this, SLOT(sltResetGlobalStatistics()));
    connect(resetGlobal, SIGNAL(clicked()), &dialog, SLOT(reject()));

    QVBoxLayout *root = new QVBoxLayout(&dialog);
    root->addWidget(globalBox);
    root->addWidget(projectBox);
    root->addWidget(buttons);
    dialog.exec();
}

//----------------------------------------------

void MainWindow::sltAppearanceApplied()
{
    applyAppStyle();
    if (_graph)
        _graph->applyTheme();
    if (_journal)
        _journal->applyTheme();
    _graphDirty = true;
    rebuildGraphIfVisible();
}

//----------------------------------------------

void MainWindow::sltProfilesChanged()
{
    applyClientToAllSessions();
}

//----------------------------------------------

void MainWindow::sltPrevConfirmation()
{
    cycleConfirmation(-1);
}

//----------------------------------------------

void MainWindow::sltNextConfirmation()
{
    cycleConfirmation(1);
}

//----------------------------------------------

void MainWindow::sltComposerSend()
{
    if (!_composer || !_composerSend || !_composerSend->isEnabled())
        return;
    const QString text = _composer->toPlainText().trimmed();
    if (text.isEmpty())
        return;
    _composer->clear();
    sltSendRequested(text);
}

//----------------------------------------------

void MainWindow::sltComposerStop()
{
    stopAllWork();
}

//----------------------------------------------

void MainWindow::sltStopAll()
{
    stopAllWork();
}

//----------------------------------------------

void MainWindow::sltTabBlink()
{
    _tabBlinkOn = !_tabBlinkOn;
    if (_tabBar)
        _tabBar->setBlinkOn(_tabBlinkOn);
}

//----------------------------------------------

void MainWindow::setupUi()
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    _settingsPage = new SettingsPage(this);
    _inspectCheck = _settingsPage->inspectCheck();

    if (_inspectCheck)
        connect(_inspectCheck, SIGNAL(toggled(bool)),
                this, SLOT(sltInspectToggled(bool)));
    connect(_settingsPage, SIGNAL(appearanceApplied()),
            this, SLOT(sltAppearanceApplied()));
    connect(_settingsPage, SIGNAL(profilesChanged()),
            this, SLOT(sltProfilesChanged()));
    connect(AppSettings::instance(), SIGNAL(profilesChanged()),
            this, SLOT(sltProfilesChanged()));

    QToolBar *toolbar = addToolBar(tr("Панель"));
    toolbar->setObjectName(QString::fromUtf8("mainToolBar"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(24, 24));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    _clearAction = toolbar->addAction(AppIcons::clearAll(), tr("Очистить всё"));
    _clearAction->setToolTip(tr("Удалить все чаты и воркеров и начать с пустого сеанса."));
    connect(_clearAction, SIGNAL(triggered()), this, SLOT(sltClear()));

    _showChatAction = toolbar->addAction(AppIcons::mainChat(), tr("Главный чат"));
    _showChatAction->setToolTip(tr("Открыть главный чат"));
    connect(_showChatAction, SIGNAL(triggered()), this, SLOT(sltShowChat()));

    _showGraphAction = toolbar->addAction(AppIcons::workerGraph(), tr("Команда"));
    _showGraphAction->setToolTip(tr("Открыть схему команды"));
    connect(_showGraphAction, SIGNAL(triggered()), this, SLOT(sltShowGraph()));

    _showJournalAction = toolbar->addAction(AppIcons::journal(), tr("Журнал"));
    _showJournalAction->setToolTip(tr("Открыть журнал: сводная лента всех чатов за этот запуск"));
    connect(_showJournalAction, SIGNAL(triggered()), this, SLOT(sltShowJournal()));

    toolbar->addSeparator();

    _prevConfirmAction = toolbar->addAction(AppIcons::prevConfirm(),
                                            tr("Предыдущее подтверждение"));
    _prevConfirmAction->setToolTip(tr("Перейти к предыдущему запросу на подтверждение"));
    _prevConfirmAction->setEnabled(false);
    connect(_prevConfirmAction, SIGNAL(triggered()), this, SLOT(sltPrevConfirmation()));

    _nextConfirmAction = toolbar->addAction(AppIcons::nextConfirm(),
                                            tr("Следующее подтверждение"));
    _nextConfirmAction->setToolTip(tr("Перейти к следующему запросу на подтверждение"));
    _nextConfirmAction->setEnabled(false);
    connect(_nextConfirmAction, SIGNAL(triggered()), this, SLOT(sltNextConfirmation()));

    _tabs = new SessionTabWidget(central);
    _tabs->setDocumentMode(true);
    _tabs->setTabsClosable(true);
    _tabs->setMovable(true);
    _tabs->setElideMode(Qt::ElideRight);
    _tabs->setUsesScrollButtons(true);
    _tabBar = static_cast<SessionTabWidget *>(_tabs)->sessionBar();
    connect(_tabs, SIGNAL(tabCloseRequested(int)),
            this, SLOT(sltTabCloseRequested(int)));
    connect(_tabs, SIGNAL(currentChanged(int)),
            this, SLOT(sltCurrentTabChanged(int)));
    connect(_tabBar, SIGNAL(tabMoved(int,int)),
            this, SLOT(updateBusyTabs()));

    _tabBlinkTimer = new QTimer(this);
    _tabBlinkTimer->setInterval(480);
    connect(_tabBlinkTimer, SIGNAL(timeout()), this, SLOT(sltTabBlink()));

    _composer = new QPlainTextEdit(central);
    _composer->setPlaceholderText(tr("Сообщение в главный чат…  Enter — отправить, Shift+Enter — новая строка"));
    _composer->setMaximumHeight(110);
    _composer->installEventFilter(this);
    _composerSend = new QPushButton(tr("Отправить"), central);
    _composerStop = new QPushButton(tr("Стоп"), central);
    _composerStop->setEnabled(false);
    _composerStop->setToolTip(tr("Остановить работу во всех чатах"));
    connect(_composerSend, SIGNAL(clicked()), this, SLOT(sltComposerSend()));
    connect(_composerStop, SIGNAL(clicked()), this, SLOT(sltComposerStop()));

    QVBoxLayout *composerButtons = new QVBoxLayout();
    composerButtons->addWidget(_composerSend);
    composerButtons->addWidget(_composerStop);
    composerButtons->addStretch();

    QHBoxLayout *composerLayout = new QHBoxLayout();
    composerLayout->addWidget(_composer, 1);
    composerLayout->addLayout(composerButtons);

    QVBoxLayout *root = new QVBoxLayout(central);
    root->setContentsMargins(6, 6, 6, 6);
    root->addWidget(_tabs, 1);
    root->addLayout(composerLayout);

    _projectDock = new ProjectTreeDock(this);
    addDockWidget(Qt::LeftDockWidgetArea, _projectDock);

    createGraphWindow();
    createJournalWindow();
    applyAppStyle();
    updateComposerState();

    _progressBar = new QProgressBar(this);
    _progressBar->setMaximumWidth(220);
    _progressBar->setMaximumHeight(14);
    _progressBar->setTextVisible(true);
    _progressBar->setRange(0, 100);
    _progressBar->setValue(0);
    _progressBar->hide();
    statusBar()->addPermanentWidget(_progressBar);
}

//----------------------------------------------

void MainWindow::setupMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("Файл"));

    _openProjectAction = fileMenu->addAction(tr("Открыть…"));
    _openProjectAction->setShortcut(QKeySequence::Open);
    connect(_openProjectAction, SIGNAL(triggered()), this, SLOT(sltOpenProject()));

    _newProjectAction = fileMenu->addAction(tr("Новый проект…"));
    connect(_newProjectAction, SIGNAL(triggered()), this, SLOT(sltNewProject()));

    _openLastAction = fileMenu->addAction(tr("Открыть последний проект"));
    connect(_openLastAction, SIGNAL(triggered()), this, SLOT(sltOpenLastProject()));

    _recentMenu = fileMenu->addMenu(tr("Недавние проекты"));
    connect(_recentMenu, SIGNAL(aboutToShow()), this, SLOT(sltRebuildRecentMenu()));
    rebuildRecentProjectsMenu();

    fileMenu->addSeparator();

    _fileClearAction = fileMenu->addAction(tr("Очистить всё"));
    connect(_fileClearAction, SIGNAL(triggered()), this, SLOT(sltClear()));

    fileMenu->addSeparator();

    QAction *balanceAction = fileMenu->addAction(tr("Баланс…"));
    connect(balanceAction, SIGNAL(triggered()), this, SLOT(sltShowBalance()));

    fileMenu->addSeparator();

    QAction *quitAction = fileMenu->addAction(tr("Выход"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, SIGNAL(triggered()), this, SLOT(close()));

    QMenu *windowMenu = menuBar()->addMenu(tr("Окно"));

    QAction *chatAction = windowMenu->addAction(tr("Главный чат"));
    connect(chatAction, SIGNAL(triggered()), this, SLOT(sltShowChat()));

    QAction *graphAction = windowMenu->addAction(tr("Команда"));
    connect(graphAction, SIGNAL(triggered()), this, SLOT(sltShowGraph()));

    QAction *journalAction = windowMenu->addAction(tr("Журнал"));
    connect(journalAction, SIGNAL(triggered()), this, SLOT(sltShowJournal()));

    QAction *settingsAction = windowMenu->addAction(tr("Настройки"));
    connect(settingsAction, SIGNAL(triggered()), this, SLOT(sltShowSettings()));

    QMenu *projectMenu = menuBar()->addMenu(tr("Проект"));

    QAction *projectSettingsAction = projectMenu->addAction(tr("Настройки проекта…"));
    connect(projectSettingsAction, SIGNAL(triggered()), this, SLOT(sltShowSettings()));

    QMenu *helpMenu = menuBar()->addMenu(tr("Справка"));
    QAction *statsAction = helpMenu->addAction(tr("Статистика"));
    connect(statsAction, SIGNAL(triggered()), this, SLOT(sltShowStatistics()));
}

//----------------------------------------------

ChatSession *MainWindow::createSession(QString id, QString title)
{
    ChatSession *session = new ChatSession(this);
    if (!id.trimmed().isEmpty())
        session->setId(id);
    session->setTitle(title.isEmpty() ? nextChatTitle() : title);
    rememberChatNumber(session->title());
    if (!_loadingProject)
        attachSessionUi(session);
    applyClientToSession(session);
    session->agent()->setProjectDirectory(_workspace->root());
    if (_inspectCheck)
        session->agent()->setInspectIo(_inspectCheck->isChecked());
    _sessions.append(session);
    return session;
}

//----------------------------------------------

void MainWindow::attachSessionUi(ChatSession *session)
{
    if (!session)
        return;
    if (session->widget())
        return;

    ConversationWidget *chat = new ConversationWidget();
    chat->applyTheme();
    chat->setAgent(session->agent());
    connect(chat, SIGNAL(sendRequested(QString)),
            this, SLOT(sltSendRequested(QString)));
    connect(chat, SIGNAL(stopRequested()),
            this, SLOT(sltStopAll()));
    connect(chat, SIGNAL(workerInterruptRequested()),
            this, SLOT(sltWorkerForceStop()));
    connect(chat, SIGNAL(reportToParentRequested()),
            this, SLOT(sltWorkerForceReport()));
    connect(chat, SIGNAL(compactRequested()),
            this, SLOT(sltCompactRequested()));
    connect(session->agent(), SIGNAL(busyChanged(bool)),
            this, SLOT(sltBusyChanged(bool)), Qt::UniqueConnection);
    connect(session->agent(), SIGNAL(usageUpdated(int,int,int,int)),
            this, SLOT(sltUsageUpdated(int,int,int,int)), Qt::UniqueConnection);
    connect(session->agent(), SIGNAL(userMessage(QString)),
            this, SLOT(sltChatUserMessage(QString)), Qt::UniqueConnection);
    connect(session->agent(), SIGNAL(inspectRequested(QString,QString)),
            this, SLOT(sltInspectRequested(QString,QString)), Qt::UniqueConnection);
    connect(chat, SIGNAL(permissionAnswered(bool,bool,QString)),
            this, SLOT(sltPermissionAnswered(bool,bool,QString)));
    connect(chat, SIGNAL(continuationAnswered(bool)),
            this, SLOT(sltContinuationAnswered(bool)));

    session->setWidget(chat);
}

//----------------------------------------------

void MainWindow::applyClientToSession(ChatSession *session)
{
    if (!session || !session->client() || !session->agent())
        return;

    WorkerType type = WorkerTypeManager;
    if (session->worker())
        type = session->worker()->type();
    const LlmProfile profile = AppSettings::instance()->profileForWorkerType(type);

    OpenAiCompatibleClient *client = qobject_cast<OpenAiCompatibleClient *>(session->client());
    if (client)
        client->setIdentity(profile.id, profile.name);
    session->client()->setBaseUrl(profile.baseUrl);
    session->client()->setModel(profile.model);
    session->client()->setApiKey(profile.apiKey);
    session->agent()->applyRoleSettings(type);

    if (session->agent()->isBusy())
    {
        session->agent()->refreshContextUsage();
        return;
    }

    session->agent()->setClient(session->client());
    session->agent()->refreshContextUsage();
}

//----------------------------------------------

void MainWindow::applyClientToAllSessions()
{
    foreach (ChatSession *session, _sessions)
        applyClientToSession(session);
}

//----------------------------------------------

void MainWindow::applyProjectToAllSessions()
{
    const QString root = _workspace->root();
    foreach (ChatSession *session, _sessions)
        session->agent()->setProjectDirectory(root);
}

//----------------------------------------------

ChatSession *MainWindow::currentSession() const
{
    if (_tabs)
    {
        ChatSession *session = sessionByTabWidget(_tabs->currentWidget());
        if (session)
            return session;
    }
    for (int i = _sessions.size() - 1; i >= 0; --i)
    {
        ChatSession *session = _sessions.at(i);
        if (session->worker() && session->worker()->isRoot())
            return session;
    }
    if (_sessions.isEmpty())
        return 0;
    return _sessions.last();
}

//----------------------------------------------

ChatSession *MainWindow::rootSession() const
{
    foreach (ChatSession *session, _sessions)
    {
        if (session && session->worker() && session->worker()->isRoot())
            return session;
    }
    return 0;
}

//----------------------------------------------

ChatSession *MainWindow::sessionByWidget(ConversationWidget *widget) const
{
    if (!widget)
        return 0;
    foreach (ChatSession *session, _sessions)
    {
        if (session->widget() == widget)
            return session;
    }
    return 0;
}

//----------------------------------------------

ChatSession *MainWindow::sessionByAgent(QObject *agent) const
{
    if (!agent)
        return 0;
    foreach (ChatSession *session, _sessions)
    {
        if (session->agent() == agent)
            return session;
    }
    return 0;
}

//----------------------------------------------

ChatSession *MainWindow::sessionByTabWidget(QWidget *widget) const
{
    if (!widget)
        return 0;
    foreach (ChatSession *session, _sessions)
    {
        if (session->widget() == widget)
            return session;
    }
    return 0;
}

//----------------------------------------------

void MainWindow::loadOpenChats()
{
    if (!hasOpenProject())
        return;
    QList<ChatSnapshot> shots;
    QString activeId;
    if (!ChatStore::load(&shots, &activeId) || shots.isEmpty())
    {
        sltNewChat();
        return;
    }

    const bool wasLoading = _loadingProject;
    _loadingProject = true;
    _pendingActiveId = activeId;
    const int total = shots.size();
    beginStatusProgress(tr("Загрузка чатов"), total);
    int i = 0;
    foreach (const ChatSnapshot &shot, shots)
    {
        ++i;
        ChatSession *session = createSession(shot.id, shot.title);
        const WorkerType type = workerTypeFromString(shot.workerType);
        Worker *worker = _workers->createWorker(type, shot.id, session, 0, shot.task);
        worker->setTitle(shot.workerTitle);
        if (type == WorkerTypeTool)
        {
            worker->setToolName(shot.toolName);
            worker->setToolPurpose(shot.toolPurpose);
            _workers->refreshPrompt(worker);
        }
        worker->applyPersistedState(workerStateFromString(shot.workerState),
                                    shot.result,
                                    shot.lastError,
                                    shot.lastAssistant,
                                    shot.permissionAction);
        worker->setAlwaysAllowedCommands(shot.alwaysCommands);
        worker->setChangedFiles(shot.changedFiles);
        applyWorkerUi(worker);
        session->applySnapshot(shot);
        session->agent()->setProjectDirectory(_workspace->root());
        rememberChatNumber(session->title());
        updateStatusProgress(i, total, session->title());
    }
    foreach (const ChatSnapshot &shot, shots)
        _workers->linkParent(_workers->workerById(shot.id), shot.parentId);
    endStatusProgress();
    if (!wasLoading)
        _loadingProject = false;
}

//----------------------------------------------

void MainWindow::resumeWorkers()
{
    if (!_workers)
        return;
    foreach (Worker *worker, _workers->workers())
    {
        if (worker)
            worker->resumeAfterRestore();
    }
}

//----------------------------------------------

void MainWindow::saveOpenChats(bool wait)
{
    if (!_projectStoreEnabled)
        return;
    if (wait)
        waitForChatSave();
    else if (_chatSaveThread && _chatSaveThread->isRunning())
    {
        _chatSavePending = true;
        return;
    }

    ChatSaveJob job;
    ChatSession *active = currentSession();
    if (active)
        job.activeId = active->id();
    foreach (ChatSession *session, _sessions)
    {
        if (!session)
            continue;
        job.index.append(session->snapshot(_closing));
        if (session->contentLoaded() && (session->contentDirty() || _closing || wait))
        {
            job.dirty.append(session->snapshot(_closing));
            session->clearContentDirty();
        }
    }

    if (wait || _closing)
    {
        if (statusBar())
            statusBar()->showMessage(tr("Сохранение…"));
        writeChatSaveJob(job);
        return;
    }

    _chatSavePending = false;
    _chatSaveThread = new ChatSaveThread(job, this);
    connect(_chatSaveThread, SIGNAL(finished()), this, SLOT(sltChatSaveFinished()));
    if (_progressDepth <= 0)
    {
        if (_progressBar)
        {
            _progressBar->setRange(0, 0);
            _progressBar->setFormat(QString::fromUtf8("%p%"));
            _progressBar->show();
        }
        if (statusBar())
            statusBar()->showMessage(tr("Сохранение…"));
    }
    _chatSaveThread->start();
}

//----------------------------------------------

void MainWindow::waitForChatSave()
{
    _chatSavePending = false;
    if (!_chatSaveThread)
        return;
    disconnect(_chatSaveThread, SIGNAL(finished()), this, SLOT(sltChatSaveFinished()));
    if (_chatSaveThread->isRunning())
        _chatSaveThread->wait();
    delete _chatSaveThread;
    _chatSaveThread = 0;
    if (_progressBar && _progressDepth <= 0)
        _progressBar->hide();
}

//----------------------------------------------

void MainWindow::sltChatSaveFinished()
{
    QThread *thread = qobject_cast<QThread *>(sender());
    if (thread == _chatSaveThread)
        _chatSaveThread = 0;
    if (thread)
        thread->deleteLater();
    if (_progressBar && _progressDepth <= 0)
        _progressBar->hide();
    if (_closing || _resetting)
        return;
    if (_chatSavePending)
    {
        _chatSavePending = false;
        saveOpenChats(false);
        return;
    }
    if (statusBar() && _progressDepth <= 0 && hasOpenProject() && _workspace)
    {
        statusBar()->showMessage(tr("Проект: %1").arg(
                                     QDir::toNativeSeparators(_workspace->root())));
    }
}

//----------------------------------------------

void MainWindow::scheduleSaveOpenChats()
{
    if (_closing || _resetting || _loadingProject || !_projectStoreEnabled)
        return;
    if (!_saveChatsTimer)
    {
        _saveChatsTimer = new QTimer(this);
        _saveChatsTimer->setSingleShot(true);
        connect(_saveChatsTimer, SIGNAL(timeout()), this, SLOT(sltFlushSaveChats()));
    }
    // Не перезапускаем активный таймер: иначе при постоянном потоке событий
    // запись откладывалась бы бесконечно либо шла слишком часто.
    if (!_saveChatsTimer->isActive())
        _saveChatsTimer->start(2500);
}

//----------------------------------------------

void MainWindow::sltFlushSaveChats()
{
    saveOpenChats();
}

//----------------------------------------------

void MainWindow::pumpUi(bool force)
{
    if (_resetting || _closing)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && now - _lastPumpMs < 32)
        return;
    _lastPumpMs = now;
    if (_progressBar && _progressBar->isVisible())
        _progressBar->repaint();
    if (statusBar())
        statusBar()->repaint();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

//----------------------------------------------

void MainWindow::beginStatusProgress(QString title, int total)
{
    ++_progressDepth;
    _progressTitle = title;
    if (_progressBar)
    {
        if (total > 0)
        {
            _progressBar->setRange(0, total);
            _progressBar->setValue(0);
            _progressBar->setFormat(QString::fromUtf8("%p%"));
        }
        else
        {
            _progressBar->setRange(0, 0);
            _progressBar->setFormat(QString::fromUtf8("%p%"));
        }
        _progressBar->show();
    }
    if (statusBar())
        statusBar()->showMessage(title);
    pumpUi(true);
}

//----------------------------------------------

void MainWindow::updateStatusProgress(int current, int total, QString detail)
{
    if (_progressBar)
    {
        if (total > 0)
        {
            _progressBar->setRange(0, total);
            _progressBar->setValue(qBound(0, current, total));
        }
        else
            _progressBar->setRange(0, 0);
        _progressBar->show();
    }
    QString msg = _progressTitle.isEmpty() ? tr("Загрузка") : _progressTitle;
    if (total > 0)
        msg += QString::fromUtf8(" %1/%2").arg(current).arg(total);
    if (!detail.isEmpty())
        msg += QString::fromUtf8(" — ") + detail;
    if (statusBar())
        statusBar()->showMessage(msg);
    pumpUi(false);
}

//----------------------------------------------

void MainWindow::endStatusProgress(QString readyMessage)
{
    if (_progressDepth > 0)
        --_progressDepth;
    if (_progressDepth > 0)
        return;
    _progressTitle.clear();
    if (_progressBar)
    {
        _progressBar->setRange(0, 100);
        _progressBar->setValue(0);
        _progressBar->hide();
    }
    if (statusBar() && !readyMessage.isEmpty())
        statusBar()->showMessage(readyMessage);
    pumpUi(true);
}

//----------------------------------------------

void MainWindow::sltFileChangeLoadProgress(int current, int total, const QString &path)
{
    if (_progressDepth > 0)
    {
        updateStatusProgress(current, total, QFileInfo(path).fileName());
        return;
    }
    if (_progressBar)
    {
        if (total > 0)
        {
            _progressBar->setRange(0, total);
            _progressBar->setValue(qBound(0, current, total));
        }
        else
            _progressBar->setRange(0, 0);
        _progressBar->show();
    }
    QString msg = tr("Подсчёт изменений в файлах");
    if (total > 0)
        msg += QString::fromUtf8(" %1/%2").arg(current).arg(total);
    if (!path.isEmpty())
        msg += QString::fromUtf8(" — ") + QFileInfo(path).fileName();
    if (statusBar())
        statusBar()->showMessage(msg);
    pumpUi();
    if (_progressBar && total > 0 && current >= total)
        _progressBar->hide();
}

//----------------------------------------------

void MainWindow::sltAdvanceConfirmation()
{
    updatePermissionHighlights();
    ConversationWidget *page = qobject_cast<ConversationWidget *>(
        _tabs ? _tabs->currentWidget() : 0);
    if (page && page->hasActivePrompt())
        return;
    if (!pendingConfirmationWorkers().isEmpty())
        cycleConfirmation(1);
}

//----------------------------------------------

void MainWindow::updateWindowTitle()
{
    if (_workspace && _workspace->isSet())
        setWindowTitle(QString::fromUtf8("dsagent — %1")
                           .arg(QDir::toNativeSeparators(_workspace->root())));
    else
        setWindowTitle(QString::fromUtf8("dsagent"));
}

//----------------------------------------------

void MainWindow::applySessionGeometries()
{
    sltShowGraph();
    pumpUi();
    ChatSession *active = 0;
    int i = 0;
    const int total = _sessions.size();
    foreach (ChatSession *session, _sessions)
    {
        ++i;
        const bool root = session->worker() && session->worker()->isRoot();
        if (!root)
            continue;
        ensureSessionVisible(session);
        updateStatusProgress(i, qMax(1, total), session->title());
        if (session->id() == _pendingActiveId)
            active = session;
        if (!active)
            active = session;
    }
    if (active)
        ensureSessionVisible(active);
}

//----------------------------------------------

void MainWindow::rememberChatNumber(QString title)
{
    QRegExp re(QString::fromUtf8("^Чат\\s+(\\d+)$"));
    if (re.exactMatch(title))
        _nextChatNumber = qMax(_nextChatNumber, re.cap(1).toInt());
}

//----------------------------------------------

QString MainWindow::nextChatTitle()
{
    ++_nextChatNumber;
    return tr("Чат %1").arg(_nextChatNumber);
}

//----------------------------------------------

void MainWindow::loadSettings()
{
    QSettings settings(AppSettings::settingsFilePath(), QSettings::IniFormat);
    _recentProjects = settings.value(QString::fromUtf8("recentProjects")).toStringList();
    if (_recentProjects.isEmpty())
    {
        const QString legacy = settings.value(QString::fromUtf8("projectDir")).toString().trimmed();
        if (!legacy.isEmpty())
            _recentProjects.append(QDir::cleanPath(legacy));
    }
    pruneRecentProjects();
    rebuildRecentProjectsMenu();

    if (_inspectCheck)
    {
        const QSignalBlocker blocker(_inspectCheck);
        _inspectCheck->setChecked(settings.value(QString::fromUtf8("inspectIo"), false).toBool());
    }
    applyInspectToAllSessions();
    updateProjectActions();
}

//----------------------------------------------

void MainWindow::applyProjectDirectory(bool loadFileChanges, bool bindTree)
{
    const QString root = (_workspace && _workspace->isSet()) ? _workspace->root() : QString();
    ChatStore::setProjectRoot(root);
    AppSettings::instance()->setProjectRoot(root);
    FileChangeStore::instance()->setRoot(root, loadFileChanges);
    if (root.isEmpty())
        _projectStoreEnabled = false;
    else
        _projectStoreEnabled = QDir(AppSettings::projectStoreDir(root)).exists();
    applyProjectToAllSessions();
    if (bindTree && _projectDock)
        _projectDock->setProjectRoot(root);
    updateWindowTitle();
    if (_settingsPage)
    {
        _settingsPage->setProjectPath(root);
        _settingsPage->refreshPermissionsList();
    }
    if (root.isEmpty())
    {
        if (_progressDepth <= 0)
            statusBar()->showMessage(tr("Откройте или создайте проект"));
        updateProjectActions();
        return;
    }
    if (_progressDepth <= 0)
        statusBar()->showMessage(tr("Проект: %1").arg(QDir::toNativeSeparators(root)));
    updateProjectActions();
}

//----------------------------------------------

bool MainWindow::hasOpenProject() const
{
    return _workspace && _workspace->isSet();
}

//----------------------------------------------

QString MainWindow::projectDialogStartPath() const
{
    if (hasOpenProject())
        return _workspace->root();
    if (!_recentProjects.isEmpty())
        return _recentProjects.first();
    return QDir::homePath();
}

//----------------------------------------------

void MainWindow::rememberRecentProject(QString root)
{
    const QString path = QDir::cleanPath(root.trimmed());
    if (path.isEmpty() || !QDir(path).exists())
        return;
    for (int i = _recentProjects.size() - 1; i >= 0; --i)
    {
        if (_recentProjects.at(i).compare(path, Qt::CaseInsensitive) == 0)
            _recentProjects.removeAt(i);
    }
    _recentProjects.prepend(path);
    while (_recentProjects.size() > 16)
        _recentProjects.removeLast();
    rebuildRecentProjectsMenu();
    updateProjectActions();
}

//----------------------------------------------

void MainWindow::forgetRecentProject(QString root)
{
    const QString path = QDir::cleanPath(root.trimmed());
    if (path.isEmpty())
        return;
    for (int i = _recentProjects.size() - 1; i >= 0; --i)
    {
        if (_recentProjects.at(i).compare(path, Qt::CaseInsensitive) == 0)
            _recentProjects.removeAt(i);
    }
}

//----------------------------------------------

void MainWindow::pruneRecentProjects()
{
    QStringList kept;
    foreach (const QString &item, _recentProjects)
    {
        const QString path = QDir::cleanPath(item.trimmed());
        if (path.isEmpty())
            continue;
        bool duplicate = false;
        foreach (const QString &have, kept)
        {
            if (have.compare(path, Qt::CaseInsensitive) == 0)
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            kept.append(path);
    }
    while (kept.size() > 16)
        kept.removeLast();
    _recentProjects = kept;
}

//----------------------------------------------

void MainWindow::rebuildRecentProjectsMenu()
{
    if (!_recentMenu)
        return;
    _recentMenu->clear();
    if (_recentProjects.isEmpty())
    {
        QAction *empty = _recentMenu->addAction(tr("Нет недавних проектов"));
        empty->setEnabled(false);
        return;
    }
    for (int i = 0; i < _recentProjects.size(); ++i)
    {
        const QString path = _recentProjects.at(i);
        const QString label = QDir::toNativeSeparators(path);
        QAction *action = _recentMenu->addAction(label);
        action->setData(path);
        action->setToolTip(label);
        connect(action, SIGNAL(triggered()), this, SLOT(sltOpenRecentProject()));
    }
}

//----------------------------------------------

void MainWindow::updateProjectActions()
{
    const bool hasProject = hasOpenProject();
    const bool busy = anySessionBusy();
    const bool allowSwitch = !busy && !_resetting && !_closing;
    if (_openProjectAction)
        _openProjectAction->setEnabled(allowSwitch);
    if (_newProjectAction)
        _newProjectAction->setEnabled(allowSwitch);
    if (_openLastAction)
        _openLastAction->setEnabled(allowSwitch && !_recentProjects.isEmpty());
    if (_recentMenu)
        _recentMenu->setEnabled(allowSwitch && !_recentProjects.isEmpty());
    if (_clearAction)
        _clearAction->setEnabled(hasProject && !_resetting);
    if (_fileClearAction)
        _fileClearAction->setEnabled(hasProject && !_resetting);
    if (_showChatAction)
        _showChatAction->setEnabled(hasProject);
}

//----------------------------------------------

void MainWindow::ensureTabVisible(QWidget *widget, QString title, QIcon icon)
{
    if (!_tabs || !widget)
        return;
    int index = _tabs->indexOf(widget);
    if (index < 0)
        index = _tabs->addTab(widget, icon, title);
    else
    {
        if (!title.isEmpty())
            _tabs->setTabText(index, title);
        if (!icon.isNull())
            _tabs->setTabIcon(index, icon);
    }
    widget->show();
    _tabs->setCurrentIndex(index);
}

//----------------------------------------------

void MainWindow::ensureSessionVisible(ChatSession *session)
{
    if (!session)
        return;
    if (!session->widget())
        attachSessionUi(session);
    restoreSessionContent(session);
    const QString title = session->worker()
                          ? workerWindowTitle(session->worker())
                          : session->title();
    ensureTabVisible(session->widget(), title, iconForSession(session));
    if (session->worker())
        applyWorkerUi(session->worker());
}

//----------------------------------------------

void MainWindow::updateSessionTabTitle(ChatSession *session)
{
    if (!_tabs || !session || !session->widget())
        return;
    const int index = _tabs->indexOf(session->widget());
    if (index < 0)
        return;
    const QString title = session->worker()
                          ? workerWindowTitle(session->worker())
                          : session->title();
    if (_tabs->tabText(index) != title)
        _tabs->setTabText(index, title);
    const QIcon icon = iconForSession(session);
    if (!icon.isNull())
        _tabs->setTabIcon(index, icon);
}

//----------------------------------------------

QIcon MainWindow::iconForSession(ChatSession *session) const
{
    if (!session)
        return AppIcons::mainChat();
    Worker *worker = session->worker();
    if (!worker)
        return AppIcons::mainChat();
    return AppIcons::forWorkerType(worker->type());
}

//----------------------------------------------

void MainWindow::applyAppStyle()
{
    AppSettings *s = AppSettings::instance();
    QFont appFont = QApplication::font();
    appFont.setPointSize(s->fontPointSize());
    QApplication::setFont(appFont);
    setFont(appFont);

    const QString win = s->windowColor().name();
    const QString text = s->textColor().name();
    const QString panel = s->panelColor().name();
    const QString accent = s->accentColor().name();
    const QString input = s->inputColor().name();
    const QString bar = s->toolbarColor().name();
    const QString border = s->borderColor().name();
    const QString muted = s->mutedTextColor().name();
    const QString select = s->selectionColor().name();
    const QString scroll = s->scrollbarColor().name();
    const QString tabSel = s->isLightTheme() ? panel : QColor(bar).lighter(118).name();

    setStyleSheet(QString::fromUtf8(
        "QMainWindow { background:%1; color:%2; }"
        "QMenuBar { background:%6; color:%2; border-bottom:1px solid %8; }"
        "QMenuBar::item:selected { background:%3; }"
        "QMenu { background:%3; color:%2; border:1px solid %8; }"
        "QMenu::item:selected { background:%9; }"
        "QStatusBar { background:%6; color:%2; border-top:1px solid %8; }"
        "QToolBar { background:%6; border:1px solid %8; spacing:4px; padding:2px 6px; }"
        "QToolButton { background:transparent; color:%2; border:none; padding:4px; }"
        "QToolButton:hover { background:%3; border-radius:4px; }"
        "QToolButton:disabled { color:%MUTED%; }"
        "QDockWidget { titlebar-close-icon: url(none); color:%2; }"
        "QDockWidget::title { background:%6; padding:5px; border:1px solid %8; }"
        "QTreeView { background:%3; color:%2; alternate-background-color:%6;"
        "  border:none; }"
        "QTreeView::branch { background:transparent; image:none; border-image:none; }"
        "QTreeView::item { color:%2; }"
        "QTreeView::item:selected { background:%9; color:%2; }"
        "QListWidget { background:%3; color:%2; border:1px solid %8; }"
        "QListWidget::item:selected { background:%9; color:%2; }"
        "QGroupBox { color:%2; border:1px solid %8; border-radius:4px;"
        "  margin-top:12px; padding-top:8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left:10px; padding:0 4px; }"
        "QLabel { color:%2; }"
        "QLineEdit, QComboBox, QPlainTextEdit, QSpinBox {"
        "  background:%5; color:%2; border:1px solid %8;"
        "  border-radius:4px; padding:4px 6px; selection-background-color:%9; }"
        "QComboBox::drop-down { border:none; }"
        "QPushButton { background:%3; color:%2; border:1px solid %8;"
        "  border-radius:4px; padding:5px 12px; }"
        "QPushButton:hover { background:%6; }"
        "QPushButton:pressed { background:%9; }"
        "QPushButton:disabled { color:%MUTED%; }"
        "QCheckBox { color:%2; spacing:6px; }"
        "QTabWidget::pane { border:none; background:%3; top:-1px; }"
        "QTabBar { background:%1; }"
        "QTabBar::tab { background:%6; color:%2; padding:7px 10px 7px 10px;"
        "  border:1px solid %8; margin-right:2px; }"
        "QTabBar::tab:selected { background:%7; color:%2;"
        "  border:1px solid %8; border-bottom:2px solid %4; }"
        "QTabBar::tab:hover { color:%2; }"
        "QTabBar::close-button { margin-left:4px; }"
        "QScrollBar:vertical { background:%3; width:10px; margin:0; }"
        "QScrollBar::handle:vertical { background:%SCROLL%; min-height:24px; border-radius:4px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
        "QScrollBar:horizontal { background:%3; height:10px; }"
        "QScrollBar::handle:horizontal { background:%SCROLL%; min-width:24px; border-radius:4px; }"
        "QDialog, QMessageBox { background:%3; color:%2; }"
        "QMessageBox QLabel { color:%2; }"
        "QProgressBar { background:%5; color:%2; border:1px solid %8;"
        "  border-radius:4px; text-align:center; max-height:14px; }"
        "QProgressBar::chunk { background:%4; }")
                      .arg(win)
                      .arg(text)
                      .arg(panel)
                      .arg(accent)
                      .arg(input)
                      .arg(bar)
                      .arg(tabSel)
                      .arg(border)
                      .arg(select)
                      .replace(QString::fromUtf8("%MUTED%"), muted)
                      .replace(QString::fromUtf8("%SCROLL%"), scroll));

    if (_projectDock)
        _projectDock->applyTheme();

    foreach (ChatSession *session, _sessions)
    {
        if (session && session->widget())
            session->widget()->applyTheme();
    }
}

//----------------------------------------------

void MainWindow::focusWorkerSession(Worker *worker)
{
    if (!worker || !worker->session())
        return;
    if (isMinimized())
        showNormal();
    show();
    raise();
    activateWindow();
    QApplication::setActiveWindow(this);
    QApplication::alert(this);
    ensureSessionVisible(worker->session());
}

//----------------------------------------------

void MainWindow::restoreSessionContent(ChatSession *session)
{
    if (!session || !session->widget())
        return;

    session->ensureContentLoaded();
    ConversationWidget *widget = session->widget();
    if (widget->displayLog().isEmpty() && session->agent())
        widget->restoreFromHistory(session->agent()->history());

    Worker *worker = session->worker();
    if (!worker)
        return;
    if (worker->state() == WorkerStateWaitingApproval)
        widget->showPermissionRequest(worker->permissionAction(),
                                      worker->permissionOfferAlways());
    else if (worker->state() == WorkerStateWaitingContinuation)
        widget->showContinuationRequest(worker->lastError());
    else
        widget->hidePermissionRequest();
}

//----------------------------------------------

void MainWindow::updateComposerState()
{
    if (!_composer || !_composerSend || !_composerStop)
        return;

    ChatSession *session = rootSession();
    Worker *worker = session ? session->worker() : 0;
    Agent *agent = session ? session->agent() : 0;
    const bool busy = agent && agent->isBusy();
    bool waiting = false;
    if (worker)
    {
        waiting = worker->state() == WorkerStateRunning
                  || worker->state() == WorkerStateWaitingChildren
                  || worker->state() == WorkerStateWaitingApproval
                  || worker->state() == WorkerStateWaitingContinuation;
    }
    const bool canSend = hasOpenProject() && session && !_resetting && !busy && !waiting;
    const bool active = anyWorkActive();
    _composer->setReadOnly(!canSend);
    _composer->setEnabled(hasOpenProject() && session && !_resetting);
    _composerSend->setEnabled(canSend);
    _composerStop->setEnabled(active && !_resetting);
    setAllStopEnabled(active && !_resetting);
    if (!hasOpenProject())
        _composer->setPlaceholderText(tr("Откройте или создайте проект"));
    else if (!session)
        _composer->setPlaceholderText(tr("Нет главного чата"));
    else if (!canSend)
        _composer->setPlaceholderText(
            tr("Главный чат занят: ожидание модели или дочерних воркеров…"));
    else
        _composer->setPlaceholderText(
            tr("Сообщение в главный чат…  Enter — отправить, Shift+Enter — новая строка"));
}

//----------------------------------------------

void MainWindow::stopAllWork()
{
    if (_resetting)
        return;
    if (_workers)
        _workers->stopAll();
    foreach (ChatSession *session, _sessions)
    {
        if (session && session->agent())
            session->agent()->abort();
        if (session && session->widget())
            session->widget()->hidePermissionRequest();
    }
    statusBar()->showMessage(tr("Остановлено во всех чатах"));
    updateComposerState();
    updateBusyTabs();
}

//----------------------------------------------

void MainWindow::updateBusyTabs()
{
    scheduleUiRefresh();
}

//----------------------------------------------

void MainWindow::refreshBusyTabs()
{
    QList<int> busy;
    if (_tabs)
    {
        foreach (ChatSession *session, _sessions)
        {
            if (!session || !session->widget() || !session->agent())
                continue;
            // «Работает» — не только когда идёт запрос к модели: воркер в
            // состоянии Running/WaitingChildren тоже занят делом, даже если
            // агент между запросами свободен.
            bool working = session->agent()->isBusy();
            if (!working && session->worker())
            {
                const WorkerState state = session->worker()->state();
                working = state == WorkerStateRunning
                          || state == WorkerStateWaitingChildren;
            }
            if (!working)
                continue;
            const int index = _tabs->indexOf(session->widget());
            if (index >= 0)
                busy.append(index);
        }
    }
    if (_tabBar)
        _tabBar->setBusyIndexes(busy);
    if (busy.isEmpty())
    {
        if (_tabBlinkTimer)
            _tabBlinkTimer->stop();
        _tabBlinkOn = false;
        if (_tabBar)
            _tabBar->setBlinkOn(false);
    }
    else if (_tabBlinkTimer && !_tabBlinkTimer->isActive())
    {
        _tabBlinkOn = true;
        if (_tabBar)
            _tabBar->setBlinkOn(true);
        _tabBlinkTimer->start();
    }
    updatePermissionHighlights();
}

//----------------------------------------------

QList<Worker *> MainWindow::pendingConfirmationWorkers() const
{
    QList<Worker *> list;
    if (!_workers)
        return list;
    foreach (Worker *worker, _workers->workers())
    {
        if (!worker)
            continue;
        const WorkerState state = worker->state();
        if (state == WorkerStateWaitingApproval
            || state == WorkerStateWaitingContinuation)
            list.append(worker);
    }
    return list;
}

//----------------------------------------------

void MainWindow::cycleConfirmation(int delta)
{
    const QList<Worker *> list = pendingConfirmationWorkers();
    if (list.isEmpty())
        return;

    int current = -1;
    QWidget *page = _tabs ? _tabs->currentWidget() : 0;
    for (int i = 0; i < list.size(); ++i)
    {
        ChatSession *session = list.at(i)->session();
        if (session && session->widget() == page)
            current = i;
    }

    int next = 0;
    if (current < 0)
        next = (delta >= 0) ? 0 : list.size() - 1;
    else
        next = (current + delta + list.size() * 8) % list.size();
    focusWorkerSession(list.at(next));
}

//----------------------------------------------

void MainWindow::updatePermissionHighlights()
{
    QList<int> pending;
    const QList<Worker *> workers = pendingConfirmationWorkers();
    if (_tabs)
    {
        foreach (Worker *worker, workers)
        {
            if (!worker || !worker->session() || !worker->session()->widget())
                continue;
            const int index = _tabs->indexOf(worker->session()->widget());
            if (index >= 0)
                pending.append(index);
        }
    }
    if (_tabBar)
        _tabBar->setPermissionIndexes(pending);
    const bool have = !workers.isEmpty();
    if (_prevConfirmAction)
        _prevConfirmAction->setEnabled(have);
    if (_nextConfirmAction)
        _nextConfirmAction->setEnabled(have);
}

//----------------------------------------------

bool MainWindow::anyWorkActive() const
{
    if (anySessionBusy())
        return true;
    if (!_workers)
        return false;
    foreach (Worker *worker, _workers->workers())
    {
        if (!worker)
            continue;
        const WorkerState state = worker->state();
        if (state == WorkerStateRunning
            || state == WorkerStateQueued
            || state == WorkerStateWaitingApproval
            || state == WorkerStateWaitingChildren
            || state == WorkerStateWaitingContinuation)
            return true;
    }
    return false;
}

//----------------------------------------------

void MainWindow::setAllStopEnabled(bool enabled)
{
    foreach (ChatSession *session, _sessions)
    {
        if (session && session->widget())
            session->widget()->setStopEnabled(enabled);
    }
}

//----------------------------------------------

bool MainWindow::anySessionBusy() const
{
    foreach (ChatSession *session, _sessions)
    {
        if (session->agent() && session->agent()->isBusy())
            return true;
    }
    return false;
}

//----------------------------------------------

void MainWindow::applyInspectToAllSessions()
{
    const bool enabled = _inspectCheck && _inspectCheck->isChecked();
    foreach (ChatSession *session, _sessions)
    {
        if (session->agent())
            session->agent()->setInspectIo(enabled);
    }
}
