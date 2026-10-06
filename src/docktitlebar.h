#ifndef DOCKTITLEBAR_H
#define DOCKTITLEBAR_H

#include <QWidget>

class QLabel;

//----------------------------------------------

/**
 * @brief Плоский заголовок дока в стиле остального UI (вместо native title bar).
 */
class DockTitleBar : public QWidget
{
    Q_OBJECT
public:
    explicit DockTitleBar(QString title, QWidget *parent = 0);

    void setTitle(QString title);
    void applyTheme(QString panel, QString text, QString border, QString muted);

protected:
    virtual QSize sizeHint() const;
    virtual QSize minimumSizeHint() const;

private:
    QLabel *_label;
};

#endif // DOCKTITLEBAR_H
