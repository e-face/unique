#include "projecttreedock.h"
//----------------------------------------------
#include "appsettings.h"
#include "docktitlebar.h"
//----------------------------------------------
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPolygon>
#include <QProcess>
#include <QPushButton>
#include <QMetaObject>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStringList>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QThread>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>
#include <QUrl>
#include <QtGlobal>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

//----------------------------------------------

static const int kFlashMs = 2000;
static const int kMaxWatchDirs = 400;

//----------------------------------------------

static bool projectSkipDirName(QString name)
{
    const QString lower = name.toLower();
    return lower.startsWith(QString::fromUtf8("___project___"))
           || lower == QString::fromUtf8(".git")
           || lower == QString::fromUtf8(".svn")
           || lower == QString::fromUtf8(".hg")
           || lower == QString::fromUtf8(".vs")
           || lower == QString::fromUtf8(".idea")
           || lower == QString::fromUtf8(".vscode")
           || lower == QString::fromUtf8("node_modules")
           || lower == QString::fromUtf8("__pycache__")
           || lower == QString::fromUtf8(".cache")
           || lower == QString::fromUtf8("build")
           || lower == QString::fromUtf8("debug")
           || lower == QString::fromUtf8("release");
}

//----------------------------------------------

static bool projectRelIsNoisy(QString rel)
{
    rel.replace(QChar('\\'), QChar('/'));
    const QStringList parts = rel.split(QChar('/'), QString::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i)
    {
        if (projectSkipDirName(parts.at(i)))
            return true;
    }
    return false;
}

//----------------------------------------------

class ProjectTreeWatchThread : public QThread
{
public:
    ProjectTreeWatchThread(QObject *receiver, QString root)
        : QThread(0)
        , _receiver(receiver)
        , _root(root)
        , _stopEvent(0)
        , _stop(0)
    {
        setPriority(QThread::LowPriority);
#ifdef Q_OS_WIN
        _stopEvent = CreateEventW(0, TRUE, FALSE, 0);
#endif
    }

    virtual ~ProjectTreeWatchThread()
    {
#ifdef Q_OS_WIN
        if (_stopEvent)
            CloseHandle(reinterpret_cast<HANDLE>(_stopEvent));
#endif
    }

    /** Wake the thread: waiting is on an event, not a blocking read. */
    void requestStop()
    {
        _stop = 1;
#ifdef Q_OS_WIN
        if (_stopEvent)
            SetEvent(reinterpret_cast<HANDLE>(_stopEvent));
#endif
    }

protected:
    virtual void run()
    {
#ifdef Q_OS_WIN
        if (!_stopEvent)
            return;
        const QString native = QDir::toNativeSeparators(_root);
        HANDLE dir = CreateFileW(
            reinterpret_cast<LPCWSTR>(native.utf16()),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            0,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            0);
        if (dir == INVALID_HANDLE_VALUE)
            return;
        HANDLE ioEvent = CreateEventW(0, TRUE, FALSE, 0);
        if (!ioEvent)
        {
            CloseHandle(dir);
            return;
        }

        DWORD buffer[16384];
        while (!_stop)
        {
            OVERLAPPED ov;
            ZeroMemory(&ov, sizeof(ov));
            ov.hEvent = ioEvent;
            ResetEvent(ioEvent);
            const BOOL ok = ReadDirectoryChangesW(
                dir,
                buffer,
                sizeof(buffer),
                TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME
                    | FILE_NOTIFY_CHANGE_DIR_NAME
                    | FILE_NOTIFY_CHANGE_LAST_WRITE
                    | FILE_NOTIFY_CHANGE_SIZE
                    | FILE_NOTIFY_CHANGE_CREATION,
                0,
                &ov,
                0);
            if (!ok)
                break;

            HANDLE handles[2];
            handles[0] = ioEvent;
            handles[1] = reinterpret_cast<HANDLE>(_stopEvent);
            const DWORD waited = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            if (waited != WAIT_OBJECT_0)
            {
                CancelIo(dir);
                WaitForSingleObject(ioEvent, 1000);
                break;
            }
            DWORD bytes = 0;
            if (!GetOverlappedResult(dir, &ov, &bytes, FALSE))
                break;
            if (bytes == 0)
                continue;

            const char *raw = reinterpret_cast<const char *>(buffer);
            DWORD offset = 0;
            int posted = 0;
            while (offset < bytes)
            {
                const FILE_NOTIFY_INFORMATION *info =
                    reinterpret_cast<const FILE_NOTIFY_INFORMATION *>(raw + offset);
                const int nameChars = int(info->FileNameLength / sizeof(WCHAR));
                const QString rel = QString::fromWCharArray(info->FileName, nameChars);
                if (!projectRelIsNoisy(rel) && posted < 40)
                {
                    const QString abs = QDir::cleanPath(_root + QChar('/') + rel);
                    if (_receiver)
                    {
                        QMetaObject::invokeMethod(_receiver, "sltOsPathChanged",
                                                  Qt::QueuedConnection,
                                                  Q_ARG(QString, abs));
                    }
                    ++posted;
                }
                if (info->NextEntryOffset == 0)
                    break;
                offset += info->NextEntryOffset;
            }
        }
        CloseHandle(ioEvent);
        CloseHandle(dir);
#else
        Q_UNUSED(_receiver);
#endif
    }

private:
    QObject *_receiver;
    QString  _root;
    void    *_stopEvent;
    volatile int _stop;
};

//----------------------------------------------

class ProjectNameFilter : public QSortFilterProxyModel
{
public:
    explicit ProjectNameFilter(QObject *parent = 0)
        : QSortFilterProxyModel(parent)
    {
    }

protected:
    virtual bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
    {
        const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
        const QString name = sourceModel()->data(index, Qt::DisplayRole).toString();
        if (name.startsWith(QString::fromUtf8("___project___"), Qt::CaseInsensitive))
            return false;
        return true;
    }

    virtual bool filterAcceptsColumn(int sourceColumn, const QModelIndex &) const
    {
        return sourceColumn == 0;
    }
};

//----------------------------------------------

class ProjectTreeView : public QTreeView
{
public:
    explicit ProjectTreeView(ProjectTreeDock *dock, QWidget *parent = 0)
        : QTreeView(parent)
        , _dock(dock)
    {
        setObjectName(QString::fromUtf8("projectTree"));
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::CopyAction);
        setDragDropOverwriteMode(false);
        setSelectionMode(QAbstractItemView::ExtendedSelection);
    }

protected:
    virtual void dragEnterEvent(QDragEnterEvent *event)
    {
        if (event && event->mimeData() && event->mimeData()->hasUrls())
        {
            event->setDropAction(Qt::CopyAction);
            event->accept();
            return;
        }
        event->ignore();
    }

    virtual void dragMoveEvent(QDragMoveEvent *event)
    {
        if (event && event->mimeData() && event->mimeData()->hasUrls())
        {
            event->setDropAction(Qt::CopyAction);
            event->accept();
            return;
        }
        event->ignore();
    }

    virtual void dropEvent(QDropEvent *event)
    {
        if (!_dock || !event || !event->mimeData() || !event->mimeData()->hasUrls())
        {
            if (event)
                event->ignore();
            return;
        }
        QStringList paths;
        foreach (const QUrl &url, event->mimeData()->urls())
        {
            const QString local = url.toLocalFile();
            if (!local.isEmpty())
                paths.append(local);
        }
        if (paths.isEmpty())
        {
            event->ignore();
            return;
        }
        const QModelIndex index = indexAt(event->pos());
        _dock->copyDroppedLocalFiles(paths, index);
        if (index.isValid())
            expand(index);
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }

private:
    ProjectTreeDock *_dock;

protected:
    virtual void drawBranches(QPainter *painter, const QRect &rect,
                              const QModelIndex &index) const
    {
        if (!painter || !index.isValid() || !model())
            return;

        int depth = 0;
        QModelIndex parent = index.parent();
        const QModelIndex root = rootIndex();
        while (parent.isValid() && parent != root)
        {
            ++depth;
            parent = parent.parent();
        }

        const int indent = qMax(12, indentation());
        const QRect arrowRect(rect.x() + depth * indent, rect.y(), indent, rect.height());
        if (!model()->hasChildren(index))
            return;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(AppSettings::instance()->mutedTextColor());
        const int cx = arrowRect.center().x();
        const int cy = arrowRect.center().y();
        QPolygon tri;
        if (isExpanded(index))
        {
            tri << QPoint(cx - 5, cy - 3)
                << QPoint(cx + 5, cy - 3)
                << QPoint(cx, cy + 5);
        }
        else
        {
            tri << QPoint(cx - 3, cy - 6)
                << QPoint(cx + 5, cy)
                << QPoint(cx - 3, cy + 6);
        }
        painter->drawPolygon(tri);
        painter->restore();
    }
};

//----------------------------------------------

class ProjectTreeDelegate : public QStyledItemDelegate
{
public:
    ProjectTreeDelegate(ProjectTreeDock *dock, QFileSystemModel *model,
                        QSortFilterProxyModel *proxy, QObject *parent)
        : QStyledItemDelegate(parent)
        , _dock(dock)
        , _model(model)
        , _proxy(proxy)
    {
    }

    QString filePath(const QModelIndex &index) const
    {
        if (!_model || !_proxy || !index.isValid())
            return QString();
        return _model->filePath(_proxy->mapToSource(index));
    }

    virtual void paint(QPainter *painter, const QStyleOptionViewItem &option,
                       const QModelIndex &index) const
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);

        const qreal amount = _dock ? _dock->flashAmount(filePath(index)) : 0;
        if (amount > 0 && painter)
        {
            const QColor green(46, 160, 90);
            QColor base = opt.palette.color(QPalette::Base);
            if (opt.state & QStyle::State_Selected)
                base = AppSettings::instance()->selectionColor();
            const QColor mix(
                int(base.red()   + (green.red()   - base.red())   * amount),
                int(base.green() + (green.green() - base.green()) * amount),
                int(base.blue()  + (green.blue()  - base.blue())  * amount));
            painter->fillRect(option.rect, mix);
            opt.backgroundBrush = Qt::NoBrush;
            opt.state &= ~QStyle::State_Selected;
        }
        QStyledItemDelegate::paint(painter, opt, index);
    }

private:
    ProjectTreeDock       *_dock;
    QFileSystemModel      *_model;
    QSortFilterProxyModel *_proxy;
};

//----------------------------------------------

ProjectTreeDock::ProjectTreeDock(QWidget *parent)
    : QDockWidget(parent)
    , _model(new QFileSystemModel(this))
    , _proxy(new ProjectNameFilter(this))
    , _contentHost(new QWidget(this))
    , _stack(new QStackedWidget(_contentHost))
    , _emptyLabel(new QLabel(_contentHost))
    , _tree(new ProjectTreeView(this, _contentHost))
    , _delegate(0)
    , _titleBar(new DockTitleBar(tr("Проект"), this))
    , _watcher(new QFileSystemWatcher(this))
    , _osWatch(0)
    , _flashTimer(new QTimer(this))
    , _ready(false)
{
    setWindowTitle(tr("Проект"));
    setObjectName(QString::fromUtf8("projectDock"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    _titleBar->setObjectName(QString::fromUtf8("projectDockTitleBar"));
    setTitleBarWidget(_titleBar);

    _contentHost->setObjectName(QString::fromUtf8("projectDockContent"));
    _contentHost->setAttribute(Qt::WA_StyledBackground, true);

    _emptyLabel->setAlignment(Qt::AlignCenter);
    _emptyLabel->setWordWrap(true);
    _emptyLabel->setText(tr("Проект не открыт"));
    _emptyLabel->setMargin(16);

    _model->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::AllDirs);
    _model->setReadOnly(true);
    _proxy->setSourceModel(_model);
    _delegate = new ProjectTreeDelegate(this, _model, _proxy, _tree);
    _tree->setItemDelegate(_delegate);
    _tree->setHeaderHidden(true);
    _tree->setAnimated(false);
    _tree->setUniformRowHeights(true);
    _tree->setExpandsOnDoubleClick(true);
    _tree->setRootIsDecorated(true);
    _tree->setItemsExpandable(true);
    _tree->setIndentation(18);
    _tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(_tree, SIGNAL(customContextMenuRequested(QPoint)),
            this, SLOT(sltContextMenu(QPoint)));
    connect(_tree, SIGNAL(doubleClicked(QModelIndex)),
            this, SLOT(sltDoubleClicked(QModelIndex)));
    connect(_tree, SIGNAL(expanded(QModelIndex)),
            this, SLOT(sltExpanded(QModelIndex)));
    connect(_watcher, SIGNAL(directoryChanged(QString)),
            this, SLOT(sltDirectoryChanged(QString)));
    _flashTimer->setInterval(40);
    connect(_flashTimer, SIGNAL(timeout()), this, SLOT(sltFlashTick()));

    _stack->addWidget(_emptyLabel);
    _stack->addWidget(_tree);
    QVBoxLayout *hostLay = new QVBoxLayout(_contentHost);
    hostLay->setContentsMargins(0, 0, 0, 0);
    hostLay->setSpacing(0);
    hostLay->addWidget(_stack, 1);
    setWidget(_contentHost);
    showEmptyState();
    applyTheme();
}

//----------------------------------------------

ProjectTreeDock::~ProjectTreeDock()
{
    stopOsWatch();
}

//----------------------------------------------

void ProjectTreeDock::releaseFileModel()
{
    clearWatches();
    _root.clear();
    if (_tree)
    {
        _tree->setUpdatesEnabled(false);
        _tree->setRootIndex(QModelIndex());
        _tree->setModel(0);
        _tree->setUpdatesEnabled(true);
    }
    if (_model)
        _model->setRootPath(QString());
    showEmptyState();
}

//----------------------------------------------

void ProjectTreeDock::setProjectRoot(QString path)
{
    const QString root = QDir::cleanPath(path.trimmed());
    // The same root and the tree is already bound: do not rebuild it,
    // or every call would restart file watching.
    if (!root.isEmpty() && normalizePath(root) == normalizePath(_root)
        && _tree && _tree->model() == _proxy && _osWatch)
        return;
    clearWatches();
    _root = root;
    if (root.isEmpty() || !QDir(root).exists())
    {
        showEmptyState();
        return;
    }

    if (_tree)
        _tree->setUpdatesEnabled(false);
    // Set the path first, then the model: otherwise Qt 5.9 crashes on the first setRootPath
    // of a QFileSystemModel already shown in a QTreeView with no root.
    const QModelIndex sourceRoot = _model->setRootPath(root);
    bindTreeModel();
    if (sourceRoot.isValid() && _proxy)
    {
        const QModelIndex viewRoot = _proxy->mapFromSource(sourceRoot);
        if (viewRoot.isValid())
            _tree->setRootIndex(viewRoot);
    }
    if (_tree)
    {
        _tree->setEnabled(true);
        _tree->setUpdatesEnabled(true);
    }
    if (_stack)
        _stack->setCurrentWidget(_tree);
    setDockTitle(tr("Проект — %1").arg(QDir(root).dirName()));
    QTimer::singleShot(0, this, SLOT(sltStartWatch()));
}

//----------------------------------------------

void ProjectTreeDock::showEmptyState()
{
    _root.clear();
    if (_stack)
        _stack->setCurrentWidget(_emptyLabel);
    setDockTitle(tr("Проект"));
}

//----------------------------------------------

void ProjectTreeDock::retranslateUi()
{
    if (_root.isEmpty())
    {
        setDockTitle(tr("Проект"));
        if (_emptyLabel)
            _emptyLabel->setText(tr("Проект не открыт"));
    }
    else
        setDockTitle(tr("Проект — %1").arg(QDir(_root).dirName()));
}

//----------------------------------------------

void ProjectTreeDock::bindTreeModel()
{
    if (!_tree || !_proxy)
        return;
    if (_tree->model() != _proxy)
        _tree->setModel(_proxy);
    if (_tree->header())
    {
        _tree->header()->setStretchLastSection(true);
        for (int c = 1; c < _model->columnCount(); ++c)
            _tree->hideColumn(c);
    }
}

//----------------------------------------------

qreal ProjectTreeDock::flashAmount(QString absPath) const
{
    const QString key = normalizePath(absPath);
    if (key.isEmpty())
        return 0;
    const qint64 until = _flashUntilMs.value(key, 0);
    if (until <= 0)
        return 0;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now >= until)
        return 0;
    const qint64 start = until - kFlashMs;
    return qBound(0.0, 1.0 - qreal(now - start) / qreal(kFlashMs), 1.0);
}

//----------------------------------------------

QString ProjectTreeDock::normalizePath(QString path) const
{
    QString abs = QDir::cleanPath(path.trimmed());
    abs.replace(QChar('\\'), QChar('/'));
#ifdef Q_OS_WIN
    abs = abs.toLower();
#endif
    return abs;
}

//----------------------------------------------

bool ProjectTreeDock::skipDirName(QString name) const
{
    return projectSkipDirName(name);
}

//----------------------------------------------

bool ProjectTreeDock::isServicePath(QString path) const
{
    if (_root.isEmpty() || path.trimmed().isEmpty())
        return false;
    const QString clean = normalizePath(path);
    const QString store = normalizePath(_root + QString::fromUtf8("/___project___"));
    return clean == store || clean.startsWith(store + QString::fromUtf8("/"));
}

//----------------------------------------------

void ProjectTreeDock::clearWatches()
{
    _ready = false;
    _flashUntilMs.clear();
    if (_flashTimer)
        _flashTimer->stop();
    stopOsWatch();
    if (!_watcher)
        return;
    // Watching is set only on directories, and capped by kMaxWatchDirs.
    // The file list used to grow without a limit: a linear search in addPath and
    // a separate OS handle per file.
    const QStringList dirs = _watcher->directories();
    if (!dirs.isEmpty())
        _watcher->removePaths(dirs);
}

//----------------------------------------------

void ProjectTreeDock::stopOsWatch()
{
    if (!_osWatch)
        return;
    ProjectTreeWatchThread *thread = _osWatch;
    _osWatch = 0;
    thread->requestStop();
    if (thread->wait(2000))
        delete thread;
    else
    {
        // The thread has not woken yet: do not kill it; let it delete itself.
        connect(thread, SIGNAL(finished()), thread, SLOT(deleteLater()));
    }
}

//----------------------------------------------

void ProjectTreeDock::startOsWatch()
{
    stopOsWatch();
    if (_root.isEmpty())
        return;
#ifdef Q_OS_WIN
    _osWatch = new ProjectTreeWatchThread(this, _root);
    _osWatch->start();
#endif
}

//----------------------------------------------

void ProjectTreeDock::sltOsPathChanged(const QString &path)
{
    if (!_ready)
        return;
    const QString clean = QDir::cleanPath(path);
    if (clean.contains(QString::fromUtf8("/___project___")))
        return;
    const QString name = QFileInfo(clean).fileName();
    if (skipDirName(name) && normalizePath(clean) != normalizePath(_root))
        return;
    startFlash(clean);
}

//----------------------------------------------

void ProjectTreeDock::sltStartWatch()
{
    if (_root.isEmpty())
        return;
    startOsWatch();
    addWatch(_root);
    _ready = true;
}

//----------------------------------------------

void ProjectTreeDock::sltExpanded(const QModelIndex &index)
{
    if (!_ready)
        return;
    const QString path = filePathForIndex(index);
    if (path.isEmpty() || !QFileInfo(path).isDir())
        return;
    addWatch(path);
}

//----------------------------------------------

void ProjectTreeDock::addWatch(QString dir)
{
    if (dir.isEmpty() || !_watcher)
        return;
    const QString clean = QDir::cleanPath(dir);
    if (clean.contains(QString::fromUtf8("/___project___")))
        return;
    const QString name = QFileInfo(clean).fileName();
    if (skipDirName(name) && QDir::cleanPath(_root) != clean)
        return;
    // One snapshot of the list instead of three QStringList copies per call.
    const QStringList watched = _watcher->directories();
    if (watched.size() >= kMaxWatchDirs)
        return;
    if (!watched.contains(clean))
        _watcher->addPath(clean);
    // Directory contents are not listed here. Changes come from
    // the OS watcher (sltOsPathChanged) and from QFileSystemWatcher
    // (sltDirectoryChanged). The old walk with a QFileInfo per file cost
    // about 420 ms on a directory of 10,000 files and ran on every expand
    // of a tree node and on every directory change.
}

//----------------------------------------------

void ProjectTreeDock::startFlash(QString absPath)
{
    const QString key = normalizePath(absPath);
    if (key.isEmpty() || _root.isEmpty())
        return;
    const qint64 until = QDateTime::currentMSecsSinceEpoch() + kFlashMs;
    _flashUntilMs.insert(key, until);

    QString parent = QFileInfo(key).path();
    const QString root = normalizePath(_root);
    while (!parent.isEmpty() && parent != root && parent.startsWith(root))
    {
        _flashUntilMs.insert(parent, until);
        const QString up = QFileInfo(parent).path();
        if (up == parent)
            break;
        parent = up;
    }
    if (!root.isEmpty())
        _flashUntilMs.insert(root, until);

    if (_flashTimer && !_flashTimer->isActive())
        _flashTimer->start();
    refreshView();
}

//----------------------------------------------

void ProjectTreeDock::refreshView()
{
    if (_tree && _tree->viewport())
        _tree->viewport()->update();
}

//----------------------------------------------

void ProjectTreeDock::sltDirectoryChanged(const QString &path)
{
    if (!_ready)
        return;
    const QString clean = QDir::cleanPath(path);
    if (clean.contains(QString::fromUtf8("/___project___")))
        return;
    // The OS watcher names changed files with exact paths. Here it is enough
    // to highlight the directory itself, without listing its contents.
    startFlash(clean);
}

//----------------------------------------------

void ProjectTreeDock::sltFlashTick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList<QString> dead;
    for (QHash<QString, qint64>::const_iterator it = _flashUntilMs.constBegin();
         it != _flashUntilMs.constEnd(); ++it)
    {
        if (it.value() <= now)
            dead.append(it.key());
    }
    foreach (const QString &key, dead)
        _flashUntilMs.remove(key);
    refreshView();
    if (_flashUntilMs.isEmpty() && _flashTimer)
        _flashTimer->stop();
}

//----------------------------------------------

void ProjectTreeDock::applyTheme()
{
    AppSettings *s = AppSettings::instance();
    const QString panel = s->panelColor().name();
    const QString win = s->windowColor().name();
    const QString text = s->textColor().name();
    const QString border = s->borderColor().name();
    const QString muted = s->mutedTextColor().name();
    const QString select = s->selectionColor().name();
    const QString hover = s->isLightTheme()
                          ? QColor(s->windowColor()).darker(104).name()
                          : select;

    setStyleSheet(QString::fromUtf8(
        "QDockWidget#projectDock {"
        "  background:%1; border:1px solid %2; border-radius:10px; }"
        "QWidget#projectDockContent { background:%1; border:none; }")
                      .arg(panel, border));

    if (_titleBar)
        _titleBar->applyTheme(panel, text, border, muted);

    if (_emptyLabel)
    {
        _emptyLabel->setStyleSheet(QString::fromUtf8(
            "QLabel { background:%1; color:%2; }")
                                       .arg(panel, muted));
    }
    if (!_tree)
        return;
    _tree->setStyleSheet(QString::fromUtf8(
        "QTreeView#projectTree { background:%1; color:%2; border:none; outline:none; }"
        "QTreeView#projectTree::item { color:%2; background:transparent;"
        "  padding:2px 4px; border-radius:6px; }"
        "QTreeView#projectTree::item:hover { background:%4; }"
        "QTreeView#projectTree::item:selected { background:%3; color:%2; }"
        "QTreeView#projectTree::branch { background:transparent; image:none; border-image:none; }")
                             .arg(panel, text, select, hover));
    Q_UNUSED(win);
}

//----------------------------------------------

void ProjectTreeDock::setDockTitle(QString title)
{
    setWindowTitle(title);
    if (_titleBar)
        _titleBar->setTitle(title);
}

//----------------------------------------------

QString ProjectTreeDock::filePathForIndex(const QModelIndex &viewIndex) const
{
    if (!viewIndex.isValid() || !_model || !_proxy || !_tree || !_tree->model())
        return QString();
    const QModelIndex source = _proxy->mapToSource(viewIndex);
    return _model->filePath(source);
}

//----------------------------------------------

QString ProjectTreeDock::selectedPath() const
{
    return filePathForIndex(_tree->currentIndex());
}

//----------------------------------------------

void ProjectTreeDock::openPath(QString path)
{
    const QFileInfo info(path);
    if (!info.exists() || info.isDir())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
}

//----------------------------------------------

void ProjectTreeDock::sltDoubleClicked(const QModelIndex &index)
{
    const QString path = filePathForIndex(index);
    const QFileInfo info(path);
    if (!info.exists() || info.isDir())
        return;
    openPath(path);
}

//----------------------------------------------

static bool sameLocalPath(const QString &left, const QString &right)
{
    QString a = QDir::cleanPath(left);
    QString b = QDir::cleanPath(right);
    a.replace(QChar('\\'), QChar('/'));
    b.replace(QChar('\\'), QChar('/'));
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

//----------------------------------------------

static bool pathIsInside(const QString &parentDir, const QString &childPath)
{
    QString parent = QDir::cleanPath(parentDir);
    QString child = QDir::cleanPath(childPath);
    parent.replace(QChar('\\'), QChar('/'));
    child.replace(QChar('\\'), QChar('/'));
    if (!parent.endsWith(QChar('/')))
        parent += QChar('/');
#ifdef Q_OS_WIN
    return child.startsWith(parent, Qt::CaseInsensitive);
#else
    return child.startsWith(parent);
#endif
}

//----------------------------------------------

/** 0 means ask, 1 means replace all, 2 means skip all, 3 means cancel. */
static int askReplaceFile(QWidget *parent, const QString &name, int *mode)
{
    if (!mode)
        return 2;
    if (*mode == 1 || *mode == 2 || *mode == 3)
        return *mode;
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(ProjectTreeDock::tr("Копирование"));
    box.setText(ProjectTreeDock::tr("«%1» уже есть. Заменить?").arg(name));
    QPushButton *yes = box.addButton(ProjectTreeDock::tr("Заменить"), QMessageBox::YesRole);
    QPushButton *all = box.addButton(ProjectTreeDock::tr("Заменить все"), QMessageBox::YesRole);
    QPushButton *skip = box.addButton(ProjectTreeDock::tr("Пропустить"), QMessageBox::NoRole);
    QPushButton *cancel = box.addButton(ProjectTreeDock::tr("Отмена"), QMessageBox::RejectRole);
    box.setDefaultButton(skip);
    box.exec();
    if (box.clickedButton() == cancel)
    {
        *mode = 3;
        return 3;
    }
    if (box.clickedButton() == all)
    {
        *mode = 1;
        return 1;
    }
    if (box.clickedButton() == yes)
        return 1;
    Q_UNUSED(skip);
    return 2;
}

//----------------------------------------------

static bool copyEntryInto(QWidget *parent, const QString &source, const QString &destDir,
                          int *mode, QString *error)
{
    if (mode && *mode == 3)
        return false;
    const QFileInfo info(source);
    if (!info.exists())
    {
        if (error)
            *error = ProjectTreeDock::tr("Не удалось скопировать: %1").arg(source);
        return false;
    }
    const QString srcClean = QDir::cleanPath(info.absoluteFilePath());
    const QString dest = QDir::cleanPath(destDir + QChar('/') + info.fileName());
    if (sameLocalPath(srcClean, dest))
        return true;
    if (pathIsInside(srcClean, dest))
    {
        if (error && error->isEmpty())
            *error = ProjectTreeDock::tr("Нельзя скопировать каталог внутрь самого себя.");
        return false;
    }
    if (info.isDir())
    {
        if (QFileInfo(dest).exists() && !QFileInfo(dest).isDir())
        {
            const int choice = askReplaceFile(parent, info.fileName(), mode);
            if (choice == 3)
                return false;
            if (choice != 1)
                return true;
            if (!QFile::remove(dest))
            {
                if (error)
                    *error = ProjectTreeDock::tr("Не удалось скопировать: %1").arg(info.fileName());
                return false;
            }
        }
        if (!QDir().mkpath(dest))
        {
            if (error)
                *error = ProjectTreeDock::tr("Не удалось скопировать: %1").arg(dest);
            return false;
        }
        const QFileInfoList entries = QDir(srcClean).entryInfoList(
            QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        foreach (const QFileInfo &entry, entries)
        {
            if (!copyEntryInto(parent, entry.absoluteFilePath(), dest, mode, error))
                return false;
        }
        return true;
    }
    if (QFileInfo(dest).exists())
    {
        const int choice = askReplaceFile(parent, info.fileName(), mode);
        if (choice == 3)
            return false;
        if (choice != 1)
            return true;
        if (QFileInfo(dest).isDir())
        {
            if (!QDir(dest).removeRecursively())
            {
                if (error)
                    *error = ProjectTreeDock::tr("Не удалось скопировать: %1").arg(info.fileName());
                return false;
            }
        }
        else if (!QFile::remove(dest))
        {
            if (error)
                *error = ProjectTreeDock::tr("Не удалось скопировать: %1").arg(info.fileName());
            return false;
        }
    }
    if (!QFile::copy(srcClean, dest))
    {
        if (error)
            *error = ProjectTreeDock::tr("Не удалось скопировать: %1").arg(info.fileName());
        return false;
    }
    return true;
}

//----------------------------------------------

QString ProjectTreeDock::dropDirectoryForIndex(const QModelIndex &index) const
{
    if (_root.isEmpty())
        return QString();
    const QString path = filePathForIndex(index);
    if (path.isEmpty())
        return _root;
    const QFileInfo info(path);
    if (!info.exists())
        return _root;
    if (info.isDir())
        return info.absoluteFilePath();
    return info.absolutePath();
}

//----------------------------------------------

void ProjectTreeDock::copyDroppedLocalFiles(const QStringList &absolutePaths,
                                           const QModelIndex &target)
{
    if (_root.isEmpty() || absolutePaths.isEmpty())
        return;
    const QString destDir = dropDirectoryForIndex(target);
    if (destDir.isEmpty())
        return;
    if (isServicePath(destDir))
    {
        QMessageBox::information(this, tr("Копирование"),
                                 tr("Сюда копировать нельзя."));
        return;
    }
    int mode = 0;
    QString error;
    foreach (const QString &path, absolutePaths)
    {
        if (mode == 3)
            break;
        if (path.trimmed().isEmpty() || isServicePath(path))
            continue;
        copyEntryInto(this, path, destDir, &mode, &error);
    }
    if (!error.isEmpty() && mode != 3)
        QMessageBox::warning(this, tr("Копирование"), error);
}

//----------------------------------------------

void ProjectTreeDock::sltContextMenu(const QPoint &pos)
{
    const QModelIndex index = _tree->indexAt(pos);
    if (!index.isValid())
        return;

    _contextPath = filePathForIndex(index);
    const bool isDir = QFileInfo(_contextPath).isDir();

    QMenu menu(this);
    if (!isDir)
    {
        QAction *openAction = menu.addAction(tr("Открыть"));
        connect(openAction, SIGNAL(triggered()), this, SLOT(sltOpenFile()));
        menu.addSeparator();
    }

    QAction *revealAction = menu.addAction(tr("Показать в проводнике"));
    connect(revealAction, SIGNAL(triggered()), this, SLOT(sltRevealInExplorer()));

    QAction *copyAction = menu.addAction(tr("Копировать путь"));
    connect(copyAction, SIGNAL(triggered()), this, SLOT(sltCopyPath()));

    const bool canDelete = normalizePath(_contextPath) != normalizePath(_root)
                           && !isServicePath(_contextPath);
    if (canDelete)
    {
        menu.addSeparator();
        QAction *deleteAction = menu.addAction(tr("Удалить"));
        connect(deleteAction, SIGNAL(triggered()), this, SLOT(sltDeleteSelected()));
    }

    menu.addSeparator();
    emit customContextMenuRequested(&menu, _contextPath, isDir);

    if (!menu.isEmpty())
        menu.exec(_tree->viewport()->mapToGlobal(pos));
}

//----------------------------------------------

void ProjectTreeDock::sltDeleteSelected()
{
    if (_contextPath.isEmpty() || _root.isEmpty() || !_tree)
        return;
    QStringList paths;
    const QModelIndex current = _tree->currentIndex();
    const QModelIndexList selected = _tree->selectionModel()
                                     ? _tree->selectionModel()->selectedRows()
                                     : QModelIndexList();
    if (selected.size() > 1 && selected.contains(current))
    {
        foreach (const QModelIndex &index, selected)
        {
            const QString path = filePathForIndex(index);
            if (!path.isEmpty())
                paths.append(path);
        }
    }
    if (paths.isEmpty())
        paths.append(_contextPath);

    QStringList victims;
    foreach (const QString &path, paths)
    {
        if (normalizePath(path) == normalizePath(_root) || isServicePath(path))
            continue;
        bool nested = false;
        foreach (const QString &other, paths)
        {
            if (sameLocalPath(other, path))
                continue;
            if (pathIsInside(other, path))
            {
                nested = true;
                break;
            }
        }
        if (!nested)
            victims.append(path);
    }
    if (victims.isEmpty())
    {
        QMessageBox::information(this, tr("Удаление"),
                                 tr("Нельзя удалить корень проекта."));
        return;
    }

    QString question;
    if (victims.size() == 1)
    {
        const QString name = QFileInfo(victims.first()).fileName();
        if (QFileInfo(victims.first()).isDir())
            question = tr("Удалить каталог «%1» и всё его содержимое?").arg(name);
        else
            question = tr("Удалить «%1»?").arg(name);
    }
    else
        question = tr("Удалить выбранное (%1)?").arg(victims.size());

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Удаление"));
    box.setText(question);
    QPushButton *yes = box.addButton(tr("Удалить"), QMessageBox::YesRole);
    QPushButton *no = box.addButton(tr("Отмена"), QMessageBox::RejectRole);
    box.setDefaultButton(no);
    box.exec();
    if (box.clickedButton() != yes)
        return;

    foreach (const QString &path, victims)
    {
        const QFileInfo info(path);
        bool ok = false;
        if (info.isDir())
            ok = QDir(path).removeRecursively();
        else
            ok = QFile::remove(path);
        if (!ok)
        {
            QMessageBox::warning(this, tr("Удаление"),
                                 tr("Не удалось удалить: %1").arg(info.fileName()));
            return;
        }
    }
}

//----------------------------------------------

void ProjectTreeDock::sltOpenFile()
{
    openPath(_contextPath);
}

//----------------------------------------------

void ProjectTreeDock::sltRevealInExplorer()
{
    if (_contextPath.isEmpty())
        return;
    const QString native = QDir::toNativeSeparators(_contextPath);
#ifdef Q_OS_WIN
    QProcess::startDetached(QString::fromUtf8("explorer"),
                            QStringList()
                                << QString::fromUtf8("/select,") + native);
#else
    const QFileInfo info(_contextPath);
    const QString folder = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
#endif
}

//----------------------------------------------

void ProjectTreeDock::sltCopyPath()
{
    if (_contextPath.isEmpty())
        return;
    QApplication::clipboard()->setText(QDir::toNativeSeparators(_contextPath));
}
