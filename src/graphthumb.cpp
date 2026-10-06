#include "graphthumb.h"
//----------------------------------------------
#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QVBoxLayout>

//----------------------------------------------

GraphThumb::GraphThumb(QWidget *parent)
    : QFrame(parent)
    , _preview(0)
    , _hover(false)
{
    setObjectName(QString::fromUtf8("graphThumb"));
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Открыть схему воркеров"));
    setMinimumSize(120, 100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    _preview = new QLabel(this);
    _preview->setAlignment(Qt::AlignCenter);
    _preview->setScaledContents(false);
    _preview->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->addWidget(_preview, 1);

    applyTheme(QString::fromUtf8("#ffffff"), QString::fromUtf8("#222222"),
               QString::fromUtf8("#c8c8c8"), QString::fromUtf8("#888888"));
}

//----------------------------------------------

void GraphThumb::setOverview(const QPixmap &pixmap)
{
    _source = pixmap;
    refreshPreview();
}

//----------------------------------------------

void GraphThumb::refreshPreview()
{
    if (!_preview)
        return;
    if (_source.isNull())
    {
        _preview->setPixmap(QPixmap());
        _preview->setText(tr("Схема"));
        return;
    }
    _preview->setText(QString());
    const QSize inner(qMax(16, width() - 16), qMax(16, height() - 16));
    _preview->setPixmap(_source.scaled(inner, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
}

//----------------------------------------------

void GraphThumb::applyTheme(QString panel, QString text, QString border, QString muted)
{
    _fill = panel;
    _border = border;
    setStyleSheet(QString::fromUtf8(
        "QFrame#graphThumb { background:transparent; border:none; }"
        "QLabel { color:%1; background:transparent; border:none; font-size:12px; }")
                      .arg(muted.isEmpty() ? text : muted));
    update();
}

//----------------------------------------------

void GraphThumb::mousePressEvent(QMouseEvent *event)
{
    if (event && event->button() == Qt::LeftButton)
        emit clicked();
    QFrame::mousePressEvent(event);
}

//----------------------------------------------

void GraphThumb::enterEvent(QEvent *event)
{
    _hover = true;
    update();
    QFrame::enterEvent(event);
}

//----------------------------------------------

void GraphThumb::leaveEvent(QEvent *event)
{
    _hover = false;
    update();
    QFrame::leaveEvent(event);
}

//----------------------------------------------

void GraphThumb::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    refreshPreview();
}

//----------------------------------------------

void GraphThumb::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    // Фон даёт родительский dock-panel; лёгкий hover без второй «карточки».
    if (!_hover)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor tint = QColor(_border.isEmpty() ? QString::fromUtf8("#c8c8c8") : _border);
    QColor fill = tint;
    fill.setAlpha(28);
    p.fillRect(rect(), fill);
}
