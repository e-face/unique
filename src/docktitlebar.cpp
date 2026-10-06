#include "docktitlebar.h"
//----------------------------------------------
#include <QHBoxLayout>
#include <QLabel>

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
