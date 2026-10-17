#include "mainwindow.h"
//----------------------------------------------
#include "agent.h"
#include "applocale.h"
#include "appicons.h"
#include "appsettings.h"
#include "appversion.h"
#include "balancedialog.h"
#include "chatsession.h"
#include "composerinput.h"
#include "conversationwidget.h"
#include "docktitlebar.h"
#include "graphthumb.h"
#include "filechangestore.h"
#include "journalwidget.h"
#include "knowledgestore.h"
#include "llmclient.h"
#include "modelcapabilities.h"
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
#include <QDockWidget>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QJsonObject>
#include <QImage>
#include <QPixmap>
#include <QBuffer>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPalette>
#include <QMessageBox>
#include <QMetaObject>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegExp>
#include <QScrollArea>
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
                setTabTextColor(i, AppSettings::instance()->textColor());
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
        const QColor border = s->borderColor();
        const QColor win = s->windowColor();
        const QColor panel = s->panelColor();
        const QColor textColor = s->textColor();
        const QColor muted = s->mutedTextColor();

        for (int i = 0; i < count(); ++i)
        {
            const QRect r = tabRect(i).adjusted(1, 2, -1, 0);
            const bool active = (i == currentIndex());
            QColor bg = active ? panel : win;
            if (_permission.contains(i))
                bg = QColor(199, 93, 66, 235);
            painter.setPen(QPen(border, 1));
            painter.setBrush(bg);
            painter.drawRoundedRect(r, 8, 8);
            if (active && !_permission.contains(i))
            {
                painter.setPen(Qt::NoPen);
                painter.setBrush(panel);
                painter.drawRect(r.adjusted(1, r.height() / 2, -1, 1));
            }

            const QRect textRect = tabRect(i).adjusted(10, 0, -24, 0);
            const QString shown = fontMetrics().elidedText(tabText(i), Qt::ElideRight,
                                                          qMax(8, textRect.width()));
            painter.setPen(_permission.contains(i)
                               ? QColor(250, 250, 250)
                               : (active ? textColor : muted));
            painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, shown);
        }

        if (_busy.isEmpty())
            return;
        const int alpha = _blinkOn ? 210 : 75;
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(34, 197, 94, alpha), 1.6));
        foreach (int index, _busy)
        {
            if (index < 0 || index >= count())
                continue;
            const QRect r = tabRect(index).adjusted(2, 3, -2, 1);
            painter.drawRoundedRect(r, 8, 8);
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

class PillStatusBar : public QStatusBar
{
public:
    explicit PillStatusBar(QWidget *parent = 0)
        : QStatusBar(parent)
    {
    }

protected:
    virtual void paintEvent(QPaintEvent *event)
    {
        QStatusBar::paintEvent(event);
        const QString msg = currentMessage();
        if (msg.isEmpty())
            return;

        int right = width() - 8;
        const QObjectList kids = children();
        for (int i = 0; i < kids.size(); ++i)
        {
            QWidget *w = qobject_cast<QWidget *>(kids.at(i));
            if (!w || !w->isVisible() || w->x() <= 2)
                continue;
            right = qMin(right, w->x() - 4);
        }
        const int maxW = right - 12;
        if (maxW < 32)
            return;

        AppSettings *s = AppSettings::instance();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(QRect(0, 1, right, height() - 1), s->windowColor());

        const QFontMetrics fm(font());
        const int padX = 8;
        const int pillW = qMin(maxW, qMax(32, fm.width(msg) + padX * 2));
        const int pillH = qMin(height() - 6, fm.height() + 6);
        const QRect pill(8, qMax(2, (height() - pillH) / 2), pillW, pillH);
        p.setPen(QPen(s->borderColor(), 1));
        p.setBrush(s->panelColor());
        p.drawRoundedRect(pill, 6, 6);
        p.setPen(s->textColor());
        const QString shown = fm.elidedText(msg, Qt::ElideRight, pill.width() - padX * 2);
        p.drawText(pill.adjusted(padX, 0, -padX, 0),
                   Qt::AlignVCenter | Qt::AlignLeft, shown);
    }
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
    , _profilesApplyTimer(0)
    , _uiRefreshTree(false)
    , _graphDirty(true)
    , _tabBlinkOn(false)
    , _graph(0)
    , _journal(0)
    , _settingsPage(0)
    , _projectSettingsPage(0)
    , _projectDock(0)
    , _graphThumbDock(0)
    , _fileMenu(0)
    , _windowMenu(0)
    , _projectMenu(0)
    , _helpMenu(0)
    , _recentMenu(0)
    , _balanceAction(0)
    , _quitAction(0)
    , _windowChatAction(0)
    , _windowGraphAction(0)
    , _windowJournalAction(0)
    , _windowSettingsAction(0)
    , _helpStatsAction(0)
    , _helpAboutAction(0)
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
    , _projectSettingsAction(0)
    , _inspectCheck(0)
    , _composerBar(0)
    , _graphThumb(0)
    , _composer(0)
    , _progressBar(0)
    , _workTimeLabel(0)
    , _changedFilesLabel(0)
    , _addedLinesLabel(0)
    , _removedLinesLabel(0)
    , _projectMeterTimer(0)
    , _chatSaveThread(0)
    , _lastPumpMs(0)
    , _progressDepth(0)
    , _nextChatNumber(0)
    , _chatsShown(false)
    , _closing(false)
    , _resetting(false)
    , _loadingProject(false)
    , _projectStoreEnabled(false)
    , _appWindowLayoutRestored(false)
    , _chatSavePending(false)
    , _pendingSwitchLoad(false)
    , _pendingSwitchTries(0)
{
    setWindowTitle(QString::fromUtf8("%1 %2")
                       .arg(QString::fromUtf8(UNIQUE_APP_NAME),
                            QString::fromUtf8(UNIQUE_APP_VERSION)));
    setWindowIcon(AppIcons::appIcon());
    resize(960, 720);
    setStatusBar(new PillStatusBar(this));

    setupUi();
    setupMenus();
    connect(AppLocale::instance(), SIGNAL(languageChanged(QString)),
            this, SLOT(sltLocaleChanged(QString)));
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
    connect(FileChangeStore::instance(), SIGNAL(changed()),
            this, SLOT(sltRefreshProjectMeters()));
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
    saveAppWindowLayout();
    saveProjectUiState();
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
    restoreAppWindowLayout();
    if (!hasOpenProject())
    {
        updateComposerState();
        updateProjectActions();
        statusBar()->showMessage(tr("Откройте или создайте проект"));
        return;
    }
    beginStatusProgress(tr("Загрузка проекта"), 0);
    loadOpenChats();
    restoreProjectUiState(false);
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
    box.setText(tr("Очистить проект до состояния нового?\n"
                   "Будут удалены чаты, воркеры, журнал сеанса, база знаний, "
                   "статистика токенов и настройки проекта в ___project___\n"
                   "(пресеты и разрешения снова возьмутся из настроек программы).\n"
                   "Исходники рабочей папки и ключи API не изменятся."));
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
        if (_journal)
            _journal->clearMessages();
        sltShowGraph();
        _resetting = false;
        updateComposerState();
        updateProjectActions();
        statusBar()->showMessage(tr("Откройте или создайте проект"));
        return;
    }

    const QString root = _workspace->root();
    const QString store = AppSettings::projectStoreDir(root);

    discardSessionUi();
    if (_journal)
        _journal->clearMessages();

    FileChangeStore::instance()->forget();
    KnowledgeStore::instance()->invalidate();
    if (_projectDock)
        _projectDock->releaseFileModel();

    // Как при создании проекта: убираем ___project___ и поднимаем заново.
    if (!retireProjectStore(store))
    {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Очистить всё"));
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
        // Частичный сброс UI уже сделан — поднимаем пустой сеанс без wipe.
        AppSettings::instance()->reseedOpenProject();
        AppSettings::instance()->resetUsage();
    }
    else
    {
        AppSettings::instance()->reseedOpenProject();
        ensureProjectStore();
        FileChangeStore::instance()->setRoot(root, false);
    }

    if (_projectSettingsPage)
    {
        _projectSettingsPage->setProjectPath(root);
        _projectSettingsPage->reloadAll();
    }
    if (_projectDock)
        _projectDock->setProjectRoot(root);

    sltNewChat();
    restoreProjectUiState(true);
    _resetting = false;
    if (rootSession() && rootSession()->agent())
        rootSession()->agent()->refreshContextUsage();
    saveOpenChats();
    sltBusyChanged(false);
    updateComposerState();
    updateProjectActions();
    statusBar()->showMessage(tr("Проект очищен — как только что созданный."));
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
    if (_journal)
        _journal->flushPersist();
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
    {
        ensureProjectStore();
        // Новый проект: не переносим счётчики с ранее открытого.
        AppSettings::instance()->resetUsage();
    }
    rememberRecentProject(hasOpenProject() ? _workspace->root() : clean);
    sltSaveSettings();
    pumpUi(true);

    if (loadExisting)
        loadOpenChats();
    else
        sltNewChat();
    if (_journal)
    {
        if (loadExisting)
            _journal->loadPersisted();
        else
            _journal->clearMessages();
    }

    updateStatusProgress(0, 0, tr("Дерево файлов"));
    if (_projectDock)
        _projectDock->setProjectRoot(hasOpenProject() ? _workspace->root() : QString());
    pumpUi(true);

    _loadingProject = false;
    // Дерево уже построено. Дальше идёт восстановление воркеров (чтение их
    // чатов с диска, пересборка графа) — показываем это отдельной подписью,
    // иначе «Дерево файлов» висит на экране всё время восстановления.
    updateStatusProgress(0, 0, tr("Восстановление воркеров"));
    if (loadExisting)
        restoreProjectUiState(false);
    else
        restoreProjectUiState(true);
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
    sendRootComposerMessage(text);
}

//----------------------------------------------

void MainWindow::sendRootComposerMessage(QString text, QJsonArray contentParts)
{
    if (!hasOpenProject())
        return;
    ChatSession *session = rootSession();
    if (!session)
    {
        session = createSession(QString(), nextChatTitle());
        if (!session)
            return;
        _workers->createRoot(session);
    }
    if (session->worker() && !session->worker()->isRoot())
        return;

    if (_settingsPage)
        _settingsPage->commitCurrentProfile();
    if (_projectSettingsPage)
        _projectSettingsPage->commitCurrentProfile();
    applyClientToAllSessions();
    sltSaveSettings();
    applyProjectDirectory();
    ensureProjectStore();
    applyClientToSession(session);
    if (_tabs && _tabs->count() == 0)
    {
        createJournalWindow();
        ensureTabVisible(_journal, tr("Журнал"), AppIcons::journal());
    }
    if (session->worker() && session->worker()->isRoot())
    {
        session->worker()->clearWorkPlan();
        session->worker()->setTask(text);
        const QString prefix = session->worker()->composeStartPrefix();
        if (!prefix.isEmpty())
            session->agent()->injectContext(prefix);
    }
    if (!contentParts.isEmpty())
        session->agent()->sendUserMultimodal(text, contentParts);
    else
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
    if (_projectSettingsPage)
        _projectSettingsPage->commitCurrentProfile();
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
                                         tr("Обнулить счётчики токенов, запросов и времени работы этого проекта?\n"
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
                                         tr("Обнулить общую статистику по всем проектам, включая время работы?\n"
                                            "Счётчики открытого проекта не изменятся."),
                                         tr("Обнулить"), tr("Отмена"));
    if (answer != QMessageBox::Yes)
        return;
    AppSettings::instance()->resetGlobalUsage();
}

//----------------------------------------------

void MainWindow::sltUsageChanged()
{
    sltRefreshProjectMeters();
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
    if (_chatsShown && !_loadingProject && !_resetting)
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
    if (page == _graph || page == _settingsPage || page == _projectSettingsPage
        || page == _journal)
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
        scheduleSaveOpenChats();
        return;
    }
    ChatSession *session = sessionByTabWidget(page);
    if (session)
    {
        restoreSessionContent(session);
        if (session->widget())
            session->widget()->syncRunningState();
    }
    refreshBusyTabs();
    updateComposerState();
    scheduleSaveOpenChats();
}

//----------------------------------------------

void MainWindow::sltChatUserMessage(const QString &text)
{
    ChatSession *session = sessionByAgent(sender());
    if (!session || !session->autoTitle())
        return;
    if (session->worker() && !session->worker()->isRoot())
        return;
    Agent *agent = qobject_cast<Agent *>(sender());
    if (agent && agent->lastUserIngress() != AgentUserIngressHuman)
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
    connect(_graph, SIGNAL(workerRemoveRequested(QString)),
            this, SLOT(sltWorkerRemoveRequested(QString)));
    connect(_graph, SIGNAL(workerInterruptRequested(QString)),
            this, SLOT(sltWorkerForceStopById(QString)));
    connect(_graph, SIGNAL(reportToParentRequested(QString)),
            this, SLOT(sltWorkerForceReportById(QString)));
    connect(_graph, SIGNAL(workerProfileChangeRequested(QString,QString)),
            this, SLOT(sltWorkerProfileChangeRequested(QString,QString)));
    if (_tabBar)
        _tabBar->setGraphPage(_graph);
}

//----------------------------------------------

void MainWindow::createJournalWindow()
{
    if (_journal)
        return;
    _journal = new JournalWidget();
    connect(_journal, SIGNAL(workerChatRequested(QString)),
            this, SLOT(sltJournalWorkerChatRequested(QString)));
    if (_tabBar)
        _tabBar->setJournalPage(_journal);
}

//----------------------------------------------

void MainWindow::sltJournalWorkerChatRequested(const QString &workerId)
{
    if (!_workers || workerId.trimmed().isEmpty())
        return;
    Worker *worker = _workers->workerById(workerId.trimmed());
    if (worker)
        focusWorkerSession(worker);
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
    QString role;
    switch (worker->type())
    {
    case WorkerTypeExecutor:
        role = tr("Исполнитель");
        break;
    case WorkerTypeTester:
        role = tr("Тестировщик");
        break;
    case WorkerTypeTool:
        if (!worker->toolName().isEmpty())
            return tr("Инструмент: %1").arg(worker->toolName());
        role = tr("Инструмент");
        break;
    case WorkerTypeManager:
    default:
        if (worker->isRoot() && worker->personName().isEmpty())
            return worker->session() ? worker->session()->title() : tr("Главный чат");
        role = tr("Менеджер");
        break;
    }
    if (worker->isGroupInterlocutor())
        role += tr(" · собеседник");
    else if (worker->isGroupChief())
        role += tr(" · главный группы");

    QString name = worker->personName();
    QString task = worker->title();
    if (task.isEmpty())
        task = Worker::shortLabelFromTask(worker->task());
    if (task.size() > 40)
        task = task.left(37) + QString::fromUtf8("…");

    if (!name.isEmpty() && !task.isEmpty())
        return name + QString::fromUtf8(" · ") + role + QString::fromUtf8(" — ") + task;
    if (!name.isEmpty())
        return name + QString::fromUtf8(" · ") + role;
    if (!task.isEmpty())
        return role + QString::fromUtf8(" — ") + task;
    return role;
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
    // Подпись автора в чате = та же, что в журнале (уникальный цвет по имени).
    worker->session()->widget()->setAuthorLabels(
        tr("Пользователь"), workerWindowTitle(worker));
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
    connect(worker, SIGNAL(consoleInputRequested(QString)),
            this, SLOT(sltConsoleInputRequested(QString)));
    connect(worker, SIGNAL(subtreeBudgetRequested(QString)),
            this, SLOT(sltSubtreeBudgetRequested(QString)));
    connect(worker, SIGNAL(continuationRequested(QString)),
            this, SLOT(sltContinuationRequested(QString)));
    connect(worker, SIGNAL(finished()),
            this, SLOT(sltWorkerFinished()));
    if (worker->session() && worker->session()->agent())
    {
        connect(worker->session()->agent(), SIGNAL(toolBudgetExhausted(int)),
                this, SLOT(sltToolBudgetExhausted(int)));
    }
    if (_journal && worker->session() && worker->session()->agent())
    {
        _journal->attachAgent(worker->session()->agent(), workerWindowTitle(worker),
                              worker->id());
        if (!_resetting && !_loadingProject && !worker->isRoot())
            _journal->addTransient(workerWindowTitle(worker), tr("Создан"),
                                   worker->task().isEmpty() ? tr("Новый воркер") : worker->task(),
                                   QString::fromUtf8("noticeMsg"), worker->id());
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
        updateGraphThumb();
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
    // Сцену держим актуальной и для вкладки, и для миниатюры у композера.
    createGraphWindow();
    if (!_graph || !_graphDirty)
        return;
    _graph->rebuild(_workers);
    _graphDirty = false;
}

//----------------------------------------------

void MainWindow::updateGraphThumb()
{
    if (!_graphThumb)
        return;
    rebuildGraphIfVisible();
    if (!_graph)
    {
        _graphThumb->setOverview(QPixmap());
        return;
    }
    if (_graphDirty)
    {
        _graph->rebuild(_workers);
        _graphDirty = false;
    }
    QSize thumbSize(240, 180);
    if (_graphThumb->width() > 40 && _graphThumb->height() > 40)
        thumbSize = QSize(_graphThumb->width() * 2, _graphThumb->height() * 2);
    _graphThumb->setOverview(_graph->overviewPixmap(thumbSize));
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

void MainWindow::sltWorkerProfileChangeRequested(const QString &workerId,
                                                 const QString &profileId)
{
    Worker *worker = _workers ? _workers->workerById(workerId) : 0;
    if (!worker)
        return;
    const LlmProfile profile = AppSettings::instance()->profileById(profileId);
    if (profile.id.isEmpty())
    {
        if (statusBar())
            statusBar()->showMessage(tr("Профиль модели не найден"));
        return;
    }
    worker->setLlmProfileId(profile.id);
    if (worker->session())
    {
        applyClientToSession(worker->session());
        // Tools зависят от modality профиля — перевешиваем после смены модели.
        if (_workers)
            _workers->installTools(worker);
        applyWorkerUi(worker);
        // Показать вкладку воркера, чтобы композитор подтянул его caps/вложения.
        ensureSessionVisible(worker->session());
    }
    // Заново определить входы модели (OpenRouter /images/models и т.п.).
    const ModelCapabilities caps =
        ModelCapabilityService::instance()->capabilitiesFor(profile, true);
    if (_graph)
        _graph->rebuild(_workers);
    _composerAttachments.clear();
    updateComposerAttachmentsUi();
    updateComposerState();
    if (!_closing && !_resetting)
        scheduleSaveOpenChats();
    QString title = profile.name.trimmed();
    if (title.isEmpty())
        title = profile.model.trimmed();
    if (title.isEmpty())
        title = profile.id;
    if (statusBar())
    {
        statusBar()->showMessage(
            tr("Профиль модели: %1 → %2 (%3)")
                .arg(workerWindowTitle(worker), title, caps.summary()));
    }
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
                               QString::fromUtf8("noticeMsg"), tool->id());
    _workers->destroyTool(id);
    if (!_closing && !_resetting)
        scheduleSaveOpenChats();
}

//----------------------------------------------

void MainWindow::sltWorkerRemoveRequested(const QString &id)
{
    Worker *worker = _workers ? _workers->workerById(id) : 0;
    if (!worker || worker->isRoot())
        return;
    QString title = workerWindowTitle(worker);
    QString text = tr("Удалить воркера «%1» с графа?\n"
                      "Дочерние воркеры будут удалены, чат закроется.")
                       .arg(title);
    const int answer = askStyledQuestion(tr("Удаление воркера"), text,
                                         tr("Удалить"), tr("Отмена"));
    if (answer != QMessageBox::Yes)
        return;
    QString error;
    if (!_workers->forceRemoveFinished(worker, &error))
    {
        if (statusBar())
            statusBar()->showMessage(error);
        return;
    }
    if (_journal)
        _journal->addTransient(title, tr("Удалён"),
                               tr("Воркер удалён пользователем с графа"),
                               QString::fromUtf8("noticeMsg"), id);
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
                               action, QString::fromUtf8("noticeMsg"), worker->id());
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

void MainWindow::sltConsoleInputRequested(const QString &transcript)
{
    Worker *worker = qobject_cast<Worker *>(sender());
    if (!worker || !worker->session())
        return;
    focusWorkerSession(worker);
    if (worker->session()->widget())
        worker->session()->widget()->showConsoleInput(transcript);
    QString journalText = transcript;
    if (journalText.size() > 500)
        journalText = journalText.right(500);
    if (_journal)
        _journal->addTransient(workerWindowTitle(worker), tr("Команда ждёт ввод"),
                               journalText, QString::fromUtf8("noticeMsg"), worker->id());
}

//----------------------------------------------

void MainWindow::sltConsoleInputAnswered(const QString &line, bool sent)
{
    ConversationWidget *widget = qobject_cast<ConversationWidget *>(sender());
    ChatSession *session = sessionByWidget(widget);
    if (!session || !session->worker())
        return;
    session->worker()->resolveConsoleInput(sent, line);
    updatePermissionHighlights();
}

//----------------------------------------------

void MainWindow::sltSubtreeBudgetRequested(const QString &text)
{
    Worker *worker = qobject_cast<Worker *>(sender());
    if (!worker || !worker->session())
        return;
    focusWorkerSession(worker);
    if (worker->session()->widget())
        worker->session()->widget()->showSubtreeBudgetRequest(text);
    if (_journal)
        _journal->addTransient(workerWindowTitle(worker), tr("Бюджет поддерева"),
                               text, QString::fromUtf8("noticeMsg"), worker->id());
}

//----------------------------------------------

void MainWindow::sltSubtreeBudgetAnswered(bool granted)
{
    ConversationWidget *widget = qobject_cast<ConversationWidget *>(sender());
    ChatSession *session = sessionByWidget(widget);
    if (!session || !session->worker())
        return;
    session->worker()->resolveSubtreeBudget(granted);
    updatePermissionHighlights();
    QTimer::singleShot(0, this, SLOT(sltAdvanceConfirmation()));
}

//----------------------------------------------

void MainWindow::sltToolBudgetExhausted(int chunk)
{
    Q_UNUSED(chunk);
    Agent *agent = qobject_cast<Agent *>(sender());
    if (!agent)
        return;
    foreach (ChatSession *session, _sessions)
    {
        if (!session || session->agent() != agent || !session->worker())
            continue;
        focusWorkerSession(session->worker());
        if (_journal)
            _journal->addTransient(workerWindowTitle(session->worker()),
                                   tr("Бюджет функций"),
                                   tr("Исчерпан лимит вызовов функций. Нужно решение."),
                                   QString::fromUtf8("noticeMsg"),
                                   session->worker()->id());
        break;
    }
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
        updateGraphThumb();
    }
}

//----------------------------------------------

void MainWindow::sltShowSettings()
{
    if (!_settingsPage)
        return;
    _settingsPage->reloadAll();
    ensureTabVisible(_settingsPage, tr("Настройки"));
}

//----------------------------------------------

void MainWindow::sltShowProjectSettings()
{
    if (!_projectSettingsPage || !hasOpenProject())
        return;
    _projectSettingsPage->setProjectPath(_workspace->root());
    _projectSettingsPage->reloadAll();
    ensureTabVisible(_projectSettingsPage, tr("Настройки проекта"));
}

//----------------------------------------------

//----------------------------------------------

void MainWindow::sltShowBalance()
{
    BalanceDialog dialog(this);
    dialog.exec();
}

//----------------------------------------------

void MainWindow::sltShowAbout()
{
    const QString email = QString::fromUtf8("e-face@bk.ru");
    const QString text =
        QString::fromUtf8("<h3>%1</h3><p>%2</p><p>%3</p><p>%4</p>")
            .arg(QString::fromUtf8(UNIQUE_APP_NAME),
                 tr("Версия %1").arg(QString::fromUtf8(UNIQUE_APP_VERSION)),
                 tr("Агент ИИ с функциями распределения ролей."),
                 tr("Связь: <a href=\"mailto:%1\">%1</a>").arg(email));
    QMessageBox::about(this, tr("О программе"), text);
}

//----------------------------------------------

static QString formatWorkClock(qint64 ms)
{
    if (ms < 0)
        ms = 0;
    const qint64 total = ms / 1000;
    const qint64 hours = total / 3600;
    const int minutes = int((total % 3600) / 60);
    const int seconds = int(total % 60);
    if (hours > 0)
    {
        return QString::fromUtf8("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    }
    return QString::fromUtf8("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

//----------------------------------------------

static QString formatWorkLong(qint64 ms)
{
    if (ms < 0)
        ms = 0;
    const qint64 total = ms / 1000;
    const qint64 hours = total / 3600;
    const int minutes = int((total % 3600) / 60);
    const int seconds = int(total % 60);
    if (hours > 0)
        return QString::fromUtf8("%1 ч %2 мин %3 с").arg(hours).arg(minutes).arg(seconds);
    if (minutes > 0)
        return QString::fromUtf8("%1 мин %2 с").arg(minutes).arg(seconds);
    return QString::fromUtf8("%1 с").arg(seconds);
}

//----------------------------------------------

void MainWindow::sltShowStatistics()
{
    AppSettings *s = AppSettings::instance();
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Статистика"));
    dialog.resize(640, 720);

    QWidget *content = new QWidget(&dialog);
    content->setObjectName(QString::fromUtf8("statsHost"));
    content->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout *contentLay = new QVBoxLayout(content);
    contentLay->setContentsMargins(0, 0, 0, 0);

    QGroupBox *globalBox = new QGroupBox(tr("Всего"), content);
    QVBoxLayout *globalLay = new QVBoxLayout(globalBox);
    UsageStatsWidget *globalView = new UsageStatsWidget(globalBox);
    globalView->setStats(s->globalUsage());
    globalView->setExtraRows(
        QStringList() << tr("Время работы"),
        QStringList() << formatWorkLong(s->liveWorkMs(false)));
    globalView->setMinimumHeight(globalView->sizeHint().height());
    globalLay->addWidget(globalView);
    contentLay->addWidget(globalBox);

    QGroupBox *projectBox = new QGroupBox(tr("Текущий проект"), content);
    QVBoxLayout *projectLay = new QVBoxLayout(projectBox);
    if (hasOpenProject())
    {
        UsageStatsWidget *projectView = new UsageStatsWidget(projectBox);
        projectView->setStats(s->projectUsage());
        const int workers = _workers ? _workers->workers().size() : 0;
        const FileLineDelta delta = FileChangeStore::instance()->totals();
        const int changedFiles = FileChangeStore::instance()->changedFileCount();
        projectView->setExtraRows(
            QStringList()
                << tr("Время работы")
                << tr("Файлов изменено")
                << tr("Строк добавлено")
                << tr("Строк удалено")
                << tr("Открытых чатов")
                << tr("Воркеров")
                << tr("Программ в «всегда»"),
            QStringList()
                << formatWorkLong(s->liveWorkMs(true))
                << QString::number(changedFiles)
                << QString::fromUtf8("+%1").arg(delta.added)
                << QString::fromUtf8("−%1").arg(delta.removed)
                << QString::number(_sessions.size())
                << QString::number(workers)
                << QString::number(s->alwaysAllowedCommands(SettingsKindProject).size()));
        projectView->setMinimumHeight(projectView->sizeHint().height());
        projectLay->addWidget(projectView);
    }
    else
    {
        QLabel *empty = new QLabel(tr("Проект не открыт"), projectBox);
        empty->setWordWrap(true);
        projectLay->addWidget(empty);
    }
    contentLay->addWidget(projectBox);
    contentLay->addStretch(1);

    QScrollArea *scroll = new QScrollArea(&dialog);
    scroll->setObjectName(QString::fromUtf8("statsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    if (QWidget *vp = scroll->viewport())
    {
        const QColor chatColor = s->chatBackgroundColor();
        vp->setAutoFillBackground(true);
        QPalette pal = vp->palette();
        pal.setColor(QPalette::Window, chatColor);
        pal.setColor(QPalette::Base, chatColor);
        vp->setPalette(pal);
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
    root->addWidget(scroll, 1);
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
    if (!_profilesApplyTimer)
    {
        _profilesApplyTimer = new QTimer(this);
        _profilesApplyTimer->setSingleShot(true);
        connect(_profilesApplyTimer, SIGNAL(timeout()),
                this, SLOT(sltApplyClientsFromProfiles()));
    }
    _profilesApplyTimer->start(350);
}

//----------------------------------------------

void MainWindow::sltDefaultProfilesChanged()
{
    sltProfilesChanged();
}

//----------------------------------------------

void MainWindow::sltApplyClientsFromProfiles()
{
    applyClientToAllSessions();
}

//----------------------------------------------

void MainWindow::sltRemoveWorkersForProfile(const QString &profileId)
{
    if (!_workers || profileId.trimmed().isEmpty())
        return;
    _workers->removeWorkersUsingProfile(profileId);
    if (!_closing && !_resetting)
        scheduleSaveOpenChats();
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
    if (!_composer || !_composerBar || !_composerBar->isSendEnabled())
        return;
    const QString text = _composer->toPlainText().trimmed();
    if (text.isEmpty() && _composerAttachments.isEmpty())
        return;

    const LlmProfile profile = profileForComposerTarget();
    const ModelCapabilities caps =
        ModelCapabilityService::instance()->capabilitiesFor(profile);

    // LLM-чат: картинки → image_url; аудио → input_audio; текст → text;
    // PDF/file → file; иначе путь для инструментов.
    QString message = text;
    QJsonArray mediaParts;
    QStringList pathOnly;
    QStringList embeddedImages;
    QStringList embeddedAudio;
    QStringList embeddedFiles;
    QStringList embeddedTexts;
    // 32-bit процесс: тело запроса + копии QByteArray/QString; держим запас.
    const int kMaxInlineBody = 8 * 1024 * 1024;
    const int kMaxTextBytes = ProjectWorkspace::maxReadBytes;
    const QString baseUrlLower = profile.baseUrl.toLower();
    const bool qwenStyleAudio =
        baseUrlLower.contains(QString::fromUtf8("dashscope"))
        || baseUrlLower.contains(QString::fromUtf8("qwen"))
        || profile.model.toLower().contains(QString::fromUtf8("qwen"));

    foreach (const QString &path, _composerAttachments)
    {
        if (caps.acceptsImage() && isImageAttachmentPath(path))
        {
            QString encodeError;
            const QString dataUrl = encodeProjectImageDataUrl(path, &encodeError);
            if (dataUrl.isEmpty())
            {
                if (statusBar())
                    statusBar()->showMessage(encodeError);
                pathOnly.append(path);
                continue;
            }
            if (dataUrl.size() > kMaxInlineBody)
            {
                if (statusBar())
                    statusBar()->showMessage(
                        tr("Картинка слишком большая для inline: %1 — только путь.")
                            .arg(path));
                pathOnly.append(path);
                continue;
            }
            embeddedImages.append(path);
            QJsonObject imageUrl;
            imageUrl.insert(QString::fromUtf8("url"), dataUrl);
            QJsonObject part;
            part.insert(QString::fromUtf8("type"), QString::fromUtf8("image_url"));
            part.insert(QString::fromUtf8("image_url"), imageUrl);
            mediaParts.append(part);
            continue;
        }

        if (caps.acceptsAudio() && isAudioAttachmentPath(path))
        {
            QString fmt;
            QString encodeError;
            const QString b64 = encodeProjectAudioBase64(path, &fmt, &encodeError);
            if (b64.isEmpty())
            {
                if (statusBar())
                    statusBar()->showMessage(encodeError);
                pathOnly.append(path);
                continue;
            }
            if (b64.size() > kMaxInlineBody)
            {
                if (statusBar())
                    statusBar()->showMessage(
                        tr("Аудио слишком большое для inline: %1 — только путь.")
                            .arg(path));
                pathOnly.append(path);
                continue;
            }
            embeddedAudio.append(path);
            // OpenAI: raw base64; Qwen OpenAI-compat: data:;base64,...
            const QString dataField = qwenStyleAudio
                ? QString::fromUtf8("data:;base64,%1").arg(b64)
                : b64;
            QJsonObject inputAudio;
            inputAudio.insert(QString::fromUtf8("data"), dataField);
            inputAudio.insert(QString::fromUtf8("format"), fmt);
            QJsonObject part;
            part.insert(QString::fromUtf8("type"), QString::fromUtf8("input_audio"));
            part.insert(QString::fromUtf8("input_audio"), inputAudio);
            mediaParts.append(part);
            continue;
        }

        if (isPdfAttachmentPath(path))
        {
            QString encodeError;
            const QString dataUrl = encodeProjectFileDataUrl(path, &encodeError);
            if (!dataUrl.isEmpty() && dataUrl.size() <= kMaxInlineBody)
            {
                embeddedFiles.append(path);
                QJsonObject fileObj;
                fileObj.insert(QString::fromUtf8("filename"), QFileInfo(path).fileName());
                fileObj.insert(QString::fromUtf8("file_data"), dataUrl);
                QJsonObject part;
                part.insert(QString::fromUtf8("type"), QString::fromUtf8("file"));
                part.insert(QString::fromUtf8("file"), fileObj);
                mediaParts.append(part);
                continue;
            }
            if (statusBar() && !encodeError.isEmpty())
                statusBar()->showMessage(encodeError);
            pathOnly.append(path);
            continue;
        }

        // Текст/код — вшиваем в content (работает на любой chat API).
        {
            QString readError;
            const QString body = readProjectTextAttachment(path, kMaxTextBytes, &readError);
            if (!body.isEmpty())
            {
                embeddedTexts.append(path);
                QJsonObject part;
                part.insert(QString::fromUtf8("type"), QString::fromUtf8("text"));
                part.insert(QString::fromUtf8("text"),
                            QString::fromUtf8("### Attached file: %1\n```\n%2\n```")
                                .arg(path, body));
                mediaParts.append(part);
                continue;
            }
            // Текст не вышел (бинарь / ошибка) — пробуем file part ниже.
            if (statusBar() && !readError.isEmpty() && isTextLikeAttachmentPath(path))
                statusBar()->showMessage(readError);
        }

        // Прочий бинарь: если модель «принимает file» — file part, иначе путь.
        if (caps.acceptsFile())
        {
            QString encodeError;
            const QString dataUrl = encodeProjectFileDataUrl(path, &encodeError);
            if (!dataUrl.isEmpty() && dataUrl.size() <= kMaxInlineBody)
            {
                embeddedFiles.append(path);
                QJsonObject fileObj;
                fileObj.insert(QString::fromUtf8("filename"), QFileInfo(path).fileName());
                fileObj.insert(QString::fromUtf8("file_data"), dataUrl);
                QJsonObject part;
                part.insert(QString::fromUtf8("type"), QString::fromUtf8("file"));
                part.insert(QString::fromUtf8("file"), fileObj);
                mediaParts.append(part);
                continue;
            }
        }
        pathOnly.append(path);
    }

    if (!pathOnly.isEmpty())
    {
        message += QString::fromUtf8(
            "\n\nAttached files (paths — use tools if content was not inlined):\n");
        foreach (const QString &path, pathOnly)
            message += QString::fromUtf8("- %1\n").arg(path);
    }
    if (!embeddedImages.isEmpty())
    {
        message += QString::fromUtf8("\n\nAttached images (embedded for the model):\n");
        foreach (const QString &path, embeddedImages)
            message += QString::fromUtf8("- %1\n").arg(path);
    }
    if (!embeddedAudio.isEmpty())
    {
        message += QString::fromUtf8("\n\nAttached audio (embedded for the model):\n");
        foreach (const QString &path, embeddedAudio)
            message += QString::fromUtf8("- %1\n").arg(path);
    }
    if (!embeddedTexts.isEmpty())
    {
        message += QString::fromUtf8("\n\nAttached text files (content inlined):\n");
        foreach (const QString &path, embeddedTexts)
            message += QString::fromUtf8("- %1\n").arg(path);
    }
    if (!embeddedFiles.isEmpty())
    {
        message += QString::fromUtf8("\n\nAttached files (embedded for the model):\n");
        foreach (const QString &path, embeddedFiles)
            message += QString::fromUtf8("- %1\n").arg(path);
    }

    if (message.trimmed().isEmpty() && mediaParts.isEmpty())
        return;

    if (!mediaParts.isEmpty() || !embeddedTexts.isEmpty())
    {
        QString prompt = text.trimmed();
        if (prompt.isEmpty())
        {
            if (!embeddedAudio.isEmpty() && embeddedImages.isEmpty()
                && embeddedTexts.isEmpty() && embeddedFiles.isEmpty())
                prompt = tr("Проанализируй прикреплённое аудио и выполни задачу.");
            else if (!embeddedImages.isEmpty() && embeddedTexts.isEmpty()
                     && embeddedFiles.isEmpty() && embeddedAudio.isEmpty())
                prompt = tr("Проанализируй прикреплённое изображение и выполни задачу.");
            else
                prompt = tr("Учти содержимое прикреплённых файлов и выполни задачу.");
        }
        if (!pathOnly.isEmpty())
        {
            prompt += QString::fromUtf8(
                "\n\nAlso attached (paths for tools; content not inlined):\n");
            foreach (const QString &path, pathOnly)
                prompt += QString::fromUtf8("- %1\n").arg(path);
        }
        QJsonArray parts;
        QJsonObject textPart;
        textPart.insert(QString::fromUtf8("type"), QString::fromUtf8("text"));
        textPart.insert(QString::fromUtf8("text"), prompt);
        parts.append(textPart);
        for (int i = 0; i < mediaParts.size(); ++i)
            parts.append(mediaParts.at(i));
        if (message.trimmed().isEmpty())
            message = prompt;
        _composer->clear();
        _composerAttachments.clear();
        updateComposerAttachmentsUi();
        sendRootComposerMessage(message, parts);
        return;
    }

    _composer->clear();
    _composerAttachments.clear();
    updateComposerAttachmentsUi();
    sendRootComposerMessage(message);
}

//----------------------------------------------

void MainWindow::sltComposerAttachPlus()
{
    if (!hasOpenProject())
        return;

    const LlmProfile profile = profileForComposerTarget();
    const ModelCapabilities caps =
        ModelCapabilityService::instance()->cachedOrHeuristic(profile);
    QStringList filters;
    if (caps.acceptsImage() || caps.allowsImageReference()
        || profile.modality == WorkerTaskKindImageGeneration
        || profile.modality == WorkerTaskKindVideoGeneration)
        filters << tr("Изображения (*.png *.jpg *.jpeg *.webp *.gif *.bmp)");
    if (caps.acceptsAudio() || profile.modality == WorkerTaskKindSpeech
        || profile.modality == WorkerTaskKindAudioGeneration)
        filters << tr("Аудио (*.wav *.mp3 *.m4a *.ogg *.flac *.aac *.webm)");
    if (caps.acceptsVideo() || profile.modality == WorkerTaskKindVideoGeneration)
        filters << tr("Видео (*.mp4 *.webm *.mov *.mkv *.avi)");
    filters << tr("Все файлы (*.*)");
    if (filters.isEmpty())
        filters << tr("Все файлы (*.*)");

    const QStringList files = pickComposerAttachments(
        filters.join(QString::fromUtf8(";;")),
        tr("Прикрепить файлы"));
    foreach (const QString &f, files)
    {
        if (!_composerAttachments.contains(f))
            _composerAttachments.append(f);
    }
    updateComposerAttachmentsUi();
}

//----------------------------------------------

void MainWindow::sltComposerAttachImage()
{
    sltComposerAttachPlus();
}

//----------------------------------------------

void MainWindow::sltComposerAttachAudio()
{
    sltComposerAttachPlus();
}

//----------------------------------------------

void MainWindow::sltComposerAttachVideo()
{
    sltComposerAttachPlus();
}

//----------------------------------------------

void MainWindow::sltComposerAttachFile()
{
    sltComposerAttachPlus();
}

//----------------------------------------------

void MainWindow::sltComposerClearAttachments()
{
    _composerAttachments.clear();
    updateComposerAttachmentsUi();
}

//----------------------------------------------

void MainWindow::sltComposerRemoveAttachment(int index)
{
    if (index < 0 || index >= _composerAttachments.size())
        return;
    _composerAttachments.removeAt(index);
    updateComposerAttachmentsUi();
}

//----------------------------------------------

void MainWindow::sltComposerFilesDropped(const QStringList &absolutePaths)
{
    addComposerAbsoluteFiles(absolutePaths);
}

//----------------------------------------------

void MainWindow::addComposerAbsoluteFiles(const QStringList &absolutePaths)
{
    if (!hasOpenProject() || !_workspace)
        return;
    foreach (const QString &abs, absolutePaths)
    {
        QString error;
        const QString rel = importAttachmentIntoProject(abs, &error);
        if (rel.isEmpty())
        {
            if (statusBar() && !error.isEmpty())
                statusBar()->showMessage(error);
            continue;
        }
        if (!_composerAttachments.contains(rel))
            _composerAttachments.append(rel);
    }
    updateComposerAttachmentsUi();
}

//----------------------------------------------

bool MainWindow::isImageAttachmentPath(QString path)
{
    const QString lower = path.toLower();
    return lower.endsWith(QString::fromUtf8(".png"))
           || lower.endsWith(QString::fromUtf8(".jpg"))
           || lower.endsWith(QString::fromUtf8(".jpeg"))
           || lower.endsWith(QString::fromUtf8(".webp"))
           || lower.endsWith(QString::fromUtf8(".gif"))
           || lower.endsWith(QString::fromUtf8(".bmp"));
}

//----------------------------------------------

bool MainWindow::isAudioAttachmentPath(QString path)
{
    const QString lower = path.toLower();
    return lower.endsWith(QString::fromUtf8(".wav"))
           || lower.endsWith(QString::fromUtf8(".mp3"))
           || lower.endsWith(QString::fromUtf8(".mpeg"))
           || lower.endsWith(QString::fromUtf8(".mpga"))
           || lower.endsWith(QString::fromUtf8(".m4a"))
           || lower.endsWith(QString::fromUtf8(".ogg"))
           || lower.endsWith(QString::fromUtf8(".flac"))
           || lower.endsWith(QString::fromUtf8(".aac"))
           || lower.endsWith(QString::fromUtf8(".webm"))
           || lower.endsWith(QString::fromUtf8(".opus"));
}

//----------------------------------------------

QString MainWindow::audioFormatForPath(QString path)
{
    const QString lower = path.toLower();
    if (lower.endsWith(QString::fromUtf8(".wav")))
        return QString::fromUtf8("wav");
    if (lower.endsWith(QString::fromUtf8(".mp3"))
        || lower.endsWith(QString::fromUtf8(".mpeg"))
        || lower.endsWith(QString::fromUtf8(".mpga")))
        return QString::fromUtf8("mp3");
    if (lower.endsWith(QString::fromUtf8(".m4a")))
        return QString::fromUtf8("m4a");
    if (lower.endsWith(QString::fromUtf8(".ogg")))
        return QString::fromUtf8("ogg");
    if (lower.endsWith(QString::fromUtf8(".flac")))
        return QString::fromUtf8("flac");
    if (lower.endsWith(QString::fromUtf8(".aac")))
        return QString::fromUtf8("aac");
    if (lower.endsWith(QString::fromUtf8(".webm")))
        return QString::fromUtf8("webm");
    if (lower.endsWith(QString::fromUtf8(".opus")))
        return QString::fromUtf8("opus");
    return QString::fromUtf8("wav");
}

//----------------------------------------------

QString MainWindow::encodeProjectAudioBase64(QString relativePath, QString *formatOut,
                                            QString *error) const
{
    if (formatOut)
        *formatOut = audioFormatForPath(relativePath);
    if (!_workspace || !_workspace->isSet())
    {
        if (error)
            *error = tr("Проект не открыт.");
        return QString();
    }
    QString abs;
    QString resolveError;
    if (!_workspace->resolvePath(relativePath, true, false, &abs, &resolveError)
        || abs.isEmpty())
    {
        if (error)
            *error = resolveError.isEmpty()
                ? tr("Файл не найден: %1").arg(relativePath)
                : resolveError;
        return QString();
    }
    QFile file(abs);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = tr("Не удалось прочитать %1: %2")
                         .arg(relativePath, file.errorString());
        return QString();
    }
    const QByteArray bytes = file.readAll();
    file.close();
    if (bytes.isEmpty())
    {
        if (error)
            *error = tr("Пустой аудиофайл: %1").arg(relativePath);
        return QString();
    }
    return QString::fromLatin1(bytes.toBase64());
}

//----------------------------------------------

bool MainWindow::isPdfAttachmentPath(QString path)
{
    return path.toLower().endsWith(QString::fromUtf8(".pdf"));
}

//----------------------------------------------

bool MainWindow::isTextLikeAttachmentPath(QString path)
{
    const QString lower = path.toLower();
    static const char *kExt[] = {
        ".txt", ".md", ".markdown", ".json", ".jsonl", ".csv", ".tsv",
        ".xml", ".yaml", ".yml", ".toml", ".ini", ".cfg", ".conf",
        ".log", ".py", ".pyw", ".js", ".ts", ".tsx", ".jsx", ".mjs", ".cjs",
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".cs", ".java",
        ".go", ".rs", ".rb", ".php", ".swift", ".kt", ".kts", ".scala",
        ".sql", ".html", ".htm", ".css", ".scss", ".less", ".vue", ".svelte",
        ".sh", ".bash", ".zsh", ".ps1", ".bat", ".cmd", ".cmake", ".make",
        ".mk", ".gradle", ".properties", ".env", ".gitignore", ".dockerfile",
        ".pro", ".pri", ".qml", ".qrc", ".ui", ".svg", ".tex", ".rst",
        ".diff", ".patch", ".r", ".m", ".mm", 0
    };
    for (int i = 0; kExt[i]; ++i)
    {
        if (lower.endsWith(QString::fromUtf8(kExt[i])))
            return true;
    }
    // Файлы без расширения / с редким расширением — по MIME text/*
    return false;
}

//----------------------------------------------

QString MainWindow::readProjectTextAttachment(QString relativePath, int maxBytes,
                                             QString *error) const
{
    if (!_workspace || !_workspace->isSet())
    {
        if (error)
            *error = tr("Проект не открыт.");
        return QString();
    }
    QString abs;
    QString resolveError;
    if (!_workspace->resolvePath(relativePath, true, false, &abs, &resolveError)
        || abs.isEmpty())
    {
        if (error)
            *error = resolveError.isEmpty()
                ? tr("Файл не найден: %1").arg(relativePath)
                : resolveError;
        return QString();
    }
    QFile file(abs);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = tr("Не удалось прочитать %1: %2")
                         .arg(relativePath, file.errorString());
        return QString();
    }
    QByteArray bytes = file.read(qMax(1, maxBytes) + 1);
    file.close();
    if (bytes.isEmpty())
    {
        if (error)
            *error = tr("Пустой файл: %1").arg(relativePath);
        return QString();
    }
    const bool truncated = bytes.size() > maxBytes;
    if (truncated)
        bytes = bytes.left(maxBytes);
    // Бинарный мусор (нулевые байты) — не вшиваем как текст.
    if (bytes.contains('\0'))
    {
        if (error)
            *error = tr("Бинарный файл, не встраивается как текст: %1").arg(relativePath);
        return QString();
    }
    QString text = ProjectWorkspace::decodeText(bytes);
    if (truncated)
        text += QString::fromUtf8("\n\n…[truncated]…");
    return text;
}

//----------------------------------------------

QString MainWindow::encodeProjectFileDataUrl(QString relativePath, QString *error) const
{
    if (!_workspace || !_workspace->isSet())
    {
        if (error)
            *error = tr("Проект не открыт.");
        return QString();
    }
    QString abs;
    QString resolveError;
    if (!_workspace->resolvePath(relativePath, true, false, &abs, &resolveError)
        || abs.isEmpty())
    {
        if (error)
            *error = resolveError.isEmpty()
                ? tr("Файл не найден: %1").arg(relativePath)
                : resolveError;
        return QString();
    }
    QFile file(abs);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = tr("Не удалось прочитать %1: %2")
                         .arg(relativePath, file.errorString());
        return QString();
    }
    const QByteArray bytes = file.readAll();
    file.close();
    if (bytes.isEmpty())
    {
        if (error)
            *error = tr("Пустой файл: %1").arg(relativePath);
        return QString();
    }
    QMimeDatabase mimeDb;
    QString mime = mimeDb.mimeTypeForFile(abs).name();
    if (mime.isEmpty() || mime == QString::fromUtf8("application/octet-stream"))
    {
        if (isPdfAttachmentPath(relativePath))
            mime = QString::fromUtf8("application/pdf");
        else if (isImageAttachmentPath(relativePath))
            mime = QString::fromUtf8("image/png");
        else
            mime = QString::fromUtf8("application/octet-stream");
    }
    return QString::fromUtf8("data:%1;base64,%2")
        .arg(mime, QString::fromLatin1(bytes.toBase64()));
}

//----------------------------------------------

QString MainWindow::encodeProjectImageDataUrl(QString relativePath, QString *error) const
{
    if (!_workspace || !_workspace->isSet())
    {
        if (error)
            *error = tr("Проект не открыт.");
        return QString();
    }
    QString abs;
    QString resolveError;
    if (!_workspace->resolvePath(relativePath, true, false, &abs, &resolveError)
        || abs.isEmpty())
    {
        if (error)
            *error = resolveError.isEmpty()
                ? tr("Файл не найден: %1").arg(relativePath)
                : resolveError;
        return QString();
    }
    QFile file(abs);
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = tr("Не удалось прочитать %1: %2")
                         .arg(relativePath, file.errorString());
        return QString();
    }
    QByteArray bytes = file.readAll();
    file.close();
    if (bytes.isEmpty())
    {
        if (error)
            *error = tr("Пустое изображение: %1").arg(relativePath);
        return QString();
    }

    QString mime = QString::fromUtf8("image/jpeg");
    // Сжимаем крупные кадры: иначе base64 + копии тела на 32-bit валят процесс.
    QImage image;
    if (image.loadFromData(bytes))
    {
        const int kMaxSide = 2048;
        if (image.width() > kMaxSide || image.height() > kMaxSide)
            image = image.scaled(kMaxSide, kMaxSide, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
        QByteArray encoded;
        QBuffer buffer(&encoded);
        buffer.open(QIODevice::WriteOnly);
        int quality = 85;
        if (!image.save(&buffer, "JPEG", quality))
        {
            buffer.close();
            encoded.clear();
            buffer.open(QIODevice::WriteOnly);
            if (image.save(&buffer, "PNG"))
                mime = QString::fromUtf8("image/png");
        }
        buffer.close();
        if (!encoded.isEmpty())
            bytes = encoded;
        else
        {
            QMimeDatabase mimeDb;
            mime = mimeDb.mimeTypeForFile(abs).name();
            if (mime.isEmpty() || !mime.startsWith(QString::fromUtf8("image/")))
                mime = QString::fromUtf8("image/png");
        }
    }
    else
    {
        QMimeDatabase mimeDb;
        mime = mimeDb.mimeTypeForFile(abs).name();
        if (mime.isEmpty() || !mime.startsWith(QString::fromUtf8("image/")))
            mime = QString::fromUtf8("image/png");
    }

    return QString::fromUtf8("data:%1;base64,%2")
        .arg(mime, QString::fromLatin1(bytes.toBase64()));
}

//----------------------------------------------

QStringList MainWindow::pickComposerAttachments(QString filter, QString title)
{
    QStringList result;
    if (!hasOpenProject() || !_workspace)
        return result;
    const QStringList selected = QFileDialog::getOpenFileNames(this, title, QString(), filter);
    foreach (const QString &abs, selected)
    {
        QString error;
        const QString rel = importAttachmentIntoProject(abs, &error);
        if (rel.isEmpty())
        {
            if (statusBar())
                statusBar()->showMessage(error);
            continue;
        }
        result.append(rel);
    }
    return result;
}

//----------------------------------------------

QString MainWindow::importAttachmentIntoProject(QString absolutePath, QString *error)
{
    if (!_workspace || !_workspace->isSet())
    {
        if (error)
            *error = tr("Проект не открыт.");
        return QString();
    }
    const QString abs = QFileInfo(absolutePath).absoluteFilePath();
    if (_workspace->contains(abs))
        return _workspace->toRelative(abs);

    if (!_workspace->ensureTempDir())
    {
        if (error)
            *error = tr("Не удалось создать tmp проекта.");
        return QString();
    }
    const QString uploads = _workspace->tempDir() + QString::fromUtf8("/uploads");
    QDir().mkpath(uploads);
    const QString destName = QFileInfo(abs).fileName();
    QString dest = uploads + QChar('/') + destName;
    int n = 1;
    while (QFileInfo::exists(dest))
    {
        dest = uploads + QChar('/') + QFileInfo(abs).completeBaseName()
               + QString::fromUtf8("_%1.").arg(n++) + QFileInfo(abs).suffix();
    }
    if (!QFile::copy(abs, dest))
    {
        if (error)
            *error = tr("Не удалось скопировать файл в проект.");
        return QString();
    }
    return _workspace->toRelative(dest);
}

//----------------------------------------------

LlmProfile MainWindow::profileForComposerTarget() const
{
    LlmProfile profile;
    ChatSession *session = rootSession();
    Worker *worker = session ? session->worker() : 0;
    if (worker && !worker->llmProfileId().trimmed().isEmpty())
        profile = AppSettings::instance()->profileById(worker->llmProfileId());
    if (profile.id.isEmpty() && worker)
        profile = AppSettings::instance()->profileForWorkerType(worker->type());
    if (profile.id.isEmpty())
        profile = AppSettings::instance()->profileForWorkerType(WorkerTypeManager);
    return profile;
}

//----------------------------------------------

QPixmap MainWindow::composerThumbForPath(QString relativePath) const
{
    if (!_workspace || !_workspace->isSet() || !isImageAttachmentPath(relativePath))
        return QPixmap();
    QString abs;
    QString error;
    if (!_workspace->resolvePath(relativePath, true, false, &abs, &error) || abs.isEmpty())
        return QPixmap();
    QPixmap pix;
    if (!pix.load(abs))
        return QPixmap();
    return pix;
}

//----------------------------------------------

void MainWindow::updateComposerAttachmentsUi()
{
    if (!_composerBar)
        return;
    QList<QPixmap> thumbs;
    thumbs.reserve(_composerAttachments.size());
    foreach (const QString &path, _composerAttachments)
        thumbs.append(composerThumbForPath(path));
    _composerBar->setAttachments(_composerAttachments, thumbs);
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

    _settingsPage = new SettingsPage(SettingsKindDefaults, this);
    _projectSettingsPage = new SettingsPage(SettingsKindProject, this);
    _settingsPage->setWorkerManager(_workers);
    _projectSettingsPage->setWorkerManager(_workers);
    _inspectCheck = _settingsPage->inspectCheck();

    if (_inspectCheck)
        connect(_inspectCheck, SIGNAL(toggled(bool)),
                this, SLOT(sltInspectToggled(bool)));
    connect(_settingsPage, SIGNAL(appearanceApplied()),
            this, SLOT(sltAppearanceApplied()));
    connect(_settingsPage, SIGNAL(removeWorkersForProfile(QString)),
            this, SLOT(sltRemoveWorkersForProfile(QString)));
    // Один канал: AppSettings уже debounce'ит profilesChanged.
    connect(AppSettings::instance(), SIGNAL(profilesChanged()),
            this, SLOT(sltProfilesChanged()));

    QToolBar *toolbar = addToolBar(L("toolbar.title"));
    toolbar->setObjectName(QString::fromUtf8("mainToolBar"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(24, 24));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    _clearAction = toolbar->addAction(AppIcons::clearAll(), L("toolbar.clear"));
    _clearAction->setToolTip(L("toolbar.clearTip"));
    connect(_clearAction, SIGNAL(triggered()), this, SLOT(sltClear()));

    _showChatAction = toolbar->addAction(AppIcons::mainChat(), L("menu.window.mainChat"));
    _showChatAction->setToolTip(L("toolbar.mainChatTip"));
    connect(_showChatAction, SIGNAL(triggered()), this, SLOT(sltShowChat()));

    _showGraphAction = toolbar->addAction(AppIcons::workerGraph(), L("menu.window.team"));
    _showGraphAction->setToolTip(L("toolbar.teamTip"));
    connect(_showGraphAction, SIGNAL(triggered()), this, SLOT(sltShowGraph()));

    _showJournalAction = toolbar->addAction(AppIcons::journal(), L("menu.window.journal"));
    _showJournalAction->setToolTip(L("toolbar.journalTip"));
    connect(_showJournalAction, SIGNAL(triggered()), this, SLOT(sltShowJournal()));

    toolbar->addSeparator();

    _prevConfirmAction = toolbar->addAction(AppIcons::prevConfirm(),
                                            L("toolbar.prevConfirm"));
    _prevConfirmAction->setToolTip(L("toolbar.prevConfirmTip"));
    _prevConfirmAction->setEnabled(false);
    connect(_prevConfirmAction, SIGNAL(triggered()), this, SLOT(sltPrevConfirmation()));

    _nextConfirmAction = toolbar->addAction(AppIcons::nextConfirm(),
                                            L("toolbar.nextConfirm"));
    _nextConfirmAction->setToolTip(L("toolbar.nextConfirmTip"));
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

    _composerBar = new ComposerInput(central);
    _composer = _composerBar->editor();
    if (_composer)
        _composer->installEventFilter(this);
    _composerBar->setPlaceholder(L("composer.placeholder"));
    connect(_composerBar, SIGNAL(sendClicked()), this, SLOT(sltComposerSend()));
    connect(_composerBar, SIGNAL(stopClicked()), this, SLOT(sltComposerStop()));
    connect(_composerBar, SIGNAL(plusClicked()), this, SLOT(sltComposerAttachPlus()));
    connect(_composerBar, SIGNAL(removeAttachmentRequested(int)),
            this, SLOT(sltComposerRemoveAttachment(int)));
    connect(_composerBar, SIGNAL(filesDropped(QStringList)),
            this, SLOT(sltComposerFilesDropped(QStringList)));

    QVBoxLayout *root = new QVBoxLayout(central);
    // Единый визуальный шаг между доками / вкладками / композером.
    const int gap = 12;
    root->setContentsMargins(gap, gap, gap, gap);
    root->setSpacing(gap);
    root->addWidget(_tabs, 1);
    root->addWidget(_composerBar);

    _projectDock = new ProjectTreeDock(this);
    addDockWidget(Qt::LeftDockWidgetArea, _projectDock);

    _graphThumbDock = new QDockWidget(tr("Команда"), this);
    _graphThumbDock->setObjectName(QString::fromUtf8("graphThumbDock"));
    _graphThumbDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    _graphThumbDock->setFeatures(QDockWidget::DockWidgetMovable
                                 | QDockWidget::DockWidgetFloatable);
    DockTitleBar *graphTitle = new DockTitleBar(tr("Команда"), _graphThumbDock);
    graphTitle->setObjectName(QString::fromUtf8("graphThumbTitleBar"));
    _graphThumbDock->setTitleBarWidget(graphTitle);
    QWidget *graphHost = new QWidget(_graphThumbDock);
    graphHost->setObjectName(QString::fromUtf8("graphThumbDockContent"));
    graphHost->setAttribute(Qt::WA_StyledBackground, true);
    _graphThumb = new GraphThumb(graphHost);
    QVBoxLayout *graphLay = new QVBoxLayout(graphHost);
    graphLay->setContentsMargins(0, 0, 0, 0);
    graphLay->setSpacing(0);
    graphLay->addWidget(_graphThumb, 1);
    _graphThumbDock->setWidget(graphHost);
    connect(_graphThumb, SIGNAL(clicked()), this, SLOT(sltShowGraph()));
    addDockWidget(Qt::LeftDockWidgetArea, _graphThumbDock);
    // По умолчанию — под деревом файлов.
    splitDockWidget(_projectDock, _graphThumbDock, Qt::Vertical);
    // Дефолтные размеры только если ещё нет сохранённых в настройках.
    {
        AppSettings *settings = AppSettings::instance();
        const bool haveSplit = settings->leftDockWidth() > 0
                               || (settings->projectDockHeight() > 0
                                   && settings->graphThumbDockHeight() > 0);
        if (settings->mainWindowState().isEmpty() && !haveSplit)
        {
            QList<QDockWidget *> docks;
            docks << _projectDock << _graphThumbDock;
            QList<int> sizes;
            sizes << 260 << 140;
            resizeDocks(docks, sizes, Qt::Vertical);
        }
    }

    createGraphWindow();
    createJournalWindow();
    applyAppStyle();
    updateComposerState();
    updateGraphThumb();

    _progressBar = new QProgressBar(this);
    _progressBar->setMaximumWidth(220);
    _progressBar->setMaximumHeight(14);
    _progressBar->setTextVisible(true);
    _progressBar->setRange(0, 100);
    _progressBar->setValue(0);
    _progressBar->hide();
    statusBar()->addPermanentWidget(_progressBar);

    _workTimeLabel = new QLabel(statusBar());
    _changedFilesLabel = new QLabel(statusBar());
    _addedLinesLabel = new QLabel(statusBar());
    _removedLinesLabel = new QLabel(statusBar());
    _workTimeLabel->setAlignment(Qt::AlignCenter);
    _changedFilesLabel->setAlignment(Qt::AlignCenter);
    _addedLinesLabel->setAlignment(Qt::AlignCenter);
    _removedLinesLabel->setAlignment(Qt::AlignCenter);
    statusBar()->addPermanentWidget(_workTimeLabel);
    statusBar()->addPermanentWidget(_changedFilesLabel);
    statusBar()->addPermanentWidget(_addedLinesLabel);
    statusBar()->addPermanentWidget(_removedLinesLabel);

    _projectMeterTimer = new QTimer(this);
    _projectMeterTimer->setInterval(1000);
    connect(_projectMeterTimer, SIGNAL(timeout()),
            this, SLOT(sltRefreshProjectMeters()));
    _projectMeterTimer->start();
    styleStatusPills();
    sltRefreshProjectMeters();
}

//----------------------------------------------

void MainWindow::setupMenus()
{
    _fileMenu = menuBar()->addMenu(L("menu.file"));

    _openProjectAction = _fileMenu->addAction(L("menu.file.open"));
    _openProjectAction->setShortcut(QKeySequence::Open);
    connect(_openProjectAction, SIGNAL(triggered()), this, SLOT(sltOpenProject()));

    _newProjectAction = _fileMenu->addAction(L("menu.file.new"));
    connect(_newProjectAction, SIGNAL(triggered()), this, SLOT(sltNewProject()));

    _openLastAction = _fileMenu->addAction(L("menu.file.openLast"));
    connect(_openLastAction, SIGNAL(triggered()), this, SLOT(sltOpenLastProject()));

    _recentMenu = _fileMenu->addMenu(L("menu.file.recent"));
    connect(_recentMenu, SIGNAL(aboutToShow()), this, SLOT(sltRebuildRecentMenu()));
    rebuildRecentProjectsMenu();

    _fileMenu->addSeparator();

    _fileClearAction = _fileMenu->addAction(L("menu.file.clear"));
    connect(_fileClearAction, SIGNAL(triggered()), this, SLOT(sltClear()));

    _fileMenu->addSeparator();

    _balanceAction = _fileMenu->addAction(L("menu.file.balance"));
    connect(_balanceAction, SIGNAL(triggered()), this, SLOT(sltShowBalance()));

    _fileMenu->addSeparator();

    _windowSettingsAction = _fileMenu->addAction(L("menu.window.settings"));
    connect(_windowSettingsAction, SIGNAL(triggered()), this, SLOT(sltShowSettings()));

    _fileMenu->addSeparator();

    _quitAction = _fileMenu->addAction(L("menu.file.quit"));
    _quitAction->setShortcut(QKeySequence::Quit);
    connect(_quitAction, SIGNAL(triggered()), this, SLOT(close()));

    _windowMenu = menuBar()->addMenu(L("menu.window"));

    _windowChatAction = _windowMenu->addAction(L("menu.window.mainChat"));
    connect(_windowChatAction, SIGNAL(triggered()), this, SLOT(sltShowChat()));

    _windowGraphAction = _windowMenu->addAction(L("menu.window.team"));
    connect(_windowGraphAction, SIGNAL(triggered()), this, SLOT(sltShowGraph()));

    _windowJournalAction = _windowMenu->addAction(L("menu.window.journal"));
    connect(_windowJournalAction, SIGNAL(triggered()), this, SLOT(sltShowJournal()));

    _projectMenu = menuBar()->addMenu(L("menu.project"));

    _projectSettingsAction = _projectMenu->addAction(L("menu.project.settings"));
    connect(_projectSettingsAction, SIGNAL(triggered()),
            this, SLOT(sltShowProjectSettings()));

    _helpMenu = menuBar()->addMenu(L("menu.help"));
    _helpStatsAction = _helpMenu->addAction(L("menu.help.stats"));
    connect(_helpStatsAction, SIGNAL(triggered()), this, SLOT(sltShowStatistics()));
    _helpMenu->addSeparator();
    _helpAboutAction = _helpMenu->addAction(L("menu.help.about"));
    connect(_helpAboutAction, SIGNAL(triggered()), this, SLOT(sltShowAbout()));
}

//----------------------------------------------

void MainWindow::retranslateUi()
{
    if (_fileMenu)
        _fileMenu->setTitle(L("menu.file"));
    if (_openProjectAction)
        _openProjectAction->setText(L("menu.file.open"));
    if (_newProjectAction)
        _newProjectAction->setText(L("menu.file.new"));
    if (_openLastAction)
        _openLastAction->setText(L("menu.file.openLast"));
    if (_recentMenu)
        _recentMenu->setTitle(L("menu.file.recent"));
    if (_fileClearAction)
        _fileClearAction->setText(L("menu.file.clear"));
    if (_balanceAction)
        _balanceAction->setText(L("menu.file.balance"));
    if (_quitAction)
        _quitAction->setText(L("menu.file.quit"));
    if (_windowMenu)
        _windowMenu->setTitle(L("menu.window"));
    if (_windowChatAction)
        _windowChatAction->setText(L("menu.window.mainChat"));
    if (_windowGraphAction)
        _windowGraphAction->setText(L("menu.window.team"));
    if (_windowJournalAction)
        _windowJournalAction->setText(L("menu.window.journal"));
    if (_windowSettingsAction)
        _windowSettingsAction->setText(L("menu.window.settings"));
    if (_projectMenu)
        _projectMenu->setTitle(L("menu.project"));
    if (_projectSettingsAction)
        _projectSettingsAction->setText(L("menu.project.settings"));
    if (_helpMenu)
        _helpMenu->setTitle(L("menu.help"));
    if (_helpStatsAction)
        _helpStatsAction->setText(L("menu.help.stats"));
    if (_helpAboutAction)
        _helpAboutAction->setText(L("menu.help.about"));
    if (_clearAction)
    {
        _clearAction->setText(L("toolbar.clear"));
        _clearAction->setToolTip(L("toolbar.clearTip"));
    }
    if (_showChatAction)
    {
        _showChatAction->setText(L("menu.window.mainChat"));
        _showChatAction->setToolTip(L("toolbar.mainChatTip"));
    }
    if (_showGraphAction)
    {
        _showGraphAction->setText(L("menu.window.team"));
        _showGraphAction->setToolTip(L("toolbar.teamTip"));
    }
    if (_showJournalAction)
    {
        _showJournalAction->setText(L("menu.window.journal"));
        _showJournalAction->setToolTip(L("toolbar.journalTip"));
    }
    if (_prevConfirmAction)
    {
        _prevConfirmAction->setText(L("toolbar.prevConfirm"));
        _prevConfirmAction->setToolTip(L("toolbar.prevConfirmTip"));
    }
    if (_nextConfirmAction)
    {
        _nextConfirmAction->setText(L("toolbar.nextConfirm"));
        _nextConfirmAction->setToolTip(L("toolbar.nextConfirmTip"));
    }
    if (_composerBar)
        _composerBar->retranslateUi();
    QToolBar *toolbar = findChild<QToolBar *>(QString::fromUtf8("mainToolBar"));
    if (toolbar)
        toolbar->setWindowTitle(L("toolbar.title"));
    if (_settingsPage)
        _settingsPage->retranslateUi();
    if (_projectSettingsPage)
        _projectSettingsPage->retranslateUi();
    if (_tabs)
    {
        if (_graph)
        {
            const int gi = _tabs->indexOf(_graph);
            if (gi >= 0)
                _tabs->setTabText(gi, L("menu.window.team"));
        }
        if (_settingsPage)
        {
            const int si = _tabs->indexOf(_settingsPage);
            if (si >= 0)
                _tabs->setTabText(si, L("menu.window.settings"));
        }
        if (_projectSettingsPage)
        {
            const int pi = _tabs->indexOf(_projectSettingsPage);
            if (pi >= 0)
                _tabs->setTabText(pi, L("menu.project.settings"));
        }
    }
    if (_graph && _workers)
    {
        _graph->rebuild(_workers);
        _graphDirty = false;
    }
    foreach (ChatSession *session, _sessions)
    {
        if (!session || !session->widget())
            continue;
        Worker *worker = session->worker();
        if (worker)
        {
            session->widget()->setAuthorLabels(
                tr("Пользователь"), workerWindowTitle(worker));
            if (_journal && session->agent())
                _journal->setAgentLabel(session->agent(), workerWindowTitle(worker));
        }
        session->widget()->retranslateUi();
        updateSessionTabTitle(session);
    }
    if (_journal)
    {
        _journal->retranslateUi();
        if (_tabs)
        {
            const int ji = _tabs->indexOf(_journal);
            if (ji >= 0)
                _tabs->setTabText(ji, L("journal.title"));
        }
    }
    if (_projectDock)
        _projectDock->retranslateUi();
    if (_graphThumbDock)
    {
        _graphThumbDock->setWindowTitle(tr("Команда"));
        DockTitleBar *title = qobject_cast<DockTitleBar *>(
            _graphThumbDock->titleBarWidget());
        if (title)
            title->setTitle(tr("Команда"));
    }
    // Placeholder/кнопки композера зависят от языка и состояния сессии.
    updateComposerState();
}

//----------------------------------------------

void MainWindow::sltLocaleChanged(const QString &code)
{
    Q_UNUSED(code);
    retranslateUi();
    sltRefreshProjectMeters();
    if (_workers)
        _workers->refreshAllPrompts();
}

//----------------------------------------------

void MainWindow::styleStatusPills()
{
    if (!_workTimeLabel || !_changedFilesLabel || !_addedLinesLabel || !_removedLinesLabel)
        return;
    AppSettings *s = AppSettings::instance();
    const QString border = s->borderColor().name();
    const QString text = s->textColor().name();
    const QString bg = s->panelColor().name();
    const QString frame = QString::fromUtf8(
        "QLabel { color:%1; background:%2; border:1px solid %3;"
        " border-radius:6px; padding:2px 8px; }");
    _workTimeLabel->setStyleSheet(frame.arg(text, bg, border));
    _changedFilesLabel->setStyleSheet(frame.arg(text, bg, border));
    const QString added = s->isLightTheme()
                          ? QString::fromUtf8("#15803d")
                          : QString::fromUtf8("#6a9955");
    const QString removed = s->isLightTheme()
                            ? QString::fromUtf8("#b91c1c")
                            : QString::fromUtf8("#e07a5f");
    _addedLinesLabel->setStyleSheet(frame.arg(added, bg, border));
    _removedLinesLabel->setStyleSheet(frame.arg(removed, bg, border));
}

//----------------------------------------------

void MainWindow::sltRefreshProjectMeters()
{
    if (!_workTimeLabel || !_changedFilesLabel || !_addedLinesLabel || !_removedLinesLabel)
        return;
    const bool open = hasOpenProject();
    _workTimeLabel->setVisible(open);
    _changedFilesLabel->setVisible(open);
    _addedLinesLabel->setVisible(open);
    _removedLinesLabel->setVisible(open);
    if (!open)
        return;

    const qint64 ms = AppSettings::instance()->liveWorkMs(true);
    _workTimeLabel->setText(formatWorkClock(ms));
    _workTimeLabel->setToolTip(
        tr("Время работы проекта: запросы к модели и вызовы функций"));

    const FileLineDelta delta = FileChangeStore::instance()->totals();
    const int files = FileChangeStore::instance()->changedFileCount();
    _changedFilesLabel->setText(QString::number(files));
    _changedFilesLabel->setToolTip(tr("Изменённых файлов: %1").arg(files));
    _addedLinesLabel->setText(QString::fromUtf8("+%1").arg(delta.added));
    _addedLinesLabel->setToolTip(tr("Добавлено строк: %1").arg(delta.added));
    _removedLinesLabel->setText(QString::fromUtf8("−%1").arg(delta.removed));
    _removedLinesLabel->setToolTip(tr("Удалено строк: %1").arg(delta.removed));
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
    connect(chat, SIGNAL(consoleInputAnswered(QString,bool)),
            this, SLOT(sltConsoleInputAnswered(QString,bool)));
    connect(chat, SIGNAL(continuationAnswered(bool)),
            this, SLOT(sltContinuationAnswered(bool)));
    connect(chat, SIGNAL(subtreeBudgetAnswered(bool)),
            this, SLOT(sltSubtreeBudgetAnswered(bool)));

    session->setWidget(chat);
}

//----------------------------------------------

void MainWindow::applyClientToSession(ChatSession *session)
{
    if (!session || !session->client() || !session->agent())
        return;

    WorkerType type = WorkerTypeManager;
    LlmProfile profile;
    if (session->worker())
    {
        type = session->worker()->type();
        if (!session->worker()->llmProfileId().isEmpty())
            profile = AppSettings::instance()->profileById(session->worker()->llmProfileId());
    }
    if (profile.id.isEmpty())
        profile = AppSettings::instance()->profileForWorkerType(type);

    OpenAiCompatibleClient *client = qobject_cast<OpenAiCompatibleClient *>(session->client());
    if (client)
        client->setIdentity(profile.id, profile.name);
    session->client()->setBaseUrl(profile.baseUrl);
    session->client()->setModel(profile.model);
    session->client()->setApiKey(profile.apiKey);
    session->agent()->applyProfileLimits(profile);

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
        Worker *worker = _workers->createWorker(type, shot.id, session, 0, shot.task,
                                                shot.llmProfileId);
        worker->setTitle(shot.workerTitle);
        if (!shot.personName.isEmpty())
            worker->setPersonName(shot.personName);
        if (!shot.groupId.isEmpty())
            worker->setGroupId(shot.groupId);
        if (!shot.groupRole.isEmpty())
            worker->setGroupRole(groupRoleFromString(shot.groupRole));
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
    // groupId/role выставляются после createWorker→installTools — перевешиваем
    // инструменты и поднимаем сохранённый диалог группы.
    _workers->reinstallToolsAndResumeGroups();
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
    saveProjectUiState();
    if (_journal)
        _journal->flushPersist();
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
    const QString base = QString::fromUtf8("%1 %2")
                             .arg(QString::fromUtf8(UNIQUE_APP_NAME),
                                  QString::fromUtf8(UNIQUE_APP_VERSION));
    if (_workspace && _workspace->isSet())
        setWindowTitle(QString::fromUtf8("%1 — %2")
                           .arg(base, QDir::toNativeSeparators(_workspace->root())));
    else
        setWindowTitle(base);
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

QString MainWindow::tabKeyForWidget(QWidget *widget) const
{
    if (!widget)
        return QString();
    if (widget == _journal)
        return QString::fromUtf8("journal");
    if (widget == _graph)
        return QString::fromUtf8("graph");
    if (widget == _settingsPage)
        return QString::fromUtf8("settings");
    if (widget == _projectSettingsPage)
        return QString::fromUtf8("projectSettings");
    ChatSession *session = sessionByTabWidget(widget);
    if (session)
        return QString::fromUtf8("chat:") + session->id();
    return QString();
}

//----------------------------------------------

void MainWindow::openTabByKey(const QString &key, bool makeCurrent)
{
    const QString k = key.trimmed();
    if (k.isEmpty())
        return;
    if (k == QString::fromUtf8("journal"))
    {
        createJournalWindow();
        if (makeCurrent)
            ensureTabVisible(_journal, tr("Журнал"), AppIcons::journal());
        else if (_tabs && _tabs->indexOf(_journal) < 0)
            _tabs->addTab(_journal, AppIcons::journal(), tr("Журнал"));
        return;
    }
    if (k == QString::fromUtf8("graph"))
    {
        if (makeCurrent)
            ensureTabVisible(_graph, tr("Команда"), AppIcons::workerGraph());
        else if (_tabs && _tabs->indexOf(_graph) < 0)
            _tabs->addTab(_graph, AppIcons::workerGraph(), tr("Команда"));
        return;
    }
    if (k == QString::fromUtf8("settings"))
    {
        if (makeCurrent)
            ensureTabVisible(_settingsPage, tr("Настройки"));
        else if (_tabs && _tabs->indexOf(_settingsPage) < 0)
            _tabs->addTab(_settingsPage, tr("Настройки"));
        return;
    }
    if (k == QString::fromUtf8("projectSettings"))
    {
        if (makeCurrent)
            ensureTabVisible(_projectSettingsPage, tr("Настройки проекта"));
        else if (_tabs && _tabs->indexOf(_projectSettingsPage) < 0)
            _tabs->addTab(_projectSettingsPage, tr("Настройки проекта"));
        return;
    }
    if (k.startsWith(QString::fromUtf8("chat:")))
    {
        const QString id = k.mid(5);
        foreach (ChatSession *session, _sessions)
        {
            if (!session || session->id() != id || !session->widget() || !_tabs)
                continue;
            session->ensureContentLoaded();
            const QString title = (session->worker() && !session->worker()->isRoot())
                                  ? workerWindowTitle(session->worker())
                                  : session->title();
            int index = _tabs->indexOf(session->widget());
            if (index < 0)
                index = _tabs->addTab(session->widget(), iconForSession(session), title);
            if (makeCurrent)
                _tabs->setCurrentIndex(index);
            return;
        }
    }
}

//----------------------------------------------

void MainWindow::saveAppWindowLayout()
{
    AppSettings *settings = AppSettings::instance();
    settings->setMainWindowLayout(saveGeometry(), saveState());
    if (_projectDock && _graphThumbDock)
    {
        settings->setMainWindowDockSplit(_projectDock->width(),
                                         _projectDock->height(),
                                         _graphThumbDock->height());
    }
}

//----------------------------------------------

void MainWindow::restoreAppWindowLayout()
{
    if (_appWindowLayoutRestored)
        return;
    AppSettings *settings = AppSettings::instance();
    QByteArray geometry = settings->mainWindowGeometry();
    QByteArray state = settings->mainWindowState();
    // Миграция: старые проекты хранили геометрию в ___project___/ui.json.
    if (geometry.isEmpty() || state.isEmpty())
    {
        ProjectUiState legacy;
        if (ChatStore::loadUiState(&legacy))
        {
            if (geometry.isEmpty())
                geometry = legacy.geometry;
            if (state.isEmpty())
                state = legacy.windowState;
            if (!geometry.isEmpty() || !state.isEmpty())
                settings->setMainWindowLayout(geometry, state);
        }
    }
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    if (!state.isEmpty())
        restoreState(state);

    // Явные размеры сплиттеров левой колонки (поверх restoreState).
    if (_projectDock)
    {
        const int leftW = settings->leftDockWidth();
        if (leftW > 40)
        {
            QList<QDockWidget *> docks;
            docks << _projectDock;
            QList<int> sizes;
            sizes << leftW;
            resizeDocks(docks, sizes, Qt::Horizontal);
        }
    }
    if (_projectDock && _graphThumbDock)
    {
        const int projH = settings->projectDockHeight();
        const int graphH = settings->graphThumbDockHeight();
        if (projH > 40 && graphH > 40)
        {
            QList<QDockWidget *> docks;
            docks << _projectDock << _graphThumbDock;
            QList<int> sizes;
            sizes << projH << graphH;
            resizeDocks(docks, sizes, Qt::Vertical);
        }
    }
    _appWindowLayoutRestored = true;
}

//----------------------------------------------

void MainWindow::saveProjectUiState()
{
    saveAppWindowLayout();
    if (!hasOpenProject() || !_projectStoreEnabled)
        return;
    ProjectUiState ui;
    // Геометрия окна и доки — в настройках программы; в проекте только вкладки.
    if (_tabs)
    {
        for (int i = 0; i < _tabs->count(); ++i)
        {
            const QString key = tabKeyForWidget(_tabs->widget(i));
            if (!key.isEmpty())
                ui.openTabs.append(key);
        }
        ui.activePage = tabKeyForWidget(_tabs->currentWidget());
    }
    if (ui.activePage.isEmpty() && !_pendingActiveId.isEmpty())
        ui.activePage = QString::fromUtf8("chat:") + _pendingActiveId;
    ChatStore::saveUiState(ui);
}

//----------------------------------------------

void MainWindow::restoreProjectUiState(bool preferJournalIfEmpty)
{
    restoreAppWindowLayout();

    ProjectUiState ui;
    const bool haveUi = ChatStore::loadUiState(&ui);

    if (!haveUi || (ui.openTabs.isEmpty() && ui.activePage.isEmpty()))
    {
        if (preferJournalIfEmpty)
        {
            createJournalWindow();
            ensureTabVisible(_journal, tr("Журнал"), AppIcons::journal());
        }
        else
            applySessionGeometries();
        return;
    }

    // Сначала открываем сохранённые вкладки без жёсткого фокуса.
    foreach (const QString &key, ui.openTabs)
        openTabByKey(key, false);

    QString active = ui.activePage;
    if (active.isEmpty() && !_pendingActiveId.isEmpty())
        active = QString::fromUtf8("chat:") + _pendingActiveId;
    if (active.isEmpty() && preferJournalIfEmpty)
        active = QString::fromUtf8("journal");
    if (!active.isEmpty())
        openTabByKey(active, true);
    else if (_tabs && _tabs->count() > 0)
        _tabs->setCurrentIndex(0);
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
        _settingsPage->refreshPermissionsList();
    if (_projectSettingsPage)
    {
        _projectSettingsPage->setProjectPath(root);
        _projectSettingsPage->reloadAll();
    }
    if (root.isEmpty())
    {
        if (_progressDepth <= 0)
            statusBar()->showMessage(tr("Откройте или создайте проект"));
        updateProjectActions();
        return;
    }
    promptOrphanProjectPresets();
    if (_progressDepth <= 0)
        statusBar()->showMessage(tr("Проект: %1").arg(QDir::toNativeSeparators(root)));
    updateProjectActions();
}

//----------------------------------------------

void MainWindow::promptOrphanProjectPresets()
{
    AppSettings *s = AppSettings::instance();
    const QList<WorkerPreset> orphans = s->orphanProjectPresets();
    if (orphans.isEmpty())
        return;

    QStringList lines;
    foreach (const WorkerPreset &preset, orphans)
    {
        QString typeName = workerTypeToString(preset.type);
        lines << QString::fromUtf8("%1 · %2")
                     .arg(typeName, preset.profileId);
    }
    const QString text = tr(
        "В настройках проекта есть пресеты воркеров, ссылающиеся на профили "
        "моделей, которых нет в настройках программы:\n\n%1\n\n"
        "Удалить эти пресеты из настроек проекта?")
                             .arg(lines.join(QChar('\n')));
    const int answer = askStyledQuestion(
        tr("Отсутствующие профили моделей"), text,
        tr("Удалить"), tr("Оставить"));
    if (answer != QMessageBox::Yes)
        return;
    s->removeOrphanProjectPresets();
    if (_projectSettingsPage)
        _projectSettingsPage->reloadAll();
    if (_settingsPage)
        _settingsPage->reloadAll();
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
    if (_projectMenu)
    {
        const QList<QAction *> projectActions = _projectMenu->actions();
        for (int i = 0; i < projectActions.size(); ++i)
        {
            QAction *action = projectActions.at(i);
            if (action && !action->isSeparator())
                action->setEnabled(hasProject);
        }
    }
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
    if (session->widget())
        session->widget()->syncRunningState();
    refreshBusyTabs();
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
    const QString border = s->borderColor().name();
    const QString muted = s->mutedTextColor().name();
    const QString select = s->selectionColor().name();
    const QString scroll = s->scrollbarColor().name();
    const QString hover = s->isLightTheme()
                          ? QColor(s->windowColor()).darker(104).name()
                          : QColor(s->toolbarColor()).lighter(118).name();
    const QString chatBg = s->chatBackgroundColor().name();

    setStyleSheet(QString::fromUtf8(
        "QMainWindow { background:%1; color:%2; }"
        "QMainWindow::separator { background:%6; width:1px; height:1px; }"
        "QMenuBar { background:%1; color:%2; border:none;"
        "  border-bottom:1px solid %6; padding:2px 6px; }"
        "QMenuBar::item { background:transparent; padding:6px 10px; border-radius:6px; }"
        "QMenuBar::item:selected { background:%HOVER%; }"
        "QMenu { background:%3; color:%2; border:1px solid %6; border-radius:8px;"
        "  padding:6px; }"
        "QMenu::item { padding:7px 28px 7px 12px; border-radius:6px; }"
        "QMenu::item:selected { background:%7; }"
        "QMenu::item:disabled { color:%MUTED%; background:transparent; }"
        "QMenu::separator { height:1px; background:%6; margin:6px 8px; }"
        "QStatusBar { background:%1; color:%MUTED%; border:none;"
        "  border-top:1px solid %6; }"
        "QToolBar { background:%1; border:none; border-bottom:1px solid %6;"
        "  spacing:4px; padding:4px 8px; }"
        "QToolBar::separator { background:%6; width:1px; margin:6px 4px; }"
        "QToolButton { background:transparent; color:%2; border:none;"
        "  padding:6px; border-radius:8px; }"
        "QToolButton:hover { background:%HOVER%; }"
        "QToolButton:pressed { background:%7; }"
        "QToolButton:disabled { color:%MUTED%; }"
        "QDockWidget#projectDock, QDockWidget#graphThumbDock {"
        "  background:%3; color:%2; border:1px solid %6; border-radius:10px;"
        "  titlebar-close-icon: url(none); titlebar-normal-icon: url(none); }"
        "QWidget#projectDockContent, QWidget#graphThumbDockContent {"
        "  background:%3; border:none; }"
        "QTreeView { background:%3; color:%2; alternate-background-color:%1;"
        "  border:none; outline:none; }"
        "QTreeView::branch { background:transparent; image:none; border-image:none; }"
        "QTreeView::item { color:%2; padding:3px 4px; border-radius:6px; }"
        "QTreeView::item:selected { background:%7; color:%2; }"
        "QTreeView::item:hover { background:%HOVER%; }"
        "QListWidget { background:%3; color:%2; border:1px solid %6; border-radius:8px; }"
        "QListWidget::item { padding:4px 6px; border-radius:6px; }"
        "QListWidget::item:selected { background:%7; color:%2; }"
        "QGroupBox { color:%2; border:1px solid %6; border-radius:8px;"
        "  margin-top:12px; padding:16px 12px 12px 12px; background:%3;"
        "  font-weight:bold; }"
        "QGroupBox * { font-weight:normal; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left;"
        "  left:12px; padding:2px 8px; background-color:%3; color:%2;"
        "  border:1px solid %6; border-radius:6px; font-weight:bold; }"
        "QLabel { color:%2; }"
        "QLineEdit, QComboBox, QPlainTextEdit, QSpinBox, QTextEdit {"
        "  background:%5; color:%2; border:1px solid %6;"
        "  border-radius:8px; padding:6px 8px; selection-background-color:%7; }"
        "QComboBox::drop-down { border:none; width:22px; }"
        "QComboBox QAbstractItemView { background:%3; color:%2; border:1px solid %6;"
        "  border-radius:8px; selection-background-color:%7; }"
        "QPushButton { background:%3; color:%2; border:1px solid %6;"
        "  border-radius:8px; padding:6px 14px; }"
        "QPushButton:hover { background:%HOVER%; }"
        "QPushButton:pressed { background:%7; }"
        "QPushButton:disabled { color:%MUTED%; }"
        "QCheckBox { color:%2; spacing:8px; }"
        "QTabWidget::pane { border:1px solid %6; border-radius:10px;"
        "  background:%3; top:-1px; margin-top:-1px; }"
        "QTabBar { background:transparent; }"
        "QTabBar::tab { background:transparent; color:%MUTED%;"
        "  padding:8px 14px 8px 12px; border:none; margin-right:4px; min-height:22px; }"
        "QTabBar::tab:selected { color:%2; }"
        "QTabBar::tab:hover { color:%2; }"
        "QTabBar::close-button { margin-left:4px; }"
        "QScrollBar:vertical { background:transparent; width:10px; margin:2px; }"
        "QScrollBar::handle:vertical { background:%SCROLL%; min-height:24px;"
        "  border-radius:5px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:none; }"
        "QScrollBar:horizontal { background:transparent; height:10px; margin:2px; }"
        "QScrollBar::handle:horizontal { background:%SCROLL%; min-width:24px;"
        "  border-radius:5px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }"
        "QScrollArea#settingsPage { background:%CHAT%; border:none; }"
        "QScrollArea#settingsPage > QWidget { background:%CHAT%; }"
        "QWidget#settingsHost { background:%CHAT%; }"
        "QScrollArea#statsScroll { background:%CHAT%; border:none; }"
        "QScrollArea#statsScroll > QWidget { background:%CHAT%; }"
        "QWidget#statsHost { background:%CHAT%; }"
        "QDialog, QMessageBox { background:%3; color:%2; }"
        "QMessageBox QLabel { color:%2; }"
        "QProgressBar { background:%5; color:%2; border:1px solid %6;"
        "  border-radius:6px; text-align:center; max-height:14px; }"
        "QProgressBar::chunk { background:%4; border-radius:5px; }")
                      .arg(win)
                      .arg(text)
                      .arg(panel)
                      .arg(accent)
                      .arg(input)
                      .arg(border)
                      .arg(select)
                      .replace(QString::fromUtf8("%MUTED%"), muted)
                      .replace(QString::fromUtf8("%SCROLL%"), scroll)
                      .replace(QString::fromUtf8("%HOVER%"), hover)
                      .replace(QString::fromUtf8("%CHAT%"), chatBg));

    if (_projectDock)
        _projectDock->applyTheme();
    if (_graphThumbDock)
    {
        _graphThumbDock->setStyleSheet(QString::fromUtf8(
            "QDockWidget#graphThumbDock {"
            "  background:%1; border:1px solid %2; border-radius:10px; }"
            "QWidget#graphThumbDockContent { background:%1; border:none; }")
                                           .arg(panel, border));
        DockTitleBar *title = qobject_cast<DockTitleBar *>(
            _graphThumbDock->titleBarWidget());
        if (title)
            title->applyTheme(panel, text, border, muted);
    }
    if (_composerBar)
        _composerBar->applyTheme(panel, input, text, border, muted, accent);
    if (_graphThumb)
        _graphThumb->applyTheme(panel, text, border, muted);
    if (_graph)
        _graph->applyTheme();
    if (_journal)
        _journal->applyTheme();
    styleStatusPills();
    updateGraphThumb();

    foreach (ChatSession *session, _sessions)
    {
        if (session && session->widget())
            session->widget()->applyTheme();
    }

    const QColor chatColor = s->chatBackgroundColor();
    SettingsPage *settingsPages[2] = { _settingsPage, _projectSettingsPage };
    for (int i = 0; i < 2; ++i)
    {
        SettingsPage *page = settingsPages[i];
        if (!page)
            continue;
        page->applyTheme();
        if (QWidget *vp = page->viewport())
        {
            vp->setAutoFillBackground(true);
            QPalette pal = vp->palette();
            pal.setColor(QPalette::Window, chatColor);
            pal.setColor(QPalette::Base, chatColor);
            vp->setPalette(pal);
        }
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
    if (worker->isWaitingConsoleInput())
        widget->showConsoleInput(worker->consoleInputTranscript());
    else if (worker->isWaitingSubtreeBudget())
        widget->showSubtreeBudgetRequest(worker->subtreeBudgetPromptText());
    else if (worker->state() == WorkerStateWaitingApproval)
        widget->showPermissionRequest(worker->permissionAction(),
                                      worker->permissionOfferAlways());
    else if (worker->state() == WorkerStateWaitingContinuation)
        widget->showContinuationRequest(worker->lastError());
    else if (session->agent() && session->agent()->isWaitingToolBudget())
        widget->showToolBudgetRequest(session->agent()->toolBudgetChunk());
    else
        widget->hidePermissionRequest();
}

//----------------------------------------------

void MainWindow::updateComposerState()
{
    if (!_composer || !_composerBar)
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
    const bool composerOn = hasOpenProject() && !_resetting;
    _composerBar->setReadOnly(!canSend);
    _composerBar->setComposerEnabled(composerOn);
    _composerBar->setSendEnabled(canSend);
    _composerBar->setStopEnabled(active && !_resetting);
    setAllStopEnabled(active && !_resetting);

    const LlmProfile profile = profileForComposerTarget();
    ModelCapabilities caps =
        ModelCapabilityService::instance()->cachedOrHeuristic(profile);
    // Если ещё только эвристика — один раз спросить каталог API (для любой modality).
    if (canSend && !profile.model.trimmed().isEmpty()
        && caps.source == QString::fromUtf8("heuristic"))
        caps = ModelCapabilityService::instance()->capabilitiesFor(profile);

    QString hint = tr("Auto");
    if (!profile.model.trimmed().isEmpty())
        hint = profile.model.trimmed();
    QString tip = tr("Модель: %1\nВходы: %2")
                      .arg(profile.model.trimmed().isEmpty()
                               ? tr("(не задана)")
                               : profile.model.trimmed(),
                           caps.inputModalities.isEmpty()
                               ? QString::fromUtf8("text")
                               : caps.inputModalities.join(QString::fromUtf8(" + ")));
    if (caps.requiresImageReference())
        tip += tr("\nНужна референс-картинка");
    _composerBar->setCapsHint(hint, tip);

    updateComposerAttachmentsUi();

    if (!hasOpenProject())
        _composerBar->setPlaceholder(L("composer.placeholder.noProject"));
    else if (!session)
        _composerBar->setPlaceholder(L("composer.placeholder.noRoot"));
    else if (!canSend)
        _composerBar->setPlaceholder(L("composer.placeholder.busy"));
    else
        _composerBar->setPlaceholder(L("composer.placeholder.ready"));
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
