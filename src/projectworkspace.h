#ifndef PROJECTWORKSPACE_H
#define PROJECTWORKSPACE_H

#include <QByteArray>
#include <QObject>
#include <QString>

//----------------------------------------------

/**
 * @brief Корневой каталог проекта и проверка, что путь не выходит за его пределы.
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
     * @brief Разрешить путь внутри проекта.
     * @param userPath относительный (или абсолютный внутри корня) путь.
     * @param mustExist файл должен существовать (чтение/удаление).
     * @param forWrite файл может ещё не существовать, проверяется родитель.
     */
    bool resolvePath(QString userPath, bool mustExist, bool forWrite,
                     QString *absolutePath, QString *error,
                     bool allowOutside = false) const;

    /**
     * @brief Разрешить каталог внутри проекта (пустой путь или «.» — корень).
     */
    bool resolveDirectory(QString userPath, QString *absolutePath, QString *error,
                          bool allowOutside = false) const;
    bool contains(QString absolutePath) const;

    /**
     * @brief Служебный каталог черновиков: `<проект>/___project___/tmp`.
     */
    QString tempDir() const;
    bool    ensureTempDir() const;
    QString toRelative(QString absolutePath) const;

    /**
     * @brief Путь (относительный к baseDir или абсолютный) указывает вне проекта.
     */
    bool refersOutside(QString userPath, QString baseDir) const;

    /**
     * @brief Декодировать текст файла: UTF-8, а если он невалиден —
     *        системная 8-битная кодировка (cp1251), чтобы не было кракозябр.
     */
    static QString decodeText(const QByteArray &bytes);
    /**
     * @brief Декодировать вывод консольной команды: UTF-8, иначе OEM-кодовая
     *        страница консоли (cp866), иначе системная (cp1251).
     */
    static QString decodeConsoleOutput(const QByteArray &bytes);

    /** Файлы больше этого read_file не читает вовсе (только через фильтр). */
    static const int maxReadBytes;
    /** Сколько символов read_file возвращает за один вызов. */
    static const int maxReadChunkChars;
    static const int maxWriteBytes;
    static const int maxListEntries;

private:
    QString normalizeDir(QString path) const;
    bool    isInsideRoot(QString candidate) const;

    QString _root;
};

#endif // PROJECTWORKSPACE_H
