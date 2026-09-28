#include "markdownformatter.h"
//----------------------------------------------
#include "appsettings.h"
//----------------------------------------------
#include <QRegExp>
#include <QStringList>

//----------------------------------------------

QString MarkdownFormatter::escapeHtml(QString text)
{
    text.replace(QChar('&'), QString::fromUtf8("&amp;"));
    text.replace(QChar('<'), QString::fromUtf8("&lt;"));
    text.replace(QChar('>'), QString::fromUtf8("&gt;"));
    text.replace(QChar('"'), QString::fromUtf8("&quot;"));
    return text;
}

//----------------------------------------------

QString MarkdownFormatter::formatInline(QString text)
{
    const bool light = AppSettings::instance()->isLightTheme();
    QRegExp code(QString::fromUtf8("`([^`]+)`"));
    text.replace(code, light
                 ? QString::fromUtf8(
                       "<code style=\"background:#f3f3f3;color:#9a3412;padding:1px 5px;"
                       "border-radius:3px;font-family:Consolas,monospace;font-size:13px;\">\\1</code>")
                 : QString::fromUtf8(
                       "<code style=\"background:#2a2a2a;color:#ffe082;padding:1px 5px;"
                       "border-radius:3px;font-family:Consolas,monospace;font-size:13px;\">\\1</code>"));

    QRegExp bold(QString::fromUtf8("\\*\\*([^*]+)\\*\\*"));
    text.replace(bold, QString::fromUtf8("<b>\\1</b>"));
    QRegExp bold2(QString::fromUtf8("__([^_]+)__"));
    text.replace(bold2, QString::fromUtf8("<b>\\1</b>"));

    QRegExp italic(QString::fromUtf8("\\*([^*]+)\\*"));
    text.replace(italic, QString::fromUtf8("<i>\\1</i>"));

    QRegExp link(QString::fromUtf8("\\[([^\\]]+)\\]\\(([^\\)]+)\\)"));
    int pos = 0;
    while ((pos = link.indexIn(text, pos)) != -1)
    {
        const QString html = QString::fromUtf8("<a href=\"%1\">%2</a>")
                             .arg(link.cap(2), link.cap(1));
        text.replace(pos, link.matchedLength(), html);
        pos += html.size();
    }
    return text;
}

//----------------------------------------------

QString MarkdownFormatter::toHtml(QString markdown)
{
    if (markdown.isEmpty())
        return QString();

    QStringList codeBlocks;
    QStringList lines = markdown.split(QChar('\n'));
    QString rebuilt;
    bool inFence = false;
    QString fenceBuf;
    foreach (const QString &line, lines)
    {
        if (line.startsWith(QString::fromUtf8("```")))
        {
            if (inFence)
            {
                const QString token = QString::fromUtf8("@@CODE%1@@").arg(codeBlocks.size());
                const bool light = AppSettings::instance()->isLightTheme();
                codeBlocks.append((light
                                   ? QString::fromUtf8(
                                         "<pre style=\"background:#f6f8fa;color:#1f1f1f;"
                                         "padding:10px 12px;border-radius:6px;border:1px solid #e5e5e5;"
                                         "font-family:Consolas,monospace;font-size:13px;"
                                         "white-space:pre-wrap;\">%1</pre>")
                                   : QString::fromUtf8(
                                         "<pre style=\"background:#1e1e1e;color:#d7e8c8;"
                                         "padding:10px 12px;border-radius:6px;border-left:3px solid #6a9955;"
                                         "font-family:Consolas,monospace;font-size:13px;"
                                         "white-space:pre-wrap;\">%1</pre>"))
                                  .arg(escapeHtml(fenceBuf.trimmed())));
                rebuilt += token;
                rebuilt += QChar('\n');
                fenceBuf.clear();
                inFence = false;
            }
            else
            {
                inFence = true;
                fenceBuf.clear();
            }
            continue;
        }
        if (inFence)
        {
            fenceBuf += line;
            fenceBuf += QChar('\n');
        }
        else
        {
            rebuilt += line;
            rebuilt += QChar('\n');
        }
    }
    if (inFence && !fenceBuf.isEmpty())
    {
        rebuilt += escapeHtml(fenceBuf);
        rebuilt += QChar('\n');
    }

    lines = rebuilt.split(QChar('\n'));
    QString html;
    bool inList = false;
    bool inOl = false;
    bool inPara = false;

    auto closeLists = [&]()
    {
        if (inPara)
        {
            html += QString::fromUtf8("</p>");
            inPara = false;
        }
        if (inList)
        {
            html += QString::fromUtf8("</ul>");
            inList = false;
        }
        if (inOl)
        {
            html += QString::fromUtf8("</ol>");
            inOl = false;
        }
    };

    foreach (QString line, lines)
    {
        if (line.startsWith(QString::fromUtf8("@@CODE")))
        {
            closeLists();
            html += line;
            continue;
        }

        const QString raw = line;
        line = escapeHtml(line);

        if (raw.startsWith(QString::fromUtf8("### ")))
        {
            closeLists();
            html += QString::fromUtf8("<h4>")
                    + formatInline(line.mid(4)) + QString::fromUtf8("</h4>");
            continue;
        }
        if (raw.startsWith(QString::fromUtf8("## ")))
        {
            closeLists();
            html += QString::fromUtf8("<h3>")
                    + formatInline(line.mid(3)) + QString::fromUtf8("</h3>");
            continue;
        }
        if (raw.startsWith(QString::fromUtf8("# ")))
        {
            closeLists();
            html += QString::fromUtf8("<h2>")
                    + formatInline(line.mid(2)) + QString::fromUtf8("</h2>");
            continue;
        }

        QRegExp ul(QString::fromUtf8("^\\s*[-*]\\s+(.+)$"));
        if (ul.exactMatch(raw))
        {
            if (inPara)
            {
                html += QString::fromUtf8("</p>");
                inPara = false;
            }
            if (inOl)
            {
                html += QString::fromUtf8("</ol>");
                inOl = false;
            }
            if (!inList)
            {
                html += QString::fromUtf8("<ul>");
                inList = true;
            }
            html += QString::fromUtf8("<li>") + formatInline(escapeHtml(ul.cap(1)))
                    + QString::fromUtf8("</li>");
            continue;
        }

        QRegExp ol(QString::fromUtf8("^\\s*\\d+\\.\\s+(.+)$"));
        if (ol.exactMatch(raw))
        {
            if (inPara)
            {
                html += QString::fromUtf8("</p>");
                inPara = false;
            }
            if (inList)
            {
                html += QString::fromUtf8("</ul>");
                inList = false;
            }
            if (!inOl)
            {
                html += QString::fromUtf8("<ol>");
                inOl = true;
            }
            html += QString::fromUtf8("<li>") + formatInline(escapeHtml(ol.cap(1)))
                    + QString::fromUtf8("</li>");
            continue;
        }

        if (raw.trimmed().isEmpty())
        {
            if (inPara)
            {
                html += QString::fromUtf8("</p>");
                inPara = false;
            }
            continue;
        }

        if (inList)
        {
            html += QString::fromUtf8("</ul>");
            inList = false;
        }
        if (inOl)
        {
            html += QString::fromUtf8("</ol>");
            inOl = false;
        }
        if (!inPara)
        {
            html += QString::fromUtf8("<p>");
            inPara = true;
        }
        else
            html += QString::fromUtf8("<br>");
        html += formatInline(line);
    }
    closeLists();

    for (int i = 0; i < codeBlocks.size(); ++i)
    {
        const QString token = QString::fromUtf8("@@CODE%1@@").arg(i);
        html.replace(token, codeBlocks.at(i));
    }
    return html;
}
