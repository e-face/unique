#ifndef WORKERGRAPHWIDGET_H
#define WORKERGRAPHWIDGET_H

#include <QGraphicsView>
#include <QHash>
#include <QPixmap>
#include <QPointer>
#include <QSize>

#include "workertypes.h"
#include "workerschema.h"

class Worker;
class WorkerManager;
class QContextMenuEvent;
class QGraphicsScene;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

//----------------------------------------------

/**
 * @brief Дерево воркеров: состояние, связи.
 *        Короткий клик открывает чат, сдвиг мыши двигает схему.
 */
class WorkerGraphWidget : public QGraphicsView
{
    Q_OBJECT
public:
    explicit WorkerGraphWidget(QWidget *parent = 0);

    void rebuild(WorkerManager *manager);
    void applyTheme();
    /**
     * @brief Миниатюра текущей сцены для превью у композера.
     *        Пустой pixmap, если граф ещё не построен.
     */
    QPixmap overviewPixmap(QSize size) const;

signals:
    void workerActivated(const QString &id);
    /** Пользователь запросил удаление инструмента (контекстное меню). */
    void toolRemoveRequested(const QString &id);
    /** Удалить завершённого воркера (не root) с графа. */
    void workerRemoveRequested(const QString &id);
    void workerInterruptRequested(const QString &id);
    void reportToParentRequested(const QString &id);
    /** Смена профиля модели воркера с схемы (на лету). */
    void workerProfileChangeRequested(const QString &workerId, const QString &profileId);

protected:
    virtual void mousePressEvent(QMouseEvent *event);
    virtual void mouseMoveEvent(QMouseEvent *event);
    virtual void mouseReleaseEvent(QMouseEvent *event);
    virtual void contextMenuEvent(QContextMenuEvent *event);
    virtual void wheelEvent(QWheelEvent *event);
    virtual void keyPressEvent(QKeyEvent *event);

private slots:
    void sltLocaleChanged();

private:
    static QString typeLabelFor(Worker *worker);
    static QString stateLabelFor(WorkerState state);
    qreal layoutWorker(Worker *worker, qreal left, qreal top);
    qreal layoutTools(qreal left, qreal top);
    void addNode(Worker *worker, qreal x, qreal y);
    void addEdge(Worker *parent, Worker *child);
    void applyZoom(qreal factor);
    QString workerIdAt(const QPoint &pos) const;

    QGraphicsScene         *_scene;
    QPointer<WorkerManager> _manager;
    QHash<Worker *, SchemaNodeItem *> _laidOut;
    qreal                   _zoom;
    bool                    _rebuilding;
    bool                    _pressing;
    bool                    _dragged;
    QPoint                  _pressPos;
    QString                 _pressWorkerId;
};

#endif // WORKERGRAPHWIDGET_H
