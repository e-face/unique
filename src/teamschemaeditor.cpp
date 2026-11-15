#include "teamschemaeditor.h"
//----------------------------------------------
#include "appsettings.h"
#include "workerschema.h"
//----------------------------------------------
#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDesktopWidget>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineF>
#include <QList>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QtMath>
#include <QWheelEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>

//----------------------------------------------

class SchemaEdgeItem;

//----------------------------------------------

class SchemaCanvas : public QGraphicsView
{
public:
    SchemaCanvas(TeamSchemaDialog *owner, QGraphicsScene *scene);

    void beginPlacement(SchemaNodeItem *node);
    void cancelPlacement();
    void commitPlacement();
    bool isPlacing() const;
    void forgetNode(SchemaNodeItem *node);
    void fitRect(const QRectF &rect);

protected:
    virtual void drawBackground(QPainter *painter, const QRectF &rect);
    virtual QSize sizeHint() const;
    virtual QSize minimumSizeHint() const;
    virtual void wheelEvent(QWheelEvent *event);
    virtual void mousePressEvent(QMouseEvent *event);
    virtual void mouseMoveEvent(QMouseEvent *event);
    virtual void mouseReleaseEvent(QMouseEvent *event);
    virtual void keyPressEvent(QKeyEvent *event);
    virtual bool eventFilter(QObject *watched, QEvent *event);

private:
    SchemaNodeItem *nodeAt(const QPoint &viewPos) const;
    void movePlacement(const QPoint &viewPos);

    TeamSchemaDialog  *_owner;
    SchemaNodeItem    *_linkFrom;
    SchemaNodeItem    *_placing;
    QGraphicsLineItem *_rubber;
    bool               _linking;
    bool               _placedClick;
    qreal              _zoom;
};

//----------------------------------------------

static QString presetDisplayName(const WorkerPreset &preset)
{
    const LlmProfile profile = AppSettings::instance()->profileById(preset.profileId);
    QString name = profile.name.trimmed();
    if (name.isEmpty())
        name = profile.model.trimmed();
    if (name.isEmpty())
        name = preset.id.trimmed();
    return name;
}

//----------------------------------------------

static QString presetPurposeText(const WorkerPreset &preset)
{
    return AppSettings::instance()->profileById(preset.profileId).purpose.trimmed();
}

//----------------------------------------------

SchemaCanvas::SchemaCanvas(TeamSchemaDialog *owner, QGraphicsScene *scene)
    : QGraphicsView(scene)
    , _owner(owner)
    , _linkFrom(0)
    , _placing(0)
    , _rubber(0)
    , _linking(false)
    , _placedClick(false)
    , _zoom(1.0)
{
    setRenderHint(QPainter::Antialiasing, true);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
    // The scale is not 1: a partial scroll copies the frame with an offset and tears the picture.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setMouseTracking(false);
}

//----------------------------------------------

QSize SchemaCanvas::sizeHint() const
{
    return QSize(960, 640);
}

//----------------------------------------------

QSize SchemaCanvas::minimumSizeHint() const
{
    return QSize(160, 64);
}

//----------------------------------------------

void SchemaCanvas::drawBackground(QPainter *painter, const QRectF &rect)
{
    QGraphicsView::drawBackground(painter, rect);
    QColor grid = AppSettings::instance()->borderColor();
    grid.setAlpha(110);
    painter->setPen(QPen(grid, 1));
    const int step = 56;
    const int left = int(qFloor(rect.left() / step) * step);
    const int right = int(qCeil(rect.right()));
    const int top = int(qFloor(rect.top() / step) * step);
    const int bottom = int(qCeil(rect.bottom()));
    for (int x = left; x <= right; x += step)
        painter->drawLine(x, top, x, bottom);
    for (int y = top; y <= bottom; y += step)
        painter->drawLine(left, y, right, y);
}

//----------------------------------------------

SchemaNodeItem *SchemaCanvas::nodeAt(const QPoint &viewPos) const
{
    if (!scene())
        return 0;
    const QPointF scenePos = mapToScene(viewPos);
    foreach (QGraphicsItem *item, scene()->items(scenePos))
    {
        if (item && item->type() == SchemaNodeItem::Type)
            return static_cast<SchemaNodeItem *>(item);
    }
    return 0;
}

//----------------------------------------------

void SchemaCanvas::beginPlacement(SchemaNodeItem *node)
{
    if (_placing && _placing != node)
        cancelPlacement();
    _placing = node;
    if (!node)
        return;
    node->setPlacing(true);
    setMouseTracking(true);
    qApp->installEventFilter(this);
    movePlacement(viewport()->mapFromGlobal(QCursor::pos()));
}

//----------------------------------------------

void SchemaCanvas::cancelPlacement()
{
    SchemaNodeItem *node = _placing;
    _placing = 0;
    qApp->removeEventFilter(this);
    if (!node)
        return;
    if (_owner)
        _owner->releaseNode(node);
    else
        delete node;
    setMouseTracking(false);
    if (_owner)
        _owner->updateInsertEnabled();
}

//----------------------------------------------

void SchemaCanvas::commitPlacement()
{
    SchemaNodeItem *node = _placing;
    if (!node)
        return;
    _placing = 0;
    qApp->removeEventFilter(this);
    setMouseTracking(false);
    node->setPlacing(false);
    if (_owner)
        _owner->notePlaced(node);
}

//----------------------------------------------

bool SchemaCanvas::isPlacing() const
{
    return _placing != 0;
}

//----------------------------------------------

void SchemaCanvas::forgetNode(SchemaNodeItem *node)
{
    if (_placing == node)
    {
        _placing = 0;
        qApp->removeEventFilter(this);
        setMouseTracking(false);
    }
    if (_linkFrom == node)
        _linkFrom = 0;
}

//----------------------------------------------

bool SchemaCanvas::eventFilter(QObject *watched, QEvent *event)
{
    if (_placing && event && event->type() == QEvent::MouseMove)
    {
        QMouseEvent *mouse = static_cast<QMouseEvent *>(event);
        movePlacement(viewport()->mapFromGlobal(mouse->globalPos()));
    }
    return QGraphicsView::eventFilter(watched, event);
}

//----------------------------------------------

void SchemaCanvas::fitRect(const QRectF &rect)
{
    if (!rect.isValid() || rect.width() < 1.0 || rect.height() < 1.0)
        return;
    resetTransform();
    _zoom = 1.0;
    fitInView(rect, Qt::KeepAspectRatio);
    qreal fitted = transform().m11();
    if (fitted > 1.0)
    {
        const qreal back = 1.0 / fitted;
        scale(back, back);
        fitted = transform().m11();
    }
    if (fitted < 0.001)
        fitted = 1.0;
    _zoom = fitted;
    centerOn(rect.center());
}

//----------------------------------------------

void SchemaCanvas::movePlacement(const QPoint &viewPos)
{
    if (!_placing)
        return;
    const QPointF scenePos = mapToScene(viewPos);
    _placing->setPos(scenePos.x() - SchemaNodeItem::cardWidth() / 2.0,
                     scenePos.y() - SchemaNodeItem::cardHeight() / 2.0);
}

//----------------------------------------------

void SchemaCanvas::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0)
    {
        event->ignore();
        return;
    }
    const qreal kZoomMin = 0.08;
    const qreal kZoomMax = 2.8;
    if (delta < 0 && _zoom <= kZoomMin)
    {
        event->accept();
        return;
    }
    const qreal step = delta > 0 ? 1.15 : (1.0 / 1.15);
    qreal next = _zoom * step;
    if (next < kZoomMin)
        next = kZoomMin;
    if (next > kZoomMax)
        next = kZoomMax;
    const qreal factor = next / _zoom;
    if (qAbs(factor - 1.0) < 0.001)
    {
        event->accept();
        return;
    }
    _zoom = next;
    scale(factor, factor);
    if (_placing)
        movePlacement(viewport()->mapFromGlobal(QCursor::pos()));
    event->accept();
}

//----------------------------------------------

void SchemaCanvas::mousePressEvent(QMouseEvent *event)
{
    if (_placing)
    {
        if (event->button() == Qt::LeftButton)
        {
            movePlacement(event->pos());
            commitPlacement();
            _placedClick = true;
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton)
    {
        SchemaNodeItem *node = nodeAt(event->pos());
        if (node && node->hitsLinkHandle(node->mapFromScene(mapToScene(event->pos()))))
        {
            if (!node->canParent())
            {
                event->accept();
                return;
            }
            _linking = true;
            _linkFrom = node;
            const QPointF anchor = node->linkAnchor();
            _rubber = scene()->addLine(QLineF(anchor, anchor),
                                       QPen(AppSettings::instance()->accentColor(), 1.6, Qt::DashLine));
            _rubber->setZValue(8);
            event->accept();
            return;
        }
    }
    QGraphicsView::mousePressEvent(event);
}

//----------------------------------------------

void SchemaCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (_placing)
    {
        movePlacement(event->pos());
        event->accept();
        return;
    }
    if (_linking && _rubber)
    {
        QLineF line = _rubber->line();
        line.setP2(mapToScene(event->pos()));
        _rubber->setLine(line);
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

//----------------------------------------------

void SchemaCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (_placedClick)
    {
        _placedClick = false;
        event->accept();
        return;
    }
    if (_linking)
    {
        SchemaNodeItem *target = nodeAt(event->pos());
        if (_rubber)
        {
            scene()->removeItem(_rubber);
            delete _rubber;
            _rubber = 0;
        }
        if (_owner && _linkFrom && target)
            _owner->addLink(_linkFrom, target);
        _linking = false;
        _linkFrom = 0;
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

//----------------------------------------------

void SchemaCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && _placing)
    {
        cancelPlacement();
        event->accept();
        return;
    }
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && _owner)
    {
        _owner->sltDeleteSelected();
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

//----------------------------------------------

TeamSchemaDialog::TeamSchemaDialog(QWidget *parent)
    : QDialog(parent)
    , _scene(0)
    , _canvas(0)
    , _presetCombo(0)
    , _purposeEdit(0)
    , _insertButton(0)
    , _loadButton(0)
    , _buttonBox(0)
    , _reviewMode(false)
{
    setWindowTitle(tr("Схема команды"));
    setMinimumSize(640, 480);
    resize(1600, 1120);
    setSizeGripEnabled(true);

    _scene = new QGraphicsScene(this);
    _scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    _scene->setSceneRect(-160, -80, 2800, 1800);
    _canvas = new SchemaCanvas(this, _scene);
    _canvas->setMinimumSize(160, 64);
    _canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    SchemaNodeItem *root = new SchemaNodeItem(true);
    root->setLinkingEnabled(true);
    root->setWorkerType(WorkerTypeExecutor, tr("Исполнитель"));
    root->setRole(tr("Исполнитель"));
    root->setPurpose(tr("Получает запрос и делает работу сам. "
                        "Детей вызывает, только когда нужен другой контекст или другая модальность."));
    root->setPos(1200, 80);
    _scene->addItem(root);
    _nodes.append(root);
    const WorkerPreset rootPreset = AppSettings::instance()->workerPresetById(
        AppSettings::instance()->rootWorkerPresetId());
    if (!rootPreset.id.isEmpty() && rootPreset.type == WorkerTypeExecutor)
        applyPreset(root, rootPreset);

    const QColor canvasColor = AppSettings::instance()->isLightTheme()
                               ? AppSettings::instance()->windowColor()
                               : AppSettings::instance()->panelColor();
    _canvas->setBackgroundBrush(canvasColor);

    QPushButton *addExecutor = new QPushButton(tr("Исполнитель"), this);
    QPushButton *addTool = new QPushButton(tr("Инструмент"), this);
    connect(addExecutor, SIGNAL(clicked()), this, SLOT(sltAddExecutor()));
    connect(addTool, SIGNAL(clicked()), this, SLOT(sltAddTool()));

    QVBoxLayout *palette = new QVBoxLayout();
    palette->setContentsMargins(0, 0, 0, 0);
    palette->setSpacing(8);
    QLabel *addLabel = new QLabel(tr("Добавить"), this);
    palette->addWidget(addLabel);
    palette->addWidget(addExecutor);
    palette->addWidget(addTool);
    palette->addSpacing(12);
    _loadButton = new QPushButton(tr("Из файла…"), this);
    connect(_loadButton, SIGNAL(clicked()), this, SLOT(sltLoadFromFile()));
    palette->addWidget(_loadButton);
    palette->addStretch(1);
    QWidget *paletteHost = new QWidget(this);
    paletteHost->setFixedWidth(148);
    paletteHost->setLayout(palette);

    _presetCombo = new QComboBox(this);
    _presetCombo->setEnabled(false);
    connect(_presetCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(sltPresetChanged(int)));

    _purposeEdit = new QPlainTextEdit(this);
    _purposeEdit->setPlaceholderText(tr("Что должен делать этот воркер"));
    {
        const QFontMetrics metrics(_purposeEdit->font());
        const int row = qMax(metrics.lineSpacing(), metrics.height() + 4);
        _purposeEdit->setMinimumHeight(row * 3 + 16);
    }
    _purposeEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    _purposeEdit->setEnabled(false);
    connect(_purposeEdit, SIGNAL(textChanged()), this, SLOT(sltPurposeEdited()));

    QFormLayout *form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setVerticalSpacing(4);
    form->setHorizontalSpacing(8);
    form->addRow(tr("Воркер:"), _presetCombo);
    form->addRow(tr("Назначение:"), _purposeEdit);

    QVBoxLayout *field = new QVBoxLayout();
    field->setContentsMargins(0, 0, 0, 0);
    field->setSpacing(8);
    field->addWidget(_canvas, 1);
    field->addLayout(form);

    QHBoxLayout *body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(12);
    body->addWidget(paletteHost);
    body->addLayout(field, 1);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    _buttonBox = buttons;
    _insertButton = buttons->button(QDialogButtonBox::Ok);
    if (_insertButton)
    {
        _insertButton->setText(tr("Вставить в запрос"));
        _insertButton->setEnabled(false);
    }
    if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel))
        cancel->setText(tr("Отмена"));
    connect(buttons, SIGNAL(accepted()), this, SLOT(accept()));
    connect(buttons, SIGNAL(rejected()), this, SLOT(reject()));

    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(8);
    rootLayout->setSizeConstraint(QLayout::SetNoConstraint);
    rootLayout->addLayout(body, 1);
    rootLayout->addWidget(buttons);

    connect(_scene, SIGNAL(selectionChanged()), this, SLOT(sltSelectionChanged()));
    root->setSelected(true);
    setMinimumSize(640, 480);
    restoreWindowGeometry();
}

//----------------------------------------------

QSize TeamSchemaDialog::sizeHint() const
{
    return QSize(1600, 1120);
}

//----------------------------------------------

QSize TeamSchemaDialog::minimumSizeHint() const
{
    return QSize(640, 480);
}

//----------------------------------------------

void TeamSchemaDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (layout())
        layout()->setSizeConstraint(QLayout::SetNoConstraint);
    setMinimumSize(640, 480);
    const QRect avail = QApplication::desktop()->availableGeometry(this);
    if (width() > avail.width() || height() > avail.height())
        resize(qMin(width(), qMax(640, avail.width() - 24)),
               qMin(height(), qMax(480, avail.height() - 24)));
    if (!avail.intersects(frameGeometry()))
        move(avail.left() + 16, avail.top() + 16);
    QTimer::singleShot(0, this, SLOT(sltFitSchema()));
}

//----------------------------------------------

void TeamSchemaDialog::done(int result)
{
    saveWindowGeometry();
    QDialog::done(result);
}

//----------------------------------------------

void TeamSchemaDialog::saveWindowGeometry() const
{
    QSettings settings(AppSettings::settingsFilePath(), QSettings::IniFormat);
    settings.setValue(QString::fromUtf8("ui/teamSchemaGeometry"),
                      QString::fromLatin1(saveGeometry().toBase64()));
    settings.sync();
}

//----------------------------------------------

void TeamSchemaDialog::restoreWindowGeometry()
{
    QSettings settings(AppSettings::settingsFilePath(), QSettings::IniFormat);
    const QByteArray geom = QByteArray::fromBase64(
        settings.value(QString::fromUtf8("ui/teamSchemaGeometry")).toString().toLatin1());
    if (!geom.isEmpty())
        restoreGeometry(geom);
}

//----------------------------------------------

void TeamSchemaDialog::sltFitSchema()
{
    if (!_canvas || !_scene || _nodes.isEmpty())
        return;
    QRectF bounds;
    bool any = false;
    foreach (SchemaNodeItem *node, _nodes)
    {
        if (!node)
            continue;
        const QRectF card(node->pos(), QSizeF(SchemaNodeItem::cardWidth(),
                                              SchemaNodeItem::cardHeight()));
        if (!any)
        {
            bounds = card;
            any = true;
        }
        else
            bounds = bounds.united(card);
    }
    if (!any || !bounds.isValid())
        return;
    bounds.adjust(-48, -48, 48, 48);
    const QRectF scene = _scene->sceneRect();
    _scene->setSceneRect(scene.united(bounds.adjusted(-160, -80, 160, 80)));
    _canvas->fitRect(bounds);
}

//----------------------------------------------

void TeamSchemaDialog::sltLoadFromFile()
{
    if (_reviewMode)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Загрузить схему"),
        QString(),
        tr("Схема команды (*.json);;Все файлы (*.*)"));
    if (path.isEmpty())
        return;
    if (!loadFromFile(path))
    {
        QMessageBox box(QMessageBox::Warning,
                        tr("Схема команды"),
                        tr("Не удалось прочитать схему из файла."),
                        QMessageBox::Ok, this);
        box.setButtonText(QMessageBox::Ok, tr("ОК"));
        box.exec();
    }
}

//----------------------------------------------

void TeamSchemaDialog::sltAddExecutor()
{
    addWorker(WorkerTypeExecutor);
}

//----------------------------------------------

void TeamSchemaDialog::sltAddTool()
{
    addWorker(WorkerTypeTool);
}

//----------------------------------------------

QString TeamSchemaDialog::typeCaption(WorkerType type) const
{
    if (type == WorkerTypeTool)
        return tr("Инструмент");
    return tr("Исполнитель");
}

//----------------------------------------------

int TeamSchemaDialog::workerCount() const
{
    int count = 0;
    foreach (SchemaNodeItem *node, _nodes)
    {
        if (node && !node->isRoot())
            ++count;
    }
    return count;
}

//----------------------------------------------

void TeamSchemaDialog::commitPlacement()
{
    if (_canvas)
        _canvas->commitPlacement();
}

//----------------------------------------------

void TeamSchemaDialog::notePlaced(SchemaNodeItem *node)
{
    if (!node || !_scene)
        return;
    _scene->clearSelection();
    node->setSelected(true);
    updateInsertEnabled();
    if (!node->presetId().isEmpty() && _purposeEdit)
        _purposeEdit->setFocus();
    else if (_presetCombo)
        _presetCombo->setFocus();
}

//----------------------------------------------

void TeamSchemaDialog::keyPressEvent(QKeyEvent *event)
{
    if (event && event->key() == Qt::Key_Escape && _canvas && _canvas->isPlacing())
    {
        _canvas->cancelPlacement();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

//----------------------------------------------

void TeamSchemaDialog::addWorker(WorkerType type)
{
    if (_canvas && _canvas->isPlacing())
        _canvas->cancelPlacement();
    SchemaNodeItem *node = new SchemaNodeItem(false);
    node->setLinkingEnabled(true);
    const QString caption = typeCaption(type);
    node->setWorkerType(type, caption);
    node->setRole(caption);
    foreach (const WorkerPreset &preset, AppSettings::instance()->workerPresets())
    {
        if (preset.type != type || preset.id.trimmed().isEmpty())
            continue;
        applyPreset(node, preset);
        break;
    }
    _scene->addItem(node);
    _nodes.append(node);
    _scene->clearSelection();
    if (_canvas)
        _canvas->beginPlacement(node);
    updateInsertEnabled();
    if (_canvas)
        _canvas->setFocus();
}

//----------------------------------------------

void TeamSchemaDialog::updateInsertEnabled()
{
    if (!_insertButton)
        return;
    _insertButton->setEnabled(_reviewMode || workerCount() > 0);
}

//----------------------------------------------

void TeamSchemaDialog::setReviewMode(bool on)
{
    _reviewMode = on;
    if (_loadButton)
        _loadButton->setVisible(!on);
    if (_buttonBox)
        _buttonBox->setVisible(!on);
    if (_insertButton)
    {
        _insertButton->setText(on ? tr("Сохранить") : tr("Вставить в запрос"));
        if (on)
        {
            _insertButton->setDefault(false);
            _insertButton->setAutoDefault(false);
        }
    }
    updateInsertEnabled();
}

//----------------------------------------------

void TeamSchemaDialog::accept()
{
    if (_reviewMode)
    {
        QDialog::reject();
        return;
    }
    QDialog::accept();
}

//----------------------------------------------

SchemaNodeItem *TeamSchemaDialog::rootNode() const
{
    foreach (SchemaNodeItem *node, _nodes)
    {
        if (node && node->isRoot())
            return node;
    }
    return 0;
}

//----------------------------------------------

void TeamSchemaDialog::clearWorkers()
{
    QList<SchemaNodeItem *> extra;
    foreach (SchemaNodeItem *node, _nodes)
    {
        if (node && !node->isRoot())
            extra.append(node);
    }
    foreach (SchemaNodeItem *node, extra)
        releaseNode(node);
}

//----------------------------------------------

bool TeamSchemaDialog::saveToFile(const QString &path) const
{
    if (path.trimmed().isEmpty())
        return false;
    QHash<const SchemaNodeItem *, int> ids;
    QJsonArray nodes;
    foreach (SchemaNodeItem *node, _nodes)
    {
        if (!node)
            continue;
        ids.insert(node, nodes.size());
        QJsonObject obj;
        obj.insert(QString::fromUtf8("root"), node->isRoot());
        obj.insert(QString::fromUtf8("type"), workerTypeToString(node->workerType()));
        obj.insert(QString::fromUtf8("preset"), node->presetId());
        obj.insert(QString::fromUtf8("role"), node->role());
        obj.insert(QString::fromUtf8("purpose"), node->purpose());
        obj.insert(QString::fromUtf8("x"), node->pos().x());
        obj.insert(QString::fromUtf8("y"), node->pos().y());
        nodes.append(obj);
    }
    QJsonArray edges;
    foreach (SchemaNodeItem *node, _nodes)
    {
        if (!node)
            continue;
        foreach (SchemaEdgeItem *edge, node->childEdges())
        {
            if (!edge || !ids.contains(edge->from()) || !ids.contains(edge->to()))
                continue;
            QJsonObject obj;
            obj.insert(QString::fromUtf8("from"), ids.value(edge->from()));
            obj.insert(QString::fromUtf8("to"), ids.value(edge->to()));
            edges.append(obj);
        }
    }
    QJsonObject root;
    root.insert(QString::fromUtf8("nodes"), nodes);
    root.insert(QString::fromUtf8("edges"), edges);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

//----------------------------------------------

bool TeamSchemaDialog::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return false;
    const QJsonArray nodes = doc.object().value(QString::fromUtf8("nodes")).toArray();
    if (nodes.isEmpty())
        return false;
    if (_canvas && _canvas->isPlacing())
        _canvas->cancelPlacement();
    clearWorkers();
    SchemaNodeItem *root = rootNode();
    QList<SchemaNodeItem *> created;
    for (int i = 0; i < nodes.size(); ++i)
    {
        const QJsonObject obj = nodes.at(i).toObject();
        const bool isRoot = obj.value(QString::fromUtf8("root")).toBool();
        const WorkerType type = workerTypeFromString(obj.value(QString::fromUtf8("type")).toString());
        const QString presetId = obj.value(QString::fromUtf8("preset")).toString().trimmed();
        const QString role = obj.value(QString::fromUtf8("role")).toString();
        const QString purpose = obj.value(QString::fromUtf8("purpose")).toString();
        const qreal x = obj.value(QString::fromUtf8("x")).toDouble();
        const qreal y = obj.value(QString::fromUtf8("y")).toDouble();
        const WorkerPreset preset = presetId.isEmpty()
                                    ? WorkerPreset()
                                    : AppSettings::instance()->workerPresetById(presetId);
        if (isRoot && root)
        {
            root->setPos(x, y);
            root->setPurpose(purpose);
            if (!preset.id.isEmpty() && preset.type == WorkerTypeExecutor)
                applyPreset(root, preset);
            else if (!role.trimmed().isEmpty())
                root->setRole(role);
            if (!presetId.isEmpty())
                root->setPresetId(presetId);
            created.append(root);
            continue;
        }
        SchemaNodeItem *node = new SchemaNodeItem(false);
        node->setLinkingEnabled(true);
        node->setWorkerType(type, typeCaption(type));
        node->setRole(role.trimmed().isEmpty() ? typeCaption(type) : role);
        node->setPurpose(purpose);
        if (!preset.id.isEmpty())
            applyPreset(node, preset);
        else if (!presetId.isEmpty())
            node->setPresetId(presetId);
        node->setPos(x, y);
        _scene->addItem(node);
        _nodes.append(node);
        created.append(node);
    }
    const QJsonArray edges = doc.object().value(QString::fromUtf8("edges")).toArray();
    for (int i = 0; i < edges.size(); ++i)
    {
        const QJsonObject obj = edges.at(i).toObject();
        const int from = obj.value(QString::fromUtf8("from")).toInt(-1);
        const int to = obj.value(QString::fromUtf8("to")).toInt(-1);
        if (from < 0 || to < 0 || from >= created.size() || to >= created.size())
            continue;
        addLink(created.at(from), created.at(to));
    }
    if (root)
        root->setSelected(true);
    updateInsertEnabled();
    sltSelectionChanged();
    if (isVisible())
        sltFitSchema();
    return true;
}

//----------------------------------------------

SchemaNodeItem *TeamSchemaDialog::selectedNode() const
{
    if (!_scene)
        return 0;
    foreach (QGraphicsItem *item, _scene->selectedItems())
    {
        if (item && item->type() == SchemaNodeItem::Type)
            return static_cast<SchemaNodeItem *>(item);
    }
    return 0;
}

//----------------------------------------------

void TeamSchemaDialog::fillPresetCombo(SchemaNodeItem *node)
{
    if (!_presetCombo)
        return;
    const QSignalBlocker blocker(_presetCombo);
    _presetCombo->clear();
    if (!node)
    {
        _presetCombo->setEnabled(false);
        return;
    }
    const QString current = node->presetId();
    int select = -1;
    foreach (const WorkerPreset &preset, AppSettings::instance()->workerPresets())
    {
        if (preset.id.trimmed().isEmpty())
            continue;
        if (node->isRoot() && preset.type != WorkerTypeExecutor)
            continue;
        QString name = presetDisplayName(preset);
        if (name.isEmpty())
            name = preset.id;
        _presetCombo->addItem(typeCaption(preset.type) + QString::fromUtf8(" · ") + name,
                              preset.id);
        if (!current.isEmpty() && preset.id == current)
            select = _presetCombo->count() - 1;
    }
    if (_presetCombo->count() == 0)
    {
        _presetCombo->addItem(tr("В настройках проекта нет воркеров"), QString());
        _presetCombo->setEnabled(false);
        return;
    }
    if (!current.isEmpty() && select < 0)
    {
        _presetCombo->addItem(tr("нет в настройках проекта"), current);
        select = _presetCombo->count() - 1;
    }
    if (select < 0)
    {
        _presetCombo->insertItem(0, tr("Выберите воркера"), QString());
        select = 0;
    }
    _presetCombo->setEnabled(true);
    _presetCombo->setCurrentIndex(select);
}

//----------------------------------------------

void TeamSchemaDialog::applyPreset(SchemaNodeItem *node, const WorkerPreset &preset)
{
    if (!node || preset.id.trimmed().isEmpty())
        return;
    if (node->isRoot() && preset.type != WorkerTypeExecutor)
        return;
    const QString previousPurpose = presetPurposeText(
        AppSettings::instance()->workerPresetById(node->presetId()));
    const WorkerType previousType = node->workerType();
    node->setPresetId(preset.id);
    if (node->isRoot())
        node->setWorkerType(WorkerTypeExecutor, tr("Исполнитель"));
    else
        node->setWorkerType(preset.type, typeCaption(preset.type));
    const QString name = presetDisplayName(preset);
    node->setRole(name.isEmpty() ? node->typeCaption() : name);
    const QString nextPurpose = presetPurposeText(preset);
    if (!nextPurpose.isEmpty()
        && (node->purpose().trimmed().isEmpty()
            || node->purpose().trimmed() == previousPurpose))
        node->setPurpose(nextPurpose);
    if (!node->isRoot() && previousType != node->workerType())
        dropIllegalEdges(node);
}

//----------------------------------------------

void TeamSchemaDialog::sltSelectionChanged()
{
    SchemaNodeItem *node = selectedNode();
    const QSignalBlocker blockPurpose(_purposeEdit);
    if (_purposeEdit)
        _purposeEdit->setEnabled(node != 0);
    if (!node)
    {
        fillPresetCombo(0);
        if (_purposeEdit)
            _purposeEdit->clear();
        return;
    }
    fillPresetCombo(node);
    if (_purposeEdit)
        _purposeEdit->setPlainText(node->purpose());
}

//----------------------------------------------

void TeamSchemaDialog::sltPresetChanged(int index)
{
    Q_UNUSED(index);
    SchemaNodeItem *node = selectedNode();
    if (!node || !_presetCombo)
        return;
    const QString id = _presetCombo->currentData().toString().trimmed();
    if (id.isEmpty() || id == node->presetId())
        return;
    const WorkerPreset preset = AppSettings::instance()->workerPresetById(id);
    if (preset.id.isEmpty())
        return;
    applyPreset(node, preset);
    if (_purposeEdit)
    {
        const QSignalBlocker blocker(_purposeEdit);
        _purposeEdit->setPlainText(node->purpose());
    }
}

//----------------------------------------------

void TeamSchemaDialog::sltPurposeEdited()
{
    SchemaNodeItem *node = selectedNode();
    if (!node || !_purposeEdit)
        return;
    node->setPurpose(_purposeEdit->toPlainText());
}

//----------------------------------------------

bool TeamSchemaDialog::reaches(SchemaNodeItem *from, SchemaNodeItem *target) const
{
    if (!from || !target)
        return false;
    if (from == target)
        return true;
    QSet<SchemaNodeItem *> seen;
    QList<SchemaNodeItem *> stack;
    stack.append(from);
    while (!stack.isEmpty())
    {
        SchemaNodeItem *current = stack.takeLast();
        if (!current || seen.contains(current))
            continue;
        seen.insert(current);
        foreach (SchemaEdgeItem *edge, current->childEdges())
        {
            if (!edge || !edge->to())
                continue;
            if (edge->to() == target)
                return true;
            stack.append(edge->to());
        }
    }
    return false;
}

//----------------------------------------------

bool TeamSchemaDialog::canParent(const SchemaNodeItem *node) const
{
    if (!node)
        return false;
    return node->workerType() == WorkerTypeExecutor;
}

//----------------------------------------------

bool TeamSchemaDialog::canChild(const SchemaNodeItem *node) const
{
    if (!node || node->isRoot())
        return false;
    return node->workerType() != WorkerTypeTool;
}

//----------------------------------------------

void TeamSchemaDialog::dropIllegalEdges(SchemaNodeItem *node)
{
    if (!node)
        return;
    QList<SchemaEdgeItem *> doomed = node->childEdges();
    if (node->parentEdge())
        doomed.append(node->parentEdge());
    foreach (SchemaEdgeItem *edge, doomed)
    {
        if (!edge)
            continue;
        const bool badParent = edge->from() && !canParent(edge->from());
        const bool badChild = edge->to() && !canChild(edge->to());
        const bool tool = (edge->from() && edge->from()->workerType() == WorkerTypeTool)
                          || (edge->to() && edge->to()->workerType() == WorkerTypeTool);
        if (badParent || badChild || tool)
            removeEdge(edge);
    }
}

//----------------------------------------------

void TeamSchemaDialog::removeEdge(SchemaEdgeItem *edge)
{
    if (!edge)
        return;
    if (_scene)
        _scene->removeItem(edge);
    delete edge;
}

//----------------------------------------------

void TeamSchemaDialog::releaseNode(SchemaNodeItem *node)
{
    if (!node)
        return;
    _nodes.removeAll(node);
    QList<SchemaEdgeItem *> edges = node->childEdges();
    if (node->parentEdge())
        edges.append(node->parentEdge());
    foreach (SchemaEdgeItem *edge, edges)
        removeEdge(edge);
    if (_scene)
        _scene->removeItem(node);
    delete node;
}

//----------------------------------------------

void TeamSchemaDialog::addLink(SchemaNodeItem *parentNode, SchemaNodeItem *childNode)
{
    if (!parentNode || !childNode || parentNode == childNode || !_scene)
        return;
    if (!canParent(parentNode) || !canChild(childNode))
        return;
    if (parentNode->workerType() == WorkerTypeTool
        || childNode->workerType() == WorkerTypeTool)
        return;
    SchemaEdgeItem *previous = childNode->parentEdge();
    if (previous && previous->from() == parentNode)
        return;
    if (reaches(childNode, parentNode))
        return;
    if (previous)
        removeEdge(previous);
    SchemaEdgeItem *edge = new SchemaEdgeItem(parentNode, childNode);
    _scene->addItem(edge);
}

//----------------------------------------------

void TeamSchemaDialog::sltDeleteSelected()
{
    if (!_scene)
        return;
    QList<SchemaEdgeItem *> edges;
    QList<SchemaNodeItem *> nodes;
    foreach (QGraphicsItem *item, _scene->selectedItems())
    {
        if (!item)
            continue;
        if (item->type() == SchemaEdgeItem::Type)
            edges.append(static_cast<SchemaEdgeItem *>(item));
        else if (item->type() == SchemaNodeItem::Type)
            nodes.append(static_cast<SchemaNodeItem *>(item));
    }
    foreach (SchemaEdgeItem *edge, edges)
        removeEdge(edge);
    foreach (SchemaNodeItem *node, nodes)
    {
        if (!node || node->isRoot())
            continue;
        if (_canvas)
            _canvas->forgetNode(node);
        releaseNode(node);
    }
    updateInsertEnabled();
    sltSelectionChanged();
}

//----------------------------------------------

QString TeamSchemaDialog::instructionText() const
{
    const QList<SchemaNodeItem *> nodes = _nodes;
    SchemaNodeItem *root = 0;
    QList<SchemaNodeItem *> workers;
    foreach (SchemaNodeItem *node, nodes)
    {
        if (!node)
            continue;
        if (node->isRoot())
            root = node;
        else
            workers.append(node);
    }
    if (workers.isEmpty())
        return QString();

    QHash<SchemaNodeItem *, QString> titles;
    QHash<QString, int> seenNames;
    foreach (SchemaNodeItem *node, workers)
    {
        QString role = node->role().trimmed();
        if (role.isEmpty())
            role = node->typeCaption();
        const QString key = role.toLower();
        const int ordinal = seenNames.value(key, 0) + 1;
        seenNames.insert(key, ordinal);
        if (ordinal > 1)
            role += QString::fromUtf8(" (%1)").arg(ordinal);
        titles.insert(node, role);
    }

    QList<SchemaNodeItem *> ordered;
    QSet<SchemaNodeItem *> visited;
    if (root)
        visited.insert(root);
    QList<SchemaNodeItem *> queue;
    if (root)
        queue.append(root);
    while (!queue.isEmpty())
    {
        SchemaNodeItem *current = queue.takeFirst();
        foreach (SchemaEdgeItem *edge, current->childEdges())
        {
            if (!edge || !edge->to() || visited.contains(edge->to()))
                continue;
            visited.insert(edge->to());
            queue.append(edge->to());
            if (!edge->to()->isRoot())
                ordered.append(edge->to());
        }
    }
    foreach (SchemaNodeItem *node, workers)
    {
        if (!visited.contains(node))
            ordered.append(node);
    }

    const QString chief = tr("корневой исполнитель");
    QString text;
    text += tr("Схема команды.");
    text += QChar('\n');
    text += tr(
        "Инструменты создавать через create_tool. "
        "Остальных — через delegate_task, родитель указан ниже. "
        "В каждый вызов передавай указанный preset_id.");
    text += QString::fromUtf8("\n\n");
    if (root && !root->purpose().simplified().isEmpty())
    {
        text += tr("Комментарий корневому исполнителю: %1").arg(root->purpose().simplified());
        text += QString::fromUtf8("\n\n");
    }

    foreach (SchemaNodeItem *node, ordered)
    {
        QString parentName = chief;
        SchemaEdgeItem *parentEdge = node->parentEdge();
        if (parentEdge && parentEdge->from() && !parentEdge->from()->isRoot())
            parentName = titles.value(parentEdge->from(), parentEdge->from()->role().trimmed());
        QString purpose = node->purpose().simplified();
        if (purpose.isEmpty())
            purpose = tr("не указано");
        const QString title = titles.value(node);
        const QString preset = node->presetId().trimmed();
        QString line;
        if (node->workerType() == WorkerTypeTool)
        {
            if (preset.isEmpty())
            {
                line = tr("Инструмент «%1». Общий, вне дерева связей. Назначение: %2.")
                           .arg(title, purpose);
            }
            else
            {
                line = tr("Инструмент «%1» (preset_id=%2). Общий, вне дерева связей. Назначение: %3.")
                           .arg(title, preset, purpose);
            }
        }
        else if (preset.isEmpty())
        {
            line = tr("%1 «%2». Родитель: %3. Назначение: %4.")
                       .arg(node->typeCaption(), title, parentName, purpose);
        }
        else
        {
            line = tr("%1 «%2» (preset_id=%3). Родитель: %4. Назначение: %5.")
                       .arg(node->typeCaption(), title, preset, parentName, purpose);
        }
        text += line;
        text += QChar('\n');
    }
    text += QChar('\n');
    text += tr(
        "Схема обязательна для каждого воркера, не только для корневого. "
        "Каждый соблюдает свой узел и не берёт чужую роль. "
        "Детям схема уже передаётся: не вставляй её текстом в delegate_task.");
    text += QString::fromUtf8("\n\n");
    text += tr("Конец схемы команды.");
    return text;
}

//----------------------------------------------

static void removeMarkedBlock(QString *text, const QString &begin, const QString &end)
{
    if (!text || begin.isEmpty() || end.isEmpty())
        return;
    const int at = text->indexOf(begin);
    if (at < 0)
        return;
    const int endAt = text->indexOf(end, at + begin.size());
    if (endAt < 0)
        return;
    int tail = endAt + end.size();
    while (tail < text->size()
           && (text->at(tail) == QChar('\n') || text->at(tail) == QChar('\r')))
        ++tail;
    text->remove(at, tail - at);
}

//----------------------------------------------

QString TeamSchemaDialog::mergeIntoQuery(QString query, QString block)
{
    QString rest = query;
    removeMarkedBlock(&rest,
                      tr("Обязательная схема команды (не менять)."),
                      tr("Конец обязательной схемы."));
    removeMarkedBlock(&rest,
                      tr("Схема команды."),
                      tr("Конец схемы команды."));
    while (rest.startsWith(QChar('\n')) || rest.startsWith(QChar('\r')))
        rest.remove(0, 1);
    if (rest.trimmed().isEmpty())
        return block;
    return block + QString::fromUtf8("\n\n") + rest;
}

//----------------------------------------------

QString TeamSchemaDialog::stripFromQuery(QString query)
{
    QString rest = query;
    removeMarkedBlock(&rest,
                      tr("Обязательная схема команды (не менять)."),
                      tr("Конец обязательной схемы."));
    removeMarkedBlock(&rest,
                      tr("Схема команды."),
                      tr("Конец схемы команды."));
    while (rest.startsWith(QChar('\n')) || rest.startsWith(QChar('\r')))
        rest.remove(0, 1);
    return rest;
}

//----------------------------------------------

static QString schemaRoleTitle(SchemaNodeItem *node)
{
    if (!node)
        return QString();
    QString role = node->role().trimmed();
    if (role.isEmpty())
        role = node->typeCaption();
    if (role.isEmpty() && node->isRoot())
        role = QString::fromUtf8("root");
    return role;
}

//----------------------------------------------

static QString schemaChildLine(SchemaNodeItem *child)
{
    if (!child)
        return QString();
    QString purpose = child->purpose().trimmed().simplified();
    if (purpose.size() > 240)
        purpose = purpose.left(237) + QString::fromUtf8("...");
    QString line = QString::fromUtf8("- %1").arg(schemaRoleTitle(child));
    const QString preset = child->presetId().trimmed();
    if (!preset.isEmpty())
        line += QString::fromUtf8(" (preset_id=%1)").arg(preset);
    if (!purpose.isEmpty())
        line += QString::fromUtf8(": %1").arg(purpose);
    return line;
}

//----------------------------------------------

QString TeamSchemaDialog::placeNote(bool isRoot, const QString &title,
                                    const QString &parentChain,
                                    const QString &profileId, int depth) const
{
    SchemaNodeItem *root = 0;
    foreach (SchemaNodeItem *node, _nodes)
    {
        if (node && node->isRoot())
        {
            root = node;
            break;
        }
    }

    QHash<SchemaNodeItem *, QString> uniqueTitles;
    QHash<QString, int> seenNames;
    foreach (SchemaNodeItem *node, _nodes)
    {
        if (!node || node->isRoot())
            continue;
        QString role = schemaRoleTitle(node);
        const int ordinal = seenNames.value(role.toLower(), 0) + 1;
        seenNames.insert(role.toLower(), ordinal);
        if (ordinal > 1)
            role += QString::fromUtf8(" (%1)").arg(ordinal);
        uniqueTitles.insert(node, role);
    }

    const QString needle = title.trimmed().toLower();
    QList<SchemaNodeItem *> titleHits;
    QList<SchemaNodeItem *> profileHits;
    if (!isRoot)
    {
        foreach (SchemaNodeItem *node, _nodes)
        {
            if (!node || node->isRoot())
                continue;
            const QString unique = uniqueTitles.value(node).trimmed().toLower();
            const QString raw = schemaRoleTitle(node).trimmed().toLower();
            if (!needle.isEmpty() && (needle == unique || needle == raw))
                titleHits.append(node);
            const QString presetId = node->presetId().trimmed();
            if (profileId.trimmed().isEmpty() || presetId.isEmpty())
                continue;
            const WorkerPreset preset = AppSettings::instance()->workerPresetById(presetId);
            if (!preset.profileId.trimmed().isEmpty()
                && preset.profileId.trimmed() == profileId.trimmed())
                profileHits.append(node);
        }
    }

    SchemaNodeItem *match = 0;
    if (isRoot)
        match = root;
    else if (titleHits.size() == 1)
        match = titleHits.first();
    else if (titleHits.size() > 1)
    {
        const QString chain = parentChain.toLower();
        foreach (SchemaNodeItem *node, titleHits)
        {
            SchemaNodeItem *parent = node->parentEdge() ? node->parentEdge()->from() : 0;
            const QString parentRole = schemaRoleTitle(parent).toLower();
            if (!parentRole.isEmpty() && chain.contains(parentRole))
            {
                match = node;
                break;
            }
        }
        if (!match)
            match = titleHits.first();
    }
    else if (profileHits.size() == 1)
        match = profileHits.first();

    QString text = QString::fromUtf8("YOUR PLACE IN THE TEAM SCHEMA:\n");
    text += QString::fromUtf8("Live depth: %1 (root = 0).\n").arg(depth);
    if (parentChain.trimmed().isEmpty())
        text += QString::fromUtf8("Live chain above you: (none, you are the top).\n");
    else
        text += QString::fromUtf8("Live chain above you: %1.\n").arg(parentChain.trimmed());

    if (!match)
    {
        text += QString::fromUtf8(
            "No schema node matches your title \"%1\". "
            "Follow the node your parent assigned and do not take another node's job.\n")
                    .arg(title.trimmed().isEmpty() ? QString::fromUtf8("(untitled)")
                                                   : title.trimmed());
        if (profileHits.size() > 1)
        {
            text += QString::fromUtf8("Nodes that use your model profile:\n");
            foreach (SchemaNodeItem *node, profileHits)
                text += schemaChildLine(node) + QChar('\n');
        }
        return text;
    }

    if (match->isRoot())
    {
        text += QString::fromUtf8(
            "You are the root of this schema. You answer the user and spawn only "
            "the direct reports listed here.\n");
    }
    else
    {
        QString parentRole = QString::fromUtf8("root");
        if (match->parentEdge() && match->parentEdge()->from()
            && !match->parentEdge()->from()->isRoot())
            parentRole = uniqueTitles.value(
                match->parentEdge()->from(),
                schemaRoleTitle(match->parentEdge()->from()));
        QString purpose = match->purpose().trimmed().simplified();
        if (purpose.isEmpty())
            purpose = QString::fromUtf8("(none)");
        text += QString::fromUtf8("You are \"%1\". Schema parent: %2. preset_id=%3.\n")
                    .arg(uniqueTitles.value(match, schemaRoleTitle(match)), parentRole,
                         match->presetId().trimmed().isEmpty()
                             ? QString::fromUtf8("(none)")
                             : match->presetId().trimmed());
        text += QString::fromUtf8("Your job: %1\n").arg(purpose);
    }

    QStringList kids;
    foreach (SchemaEdgeItem *edge, match->childEdges())
    {
        if (!edge || !edge->to())
            continue;
        kids.append(schemaChildLine(edge->to()));
    }
    if (kids.isEmpty())
    {
        text += QString::fromUtf8(
            "Schema children under you: none. Do the work yourself.\n");
    }
    else
    {
        text += QString::fromUtf8(
            "Schema children under you (create only these, and only when the split "
            "is worth it):\n");
        text += kids.join(QString::fromUtf8("\n"));
        text += QChar('\n');
    }
    return text;
}

//----------------------------------------------

QString TeamSchemaDialog::workerContextFromFile(const QString &path, bool isRoot,
                                                const QString &title,
                                                const QString &parentChain,
                                                const QString &profileId, int depth)
{
    if (path.trimmed().isEmpty() || !QFileInfo(path).isFile())
        return QString();
    TeamSchemaDialog dialog;
    if (!dialog.loadFromFile(path))
        return QString();
    const QString schema = dialog.instructionText().trimmed();
    if (schema.isEmpty())
        return QString();
    QString text = QString::fromUtf8(
        "TEAM SCHEMA (the user attached this as a file; it binds every worker, "
        "not only the root). Obey your own node. Do not browse ___project___. "
        "Do not paste the schema into delegate_task: each child already receives it "
        "together with its place.\n\n");
    text += schema;
    text += QString::fromUtf8("\n\n");
    text += dialog.placeNote(isRoot, title, parentChain, profileId, depth);
    return text.trimmed();
}
