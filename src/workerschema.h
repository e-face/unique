#ifndef WORKERSCHEMA_H
#define WORKERSCHEMA_H

#include "workertypes.h"
//----------------------------------------------
#include <QColor>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QList>
#include <QString>
#include <QStringList>

class SchemaEdgeItem;

//----------------------------------------------

/**
 * @brief A worker card on the schema.
 *
 * The schema editor and the Team tab use the same look.
 */
class SchemaNodeItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 41 };

    explicit SchemaNodeItem(bool root);
    ~SchemaNodeItem();

    int type() const { return Type; }
    bool isRoot() const { return _root; }
    WorkerType workerType() const { return _workerType; }
    QString role() const { return _role; }
    QString purpose() const { return _purpose; }
    QString typeCaption() const { return _typeCaption; }
    QString presetId() const { return _presetId; }

    void setWorkerType(WorkerType type, const QString &caption);
    void setRole(const QString &role);
    void setPurpose(const QString &purpose);
    void setExtraLines(const QStringList &lines);
    void setPresetId(const QString &id);
    void setWorkerId(const QString &id);
    void setBorderColor(const QColor &color);
    void setLinkingEnabled(bool on);
    void setMovable(bool on);
    void setPlacing(bool on);

    bool canParent() const;
    bool hitsLinkHandle(const QPointF &itemPos) const;
    QPointF linkAnchor() const;
    SchemaEdgeItem *parentEdge() const { return _parentEdge; }
    const QList<SchemaEdgeItem *> &childEdges() const { return _childEdges; }
    void attachChildEdge(SchemaEdgeItem *edge);
    void attachParentEdge(SchemaEdgeItem *edge);
    void detachEdge(SchemaEdgeItem *edge);
    void relayoutEdges();

    static qreal cardWidth();
    static qreal cardHeight();

    virtual QRectF boundingRect() const;
    virtual void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);

protected:
    virtual QVariant itemChange(GraphicsItemChange change, const QVariant &value);

private:
    QRectF linkHandleRect() const;

    WorkerType               _workerType;
    QString                  _typeCaption;
    QString                  _role;
    QString                  _presetId;
    QString                  _purpose;
    QStringList              _extraLines;
    QColor                   _borderColor;
    bool                     _root;
    bool                     _linkingEnabled;
    SchemaEdgeItem          *_parentEdge;
    QList<SchemaEdgeItem *>  _childEdges;
};

//----------------------------------------------

/**
 * @brief A cubic parent-to-child link with an arrow.
 */
class SchemaEdgeItem : public QGraphicsPathItem
{
public:
    enum { Type = UserType + 42 };

    SchemaEdgeItem(SchemaNodeItem *from, SchemaNodeItem *to);
    ~SchemaEdgeItem();

    int type() const { return Type; }
    SchemaNodeItem *from() const { return _from; }
    SchemaNodeItem *to() const { return _to; }
    void relayout();
    void detach();
    void forget(SchemaNodeItem *node);

    virtual QRectF boundingRect() const;
    virtual QPainterPath shape() const;
    virtual void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);

private:
    SchemaNodeItem *_from;
    SchemaNodeItem *_to;
    QPainterPath    _hitShape;
};

#endif // WORKERSCHEMA_H
