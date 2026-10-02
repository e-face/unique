#include "workergraphwidget.h"
//----------------------------------------------
#include "appicons.h"
#include "appsettings.h"
#include "worker.h"
#include "workermanager.h"
//----------------------------------------------
#include <QAction>
#include <QActionGroup>
#include <QBrush>
#include <QColor>
#include <QContextMenuEvent>
#include <QCursor>
#include <QFont>
#include <QFontMetrics>
#include <QMenu>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QSet>
#include <QStringList>
#include <QWheelEvent>

//----------------------------------------------

static const qreal kNodeWidth  = 200;
static const qreal kNodeHeight = 96;
static const qreal kHGap       = 28;
static const qreal kVGap       = 48;
static const qreal kIconSize   = 20;
static const qreal kPad        = 6;

//----------------------------------------------

static QFont nodeFont(const QWidget *widget)
{
    QFont f = widget ? widget->font() : QFont();
    const int pt = f.pointSize();
    if (pt > 0)
        f.setPointSize(qMax(6, (pt * 2 + 1) / 3));
    else
    {
        const int px = f.pixelSize();
        if (px > 0)
            f.setPixelSize(qMax(8, (px * 2 + 1) / 3));
    }
    return f;
}

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

static QString typeLabel(Worker *worker)
{
    if (!worker)
        return QString::fromUtf8("Воркер");
    QString role;
    if (worker->isRoot())
        role = QString::fromUtf8("Главный менеджер");
    else
    {
        switch (worker->type())
        {
        case WorkerTypeExecutor:
            role = QString::fromUtf8("Исполнитель");
            break;
        case WorkerTypeTester:
            role = QString::fromUtf8("Тестировщик");
            break;
        case WorkerTypeTool:
            role = QString::fromUtf8("Инструмент");
            break;
        case WorkerTypeManager:
        default:
            role = QString::fromUtf8("Менеджер");
            break;
        }
    }
    if (worker->isGroupInterlocutor())
        role += QString::fromUtf8(" · собеседник");
    else if (worker->isGroupChief())
        role += QString::fromUtf8(" · группа");
    if (!worker->personName().isEmpty())
        return worker->personName() + QString::fromUtf8(" · ") + role;
    return role;
}

//----------------------------------------------

static QString stateLabel(WorkerState state)
{
    switch (state)
    {
    case WorkerStateRunning:
        return QString::fromUtf8("выполняется");
    case WorkerStateWaitingApproval:
        return QString::fromUtf8("ждёт разрешения");
    case WorkerStateWaitingChildren:
        return QString::fromUtf8("ждёт детей");
    case WorkerStateWaitingContinuation:
        return QString::fromUtf8("ждёт продолжения");
    case WorkerStateCompleted:
        return QString::fromUtf8("завершён");
    case WorkerStateFailed:
        return QString::fromUtf8("ошибка");
    case WorkerStateQueued:
        return QString::fromUtf8("в очереди");
    case WorkerStateIdle:
    default:
        return QString::fromUtf8("ожидает");
    }
}

//----------------------------------------------

static QColor stateColor(WorkerState state)
{
    const bool light = AppSettings::instance()->isLightTheme();
    switch (state)
    {
    case WorkerStateRunning:
        return QColor(QString::fromUtf8(light ? "#dbeafe" : "#1a5580"));
    case WorkerStateWaitingApproval:
        return QColor(QString::fromUtf8(light ? "#fee2e2" : "#b44532"));
    case WorkerStateWaitingChildren:
        return QColor(QString::fromUtf8(light ? "#ede9fe" : "#3d2e55"));
    case WorkerStateWaitingContinuation:
        return QColor(QString::fromUtf8(light ? "#ffedd5" : "#d9785c"));
    case WorkerStateCompleted:
        return QColor(QString::fromUtf8(light ? "#dcfce7" : "#2d5640"));
    case WorkerStateFailed:
        return QColor(QString::fromUtf8(light ? "#fecaca" : "#5a3232"));
    case WorkerStateQueued:
        return QColor(QString::fromUtf8(light ? "#fef3c7" : "#4a4632"));
    case WorkerStateIdle:
    default:
        return QColor(QString::fromUtf8(light ? "#f3f4f6" : "#4a4a4a"));
    }
}

//----------------------------------------------

static QColor stateBorder(WorkerState state)
{
    switch (state)
    {
    case WorkerStateRunning:
        return QColor(QString::fromUtf8("#5aa2ff"));
    case WorkerStateWaitingApproval:
        return QColor(QString::fromUtf8("#e07a5f"));
    case WorkerStateWaitingChildren:
        return QColor(QString::fromUtf8("#c586c0"));
    case WorkerStateWaitingContinuation:
        return QColor(QString::fromUtf8("#e8a090"));
    case WorkerStateCompleted:
        return QColor(QString::fromUtf8("#6a9955"));
    case WorkerStateFailed:
        return QColor(QString::fromUtf8("#f44747"));
    case WorkerStateQueued:
        return QColor(QString::fromUtf8("#d7ba7d"));
    case WorkerStateIdle:
    default:
        return QColor(QString::fromUtf8("#8a8a8a"));
    }
}

//----------------------------------------------

static QString elideLine(QString text, const QFontMetrics &fm, int maxWidth)
{
    text = text.simplified();
    if (text.isEmpty())
        return text;
    return fm.elidedText(text, Qt::ElideRight, maxWidth);
}

//----------------------------------------------

WorkerGraphWidget::WorkerGraphWidget(QWidget *parent)
    : QGraphicsView(parent)
    , _scene(new QGraphicsScene(this))
    , _zoom(1.0)
    , _rebuilding(false)
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
    applyTheme();
    connect(_scene, SIGNAL(selectionChanged()), this, SLOT(sltSelectionChanged()));
}

//----------------------------------------------

void WorkerGraphWidget::applyTheme()
{
    // Тот же фон, что у страниц чатов (панель), без рамки QGraphicsView,
    // чтобы вкладка «Команда» не отличалась оттенком от остальных.
    const QColor panel = AppSettings::instance()->panelColor();
    _scene->setBackgroundBrush(QBrush(panel));
    setBackgroundBrush(QBrush(panel));
    QPalette pal = viewport()->palette();
    pal.setColor(QPalette::Base, panel);
    pal.setColor(QPalette::Window, panel);
    viewport()->setPalette(pal);
    viewport()->setAutoFillBackground(true);
}

//----------------------------------------------

void WorkerGraphWidget::rebuild(WorkerManager *manager)
{
    if (_rebuilding)
        return;
    _rebuilding = true;
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
    // Инструменты — отдельной колонкой справа от дерева, за разделителем.
    if (!manager->tools().isEmpty())
    {
        const qreal treeRight = _scene->itemsBoundingRect().right();
        const qreal toolsLeft = qMax(left, treeRight + kHGap * 2);
        layoutTools(toolsLeft, top);
    }

    // Пунктирные рамки групп обсуждения.
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
                           kNodeWidth + 20, kNodeHeight + 20);
            box = any ? box.united(r) : r;
            any = true;
        }
        if (!any)
            continue;
        QPen pen(AppSettings::instance()->accentColor(), 1.6, Qt::DashLine);
        QGraphicsRectItem *frame = _scene->addRect(box.adjusted(-4, -4, 4, 4),
                                                   pen, Qt::NoBrush);
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
                              top + 30 + tools.size() * (kNodeHeight + kVGap / 2));
    QPen separator(AppSettings::instance()->borderColor(), 1.2, Qt::DashLine);
    QGraphicsLineItem *line = _scene->addLine(left, top - 10, left, bottom, separator);
    line->setZValue(0);

    QGraphicsTextItem *header = _scene->addText(QString::fromUtf8("Инструменты"));
    QFont bold = font();
    bold.setBold(true);
    header->setFont(bold);
    header->setDefaultTextColor(AppSettings::instance()->isLightTheme()
                                ? AppSettings::instance()->textColor()
                                : QColor(QString::fromUtf8("#d7ba7d")));
    header->setPos(columnLeft, top - 6);
    header->setZValue(2);

    qreal y = top + 26;
    foreach (Worker *tool, tools)
    {
        addNode(tool, columnLeft, y);
        y += kNodeHeight + kVGap / 2;
    }
    return kNodeWidth + kHGap;
}

//----------------------------------------------

qreal WorkerGraphWidget::layoutWorker(Worker *worker, qreal left, qreal top)
{
    if (!worker)
        return kNodeWidth;

    if (worker->children().isEmpty())
    {
        addNode(worker, left, top);
        return kNodeWidth;
    }

    // Собеседников группы кладём рядом друг с другом, чтобы пунктирная
    // рамка не захватывала обычных детей-исполнителей между ними.
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
    qreal childTop = top + kNodeHeight + kVGap;
    qreal total = 0;
    foreach (Worker *child, ordered)
    {
        const qreal width = layoutWorker(child, x, childTop);
        x += width + kHGap;
        total += width + kHGap;
    }
    if (total > 0)
        total -= kHGap;

    const qreal nodeLeft = left + qMax(qreal(0), (total - kNodeWidth) / 2);
    addNode(worker, nodeLeft, top);
    foreach (Worker *child, ordered)
        addEdge(worker, child);
    return qMax(total, kNodeWidth);
}

//----------------------------------------------

void WorkerGraphWidget::addNode(Worker *worker, qreal x, qreal y)
{
    if (!worker)
        return;

    worker->setProperty("graphX", x);
    worker->setProperty("graphY", y);

    QPen border(stateBorder(worker->state()), 2);
    if (worker->isTool())
        border.setStyle(Qt::DotLine);
    // Геометрия сразу в координатах сцены — как раньше: так клики, рёбра
    // и itemsBoundingRect снова совпадают.
    QGraphicsRectItem *rect = _scene->addRect(x, y, kNodeWidth, kNodeHeight,
                                              border,
                                              QBrush(stateColor(worker->state())));
    rect->setFlag(QGraphicsItem::ItemIsSelectable, true);
    rect->setData(0, worker->id());
    rect->setCursor(Qt::PointingHandCursor);
    rect->setZValue(1);

    const QFont labelFont = nodeFont(this);
    const QFontMetrics fm(labelFont);
    const int textMaxW = int(kNodeWidth - kPad * 3 - kIconSize);

    QStringList lines;
    lines << typeLabel(worker);
    const QString profileName = profileLabel(worker);
    if (!profileName.isEmpty())
        lines << elideLine(QString::fromUtf8("модель: ") + profileName, fm, textMaxW);
    QString tip;
    if (worker->isTool())
    {
        QString name = worker->toolName().simplified();
        if (!name.isEmpty())
            lines << elideLine(name, fm, textMaxW);
        const bool busy = _manager && _manager->toolBusy(worker);
        QString status = busy ? QString::fromUtf8("занят") : QString::fromUtf8("свободен");
        const int queued = _manager ? _manager->toolQueueLength(worker) : 0;
        if (queued > 0)
            status += QString::fromUtf8(", в очереди: %1").arg(queued);
        lines << status;
        QString purpose = worker->toolPurpose().simplified();
        if (!purpose.isEmpty())
            lines << elideLine(purpose, fm, textMaxW);
        tip = worker->toolPurpose().trimmed();
        if (!tip.isEmpty())
            tip += QString::fromUtf8("\n\n");
        tip += tr("Правый клик — удалить инструмент");
    }
    else
    {
        QString task = worker->displayLabel();
        if (!task.isEmpty() && !worker->isRoot())
            lines << elideLine(task, fm, textMaxW);
        lines << stateLabel(worker->state());
        tip = worker->displayLabel();
        if (!profileName.isEmpty())
        {
            if (!tip.isEmpty())
                tip += QString::fromUtf8("\n");
            tip += QString::fromUtf8("Профиль: ") + profileName;
        }
        if (!tip.isEmpty())
            tip += QString::fromUtf8("\n");
        tip += tr("Правый клик — сменить профиль модели");
    }
    QList<Worker *> waiting = worker->unfinishedChildren();
    if (!waiting.isEmpty() && lines.size() < 5)
    {
        QStringList names;
        foreach (Worker *child, waiting)
            names.append(typeLabel(child));
        lines << elideLine(QString::fromUtf8("ждёт: ") + names.join(QString::fromUtf8(", ")),
                           fm, textMaxW);
    }
    while (lines.size() > 5)
        lines.removeLast();

    if (!tip.trimmed().isEmpty())
        rect->setToolTip(tip);

    const QIcon icon = AppIcons::forWorkerType(worker->type());
    const QPixmap pm = icon.pixmap(int(kIconSize), int(kIconSize));
    if (!pm.isNull())
    {
        QGraphicsPixmapItem *pic = _scene->addPixmap(pm);
        pic->setPos(x + kPad, y + kPad);
        pic->setZValue(2);
        pic->setFlag(QGraphicsItem::ItemIsSelectable, false);
        pic->setData(0, worker->id());
    }

    QGraphicsTextItem *label = _scene->addText(lines.join(QChar('\n')));
    label->setFont(labelFont);
    label->setTextWidth(textMaxW);
    label->setPos(x + kPad * 2 + kIconSize, y + kPad - 2);
    label->setDefaultTextColor(AppSettings::instance()->isLightTheme()
                               ? AppSettings::instance()->textColor()
                               : QColor(QString::fromUtf8("#f2f2f2")));
    label->setZValue(2);
    label->setFlag(QGraphicsItem::ItemIsSelectable, false);
    label->setData(0, worker->id());
    // Обрезаем текст по высоте узла: иначе document раздувает label за рамку.
    const qreal maxLabelH = kNodeHeight - kPad * 2 + 4;
    if (label->boundingRect().height() > maxLabelH)
    {
        QString shorter = lines.join(QChar('\n'));
        while (lines.size() > 1
               && label->boundingRect().height() > maxLabelH)
        {
            lines.removeLast();
            shorter = lines.join(QChar('\n'));
            label->setPlainText(shorter);
        }
    }
}

//----------------------------------------------

void WorkerGraphWidget::addEdge(Worker *parent, Worker *child)
{
    if (!parent || !child)
        return;
    const qreal x1 = parent->property("graphX").toReal() + kNodeWidth / 2;
    const qreal y1 = parent->property("graphY").toReal() + kNodeHeight;
    const qreal x2 = child->property("graphX").toReal() + kNodeWidth / 2;
    const qreal y2 = child->property("graphY").toReal();
    QGraphicsLineItem *line = _scene->addLine(x1, y1, x2, y2,
                                              QPen(AppSettings::instance()->borderColor(), 1.5));
    line->setZValue(0);
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
    if (event->button() == Qt::RightButton)
    {
        // Контекстное меню обработает contextMenuEvent.
        event->accept();
        return;
    }
    const QString id = workerIdAt(event->pos());
    QGraphicsView::mousePressEvent(event);
    if (!id.isEmpty())
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
    QAction *openAction = menu.addAction(tr("Открыть чат"));
    menu.addSeparator();

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
    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen)
        return;
    if (chosen == openAction)
        emit workerActivated(id);
    else if (chosen == stopAction)
        emit workerInterruptRequested(id);
    else if (reportAction && chosen == reportAction)
        emit reportToParentRequested(id);
    else if (removeAction && chosen == removeAction)
        emit toolRemoveRequested(id);
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

void WorkerGraphWidget::sltSelectionChanged()
{
    foreach (QGraphicsItem *item, _scene->selectedItems())
    {
        const QString id = item->data(0).toString();
        if (!id.isEmpty())
        {
            emit workerActivated(id);
            return;
        }
    }
}
