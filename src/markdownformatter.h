#ifndef MARKDOWNFORMATTER_H
#define MARKDOWNFORMATTER_H

#include <QString>
#include <QStringList>

//----------------------------------------------

/**
 * @brief A simple Markdown-to-HTML conversion for display in chat.
 *
 * Headings, lists, bold and italic, inline code, code blocks, links,
 * tables, images, and fenced chart and diagram blocks are supported.
 * No external libraries. @p imageBaseDir resolves a project-relative image path.
 */
class MarkdownFormatter
{
public:
    static QString toHtml(QString markdown, QString imageBaseDir = QString());
    static QString escapeHtml(QString text);

private:
    static QString formatInline(QString text);
    static QString richText(const QString &raw, QStringList *snippets, const QString &baseDir);
};

#endif // MARKDOWNFORMATTER_H
