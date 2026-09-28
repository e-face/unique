#ifndef PROJECTTREEDOCK_H
#define PROJECTTREEDOCK_H

#include <QDockWidget>
#include <QHash>
#include <QModelIndex>
#include <QString>

class ProjectTreeDelegate;
class ProjectTreeWatchThread;
class QFileSystemModel;
class QFileSystemWatcher;
class QLabel;
class QMenu;
class QSortFilterProxyModel;
class QStackedWidget;
class QTimer;
class QTreeView;

//----------------------------------------------

/**
 * @brief Дерево файлов проекта: подсветка недавно изменённых файлов.
 */
class ProjectTreeDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit ProjectTreeDock(QWidget *parent = 0);
    virtual ~ProjectTreeDock();
    void setProjectRoot(QString path);
    void releaseFileModel();
    void applyTheme();
    qreal flashAmount(QString absPath) const;

signals:
    void customContextMenuRequested(QMenu *menu, QString path, bool isDir);

private slots:
    void sltContextMenu(const QPoint &pos);
    void sltDoubleClicked(const QModelIndex &index);
    void sltOpenFile();
    void sltRevealInExplorer();
    void sltCopyPath();
    void sltDirectoryChanged(const QString &path);
    void sltFlashTick();
    void sltExpanded(const QModelIndex &index);
    void sltStartWatch();
    void sltOsPathChanged(const QString &path);

private:
    QString selectedPath() const;
    void openPath(QString path);
    QString filePathForIndex(const QModelIndex &viewIndex) const;
    QString normalizePath(QString path) const;
    bool skipDirName(QString name) const;
    void clearWatches();
    void startOsWatch();
    void stopOsWatch();
    void bindTreeModel();
    void showEmptyState();
    void addWatch(QString dir);
    void startFlash(QString absPath);
    void refreshView();

    QFileSystemModel      *_model;
    QSortFilterProxyModel *_proxy;
    QStackedWidget        *_stack;
    QLabel                *_emptyLabel;
    QTreeView             *_tree;
    ProjectTreeDelegate   *_delegate;
    QFileSystemWatcher       *_watcher;
    ProjectTreeWatchThread   *_osWatch;
    QTimer                   *_flashTimer;
    QString                _root;
    QString                _contextPath;
    QHash<QString, qint64> _flashUntilMs;
    bool                   _ready;
};

#endif // PROJECTTREEDOCK_H
