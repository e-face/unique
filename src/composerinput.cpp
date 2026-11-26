#include "composerinput.h"
//----------------------------------------------
#include "applocale.h"
#include "appsettings.h"
#include "speechinput.h"
//----------------------------------------------
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFontMetrics>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStyleOption>
#include <QTextCursor>
#include <QToolButton>
#include <QRegExp>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QtGlobal>

//----------------------------------------------

/** Text from the clipboard longer than this is attached as a file, not inserted into the query. */
static const int kPasteAsFileChars = 2000;
static const int kPasteAsFileLines = 40;

//----------------------------------------------

static QString writeTempPasteFile(const QString &fileName, const QByteArray &bytes)
{
    const QString path = QDir(QDir::tempPath()).filePath(fileName);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    if (file.write(bytes) != bytes.size())
    {
        file.close();
        QFile::remove(path);
        return QString();
    }
    file.close();
    return path;
}

//----------------------------------------------

class ComposerEditor : public QPlainTextEdit
{
public:
    ComposerEditor(ComposerInput *owner, QWidget *parent)
        : QPlainTextEdit(parent)
        , _owner(owner)
    {
    }

protected:
    virtual bool canInsertFromMimeData(const QMimeData *source) const
    {
        if (source && (source->hasUrls() || source->hasImage() || source->hasText()))
            return true;
        return QPlainTextEdit::canInsertFromMimeData(source);
    }

    virtual void insertFromMimeData(const QMimeData *source)
    {
        if (_owner && _owner->handlePasteMime(source))
            return;
        QPlainTextEdit::insertFromMimeData(source);
    }

private:
    ComposerInput *_owner;
};

//----------------------------------------------

namespace {

class AttachmentChip : public QFrame
{
public:
    AttachmentChip(ComposerInput *owner, int index, QString path, QPixmap thumb,
                   QWidget *parent)
        : QFrame(parent)
        , _owner(owner)
        , _index(index)
    {
        setFixedSize(64, 64);
        setObjectName(QString::fromUtf8("composerChip"));
        setToolTip(path);
        setCursor(Qt::PointingHandCursor);

        _preview = new QLabel(this);
        _preview->setGeometry(4, 4, 56, 56);
        _preview->setAlignment(Qt::AlignCenter);
        _preview->setScaledContents(false);
        _preview->setAttribute(Qt::WA_TransparentForMouseEvents, true);

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
    virtual void mouseReleaseEvent(QMouseEvent *event)
    {
        if (_owner && event && event->button() == Qt::LeftButton
            && rect().contains(event->pos()) && childAt(event->pos()) != _remove)
            _owner->activateAttachment(_index);
        QFrame::mouseReleaseEvent(event);
    }

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
    ComposerInput *_owner;
    int            _index;
    QLabel        *_preview;
    QToolButton   *_remove;
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
    , _vcsBranch(0)
    , _vcsActions(0)
    , _autoApproveBox(0)
    , _reasoningBox(0)
    , _stopButton(0)
    , _micButton(0)
    , _sendButton(0)
    , _speech(0)
    , _micIdleColor(QString::fromUtf8("#222222"))
    , _micLiveColor(QString::fromUtf8("#ffffff"))
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

    _editor = new ComposerEditor(this, this);
    _editor->setObjectName(QString::fromUtf8("composerEditor"));
    _editor->setFrameShape(QFrame::NoFrame);
    _editor->setMaximumHeight(120);
    _editor->setMinimumHeight(36);
    _editor->setTabChangesFocus(false);
    _editor->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _editor->viewport()->setAutoFillBackground(false);
    // Otherwise Qt inserts file paths as text instead of our chips.
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

    _vcsBranch = new QLabel(this);
    _vcsBranch->setObjectName(QString::fromUtf8("composerVcsBranch"));
    _vcsBranch->hide();

    _vcsActions = new QComboBox(this);
    _vcsActions->setObjectName(QString::fromUtf8("composerVcsActions"));
    _vcsActions->setCursor(Qt::PointingHandCursor);
    _vcsActions->setMinimumWidth(148);
    _vcsActions->setFixedHeight(32);
    _vcsActions->hide();
    connect(_vcsActions, SIGNAL(activated(int)), this, SLOT(sltVcsActivated(int)));
    _vcsActions->installEventFilter(this);

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

    _reasoningBox = new QCheckBox(this);
    _reasoningBox->setObjectName(QString::fromUtf8("composerReasoning"));
    _reasoningBox->setCursor(Qt::PointingHandCursor);
    _reasoningBox->setChecked(AppSettings::instance()->reasoningEnabled());
    connect(_reasoningBox, SIGNAL(toggled(bool)),
            this, SLOT(sltReasoningToggled(bool)));

    _stopButton = new QPushButton(this);
    _stopButton->setObjectName(QString::fromUtf8("composerStop"));
    _stopButton->setCursor(Qt::PointingHandCursor);
    _stopButton->setEnabled(false);
    connect(_stopButton, SIGNAL(clicked()), this, SIGNAL(stopClicked()));

    _micButton = new QPushButton(this);
    _micButton->setObjectName(QString::fromUtf8("composerMic"));
    _micButton->setFixedSize(36, 36);
    _micButton->setIconSize(QSize(22, 22));
    _micButton->setCursor(Qt::PointingHandCursor);
    connect(_micButton, SIGNAL(clicked()), this, SLOT(sltMicClicked()));

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
    toolbar->addWidget(_vcsBranch, 0, Qt::AlignVCenter);
    toolbar->addWidget(_vcsActions, 0, Qt::AlignVCenter);
    toolbar->addStretch(1);
    toolbar->addWidget(_reasoningBox, 0, Qt::AlignVCenter);
    toolbar->addWidget(_autoApproveBox, 0, Qt::AlignVCenter);
    toolbar->addWidget(_stopButton, 0, Qt::AlignVCenter);
    toolbar->addWidget(_micButton, 0, Qt::AlignVCenter);
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
    // The placeholder is set by MainWindow::updateComposerState() from the session state.
    if (_plusButton)
        _plusButton->setToolTip(L("composer.attachTip"));
    if (_schemaButton)
    {
        _schemaButton->setText(L("composer.schema"));
        _schemaButton->setToolTip(L("composer.schemaTip"));
    }
    if (_vcsBranch)
        _vcsBranch->setToolTip(L("vcs.branchTip"));
    if (_vcsActions)
    {
        _vcsActions->blockSignals(true);
        _vcsActions->clear();
        _vcsActions->addItem(L("vcs.actions"), QString());
        _vcsActions->addItem(L("vcs.commit"), QString::fromUtf8("commit"));
        _vcsActions->addItem(L("vcs.push"), QString::fromUtf8("push"));
        _vcsActions->addItem(L("vcs.pull"), QString::fromUtf8("pull"));
        _vcsActions->addItem(L("vcs.newBranch"), QString::fromUtf8("new_branch"));
        _vcsActions->addItem(L("vcs.switchBranch"), QString::fromUtf8("switch_branch"));
        _vcsActions->setCurrentIndex(0);
        _vcsActions->blockSignals(false);
        _vcsActions->setToolTip(L("vcs.actionsTip"));
    }
    if (_autoApproveBox)
    {
        _autoApproveBox->setText(L("composer.autoApprove"));
        _autoApproveBox->setToolTip(L("composer.autoApproveTip"));
    }
    if (_reasoningBox)
    {
        _reasoningBox->setText(L("composer.reasoning"));
        _reasoningBox->setToolTip(L("composer.reasoningTip"));
    }
    if (_stopButton)
    {
        _stopButton->setText(L("composer.stop"));
        _stopButton->setToolTip(L("composer.stopTip"));
    }
    if (_sendButton)
    {
        // The arrow is visual; the accessible name is the localized Send.
        _sendButton->setToolTip(L("composer.send"));
        _sendButton->setAccessibleName(L("composer.send"));
    }
    updateMicAppearance();
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
    if (!on)
        commitSpeech();
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

void ComposerInput::setVersionControl(bool visible, QString branch, bool enabled)
{
    if (_vcsBranch)
    {
        QString shown = branch.trimmed();
        if (shown.isEmpty())
            shown = QString::fromUtf8("HEAD");
        const QFontMetrics metrics(_vcsBranch->font());
        _vcsBranch->setText(metrics.elidedText(shown, Qt::ElideMiddle, 160));
        _vcsBranch->setToolTip(L("vcs.branchTip") + QLatin1Char('\n') + shown);
        _vcsBranch->setVisible(visible);
    }
    if (_vcsActions)
    {
        _vcsActions->setVisible(visible);
        _vcsActions->setEnabled(enabled);
    }
}

//----------------------------------------------

void ComposerInput::sltVcsActivated(int index)
{
    if (!_vcsActions || index <= 0)
        return;
    const QString id = _vcsActions->itemData(index).toString();
    _vcsActions->blockSignals(true);
    _vcsActions->setCurrentIndex(0);
    _vcsActions->blockSignals(false);
    if (!id.isEmpty())
        emit vcsActionRequested(id);
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
        "QLabel#composerVcsBranch {"
        "  background:transparent; color:%3; border:1px solid %2; border-radius:12px;"
        "  padding:4px 10px; }"
        "QComboBox#composerVcsActions {"
        "  background:%1; color:%3; border:1px solid %2; border-radius:12px;"
        "  padding:2px 8px; }"
        "QComboBox#composerVcsActions:hover { background:%6; }"
        "QComboBox#composerVcsActions:disabled { color:%5; }"
        "QComboBox#composerVcsActions::drop-down { border:none; width:16px; }"
        "QPushButton#composerSend {"
        "  background:%3; color:%1; border:none; border-radius:16px;"
        "  font-size:16px; font-weight:bold; padding:0; }"
        "QPushButton#composerSend:hover { background:%4; }"
        "QPushButton#composerSend:disabled { background:%5; color:%1; }"
        "QPushButton#composerMic {"
        "  background:transparent; color:%3; border:1px solid %2; border-radius:18px;"
        "  padding:0; }"
        "QPushButton#composerMic:hover { background:%6; }"
        "QPushButton#composerMic:disabled { color:%5; border-color:%5; }"
        "QPushButton#composerMic[listening=\"true\"] {"
        "  background:%4; border:none; }"
        "QCheckBox#composerAutoApprove, QCheckBox#composerReasoning {"
        "  background:transparent; color:%3; spacing:6px; }"
        "QCheckBox#composerAutoApprove::indicator, QCheckBox#composerReasoning::indicator {"
        "  width:15px; height:15px; border:1px solid %2; border-radius:3px; background:%1; }"
        "QCheckBox#composerAutoApprove::indicator:checked,"
        "QCheckBox#composerReasoning::indicator:checked {"
        "  background:%4; border:1px solid %4; }"
        "QPushButton#composerStop {"
        "  background:transparent; color:%3; border:1px solid %2; border-radius:12px;"
        "  padding:4px 10px; }"
        "QPushButton#composerStop:hover { background:%6; }"
        "QPushButton#composerStop:disabled { color:%5; border-color:%5; }"
        "QFrame#composerChip { background:transparent; border:none; }")
                      .arg(input, border, text, accent, muted, hover));
    _micIdleColor = text;
    _micLiveColor = input;
    updateMicAppearance();
    // Chips use the input-field fill so the attachment strip does not stand out.
    setProperty("chipFill", input);
    update();
}

//----------------------------------------------

void ComposerInput::sltAutoApproveToggled(bool checked)
{
    AppSettings::instance()->setAutoApprovePermissions(checked);
}

//----------------------------------------------

void ComposerInput::sltReasoningToggled(bool checked)
{
    AppSettings::instance()->setReasoningEnabled(checked);
}

//----------------------------------------------

void ComposerInput::sltDisplayOptionsChanged()
{
    if (_autoApproveBox)
    {
        const bool on = AppSettings::instance()->autoApprovePermissions();
        if (_autoApproveBox->isChecked() != on)
        {
            _autoApproveBox->blockSignals(true);
            _autoApproveBox->setChecked(on);
            _autoApproveBox->blockSignals(false);
        }
    }
    if (_reasoningBox)
    {
        const bool on = AppSettings::instance()->reasoningEnabled();
        if (_reasoningBox->isChecked() != on)
        {
            _reasoningBox->blockSignals(true);
            _reasoningBox->setChecked(on);
            _reasoningBox->blockSignals(false);
        }
    }
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
        AttachmentChip *chip = new AttachmentChip(this, i, relativePaths.at(i), thumb,
                                                  _attachHost);
        connect(chip->removeButton(), SIGNAL(clicked()), this, SLOT(sltRemoveChip()));
        _attachLayout->addWidget(chip, 0, Qt::AlignLeft | Qt::AlignVCenter);
    }
    _attachLayout->addStretch();
    _attachmentPaths = relativePaths;
    updateAttachStripVisible();
}

//----------------------------------------------

void ComposerInput::activateAttachment(int index)
{
    if (index < 0 || index >= _attachmentPaths.size())
        return;
    emit attachmentOpenRequested(_attachmentPaths.at(index));
}

//----------------------------------------------

void ComposerInput::updateAttachStripVisible()
{
    if (!_attachScroll || !_attachLayout)
        return;
    const bool has = _attachLayout->count() > 1; // the stretch is always there
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

bool ComposerInput::handlePasteMime(const QMimeData *mime)
{
    if (!mime || !isEnabled() || (_editor && _editor->isReadOnly()))
        return false;

    QStringList files;
    if (mime->hasUrls())
    {
        foreach (const QUrl &url, mime->urls())
        {
            if (!url.isLocalFile())
                continue;
            const QString path = QFileInfo(url.toLocalFile()).absoluteFilePath();
            if (QFileInfo(path).isFile() && !files.contains(path))
                files.append(path);
        }
    }
    if (!files.isEmpty())
    {
        emit filesDropped(files);
        return true;
    }

    QImage image;
    if (mime->hasImage())
    {
        image = qvariant_cast<QImage>(mime->imageData());
        if (image.isNull())
        {
            const QClipboard *clip = QApplication::clipboard();
            if (clip)
                image = clip->image();
        }
    }
    const bool hasImage = !image.isNull() && image.width() > 0 && image.height() > 0;

    QString text;
    if (mime->hasText())
        text = mime->text();
    const QString trimmed = text.trimmed();
    const int lines = trimmed.isEmpty() ? 0 : trimmed.count(QChar('\n')) + 1;
    const bool longText = !trimmed.isEmpty()
                           && (trimmed.size() >= kPasteAsFileChars
                               || lines >= kPasteAsFileLines);

    QString imagePath;
    QString textPath;
    if (hasImage)
    {
        const QString path = QDir(QDir::tempPath()).filePath(
            QString::fromUtf8("screenshot.png"));
        if (image.save(path, "PNG"))
            imagePath = path;
    }
    if (longText)
        textPath = writeTempPasteFile(QString::fromUtf8("pasted.txt"), text.toUtf8());

    if (imagePath.isEmpty() && textPath.isEmpty())
    {
        if (hasImage)
        {
            emit pasteHint(tr("Не удалось сохранить вложение из буфера."), true);
            return true;
        }
        return false;
    }

    QStringList temps;
    if (!imagePath.isEmpty())
        temps.append(imagePath);
    if (!textPath.isEmpty())
        temps.append(textPath);
    emit filesDropped(temps);
    if (!imagePath.isEmpty() && !textPath.isEmpty())
        emit pasteHint(tr("Снимок и длинный текст прикреплены файлами."), false);
    else if (!imagePath.isEmpty())
        emit pasteHint(tr("Снимок из буфера прикреплён."), false);
    else
        emit pasteHint(tr("Длинный текст прикреплён файлом."), false);
    foreach (const QString &path, temps)
        QFile::remove(path);

    if (!imagePath.isEmpty() && textPath.isEmpty() && !trimmed.isEmpty() && _editor)
        _editor->insertPlainText(text);
    return true;
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
    // Some sources, including text from Explorer, provide only paths as a string.
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
    if (watched == _vcsActions && event && event->type() == QEvent::Wheel)
        return true;
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

//----------------------------------------------

void ComposerInput::commitSpeech()
{
    if (!_speech || !_speech->isListening())
        return;
    const QString said = _speech->stop();
    applySpeechText(said);
}

//----------------------------------------------

void ComposerInput::applySpeechText(const QString &said)
{
    if (!_editor)
        return;
    QString text = _speechPrefix;
    const QString body = said.trimmed();
    if (!text.isEmpty() && !body.isEmpty()
        && !text.endsWith(QLatin1Char(' '))
        && !text.endsWith(QLatin1Char('\n')))
        text += QLatin1Char(' ');
    text += body;
    _editor->setPlainText(text);
    QTextCursor cursor = _editor->textCursor();
    cursor.movePosition(QTextCursor::End);
    _editor->setTextCursor(cursor);
}

//----------------------------------------------

void ComposerInput::updateMicAppearance()
{
    if (!_micButton)
        return;
    const bool listening = _speech && _speech->isListening();
    _micButton->setProperty("listening", listening);
    const QColor color(listening ? _micLiveColor : _micIdleColor);
    _micButton->setIcon(microphoneIcon(color));
    _micButton->setToolTip(listening ? L("composer.micStop") : L("composer.mic"));
    _micButton->setAccessibleName(L("composer.mic"));
    _micButton->style()->unpolish(_micButton);
    _micButton->style()->polish(_micButton);
}

//----------------------------------------------

void ComposerInput::sltMicClicked()
{
    if (!_speech)
    {
        _speech = new SpeechInput(this);
        connect(_speech, SIGNAL(transcriptChanged(QString)),
                this, SLOT(sltSpeechTranscript(QString)));
        connect(_speech, SIGNAL(failed(QString)),
                this, SLOT(sltSpeechFailed(QString)));
        connect(_speech, SIGNAL(listeningChanged(bool)),
                this, SLOT(sltSpeechListening(bool)));
    }
    if (_speech->isListening())
    {
        commitSpeech();
        return;
    }
    _speechPrefix = _editor ? _editor->toPlainText() : QString();
    QString error;
    if (!_speech->start(&error))
    {
        updateMicAppearance();
        if (!error.isEmpty())
            QMessageBox::warning(this, L("composer.mic"), error);
        return;
    }
    updateMicAppearance();
}

//----------------------------------------------

void ComposerInput::sltSpeechTranscript(QString text)
{
    if (!_speech || !_speech->isListening())
        return;
    applySpeechText(text);
}

//----------------------------------------------

void ComposerInput::sltSpeechFailed(QString message)
{
    updateMicAppearance();
    if (!message.isEmpty())
        QMessageBox::warning(this, L("composer.mic"), message);
}

//----------------------------------------------

void ComposerInput::sltSpeechListening(bool on)
{
    Q_UNUSED(on);
    updateMicAppearance();
}
