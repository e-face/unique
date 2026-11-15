#ifndef MARKDOWNFORMATTER_H
#define MARKDOWNFORMATTER_H

#include <QString>

//----------------------------------------------

/**
 * @brief A simple Markdown-to-HTML conversion for display in chat.
 *
 * Headings, lists, bold and italic, inline code,
 * code blocks, and links are supported. No external libraries.
 */
class MarkdownFormatter
{
public:
    static QString toHtml(QString markdown);
    static QString escapeHtml(QString text);

private:
    static QString formatInline(QString text);
};

#endif // MARKDOWNFORMATTER_H
