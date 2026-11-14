#ifndef PROJECTTREEDOCK_H
#define PROJECTTREEDOCK_H

#include <QDockWidget>
#include <QHash>
#include <QModelIndex>
#include <QString>
#include <QStringList>

class ProjectTreeDelegate;
class ProjectTreeWatchThread;
class DockTitleBar;
class QFileSystemModel;
class QFileSystemWatcher;
class QLabel;
class QMenu;
class QSortFilterProxyModel;
class QStackedWidget;
class QTimer;
class QTreeView;
class QWidget;

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
    void retranslateUi();
    qreal flashAmount(QString absPath) const;
    QString dropDirectoryForIndex(const QModelIndex &index) const;
    void copyDroppedLocalFiles(const QStringList &absolutePaths, const QModelIndex &target);

signals:
    void customContextMenuRequested(QMenu *menu, QString path, bool isDir);

private slots:
    void sltContextMenu(const QPoint &pos);
    void sltDoubleClicked(const QModelIndex &index);
    void sltOpenFile();
    void sltRevealInExplorer();
    void sltCopyPath();
    void sltDeleteSelected();
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
    bool isServicePath(QString path) const;
    void clearWatches();
    void startOsWatch();
    void stopOsWatch();
    void bindTreeModel();
    void showEmptyState();
    void addWatch(QString dir);
    void startFlash(QString absPath);
    void refreshView();
    void setDockTitle(QString title);

    QFileSystemModel      *_model;
    QSortFilterProxyModel *_proxy;
    QWidget               *_contentHost;
    QStackedWidget        *_stack;
    QLabel                *_emptyLabel;
    QTreeView             *_tree;
    ProjectTreeDelegate   *_delegate;
    DockTitleBar          *_titleBar;
    QFileSystemWatcher       *_watcher;
    ProjectTreeWatchThread   *_osWatch;
    QTimer                   *_flashTimer;
    QString                _root;
    QString                _contextPath;
    QHash<QString, qint64> _flashUntilMs;
    bool                   _ready;
};

#endif // PROJECTTREEDOCK_H
