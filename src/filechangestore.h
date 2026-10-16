#ifndef FILECHANGESTORE_H
#define FILECHANGESTORE_H

#include <QHash>
#include <QObject>
#include <QString>

//----------------------------------------------

struct FileLineDelta
{
    FileLineDelta()
        : added(0)
        , removed(0)
    {
    }

    int added;
    int removed;

    bool isEmpty() const
    {
        return added <= 0 && removed <= 0;
    }
};

//----------------------------------------------

/**
 * @brief Счётчики строк «как в Cursor»: +добавлено / −удалено относительно
 *        исходного содержимого файла (снимок до первой правки в проекте).
 */
class FileChangeStore : public QObject
{
    Q_OBJECT
public:
    static FileChangeStore *instance();

    void setRoot(QString root, bool loadFromDisk = true);
    void forget();
    QString root() const;

    void noteCreated(QString absPath, QString content);
    void noteWritten(QString absPath, QString previous, QString next);
    void noteDeleted(QString absPath);
    void noteRenamed(QString fromAbs, QString toAbs);
    void clear();

    FileLineDelta deltaFor(QString absPath) const;
    /** Сумма по всем изменённым файлам проекта. */
    FileLineDelta totals() const;
    int changedFileCount() const;
    QString relativePath(QString absPath) const;

    static FileLineDelta countLineDiff(QString oldText, QString newText);

signals:
    void changed();
    void loadProgress(int current, int total, QString path);

private:
    explicit FileChangeStore(QObject *parent = 0);
    QString normalizeAbs(QString absPath) const;
    QString indexPath() const;
    QString baselineDir() const;
    QString baselineFile(QString rel) const;
    QString readText(QString absPath) const;
    bool writeText(QString absPath, QString text) const;
    bool loadIndex();
    void saveIndex() const;
    void ensureBaseline(QString rel, QString previous);
    void updateDelta(QString rel, QString current);
    void removeEntry(QString rel);
    void rebuildFromDisk();
    void emitChanged();

    QString                       _root;
    QHash<QString, QString>       _baseline;
    QHash<QString, FileLineDelta> _delta;
};

#endif // FILECHANGESTORE_H
