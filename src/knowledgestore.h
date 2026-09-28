#ifndef KNOWLEDGESTORE_H
#define KNOWLEDGESTORE_H

#include "workertypes.h"
//----------------------------------------------
#include <QList>
#include <QSet>
#include <QString>

//----------------------------------------------

/**
 * @brief Урок, оставленный воркером после выполнения задачи.
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
    qint64  created;
    int     uses;
    bool    ok;
};

//----------------------------------------------

/**
 * @brief База опыта проекта: что узнали воркеры, решая похожие задачи.
 *
 * Хранится в каталоге сессий проекта (___project___/knowledge.json).
 * При запуске нового воркера подходящие записи подмешиваются в его задачу,
 * чтобы он не повторял чужие ошибки и сразу знал рабочие команды.
 */
class KnowledgeStore
{
public:
    static KnowledgeStore *instance();

    /**
     * @brief Запомнить урок. Похожая запись того же типа обновляется, а не дублируется.
     */
    void remember(WorkerType type, QString task, QString lessons, bool ok);

    /**
     * @brief Подобрать записи, релевантные задаче (сначала того же типа).
     */
    QList<KnowledgeEntry> recall(WorkerType type, QString task, int maxCount = 4);

    /**
     * @brief Готовый текстовый блок для вставки в контекст воркера (или пусто).
     */
    QString hintFor(WorkerType type, QString task);

    int count();

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
