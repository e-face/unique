#ifndef PROJECTBRIEF_H
#define PROJECTBRIEF_H

#include <QString>
#include <QStringList>

//----------------------------------------------

/**
 * @brief Кешированный обзор проекта: дерево каталогов и ключевые файлы.
 *
 * Собирается детерминированно, без вызова модели. Инвалидируется при
 * смене корня или записи файлов воркерами. Вкладывается в первое
 * сообщение ребёнка, чтобы тот не тратил токены на разведку.
 */
class ProjectBrief
{
public:
    static ProjectBrief *instance();

    void setRoot(QString root);
    void invalidate();
    QString text();

private:
    ProjectBrief();
    void rebuild();
    static bool skipDirName(QString name);
    static QString clip(QString text, int maxChars);
    QString collectTree(QString dir, int depth, int *entriesLeft) const;
    QString collectKeyFiles() const;
    QString fileExcerpt(QString absolutePath, int maxLines) const;

    QString _root;
    QString _cached;
    bool    _dirty;
};

#endif // PROJECTBRIEF_H
