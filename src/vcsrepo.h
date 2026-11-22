#ifndef VCSREPO_H
#define VCSREPO_H

//----------------------------------------------
#include <QByteArray>
#include <QString>
#include <QStringList>

class QWidget;

//----------------------------------------------

/**
 * @brief Git repository in a project directory.
 * A repository is recognized only when that directory itself contains .git.
 */
class VcsRepo
{
public:
    static bool isRepository(const QString &root);
    static QString currentBranch(const QString &root);
    static QString gitProgram();

    static bool run(QWidget *parent, const QString &root, const QStringList &args,
                    const QByteArray &input, QString *text, bool *canceled);

    static QStringList localBranches(QWidget *parent, const QString &root,
                                     QString *text, bool *canceled);
};

#endif // VCSREPO_H
