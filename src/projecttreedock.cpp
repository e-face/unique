#include "projecttreedock.h"
//----------------------------------------------
#include "appsettings.h"
//----------------------------------------------
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPolygon>
#include <QProcess>
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

    /** Разбудить поток: ожидание идёт на событии, а не в блокирующем чтении. */
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
    explicit ProjectTreeView(QWidget *parent = 0)
        : QTreeView(parent)
    {
        setObjectName(QString::fromUtf8("projectTree"));
    }

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
    , _stack(new QStackedWidget(this))
    , _emptyLabel(new QLabel(this))
    , _tree(new ProjectTreeView(this))
    , _delegate(0)
    , _watcher(new QFileSystemWatcher(this))
    , _osWatch(0)
    , _flashTimer(new QTimer(this))
    , _ready(false)
{
    setWindowTitle(tr("Проект"));
    setObjectName(QString::fromUtf8("projectDock"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

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
    setWidget(_stack);
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
    // Тот же корень и дерево уже привязано: ничего не перестраивать,
    // иначе каждый вызов перезапускал бы слежение за файлами.
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
    // Сначала путь, потом модель: иначе Qt 5.9 падает на первом setRootPath
    // у QFileSystemModel, уже показанного в QTreeView без корня.
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
    setWindowTitle(tr("Проект — %1").arg(QDir(root).dirName()));
    QTimer::singleShot(0, this, SLOT(sltStartWatch()));
}

//----------------------------------------------

void ProjectTreeDock::showEmptyState()
{
    _root.clear();
    if (_stack)
        _stack->setCurrentWidget(_emptyLabel);
    setWindowTitle(tr("Проект"));
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

void ProjectTreeDock::clearWatches()
{
    _ready = false;
    _flashUntilMs.clear();
    if (_flashTimer)
        _flashTimer->stop();
    stopOsWatch();
    if (!_watcher)
        return;
    // Слежение ставится только на каталоги (и ограничено kMaxWatchDirs);
    // список файлов раньше рос без предела — линейный поиск в addPath и
    // отдельный хэндл ОС на каждый файл.
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
        // Поток ещё не проснулся: не убивать, пусть удалится сам.
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
    // Один снимок списка вместо трёх копий QStringList на каждый вызов.
    const QStringList watched = _watcher->directories();
    if (watched.size() >= kMaxWatchDirs)
        return;
    if (!watched.contains(clean))
        _watcher->addPath(clean);
    // Содержимое каталога здесь не перечисляется: изменения приходят от
    // ОС-наблюдателя (sltOsPathChanged) и от QFileSystemWatcher
    // (sltDirectoryChanged). Прежний обход с QFileInfo на каждый файл стоил
    // ~420 мс на каталог в 10 000 файлов и выполнялся при каждом раскрытии
    // узла дерева и при каждом изменении каталога.
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
    // Изменённые файлы ОС-наблюдатель называет точными путями; здесь достаточно
    // подсветить сам каталог, не перечисляя его содержимое.
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
    // В два раза светлее окна (V * 2 в HSV).
    const QString treeBg = s->windowColor().lighter(200).name();
    const QString text = s->textColor().name();
    const QString select = s->selectionColor().name();
    if (_emptyLabel)
    {
        _emptyLabel->setStyleSheet(QString::fromUtf8(
            "QLabel { background:%1; color:%2; }")
                                       .arg(treeBg, text));
    }
    if (!_tree)
        return;
    _tree->setStyleSheet(QString::fromUtf8(
        "QTreeView#projectTree { background:%1; color:%2; border:none; outline:none; }"
        "QTreeView#projectTree::item { color:%2; background:transparent; }"
        "QTreeView#projectTree::item:hover { background:%3; }"
        "QTreeView#projectTree::item:selected { background:%3; color:%2; }"
        "QTreeView#projectTree::branch { background:transparent; image:none; border-image:none; }")
                             .arg(treeBg, text, select));
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

    menu.addSeparator();
    emit customContextMenuRequested(&menu, _contextPath, isDir);

    if (!menu.isEmpty())
        menu.exec(_tree->viewport()->mapToGlobal(pos));
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
