#ifndef PROJECTBRIEF_H
#define PROJECTBRIEF_H

#include <QString>
#include <QStringList>

//----------------------------------------------

/**
 * @brief Cached project overview: the directory tree and key files.
 *
 * Built deterministically, without calling the model. Invalidated when
 * the root changes or workers write files. Inserted into the child's first
 * message so it does not spend tokens on scouting.
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
