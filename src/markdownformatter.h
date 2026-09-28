#ifndef MARKDOWNFORMATTER_H
#define MARKDOWNFORMATTER_H

#include <QString>

//----------------------------------------------

/**
 * @brief Простое преобразование Markdown в HTML для отображения в чате.
 *
 * Поддерживаются заголовки, списки, жирный/курсив, инлайн-код,
 * блоки кода и ссылки. Без внешних библиотек.
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
