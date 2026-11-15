#include "workergraphwidget.h"
//----------------------------------------------
#include "applocale.h"
#include "appsettings.h"
#include "worker.h"
#include "workermanager.h"
//----------------------------------------------
#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QBrush>
#include <QColor>
#include <QContextMenuEvent>
#include <QCursor>
#include <QFont>
#include <QMenu>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QSet>
#include <QStringList>
#include <QWheelEvent>

//----------------------------------------------

static const qreal kHGap = 28;
static const qreal kVGap = 48;

//----------------------------------------------

static QString profileLabel(Worker *worker)
{
    if (!worker)
        return QString();
    LlmProfile profile;
    if (!worker->llmProfileId().isEmpty())
        profile = AppSettings::instance()->profileById(worker->llmProfileId());
    if (profile.id.isEmpty())
        profile = AppSettings::instance()->profileForWorkerType(worker->type());
    QString name = profile.name.trimmed();
    if (name.isEmpty())
        name = profile.model.trimmed();
    if (name.isEmpty())
        name = profile.id;
    return name;
}

//----------------------------------------------

QString WorkerGraphWidget::typeLabelFor(Worker *worker)
{
    if (!worker)
        return WorkerGraphWidget::tr("Воркер");
    QString role;
    if (worker->isTool())
        role = WorkerGraphWidget::tr("Инструмент");
    else if (worker->isCheckOnly())
        role = WorkerGraphWidget::tr("Проверка");
    else
        role = WorkerGraphWidget::tr("Исполнитель");
    if (worker->isGroupInterlocutor())
        role += WorkerGraphWidget::tr(" · собеседник");
    else if (worker->isGroupChief())
        role += WorkerGraphWidget::tr(" · группа");
    if (!worker->personName().isEmpty())
        return worker->personName() + WorkerGraphWidget::tr(" · ") + role;
    return role;
}

//----------------------------------------------

QString WorkerGraphWidget::stateLabelFor(WorkerState state)
{
    switch (state)
    {
    case WorkerStateRunning:
        return WorkerGraphWidget::tr("выполняется");
    case WorkerStateWaitingApproval:
        return WorkerGraphWidget::tr("ждёт разрешения");
    case WorkerStateWaitingChildren:
        return WorkerGraphWidget::tr("ждёт детей");
    case WorkerStateWaitingContinuation:
        return WorkerGraphWidget::tr("ждёт продолжения");
    case WorkerStateCompleted:
        return WorkerGraphWidget::tr("завершён");
    case WorkerStateFailed:
        return WorkerGraphWidget::tr("ошибка");
    case WorkerStateQueued:
        return WorkerGraphWidget::tr("в очереди");
    case WorkerStateIdle:
    default:
        return WorkerGraphWidget::tr("ожидает");
    }
}

//----------------------------------------------

static QColor stateBorder(WorkerState state)
{
    switch (state)
    {
    case WorkerStateRunning:
        return QColor(QString::fromUtf8("#1565c0"));
    case WorkerStateWaitingApproval:
        return QColor(QString::fromUtf8("#e04b32"));
    case WorkerStateWaitingChildren:
        return QColor(QString::fromUtf8("#7b3fb0"));
    case WorkerStateWaitingContinuation:
        return QColor(QString::fromUtf8("#e08612"));
    case WorkerStateCompleted:
        return QColor(QString::fromUtf8("#1f8f3a"));
    case WorkerStateFailed:
        return QColor(QString::fromUtf8("#d01212"));
    case WorkerStateQueued:
        return QColor(QString::fromUtf8("#c49208"));
    case WorkerStateIdle:
    default:
        return AppSettings::instance()->borderColor();
    }
}

//----------------------------------------------

WorkerGraphWidget::WorkerGraphWidget(QWidget *parent)
    : QGraphicsView(parent)
    , _scene(new QGraphicsScene(this))
    , _zoom(1.0)
    , _rebuilding(false)
    , _pressing(false)
    , _dragged(false)
{
    setScene(_scene);
    setRenderHint(QPainter::Antialiasing, true);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setMinimumSize(280, 220);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(tr("Колёсико мыши — масштаб, перетаскивание — сдвиг, 0 — сброс"));
    setFrameShape(QFrame::NoFrame);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    applyTheme();
    connect(AppLocale::instance(), SIGNAL(languageChanged(QString)),
            this, SLOT(sltLocaleChanged()));
}

//----------------------------------------------

void WorkerGraphWidget::applyTheme()
{
    // The same background as the window and the chat feed, with no brightness step on the tab.
    const QColor bg = AppSettings::instance()->isLightTheme()
                      ? AppSettings::instance()->windowColor()
                      : AppSettings::instance()->panelColor();
    _scene->setBackgroundBrush(QBrush(bg));
    setBackgroundBrush(QBrush(bg));
    QPalette pal = viewport()->palette();
    pal.setColor(QPalette::Base, bg);
    pal.setColor(QPalette::Window, bg);
    viewport()->setPalette(pal);
    viewport()->setAutoFillBackground(true);
}

//----------------------------------------------

QPixmap WorkerGraphWidget::overviewPixmap(QSize size) const
{
    if (!_scene || size.width() < 8 || size.height() < 8)
        return QPixmap();
    QRectF bounds = _scene->itemsBoundingRect();
    if (bounds.isEmpty() || !bounds.isValid())
    {
        QPixmap empty(size);
        empty.fill(Qt::transparent);
        return empty;
    }
    bounds.adjust(-16, -16, 16, 16);
    QPixmap pix(size);
    pix.fill(Qt::transparent);
    const QBrush savedBackground = _scene->backgroundBrush();
    _scene->setBackgroundBrush(Qt::NoBrush);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    _scene->render(&painter, QRectF(0, 0, size.width(), size.height()), bounds,
                   Qt::KeepAspectRatio);
    painter.end();
    _scene->setBackgroundBrush(savedBackground);
    return pix;
}

//----------------------------------------------

void WorkerGraphWidget::rebuild(WorkerManager *manager)
{
    if (_rebuilding)
        return;
    _rebuilding = true;
    _laidOut.clear();
    _scene->blockSignals(true);
    _scene->clear();
    _scene->blockSignals(false);
    _manager = manager;
    if (!manager)
    {
        _rebuilding = false;
        return;
    }

    qreal left = 20;
    const qreal top = 20;
    foreach (Worker *root, manager->roots())
    {
        const qreal width = layoutWorker(root, left, top);
        left += width + kHGap * 2;
    }
    // Tools sit in a separate column to the right of the tree, past a divider.
    if (!manager->tools().isEmpty())
    {
        const qreal treeRight = _scene->itemsBoundingRect().right();
        const qreal toolsLeft = qMax(left, treeRight + kHGap * 2);
        layoutTools(toolsLeft, top);
    }

    // Dotted frames of discussion groups.
    QSet<QString> drawnGroups;
    foreach (Worker *worker, manager->workers())
    {
        if (!worker || worker->groupId().isEmpty())
            continue;
        if (drawnGroups.contains(worker->groupId()))
            continue;
        drawnGroups.insert(worker->groupId());
        QRectF box;
        bool any = false;
        foreach (Worker *member, manager->groupMembers(worker->groupId()))
        {
            if (!member || !member->property("graphX").isValid())
                continue;
            const QRectF r(member->property("graphX").toReal() - 10,
                           member->property("graphY").toReal() - 10,
                           SchemaNodeItem::cardWidth() + 20, SchemaNodeItem::cardHeight() + 20);
            box = any ? box.united(r) : r;
            any = true;
        }
        if (!any)
            continue;
        QPen pen(AppSettings::instance()->accentColor(), 1.6, Qt::DashLine);
        QPainterPath framePath;
        framePath.addRoundedRect(box.adjusted(-4, -4, 4, 4), 12, 12);
        QGraphicsPathItem *frame = _scene->addPath(framePath, pen, Qt::NoBrush);
        frame->setZValue(0);
        frame->setFlag(QGraphicsItem::ItemIsSelectable, false);
    }

    _scene->setSceneRect(_scene->itemsBoundingRect().adjusted(-20, -20, 20, 20));
    _rebuilding = false;
}

//----------------------------------------------

qreal WorkerGraphWidget::layoutTools(qreal left, qreal top)
{
    if (!_manager)
        return 0;
    const QList<Worker *> tools = _manager->tools();
    if (tools.isEmpty())
        return 0;

    const qreal columnLeft = left + kHGap;
    const qreal bottom = qMax(_scene->itemsBoundingRect().bottom(),
                              top + 30 + tools.size() * (SchemaNodeItem::cardHeight() + kVGap / 2));
    QPen separator(AppSettings::instance()->borderColor(), 1.2, Qt::DashLine);
    QGraphicsLineItem *line = _scene->addLine(left, top - 10, left, bottom, separator);
    line->setZValue(0);

    QGraphicsTextItem *header = _scene->addText(tr("Инструменты"));
    QFont bold = font();
    bold.setBold(true);
    header->setFont(bold);
    header->setDefaultTextColor(AppSettings::instance()->textColor());
    header->setPos(columnLeft, top - 6);
    header->setZValue(2);

    qreal y = top + 26;
    foreach (Worker *tool, tools)
    {
        addNode(tool, columnLeft, y);
        y += SchemaNodeItem::cardHeight() + kVGap / 2;
    }
    return SchemaNodeItem::cardWidth() + kHGap;
}

//----------------------------------------------

qreal WorkerGraphWidget::layoutWorker(Worker *worker, qreal left, qreal top)
{
    if (!worker)
        return SchemaNodeItem::cardWidth();

    if (worker->children().isEmpty())
    {
        addNode(worker, left, top);
        return SchemaNodeItem::cardWidth();
    }

    // Place group peers next to each other so the dotted
    // frame does not enclose ordinary executor children between them.
    QList<Worker *> peers;
    QList<Worker *> others;
    foreach (Worker *child, worker->children())
    {
        if (!child)
            continue;
        if (child->isGroupInterlocutor()
            && !worker->groupId().isEmpty()
            && child->groupId() == worker->groupId())
            peers.append(child);
        else
            others.append(child);
    }
    QList<Worker *> ordered = peers;
    ordered += others;

    qreal x = left;
    qreal childTop = top + SchemaNodeItem::cardHeight() + kVGap;
    qreal total = 0;
    foreach (Worker *child, ordered)
    {
        const qreal width = layoutWorker(child, x, childTop);
        x += width + kHGap;
        total += width + kHGap;
    }
    if (total > 0)
        total -= kHGap;

    const qreal nodeLeft = left + qMax(qreal(0), (total - SchemaNodeItem::cardWidth()) / 2);
    addNode(worker, nodeLeft, top);
    foreach (Worker *child, ordered)
        addEdge(worker, child);
    return qMax(total, SchemaNodeItem::cardWidth());
}

//----------------------------------------------

void WorkerGraphWidget::addNode(Worker *worker, qreal x, qreal y)
{
    if (!worker)
        return;

    worker->setProperty("graphX", x);
    worker->setProperty("graphY", y);

    QString caption;
    if (worker->isTool())
        caption = tr("Инструмент");
    else if (worker->isCheckOnly())
        caption = tr("Проверка");
    else
        caption = tr("Исполнитель");
    if (worker->isGroupInterlocutor())
        caption += tr(" · собеседник");
    else if (worker->isGroupChief())
        caption += tr(" · группа");

    QString title = worker->personName().trimmed();
    if (worker->isTool())
    {
        const QString name = worker->toolName().simplified();
        if (!name.isEmpty())
            title = name;
    }
    if (title.isEmpty())
        title = worker->displayLabel().simplified();

    const QString profileName = profileLabel(worker);
    QStringList extra;
    if (!profileName.isEmpty())
        extra << tr("модель: %1").arg(profileName);

    QString purpose;
    QString tip;
    if (worker->isTool())
    {
        const bool busy = _manager && _manager->toolBusy(worker);
        purpose = busy ? tr("занят") : tr("свободен");
        const int queued = _manager ? _manager->toolQueueLength(worker) : 0;
        if (queued > 0)
            purpose += tr(", в очереди: %1").arg(queued);
        const QString toolPurpose = worker->toolPurpose().simplified();
        if (!toolPurpose.isEmpty())
            extra << toolPurpose;
        tip = worker->toolPurpose().trimmed();
    }
    else
    {
        purpose = stateLabelFor(worker->state());
        const QString task = worker->displayLabel().simplified();
        if (!task.isEmpty() && !worker->isRoot() && task != title)
            extra << task;
        tip = worker->displayLabel();
        if (!profileName.isEmpty())
        {
            if (!tip.isEmpty())
                tip += QString::fromUtf8("\n");
            tip += tr("Профиль: %1").arg(profileName);
        }
    }

    const QList<Worker *> waiting = worker->unfinishedChildren();
    if (!waiting.isEmpty())
    {
        QStringList names;
        foreach (Worker *child, waiting)
            names.append(typeLabelFor(child));
        extra << tr("ждёт: %1").arg(names.join(tr(", ")));
    }

    SchemaNodeItem *node = new SchemaNodeItem(worker->isRoot());
    node->setPos(x, y);
    node->setMovable(false);
    node->setLinkingEnabled(false);
    node->setWorkerId(worker->id());
    node->setBorderColor(stateBorder(worker->state()));
    node->setWorkerType(worker->type(), caption);
    node->setRole(title);
    node->setPurpose(purpose);
    node->setExtraLines(extra);
    if (!tip.trimmed().isEmpty())
        node->setToolTip(tip);
    _scene->addItem(node);
    _laidOut.insert(worker, node);
}

//----------------------------------------------

void WorkerGraphWidget::addEdge(Worker *parent, Worker *child)
{
    if (!parent || !child)
        return;
    SchemaNodeItem *from = _laidOut.value(parent, 0);
    SchemaNodeItem *to = _laidOut.value(child, 0);
    if (!from || !to)
        return;
    SchemaEdgeItem *edge = new SchemaEdgeItem(from, to);
    edge->setFlag(QGraphicsItem::ItemIsSelectable, false);
    _scene->addItem(edge);
}

//----------------------------------------------

QString WorkerGraphWidget::workerIdAt(const QPoint &pos) const
{
    foreach (QGraphicsItem *item, items(pos))
    {
        const QString id = item->data(0).toString();
        if (!id.isEmpty())
            return id;
    }
    return QString();
}

//----------------------------------------------

void WorkerGraphWidget::mousePressEvent(QMouseEvent *event)
{
    if (!event)
        return;
    if (event->button() == Qt::RightButton)
    {
        // The context menu is handled by contextMenuEvent.
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton)
    {
        _pressing = true;
        _dragged = false;
        _pressPos = event->pos();
        _pressWorkerId = workerIdAt(event->pos());
    }
    QGraphicsView::mousePressEvent(event);
}

//----------------------------------------------

void WorkerGraphWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!event)
        return;
    if (_pressing && !_dragged)
    {
        const QPoint delta = event->pos() - _pressPos;
        if (delta.manhattanLength() >= QApplication::startDragDistance())
            _dragged = true;
    }
    QGraphicsView::mouseMoveEvent(event);
}

//----------------------------------------------

void WorkerGraphWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!event)
        return;
    QString id;
    bool openChat = false;
    if (event->button() == Qt::LeftButton && _pressing)
    {
        openChat = !_dragged && !_pressWorkerId.isEmpty();
        id = _pressWorkerId;
        _pressing = false;
        _dragged = false;
        _pressWorkerId.clear();
    }
    QGraphicsView::mouseReleaseEvent(event);
    if (openChat)
        emit workerActivated(id);
}

//----------------------------------------------

void WorkerGraphWidget::contextMenuEvent(QContextMenuEvent *event)
{
    if (!event)
        return;
    const QString id = workerIdAt(event->pos());
    Worker *worker = (_manager && !id.isEmpty()) ? _manager->workerById(id) : 0;
    if (!worker)
    {
        QGraphicsView::contextMenuEvent(event);
        return;
    }

    QMenu menu(this);
    QMenu *profileMenu = menu.addMenu(tr("Профиль модели"));
    QString currentProfileId = worker->llmProfileId().trimmed();
    if (currentProfileId.isEmpty())
        currentProfileId = AppSettings::instance()->profileForWorkerType(worker->type()).id;
    QActionGroup *profileGroup = new QActionGroup(profileMenu);
    profileGroup->setExclusive(true);
    const QList<LlmProfile> profiles = AppSettings::instance()->profiles();
    if (profiles.isEmpty())
    {
        QAction *empty = profileMenu->addAction(tr("(нет профилей в настройках)"));
        empty->setEnabled(false);
    }
    else
    {
        foreach (const LlmProfile &profile, profiles)
        {
            QString title = profile.name.trimmed();
            if (title.isEmpty())
                title = profile.model.trimmed();
            if (title.isEmpty())
                title = profile.id;
            if (!profile.model.trimmed().isEmpty()
                && profile.name.trimmed() != profile.model.trimmed())
                title += QString::fromUtf8(" · ") + profile.model.trimmed();
            QAction *act = profileMenu->addAction(title);
            act->setCheckable(true);
            act->setData(profile.id);
            act->setChecked(profile.id == currentProfileId);
            profileGroup->addAction(act);
        }
    }

    menu.addSeparator();
    QAction *stopAction = menu.addAction(tr("Прервать воркер"));
    QAction *reportAction = 0;
    if (!worker->isRoot())
        reportAction = menu.addAction(tr("Отправить отчёт родителю"));
    QAction *removeAction = 0;
    if (worker->isTool())
    {
        menu.addSeparator();
        removeAction = menu.addAction(tr("Удалить инструмент"));
    }
    QAction *workerRemoveAction = 0;
    if (!worker->isRoot())
    {
        const WorkerState st = worker->state();
        if (st == WorkerStateCompleted || st == WorkerStateFailed)
        {
            if (!removeAction)
                menu.addSeparator();
            workerRemoveAction = menu.addAction(tr("Удалить воркера"));
        }
    }
    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen)
        return;
    if (chosen == stopAction)
        emit workerInterruptRequested(id);
    else if (reportAction && chosen == reportAction)
        emit reportToParentRequested(id);
    else if (removeAction && chosen == removeAction)
        emit toolRemoveRequested(id);
    else if (workerRemoveAction && chosen == workerRemoveAction)
        emit workerRemoveRequested(id);
    else if (profileGroup->actions().contains(chosen))
    {
        const QString profileId = chosen->data().toString().trimmed();
        if (!profileId.isEmpty() && profileId != currentProfileId)
            emit workerProfileChangeRequested(id, profileId);
    }
}

//----------------------------------------------

void WorkerGraphWidget::applyZoom(qreal factor)
{
    const qreal next = _zoom * factor;
    if (next < 0.25 || next > 4.0)
        return;
    scale(factor, factor);
    _zoom = next;
}

//----------------------------------------------

void WorkerGraphWidget::wheelEvent(QWheelEvent *event)
{
    if (!event)
        return;
    const int delta = event->angleDelta().y();
    if (delta == 0)
    {
        QGraphicsView::wheelEvent(event);
        return;
    }
    applyZoom(delta > 0 ? 1.15 : (1.0 / 1.15));
    event->accept();
}

//----------------------------------------------

void WorkerGraphWidget::keyPressEvent(QKeyEvent *event)
{
    if (!event)
        return;
    if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal)
    {
        applyZoom(1.15);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Minus)
    {
        applyZoom(1.0 / 1.15);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_0)
    {
        if (_zoom > 0)
            scale(1.0 / _zoom, 1.0 / _zoom);
        _zoom = 1.0;
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

//----------------------------------------------

void WorkerGraphWidget::sltLocaleChanged()
{
    setToolTip(tr("Колёсико мыши — масштаб, перетаскивание — сдвиг, 0 — сброс"));
    rebuild(_manager.data());
}
