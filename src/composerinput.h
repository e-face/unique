#ifndef COMPOSERINPUT_H
#define COMPOSERINPUT_H

//----------------------------------------------
#include <QFrame>
#include <QList>
#include <QPixmap>
#include <QString>
#include <QStringList>

class QEvent;
class QHBoxLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class QWidget;

//----------------------------------------------

/**
 * @brief Поле ввода в стиле chat-composer: превью вложений внутри рамки,
 *        «+», Send/Stop и приём файлов drag-and-drop.
 */
class ComposerInput : public QFrame
{
    Q_OBJECT
public:
    explicit ComposerInput(QWidget *parent = 0);

    QPlainTextEdit *editor() const;
    QString text() const;
    void clearText();
    void setPlaceholder(QString text);
    void setCapsHint(QString text, QString toolTip = QString());
    void setAttachments(const QStringList &relativePaths,
                        const QList<QPixmap> &thumbs);
    void setSendEnabled(bool on);
    void setStopEnabled(bool on);
    bool isSendEnabled() const;
    void setComposerEnabled(bool on);
    void setReadOnly(bool on);
    void applyTheme(QString panel, QString input, QString text, QString border,
                    QString muted, QString accent);
    void retranslateUi();

signals:
    void sendClicked();
    void stopClicked();
    void plusClicked();
    void removeAttachmentRequested(int index);
    void filesDropped(const QStringList &absolutePaths);

protected:
    virtual bool eventFilter(QObject *watched, QEvent *event);
    virtual void dragEnterEvent(QDragEnterEvent *event);
    virtual void dragMoveEvent(QDragMoveEvent *event);
    virtual void dragLeaveEvent(QDragLeaveEvent *event);
    virtual void dropEvent(QDropEvent *event);
    virtual void paintEvent(QPaintEvent *event);

private slots:
    void sltRemoveChip();

private:
    void rebuildChips(const QStringList &relativePaths, const QList<QPixmap> &thumbs);
    bool acceptDrop(const QMimeData *mime) const;
    QStringList absolutePathsFromMime(const QMimeData *mime) const;
    void updateAttachStripVisible();
    void setDropHighlight(bool on);
    bool handleChildDrag(QEvent *event);

    QWidget        *_attachHost;
    QHBoxLayout    *_attachLayout;
    QScrollArea    *_attachScroll;
    QPlainTextEdit *_editor;
    QLabel         *_capsHint;
    QToolButton    *_plusButton;
    QPushButton    *_stopButton;
    QPushButton    *_sendButton;
    bool            _dropHighlight;
};

#endif // COMPOSERINPUT_H
