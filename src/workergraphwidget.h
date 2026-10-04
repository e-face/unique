#ifndef WORKERGRAPHWIDGET_H
#define WORKERGRAPHWIDGET_H

#include <QGraphicsView>
#include <QPointer>

#include "workertypes.h"

class Worker;
class WorkerManager;
class QContextMenuEvent;
class QGraphicsScene;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

//----------------------------------------------

/**
 * @brief Дерево воркеров: состояние, связи, клик открывает чат.
 */
class WorkerGraphWidget : public QGraphicsView
{
    Q_OBJECT
public:
    explicit WorkerGraphWidget(QWidget *parent = 0);

    void rebuild(WorkerManager *manager);
    void applyTheme();

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
    virtual void contextMenuEvent(QContextMenuEvent *event);
    virtual void wheelEvent(QWheelEvent *event);
    virtual void keyPressEvent(QKeyEvent *event);

private slots:
    void sltSelectionChanged();
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
    qreal                   _zoom;
    bool                    _rebuilding;
};

#endif // WORKERGRAPHWIDGET_H
