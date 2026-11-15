#ifndef COMPOSERINPUT_H
#define COMPOSERINPUT_H

//----------------------------------------------
#include <QFrame>
#include <QList>
#include <QPixmap>
#include <QString>
#include <QStringList>

class ComposerEditor;
class QCheckBox;
class QEvent;
class QHBoxLayout;
class QLabel;
class QMimeData;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class QWidget;

//----------------------------------------------

/**
 * @brief Chat-composer input: attachment previews inside the frame,
 *        a plus button, Send/Stop, drag-and-drop files, and paste from the clipboard.
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
    void activateAttachment(int index);
    void setSendEnabled(bool on);
    void setStopEnabled(bool on);
    bool isSendEnabled() const;
    void setComposerEnabled(bool on);
    void setSchemaAvailable(bool on);
    void setReadOnly(bool on);
    void applyTheme(QString panel, QString input, QString text, QString border,
                    QString muted, QString accent);
    void retranslateUi();

signals:
    void sendClicked();
    void stopClicked();
    void plusClicked();
    void schemaClicked();
    void removeAttachmentRequested(int index);
    void filesDropped(const QStringList &absolutePaths);
    void pasteHint(QString text, bool force);
    void attachmentOpenRequested(QString relativePath);

protected:
    virtual bool eventFilter(QObject *watched, QEvent *event);
    virtual void dragEnterEvent(QDragEnterEvent *event);
    virtual void dragMoveEvent(QDragMoveEvent *event);
    virtual void dragLeaveEvent(QDragLeaveEvent *event);
    virtual void dropEvent(QDropEvent *event);
    virtual void paintEvent(QPaintEvent *event);

private slots:
    void sltRemoveChip();
    void sltAutoApproveToggled(bool checked);
    void sltReasoningToggled(bool checked);
    void sltDisplayOptionsChanged();

private:
    friend class ComposerEditor;

    void rebuildChips(const QStringList &relativePaths, const QList<QPixmap> &thumbs);
    bool handlePasteMime(const QMimeData *mime);
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
    QPushButton    *_schemaButton;
    QCheckBox      *_autoApproveBox;
    QCheckBox      *_reasoningBox;
    QPushButton    *_stopButton;
    QPushButton    *_sendButton;
    bool            _dropHighlight;
    QStringList     _attachmentPaths;
};

#endif // COMPOSERINPUT_H
