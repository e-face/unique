#ifndef PROJECTWORKSPACE_H
#define PROJECTWORKSPACE_H

#include <QByteArray>
#include <QObject>
#include <QString>

//----------------------------------------------

/**
 * @brief Project root directory, and a check that a path does not leave it.
 */
class ProjectWorkspace : public QObject
{
    Q_OBJECT
public:
    explicit ProjectWorkspace(QObject *parent = 0);

    void    setRoot(QString path);
    QString root() const;
    bool    isSet() const;

    /**
     * @brief Resolve a path inside the project.
     * @param userPath path relative to the root, or absolute and inside the root.
     * @param mustExist the file must exist (read or delete).
     * @param forWrite the file may not exist yet; the parent is checked.
     */
    bool resolvePath(QString userPath, bool mustExist, bool forWrite,
                     QString *absolutePath, QString *error,
                     bool allowOutside = false) const;

    /**
     * @brief Resolve a directory inside the project. An empty path or "." is the root.
     */
    bool resolveDirectory(QString userPath, QString *absolutePath, QString *error,
                          bool allowOutside = false) const;
    bool contains(QString absolutePath) const;

    /**
     * @brief Service draft directory: <project>/___project___/tmp.
     */
    QString tempDir() const;
    bool    ensureTempDir() const;
    QString toRelative(QString absolutePath) const;

    /**
     * @brief A path (relative to baseDir, or absolute) points outside the project.
     */
    bool refersOutside(QString userPath, QString baseDir) const;

    /**
     * @brief Decode file text as UTF-8, and if that is invalid
     *        use the system 8-bit encoding (cp1251), so there is no mojibake.
     */
    static QString decodeText(const QByteArray &bytes);
    /**
     * @brief Decode console-command output: UTF-8, otherwise the console OEM
     *        code page (cp866), otherwise the system page (cp1251).
     */
    static QString decodeConsoleOutput(const QByteArray &bytes);

    /** read_file does not read files larger than this at all (only through a filter). */
    static const int maxReadBytes;
    /** How many characters read_file returns for one file. */
    static const int maxReadChunkChars;
    /** Total character cap for one read_file call (all paths). */
    static const int maxReadCallChars;
    /** Character cap of stdout in the run_command reply (the full log is in tmp). */
    static const int maxCommandReplyChars;
    static const int maxWriteBytes;
    static const int maxListEntries;

private:
    QString normalizeDir(QString path) const;
    bool    isInsideRoot(QString candidate) const;

    QString _root;
};

#endif // PROJECTWORKSPACE_H
