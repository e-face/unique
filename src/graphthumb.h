#ifndef GRAPHTHUMB_H
#define GRAPHTHUMB_H

//----------------------------------------------
#include <QFrame>
#include <QPixmap>

class QLabel;

//----------------------------------------------

/**
 * @brief Thumbnail of the worker schema (dock contents); a click opens the schema tab.
 */
class GraphThumb : public QFrame
{
    Q_OBJECT
public:
    explicit GraphThumb(QWidget *parent = 0);

    void setOverview(const QPixmap &pixmap);
    void applyTheme(QString panel, QString text, QString border, QString muted);

signals:
    void clicked();

protected:
    virtual void mousePressEvent(QMouseEvent *event);
    virtual void paintEvent(QPaintEvent *event);
    virtual void enterEvent(QEvent *event);
    virtual void leaveEvent(QEvent *event);
    virtual void resizeEvent(QResizeEvent *event);

private:
    void refreshPreview();

    QLabel  *_preview;
    QPixmap  _source;
    bool     _hover;
    QString  _fill;
    QString  _border;
};

#endif // GRAPHTHUMB_H
