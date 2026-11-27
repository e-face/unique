#include "workerschema.h"
//----------------------------------------------
#include "appicons.h"
#include "appsettings.h"
//----------------------------------------------
#include <QFontMetrics>
#include <QLinearGradient>
#include <QLineF>
#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

//----------------------------------------------

static const qreal kCardW = 208;
static const qreal kCardH = 104;
static const qreal kIcon = 20;

//----------------------------------------------

qreal SchemaNodeItem::cardWidth()
{
    return kCardW;
}

//----------------------------------------------

qreal SchemaNodeItem::cardHeight()
{
    return kCardH;
}

//----------------------------------------------

SchemaNodeItem::SchemaNodeItem(bool root)
    : QGraphicsItem()
    , _workerType(WorkerTypeExecutor)
    , _typeCaption()
    , _role()
    , _presetId()
    , _purpose()
    , _extraLines()
    , _borderColor()
    , _root(root)
    , _linkingEnabled(false)
    , _parentEdge(0)
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setZValue(2);
    setCursor(Qt::OpenHandCursor);
}

//----------------------------------------------

SchemaNodeItem::~SchemaNodeItem()
{
    if (_parentEdge)
        _parentEdge->forget(this);
    foreach (SchemaEdgeItem *edge, _childEdges)
    {
        if (edge)
            edge->forget(this);
    }
    _parentEdge = 0;
    _childEdges.clear();
}

//----------------------------------------------

void SchemaNodeItem::setWorkerType(WorkerType type, const QString &caption)
{
    _workerType = normalizeWorkerType(type);
    _typeCaption = caption;
    update();
}

//----------------------------------------------

void SchemaNodeItem::setRole(const QString &role)
{
    _role = role;
    update();
}

//----------------------------------------------

void SchemaNodeItem::setPurpose(const QString &purpose)
{
    _purpose = purpose;
    setToolTip(purpose.trimmed());
    update();
}

//----------------------------------------------

void SchemaNodeItem::setExtraLines(const QStringList &lines)
{
    _extraLines = lines;
    update();
}

//----------------------------------------------

void SchemaNodeItem::setPresetId(const QString &id)
{
    _presetId = id.trimmed();
}

//----------------------------------------------

void SchemaNodeItem::setWorkerId(const QString &id)
{
    setData(0, id);
}

//----------------------------------------------

void SchemaNodeItem::setBorderColor(const QColor &color)
{
    _borderColor = color;
    update();
}

//----------------------------------------------

void SchemaNodeItem::setLinkingEnabled(bool on)
{
    _linkingEnabled = on;
    update();
}

//----------------------------------------------

void SchemaNodeItem::setMovable(bool on)
{
    setFlag(ItemIsMovable, on);
    if (!on)
        setCursor(Qt::PointingHandCursor);
    else
        setCursor(Qt::OpenHandCursor);
}

//----------------------------------------------

void SchemaNodeItem::setPlacing(bool on)
{
    setFlag(ItemIsMovable, !on);
    setFlag(ItemIsSelectable, !on);
    setOpacity(on ? 0.88 : 1.0);
    setCursor(on ? Qt::CrossCursor : Qt::OpenHandCursor);
    setZValue(on ? 6 : 2);
}

//----------------------------------------------

QRectF SchemaNodeItem::boundingRect() const
{
    return QRectF(0, 0, kCardW, kCardH);
}

//----------------------------------------------

QRectF SchemaNodeItem::linkHandleRect() const
{
    return QRectF(kCardW / 2.0 - 6.0, kCardH - 14.0, 12.0, 12.0);
}

//----------------------------------------------

bool SchemaNodeItem::canParent() const
{
    return _workerType == WorkerTypeExecutor;
}

//----------------------------------------------

bool SchemaNodeItem::hitsLinkHandle(const QPointF &itemPos) const
{
    if (!_linkingEnabled || !canParent())
        return false;
    return linkHandleRect().adjusted(-6, -6, 6, 6).contains(itemPos);
}

//----------------------------------------------

QPointF SchemaNodeItem::linkAnchor() const
{
    return mapToScene(QPointF(kCardW / 2.0, kCardH - 2.0));
}

//----------------------------------------------

void SchemaNodeItem::attachChildEdge(SchemaEdgeItem *edge)
{
    if (edge && !_childEdges.contains(edge))
        _childEdges.append(edge);
}

//----------------------------------------------

void SchemaNodeItem::attachParentEdge(SchemaEdgeItem *edge)
{
    _parentEdge = edge;
}

//----------------------------------------------

void SchemaNodeItem::detachEdge(SchemaEdgeItem *edge)
{
    if (!edge)
        return;
    if (_parentEdge == edge)
        _parentEdge = 0;
    _childEdges.removeAll(edge);
}

//----------------------------------------------

void SchemaNodeItem::relayoutEdges()
{
    if (_parentEdge)
        _parentEdge->relayout();
    foreach (SchemaEdgeItem *edge, _childEdges)
    {
        if (edge)
            edge->relayout();
    }
}

//----------------------------------------------

QVariant SchemaNodeItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    const QVariant result = QGraphicsItem::itemChange(change, value);
    if (change == ItemPositionHasChanged)
        relayoutEdges();
    return result;
}

//----------------------------------------------

void SchemaNodeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    AppSettings *settings = AppSettings::instance();
    QColor border = _borderColor.isValid() ? _borderColor : settings->borderColor();
    const bool stateMark = _borderColor.isValid()
                           && _borderColor != settings->borderColor();
    qreal borderWidth = stateMark ? 3.0 : 1.2;
    if (!_borderColor.isValid() && isSelected())
    {
        border = settings->accentColor();
        borderWidth = 2.0;
    }
    qreal inset = 1.0;
    if (stateMark)
    {
        const qreal scale = qAbs(painter->transform().m11());
        const qreal safe = scale > 0.02 ? scale : 0.02;
        inset = (borderWidth * 0.5) / safe + 0.5;
        const qreal limit = qMin(boundingRect().width(), boundingRect().height()) * 0.22;
        if (inset > limit)
            inset = limit;
    }
    const QRectF card = boundingRect().adjusted(inset, inset, -inset, -inset);
    QColor bottom;
    QColor top;
    if (_workerType == WorkerTypeTool)
    {
        top = QColor(QString::fromUtf8("#ffffff"));
        bottom = QColor(QString::fromUtf8("#e4e4e4"));
    }
    else
    {
        bottom = settings->workerCardColor(_workerType, _root);
        top = bottom.lighter(116);
    }
    QLinearGradient fill(card.topLeft(), card.bottomLeft());
    fill.setColorAt(0.0, top);
    fill.setColorAt(1.0, bottom);
    const QColor ink = settings->workerCardTextColor();
    const QColor muted = settings->workerCardMutedColor();
    QPen pen(border, borderWidth);
    pen.setCosmetic(stateMark);
    pen.setJoinStyle(Qt::RoundJoin);
    if (_workerType == WorkerTypeTool && !_root)
        pen.setStyle(Qt::DashLine);
    painter->setPen(pen);
    painter->setBrush(fill);
    painter->drawRoundedRect(card, 10, 10);

    const QIcon icon = AppIcons::forWorkerType(_workerType);
    const QPixmap picture = icon.pixmap(int(kIcon), int(kIcon));
    if (!picture.isNull())
        painter->drawPixmap(QPointF(8, 8), picture);

    if (_linkingEnabled && canParent())
    {
        painter->setPen(QPen(settings->borderColor(), 1));
        painter->setBrush(QColor(QString::fromUtf8("#ffffff")));
        painter->drawEllipse(linkHandleRect());
    }

    const QRectF textRect(36, 6, kCardW - 46, kCardH - 18);
    QStringList lines;
    const QString purpose = _purpose.simplified();
    if (!purpose.isEmpty())
        lines << purpose;
    foreach (const QString &extra, _extraLines)
    {
        const QString line = extra.simplified();
        if (!line.isEmpty())
            lines << line;
    }

    // Point size follows the screen DPI and the application font, so on some
    // displays the glyphs are taller than the card and the rest is clipped.
    // Pixel size is a fraction of the card, in the same units as the shape,
    // and the view transform scales both together.
    const QFont baseFont = painter->font();
    const int bodyLines = qMin(2, lines.size());
    int captionPx = qMax(1, qRound(textRect.height() * 0.22));
    int rolePx = qMax(captionPx, qRound(textRect.height() * 0.26));
    QFont captionFont = baseFont;
    QFont roleFont = baseFont;
    captionFont.setBold(false);
    roleFont.setBold(true);
    for (;;)
    {
        captionFont.setPixelSize(captionPx);
        roleFont.setPixelSize(rolePx);
        const int captionH = QFontMetrics(captionFont).height();
        const int roleH = QFontMetrics(roleFont).height();
        const qreal used = captionH + roleH + 1 + bodyLines * captionH;
        if (used <= textRect.height() + 0.5 || captionPx <= 1)
            break;
        --captionPx;
        rolePx = qMax(1, captionPx + 1);
    }
    painter->setFont(captionFont);
    const QFontMetrics captionFm(captionFont);
    qreal y = textRect.top();
    painter->setPen(muted);
    painter->drawText(QRectF(textRect.left(), y, textRect.width(), captionFm.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      captionFm.elidedText(_typeCaption, Qt::ElideRight, int(textRect.width())));
    y += captionFm.height();

    painter->setFont(roleFont);
    painter->setPen(ink);
    const QFontMetrics roleFm(roleFont);
    const QString title = _role.trimmed().isEmpty() ? _typeCaption : _role.trimmed();
    painter->drawText(QRectF(textRect.left(), y, textRect.width(), roleFm.height()),
                      Qt::AlignLeft | Qt::AlignVCenter,
                      roleFm.elidedText(title, Qt::ElideRight, int(textRect.width())));
    y += roleFm.height() + 1;

    painter->setFont(captionFont);
    painter->setPen(muted);
    foreach (const QString &line, lines)
    {
        if (y + captionFm.height() > textRect.bottom())
            break;
        painter->drawText(QRectF(textRect.left(), y, textRect.width(), captionFm.height()),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          captionFm.elidedText(line, Qt::ElideRight, int(textRect.width())));
        y += captionFm.height();
    }
    painter->restore();
}

//----------------------------------------------

SchemaEdgeItem::SchemaEdgeItem(SchemaNodeItem *from, SchemaNodeItem *to)
    : QGraphicsPathItem()
    , _from(from)
    , _to(to)
{
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setZValue(1);
    if (_from)
        _from->attachChildEdge(this);
    if (_to)
        _to->attachParentEdge(this);
    relayout();
}

//----------------------------------------------

SchemaEdgeItem::~SchemaEdgeItem()
{
    detach();
}

//----------------------------------------------

void SchemaEdgeItem::forget(SchemaNodeItem *node)
{
    if (_from == node)
        _from = 0;
    if (_to == node)
        _to = 0;
}

//----------------------------------------------

void SchemaEdgeItem::detach()
{
    SchemaNodeItem *from = _from;
    SchemaNodeItem *to = _to;
    _from = 0;
    _to = 0;
    if (from)
        from->detachEdge(this);
    if (to)
        to->detachEdge(this);
}

//----------------------------------------------

void SchemaEdgeItem::relayout()
{
    if (!_from || !_to)
        return;
    const QPointF a = _from->linkAnchor();
    const QPointF b = _to->mapToScene(QPointF(kCardW / 2.0, 1));
    const qreal midY = (a.y() + b.y()) / 2.0;
    QPainterPath path;
    path.moveTo(a);
    path.cubicTo(QPointF(a.x(), midY), QPointF(b.x(), midY), b);
    setPath(path);
    QPainterPathStroker stroker;
    stroker.setWidth(12);
    _hitShape = stroker.createStroke(path);
}

//----------------------------------------------

QRectF SchemaEdgeItem::boundingRect() const
{
    return QGraphicsPathItem::boundingRect().adjusted(-14, -14, 14, 14);
}

//----------------------------------------------

QPainterPath SchemaEdgeItem::shape() const
{
    return _hitShape;
}

//----------------------------------------------

void SchemaEdgeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->save();
    AppSettings *settings = AppSettings::instance();
    const QColor color = isSelected()
                         ? settings->accentColor()
                         : settings->workerCardMutedColor();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(color, isSelected() ? 2.6 : 1.8, Qt::SolidLine, Qt::RoundCap));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path());
    if (path().elementCount() >= 2)
    {
        const QPointF tip = path().pointAtPercent(1);
        const QPointF prev = path().pointAtPercent(0.92);
        QLineF travel(prev, tip);
        if (travel.length() >= 1)
        {
            QLineF wing1 = QLineF::fromPolar(11, travel.angle() + 155);
            QLineF wing2 = QLineF::fromPolar(11, travel.angle() - 155);
            wing1.translate(tip);
            wing2.translate(tip);
            painter->setBrush(color);
            painter->setPen(Qt::NoPen);
            QPolygonF head;
            head << tip << wing1.p2() << wing2.p2();
            painter->drawPolygon(head);
        }
    }
    painter->restore();
}
