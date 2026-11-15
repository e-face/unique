#ifndef KNOWLEDGESTORE_H
#define KNOWLEDGESTORE_H

#include "workertypes.h"
//----------------------------------------------
#include <QList>
#include <QSet>
#include <QString>

//----------------------------------------------

/**
 * @brief A lesson left by a worker after finishing a task.
 */
struct KnowledgeEntry
{
    KnowledgeEntry()
        : created(0)
        , uses(0)
        , ok(true)
    {
    }

    QString id;
    QString workerType;
    QString task;
    QString lessons;
    QString command;
    QString error;
    QString fix;
    QString path;
    QString tags;
    qint64  created;
    int     uses;
    bool    ok;
};

//----------------------------------------------

/**
 * @brief The project's experience base: what workers learned on similar tasks.
 *
 * Stored in the project session directory (___project___/knowledge.json).
 * When a new worker starts, matching records are mixed into its task
 * so it does not repeat others' mistakes and already knows working commands.
 */
class KnowledgeStore
{
public:
    static KnowledgeStore *instance();

    /**
     * @brief Remember a lesson. A similar record of the same type is updated, not duplicated.
     */
    void remember(WorkerType type, QString task, QString lessons, bool ok);

    /**
     * @brief Pick records relevant to the task (same type first).
     */
    QList<KnowledgeEntry> recall(WorkerType type, QString task, int maxCount = 4);

    /**
     * @brief A ready text block to insert into the worker context, or empty.
     */
    QString hintFor(WorkerType type, QString task);

    int count();
    /** Drop the cache after clearing or recreating ___project___. */
    void invalidate();

private:
    KnowledgeStore();
    void ensureLoaded();
    void load();
    bool save();
    QString filePath() const;
    static QSet<QString> tokens(QString text);
    static double similarity(const QSet<QString> &a, const QSet<QString> &b);
    static QString clip(QString text, int maxChars);
    static QString newId();

    QString              _loadedDir;
    QList<KnowledgeEntry> _entries;
    bool                 _dirtyUses;
};

#endif // KNOWLEDGESTORE_H
