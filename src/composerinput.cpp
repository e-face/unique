#include "composerinput.h"
//----------------------------------------------
#include "applocale.h"
#include "appsettings.h"
//----------------------------------------------
#include <QCheckBox>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStyleOption>
#include <QToolButton>
#include <QRegExp>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QtGlobal>

//----------------------------------------------

namespace {

class AttachmentChip : public QFrame
{
public:
    AttachmentChip(int index, QString path, QPixmap thumb, QWidget *parent)
        : QFrame(parent)
        , _index(index)
    {
        setFixedSize(64, 64);
        setObjectName(QString::fromUtf8("composerChip"));
        setToolTip(path);

        _preview = new QLabel(this);
        _preview->setGeometry(4, 4, 56, 56);
        _preview->setAlignment(Qt::AlignCenter);
        _preview->setScaledContents(false);

        if (!thumb.isNull())
        {
            QPixmap scaled = thumb.scaled(56, 56, Qt::KeepAspectRatioByExpanding,
                                          Qt::SmoothTransformation);
            if (scaled.width() > 56 || scaled.height() > 56)
            {
                const int x = qMax(0, (scaled.width() - 56) / 2);
                const int y = qMax(0, (scaled.height() - 56) / 2);
                scaled = scaled.copy(x, y, 56, 56);
            }
            _preview->setPixmap(scaled);
        }
        else
        {
            const QString name = QFileInfo(path).fileName();
            QString shortName = name;
            if (shortName.size() > 10)
                shortName = shortName.left(8) + QString::fromUtf8("…");
            _preview->setText(shortName);
            _preview->setWordWrap(true);
            _preview->setStyleSheet(QString::fromUtf8(
                "QLabel { color:#666; font-size:9px; padding:4px; }"));
        }

        _remove = new QToolButton(this);
        _remove->setText(QString::fromUtf8("×"));
        _remove->setFixedSize(18, 18);
        _remove->move(42, 2);
        _remove->setCursor(Qt::PointingHandCursor);
        _remove->setAutoRaise(true);
        _remove->setObjectName(QString::fromUtf8("composerChipRemove"));
        _remove->setStyleSheet(QString::fromUtf8(
            "QToolButton#composerChipRemove {"
            "  background:rgba(40,40,40,200); color:white; border:none;"
            "  border-radius:9px; font-size:12px; font-weight:bold; padding:0; }"
            "QToolButton#composerChipRemove:hover { background:rgba(0,0,0,230); }"));
        _remove->setProperty("chipIndex", index);
    }

    QToolButton *removeButton() const { return _remove; }
    int index() const { return _index; }

protected:
    virtual void paintEvent(QPaintEvent *)
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
        QColor fill = QColor(245, 245, 245);
        QWidget *bar = parentWidget();
        while (bar && bar->objectName() != QString::fromUtf8("composerInput"))
            bar = bar->parentWidget();
        if (bar)
        {
            const QVariant v = bar->property("chipFill");
            if (v.isValid())
                fill = QColor(v.toString());
        }
        p.fillPath(path, fill);
        p.setPen(QPen(QColor(210, 210, 210), 1));
        p.drawPath(path);
    }

private:
    int          _index;
    QLabel      *_preview;
    QToolButton *_remove;
};

} // namespace

//----------------------------------------------

ComposerInput::ComposerInput(QWidget *parent)
    : QFrame(parent)
    , _attachHost(0)
    , _attachLayout(0)
    , _attachScroll(0)
    , _editor(0)
    , _capsHint(0)
    , _plusButton(0)
    , _schemaButton(0)
    , _autoApproveBox(0)
    , _stopButton(0)
    , _sendButton(0)
    , _dropHighlight(false)
{
    setObjectName(QString::fromUtf8("composerInput"));
    setAcceptDrops(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setMinimumHeight(96);

    _attachScroll = new QScrollArea(this);
    _attachScroll->setWidgetResizable(true);
    _attachScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _attachScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    _attachScroll->setFixedHeight(76);
    _attachScroll->setFrameShape(QFrame::NoFrame);
    _attachScroll->setAcceptDrops(false);
    _attachScroll->setObjectName(QString::fromUtf8("composerAttachScroll"));
    if (_attachScroll->viewport())
        _attachScroll->viewport()->setAutoFillBackground(true);

    _attachHost = new QWidget(_attachScroll);
    _attachHost->setObjectName(QString::fromUtf8("composerAttachHost"));
    _attachHost->setAcceptDrops(false);
    _attachHost->setAutoFillBackground(true);
    _attachLayout = new QHBoxLayout(_attachHost);
    _attachLayout->setContentsMargins(4, 4, 4, 0);
    _attachLayout->setSpacing(8);
    _attachLayout->addStretch();
    _attachScroll->setWidget(_attachHost);
    _attachScroll->hide();

    _editor = new QPlainTextEdit(this);
    _editor->setObjectName(QString::fromUtf8("composerEditor"));
    _editor->setFrameShape(QFrame::NoFrame);
    _editor->setMaximumHeight(120);
    _editor->setMinimumHeight(36);
    _editor->setTabChangesFocus(false);
    _editor->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _editor->viewport()->setAutoFillBackground(false);
    // Иначе Qt вставляет пути файлов текстом вместо наших чипов.
    _editor->setAcceptDrops(false);
    _editor->viewport()->setAcceptDrops(false);
    _editor->installEventFilter(this);
    _editor->viewport()->installEventFilter(this);

    _plusButton = new QToolButton(this);
    _plusButton->setObjectName(QString::fromUtf8("composerPlus"));
    _plusButton->setText(QString::fromUtf8("+"));
    _plusButton->setFixedSize(32, 32);
    _plusButton->setCursor(Qt::PointingHandCursor);
    connect(_plusButton, SIGNAL(clicked()), this, SIGNAL(plusClicked()));

    _schemaButton = new QPushButton(this);
    _schemaButton->setObjectName(QString::fromUtf8("composerSchema"));
    _schemaButton->setCursor(Qt::PointingHandCursor);
    connect(_schemaButton, SIGNAL(clicked()), this, SIGNAL(schemaClicked()));

    _capsHint = new QLabel(this);
    _capsHint->setObjectName(QString::fromUtf8("composerCapsHint"));
    _capsHint->hide();

    _autoApproveBox = new QCheckBox(this);
    _autoApproveBox->setObjectName(QString::fromUtf8("composerAutoApprove"));
    _autoApproveBox->setCursor(Qt::PointingHandCursor);
    _autoApproveBox->setChecked(AppSettings::instance()->autoApprovePermissions());
    connect(_autoApproveBox, SIGNAL(toggled(bool)),
            this, SLOT(sltAutoApproveToggled(bool)));
    connect(AppSettings::instance(), SIGNAL(displayOptionsChanged()),
            this, SLOT(sltDisplayOptionsChanged()));

    _stopButton = new QPushButton(this);
    _stopButton->setObjectName(QString::fromUtf8("composerStop"));
    _stopButton->setCursor(Qt::PointingHandCursor);
    _stopButton->setEnabled(false);
    connect(_stopButton, SIGNAL(clicked()), this, SIGNAL(stopClicked()));

    _sendButton = new QPushButton(this);
    _sendButton->setObjectName(QString::fromUtf8("composerSend"));
    _sendButton->setFixedSize(36, 36);
    _sendButton->setCursor(Qt::PointingHandCursor);
    _sendButton->setText(QString::fromUtf8("↑"));
    connect(_sendButton, SIGNAL(clicked()), this, SIGNAL(sendClicked()));

    retranslateUi();

    QHBoxLayout *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(4, 0, 4, 2);
    toolbar->setSpacing(8);
    toolbar->addWidget(_plusButton, 0, Qt::AlignVCenter);
    toolbar->addWidget(_schemaButton, 0, Qt::AlignVCenter);
    toolbar->addStretch(1);
    toolbar->addWidget(_autoApproveBox, 0, Qt::AlignVCenter);
    toolbar->addWidget(_stopButton, 0, Qt::AlignVCenter);
    toolbar->addWidget(_sendButton, 0, Qt::AlignVCenter);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 10);
    root->setSpacing(6);
    root->addWidget(_attachScroll);
    root->addWidget(_editor, 1);
    root->addLayout(toolbar);

    applyTheme(QString::fromUtf8("#ffffff"), QString::fromUtf8("#ffffff"),
               QString::fromUtf8("#222222"), QString::fromUtf8("#d0d0d0"),
               QString::fromUtf8("#888888"), QString::fromUtf8("#222222"));
}

//----------------------------------------------

QPlainTextEdit *ComposerInput::editor() const
{
    return _editor;
}

//----------------------------------------------

QString ComposerInput::text() const
{
    return _editor ? _editor->toPlainText() : QString();
}

//----------------------------------------------

void ComposerInput::clearText()
{
    if (_editor)
        _editor->clear();
}

//----------------------------------------------

void ComposerInput::setPlaceholder(QString text)
{
    if (_editor)
        _editor->setPlaceholderText(text);
}

//----------------------------------------------

void ComposerInput::retranslateUi()
{
    // Placeholder подставляет MainWindow::updateComposerState() по состоянию.
    if (_plusButton)
        _plusButton->setToolTip(L("composer.attachTip"));
    if (_schemaButton)
    {
        _schemaButton->setText(L("composer.schema"));
        _schemaButton->setToolTip(L("composer.schemaTip"));
    }
    if (_autoApproveBox)
    {
        _autoApproveBox->setText(L("composer.autoApprove"));
        _autoApproveBox->setToolTip(L("composer.autoApproveTip"));
    }
    if (_stopButton)
    {
        _stopButton->setText(L("composer.stop"));
        _stopButton->setToolTip(L("composer.stopTip"));
    }
    if (_sendButton)
    {
        // Стрелка — визуал; доступное имя — локализованный Send.
        _sendButton->setToolTip(L("composer.send"));
        _sendButton->setAccessibleName(L("composer.send"));
    }
    if (_capsHint)
    {
        const QString cur = _capsHint->text().trimmed();
        if (cur.isEmpty()
            || cur == QString::fromUtf8("Auto")
            || cur == QString::fromUtf8("Авто")
            || cur == QString::fromUtf8("自动"))
            _capsHint->setText(tr("Auto"));
    }
}

//----------------------------------------------

void ComposerInput::setCapsHint(QString text, QString toolTip)
{
    Q_UNUSED(text);
    Q_UNUSED(toolTip);
    if (!_capsHint)
        return;
    _capsHint->clear();
    _capsHint->hide();
}

//----------------------------------------------

void ComposerInput::setAttachments(const QStringList &relativePaths,
                                   const QList<QPixmap> &thumbs)
{
    rebuildChips(relativePaths, thumbs);
}

//----------------------------------------------

void ComposerInput::setSendEnabled(bool on)
{
    if (_sendButton)
        _sendButton->setEnabled(on);
}

//----------------------------------------------

void ComposerInput::setStopEnabled(bool on)
{
    if (_stopButton)
        _stopButton->setEnabled(on);
}

//----------------------------------------------

bool ComposerInput::isSendEnabled() const
{
    return _sendButton && _sendButton->isEnabled();
}

//----------------------------------------------

void ComposerInput::setComposerEnabled(bool on)
{
    setEnabled(on);
    if (_editor)
        _editor->setEnabled(on);
    if (_plusButton)
        _plusButton->setEnabled(on);
}

//----------------------------------------------

void ComposerInput::setSchemaAvailable(bool on)
{
    if (_schemaButton)
        _schemaButton->setVisible(on);
}

//----------------------------------------------

void ComposerInput::setReadOnly(bool on)
{
    if (_editor)
        _editor->setReadOnly(on);
}

//----------------------------------------------

void ComposerInput::applyTheme(QString panel, QString input, QString text, QString border,
                               QString muted, QString accent)
{
    Q_UNUSED(panel);
    const QString hover = QColor(muted).lighter(170).name();
    setStyleSheet(QString::fromUtf8(
        "QFrame#composerInput {"
        "  background:%1; border:1px solid %2; border-radius:16px; }"
        "QScrollArea#composerAttachScroll,"
        "QScrollArea#composerAttachScroll > QWidget > QWidget,"
        "QWidget#composerAttachHost {"
        "  background:%1; border:none; }"
        "QPlainTextEdit#composerEditor {"
        "  background:%1; color:%3; border:none; padding:4px 6px;"
        "  selection-background-color:%4; }"
        "QLabel#composerCapsHint { color:%5; background:transparent; border:none; }"
        "QToolButton#composerPlus {"
        "  background:%1; color:%3; border:1px solid %2; border-radius:16px;"
        "  font-size:18px; font-weight:bold; padding:0; }"
        "QToolButton#composerPlus:hover { background:%6; }"
        "QToolButton#composerPlus:disabled { color:%5; }"
        "QPushButton#composerSchema {"
        "  background:transparent; color:%3; border:1px solid %2; border-radius:12px;"
        "  padding:4px 10px; }"
        "QPushButton#composerSchema:hover { background:%6; }"
        "QPushButton#composerSchema:disabled { color:%5; border-color:%5; }"
        "QPushButton#composerSend {"
        "  background:%3; color:%1; border:none; border-radius:16px;"
        "  font-size:16px; font-weight:bold; padding:0; }"
        "QPushButton#composerSend:hover { background:%4; }"
        "QPushButton#composerSend:disabled { background:%5; color:%1; }"
        "QCheckBox#composerAutoApprove {"
        "  background:transparent; color:%3; spacing:6px; }"
        "QCheckBox#composerAutoApprove::indicator {"
        "  width:15px; height:15px; border:1px solid %2; border-radius:3px; background:%1; }"
        "QCheckBox#composerAutoApprove::indicator:checked {"
        "  background:%4; border:1px solid %4; }"
        "QPushButton#composerStop {"
        "  background:transparent; color:%3; border:1px solid %2; border-radius:12px;"
        "  padding:4px 10px; }"
        "QPushButton#composerStop:hover { background:%6; }"
        "QPushButton#composerStop:disabled { color:%5; border-color:%5; }"
        "QFrame#composerChip { background:transparent; border:none; }")
                      .arg(input, border, text, accent, muted, hover));
    // Чипы: заливка как у поля ввода, чтобы полоса вложений не отличалась.
    setProperty("chipFill", input);
    update();
}

//----------------------------------------------

void ComposerInput::sltAutoApproveToggled(bool checked)
{
    AppSettings::instance()->setAutoApprovePermissions(checked);
}

//----------------------------------------------

void ComposerInput::sltDisplayOptionsChanged()
{
    if (!_autoApproveBox)
        return;
    const bool on = AppSettings::instance()->autoApprovePermissions();
    if (_autoApproveBox->isChecked() == on)
        return;
    _autoApproveBox->blockSignals(true);
    _autoApproveBox->setChecked(on);
    _autoApproveBox->blockSignals(false);
}

//----------------------------------------------

void ComposerInput::rebuildChips(const QStringList &relativePaths,
                                 const QList<QPixmap> &thumbs)
{
    if (!_attachLayout || !_attachHost)
        return;

    while (QLayoutItem *item = _attachLayout->takeAt(0))
    {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    for (int i = 0; i < relativePaths.size(); ++i)
    {
        QPixmap thumb;
        if (i < thumbs.size())
            thumb = thumbs.at(i);
        AttachmentChip *chip = new AttachmentChip(i, relativePaths.at(i), thumb, _attachHost);
        connect(chip->removeButton(), SIGNAL(clicked()), this, SLOT(sltRemoveChip()));
        _attachLayout->addWidget(chip, 0, Qt::AlignLeft | Qt::AlignVCenter);
    }
    _attachLayout->addStretch();
    updateAttachStripVisible();
}

//----------------------------------------------

void ComposerInput::updateAttachStripVisible()
{
    if (!_attachScroll || !_attachLayout)
        return;
    const bool has = _attachLayout->count() > 1; // stretch всегда есть
    _attachScroll->setVisible(has);
}

//----------------------------------------------

void ComposerInput::sltRemoveChip()
{
    QToolButton *btn = qobject_cast<QToolButton *>(sender());
    if (!btn)
        return;
    const int index = btn->property("chipIndex").toInt();
    emit removeAttachmentRequested(index);
}

//----------------------------------------------

bool ComposerInput::acceptDrop(const QMimeData *mime) const
{
    if (!mime || !isEnabled())
        return false;
    return !absolutePathsFromMime(mime).isEmpty();
}

//----------------------------------------------

QStringList ComposerInput::absolutePathsFromMime(const QMimeData *mime) const
{
    QStringList out;
    if (!mime)
        return out;
    if (mime->hasUrls())
    {
        foreach (const QUrl &url, mime->urls())
        {
            if (!url.isLocalFile())
                continue;
            const QString path = QFileInfo(url.toLocalFile()).absoluteFilePath();
            if (QFileInfo(path).isFile() && !out.contains(path))
                out.append(path);
        }
    }
    // Некоторые источники (в т.ч. текст из проводника) отдают только пути строкой.
    if (out.isEmpty() && mime->hasText())
    {
        const QStringList lines = mime->text().split(QRegExp(QString::fromUtf8("[\\r\\n]+")),
                                                     QString::SkipEmptyParts);
        foreach (QString line, lines)
        {
            line = line.trimmed();
            if (line.startsWith(QString::fromUtf8("file:///"), Qt::CaseInsensitive))
                line = QUrl(line).toLocalFile();
            line.replace(QChar('"'), QString());
            const QString path = QFileInfo(line).absoluteFilePath();
            if (QFileInfo(path).isFile() && !out.contains(path))
                out.append(path);
        }
    }
    return out;
}

//----------------------------------------------

void ComposerInput::setDropHighlight(bool on)
{
    if (_dropHighlight == on)
        return;
    _dropHighlight = on;
    update();
}

//----------------------------------------------

bool ComposerInput::handleChildDrag(QEvent *event)
{
    if (!event)
        return false;
    if (event->type() == QEvent::DragEnter)
    {
        QDragEnterEvent *e = static_cast<QDragEnterEvent *>(event);
        if (acceptDrop(e->mimeData()))
        {
            e->acceptProposedAction();
            setDropHighlight(true);
            return true;
        }
    }
    else if (event->type() == QEvent::DragMove)
    {
        QDragMoveEvent *e = static_cast<QDragMoveEvent *>(event);
        if (acceptDrop(e->mimeData()))
        {
            e->acceptProposedAction();
            setDropHighlight(true);
            return true;
        }
    }
    else if (event->type() == QEvent::DragLeave)
    {
        setDropHighlight(false);
        event->accept();
        return true;
    }
    else if (event->type() == QEvent::Drop)
    {
        QDropEvent *e = static_cast<QDropEvent *>(event);
        setDropHighlight(false);
        if (acceptDrop(e->mimeData()))
        {
            const QStringList paths = absolutePathsFromMime(e->mimeData());
            e->acceptProposedAction();
            if (!paths.isEmpty())
                emit filesDropped(paths);
            return true;
        }
    }
    return false;
}

//----------------------------------------------

bool ComposerInput::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == _editor || (_editor && watched == _editor->viewport()))
    {
        if (handleChildDrag(event))
            return true;
    }
    return QFrame::eventFilter(watched, event);
}

//----------------------------------------------

void ComposerInput::dragEnterEvent(QDragEnterEvent *event)
{
    if (event && acceptDrop(event->mimeData()))
    {
        event->acceptProposedAction();
        setDropHighlight(true);
        return;
    }
    QFrame::dragEnterEvent(event);
}

//----------------------------------------------

void ComposerInput::dragMoveEvent(QDragMoveEvent *event)
{
    if (event && acceptDrop(event->mimeData()))
    {
        event->acceptProposedAction();
        setDropHighlight(true);
        return;
    }
    QFrame::dragMoveEvent(event);
}

//----------------------------------------------

void ComposerInput::dragLeaveEvent(QDragLeaveEvent *event)
{
    setDropHighlight(false);
    QFrame::dragLeaveEvent(event);
}

//----------------------------------------------

void ComposerInput::dropEvent(QDropEvent *event)
{
    setDropHighlight(false);
    if (!event || !acceptDrop(event->mimeData()))
    {
        QFrame::dropEvent(event);
        return;
    }
    const QStringList paths = absolutePathsFromMime(event->mimeData());
    event->acceptProposedAction();
    if (!paths.isEmpty())
        emit filesDropped(paths);
}

//----------------------------------------------

void ComposerInput::paintEvent(QPaintEvent *event)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    if (_dropHighlight)
    {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(80, 140, 255), 2, Qt::DashLine));
        p.setBrush(QColor(80, 140, 255, 28));
        p.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 16, 16);
    }
    Q_UNUSED(event);
}
