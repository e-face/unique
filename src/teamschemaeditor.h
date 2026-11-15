#ifndef TEAMSCHEMAEDITOR_H
#define TEAMSCHEMAEDITOR_H

#include "workertypes.h"
//----------------------------------------------
#include <QDialog>
#include <QList>
#include <QString>

class QComboBox;
class QDialogButtonBox;
class QGraphicsScene;
class QPlainTextEdit;
class QKeyEvent;
class QPushButton;
class QShowEvent;
class SchemaCanvas;
class SchemaEdgeItem;
class SchemaNodeItem;

//----------------------------------------------

/**
 * @brief Team-schema editor.
 *
 * Workers are placed on the field, linked, and described briefly.
 * The schema is sent as an attached file, not as a paragraph of the query.
 */
class TeamSchemaDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TeamSchemaDialog(QWidget *parent = 0);

    QString instructionText() const;
    void commitPlacement();
    void setReviewMode(bool on);
    bool saveToFile(const QString &path) const;
    bool loadFromFile(const QString &path);
    static QString mergeIntoQuery(QString query, QString block);
    static QString stripFromQuery(QString query);
    /**
     * @brief Schema text and this worker's place in it.
     *
     * Empty if there is no saved schema.
     */
    static QString workerContextFromFile(const QString &path, bool isRoot,
                                         const QString &title,
                                         const QString &parentChain,
                                         const QString &profileId, int depth);

    virtual QSize sizeHint() const;
    virtual QSize minimumSizeHint() const;

protected:
    virtual void showEvent(QShowEvent *event);
    virtual void keyPressEvent(QKeyEvent *event);
    virtual void done(int result);

private slots:
    void sltAddExecutor();
    void sltAddTool();
    void sltLoadFromFile();
    void sltFitSchema();
    void sltSelectionChanged();
    void sltPresetChanged(int index);
    void sltPurposeEdited();
    void sltDeleteSelected();
    void accept();

private:
    friend class SchemaCanvas;

    void addWorker(WorkerType type);
    void addLink(SchemaNodeItem *parentNode, SchemaNodeItem *childNode);
    void removeEdge(SchemaEdgeItem *edge);
    void releaseNode(SchemaNodeItem *node);
    void updateInsertEnabled();
    QString typeCaption(WorkerType type) const;
    SchemaNodeItem *selectedNode() const;
    bool reaches(SchemaNodeItem *from, SchemaNodeItem *target) const;
    bool canParent(const SchemaNodeItem *node) const;
    bool canChild(const SchemaNodeItem *node) const;
    void dropIllegalEdges(SchemaNodeItem *node);
    int workerCount() const;
    void notePlaced(SchemaNodeItem *node);
    SchemaNodeItem *rootNode() const;
    void clearWorkers();
    void saveWindowGeometry() const;
    void restoreWindowGeometry();
    void fillPresetCombo(SchemaNodeItem *node);
    void applyPreset(SchemaNodeItem *node, const WorkerPreset &preset);
    QString placeNote(bool isRoot, const QString &title, const QString &parentChain,
                      const QString &profileId, int depth) const;

    QGraphicsScene        *_scene;
    SchemaCanvas          *_canvas;
    QComboBox             *_presetCombo;
    QPlainTextEdit        *_purposeEdit;
    QPushButton           *_insertButton;
    QPushButton           *_loadButton;
    QDialogButtonBox      *_buttonBox;
    QList<SchemaNodeItem *> _nodes;
    bool                   _reviewMode;
};

#endif // TEAMSCHEMAEDITOR_H
