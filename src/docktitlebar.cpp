#include "docktitlebar.h"
//----------------------------------------------
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainterPath>
#include <QRegion>

//----------------------------------------------

namespace
{
const int kDockCornerRadius = 10;

class DockRoundClip : public QObject
{
public:
    explicit DockRoundClip(QWidget *dock, QObject *parent = 0)
        : QObject(parent)
        , _dock(dock)
    {
        if (!_dock)
            return;
        _dock->setAttribute(Qt::WA_StyledBackground, true);
        _dock->installEventFilter(this);
        apply();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event)
    {
        if (watched == _dock && event
            && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
            apply();
        return false;
    }

private:
    void apply()
    {
        if (!_dock)
            return;
        const QRect rect = _dock->rect();
        if (rect.width() < 4 || rect.height() < 4)
            return;
        const qreal radius = qMin(qreal(kDockCornerRadius),
                                  qMin(rect.width(), rect.height()) / 2.0);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect), radius, radius);
        _dock->setMask(QRegion(path.toFillPolygon().toPolygon()));
    }

    QWidget *_dock;
};
}

//----------------------------------------------

DockTitleBar::DockTitleBar(QString title, QWidget *parent)
    : QWidget(parent)
    , _label(new QLabel(title, this))
{
    setObjectName(QString::fromUtf8("dockTitleBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    _label->setObjectName(QString::fromUtf8("dockTitleLabel"));
    _label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    QHBoxLayout *lay = new QHBoxLayout(this);
    lay->setContentsMargins(12, 8, 12, 8);
    lay->setSpacing(0);
    lay->addWidget(_label, 1);
    if (QWidget *dock = parentWidget())
        new DockRoundClip(dock, this);
}

//----------------------------------------------

void DockTitleBar::setTitle(QString title)
{
    if (_label)
        _label->setText(title);
}

//----------------------------------------------

void DockTitleBar::applyTheme(QString panel, QString text, QString border, QString muted)
{
    Q_UNUSED(muted);
    setStyleSheet(QString::fromUtf8(
        "QWidget#dockTitleBar {"
        "  background:%1; border:none; border-bottom:1px solid %3; }"
        "QLabel#dockTitleLabel {"
        "  background:transparent; color:%2; border:none;"
        "  font-size:12px; font-weight:600; }")
                      .arg(panel, text, border));
}

//----------------------------------------------

QSize DockTitleBar::sizeHint() const
{
    return QSize(160, 34);
}

//----------------------------------------------

QSize DockTitleBar::minimumSizeHint() const
{
    return QSize(80, 34);
}
