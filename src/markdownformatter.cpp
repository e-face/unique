#include "markdownformatter.h"
//----------------------------------------------
#include "appsettings.h"
//----------------------------------------------
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QRegExp>
#include <QStringList>
#include <QUrl>
#include <QtMath>
#include <cmath>

//----------------------------------------------

static const int kMediaMaxPoints = 24;
static const int kMediaMaxNodes  = 24;

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

static QString resolveImageFile(QString src, const QString &baseDir)
{
    src = src.trimmed();
    if (src.startsWith(QLatin1Char('<')) && src.endsWith(QLatin1Char('>')))
        src = src.mid(1, src.size() - 2).trimmed();
    const QUrl asUrl(src);
    if (asUrl.isLocalFile())
        src = asUrl.toLocalFile();
    QFileInfo direct(src);
    if (direct.isAbsolute() && direct.isFile())
        return direct.absoluteFilePath();
    if (!baseDir.trimmed().isEmpty())
    {
        const QFileInfo relative(QDir(baseDir).filePath(src));
        if (relative.isFile())
            return relative.absoluteFilePath();
    }
    if (direct.isFile())
        return direct.absoluteFilePath();
    return QString();
}

//----------------------------------------------

static QString imageTag(const QString &path)
{
    const QString url = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
    return QString::fromUtf8("<img src=\"%1\" />").arg(url);
}

//----------------------------------------------

static QString figureHtml(const QString &inner, const QString &caption)
{
    QString html = QString::fromUtf8("<p align=\"center\">%1</p>").arg(inner);
    if (caption.trimmed().isEmpty())
        return html;
    const QString muted = AppSettings::instance()->mutedTextColor().name();
    html += QString::fromUtf8("<p align=\"center\"><span style=\"color:%1;\">%2</span></p>")
                .arg(muted, MarkdownFormatter::escapeHtml(caption.trimmed()));
    return html;
}

//----------------------------------------------

static QString imageHtml(const QString &alt, const QString &src, const QString &baseDir)
{
    const QString path = resolveImageFile(src, baseDir);
    if (path.isEmpty())
    {
        QString text = alt.trimmed();
        if (!text.isEmpty())
            text += QString::fromUtf8(" — ");
        text += src.trimmed();
        return figureHtml(MarkdownFormatter::escapeHtml(text), QString());
    }
    return figureHtml(imageTag(path), alt);
}

//----------------------------------------------

static QString pullImages(const QString &raw, QStringList *snippets, const QString &baseDir)
{
    QRegExp re(QString::fromUtf8("!\\[([^\\]]*)\\]\\(([^)]+)\\)"));
    QString out;
    int last = 0;
    int pos = 0;
    while ((pos = re.indexIn(raw, pos)) != -1)
    {
        out += raw.mid(last, pos - last);
        out += QString::fromUtf8("@@IMG%1@@").arg(snippets->size());
        const QString path = resolveImageFile(re.cap(2), baseDir);
        snippets->append(path.isEmpty()
                         ? MarkdownFormatter::escapeHtml(re.cap(1))
                         : imageTag(path));
        pos += re.matchedLength();
        last = pos;
    }
    out += raw.mid(last);
    return out;
}

//----------------------------------------------

static QString saveMediaPng(const QImage &image, const QByteArray &key)
{
    const QString dir = QDir(QDir::tempPath()).filePath(QString::fromUtf8("unique-chat-media"));
    if (!QDir().mkpath(dir))
        return QString();
    const QString path = QDir(dir).filePath(
        QString::fromUtf8(key.toHex()) + QString::fromUtf8(".png"));
    if (!QFileInfo(path).isFile())
    {
        if (!image.save(path, "PNG"))
            return QString();
    }
    return path;
}

//----------------------------------------------

static bool parseNumber(QString text, double *out)
{
    text = text.trimmed();
    if (text.endsWith(QLatin1Char('%')))
        text.chop(1);
    text.replace(QLatin1Char(','), QLatin1Char('.'));
    text = text.trimmed();
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok)
        return false;
    *out = value;
    return true;
}

//----------------------------------------------

struct ChartPoint
{
    QString label;
    double  value;
};

static bool parseChart(const QString &body, QString *type, QString *title,
                       QList<ChartPoint> *points)
{
    type->clear();
    title->clear();
    points->clear();
    const QStringList lines = body.split(QChar('\n'));
    bool typeSeen = false;
    foreach (QString line, lines)
    {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        if (!typeSeen)
        {
            const QString kind = line.toLower();
            if (kind != QString::fromUtf8("bar")
                && kind != QString::fromUtf8("line")
                && kind != QString::fromUtf8("pie"))
                return false;
            *type = kind;
            typeSeen = true;
            continue;
        }
        if (title->isEmpty() && !line.contains(QLatin1Char('|'))
            && line.startsWith(QString::fromUtf8("title:"), Qt::CaseInsensitive))
        {
            *title = line.mid(6).trimmed();
            continue;
        }
        const int bar = line.indexOf(QLatin1Char('|'));
        if (bar <= 0)
            return false;
        ChartPoint point;
        point.label = line.left(bar).trimmed();
        if (point.label.isEmpty())
            return false;
        if (!parseNumber(line.mid(bar + 1), &point.value))
            return false;
        points->append(point);
        if (points->size() > kMediaMaxPoints)
            return false;
    }
    return typeSeen && !points->isEmpty();
}

//----------------------------------------------

static QColor chartColor(int index)
{
    static const char *colors[] = {
        "#3b82f6", "#f59e0b", "#10b981", "#ef4444",
        "#8b5cf6", "#06b6d4", "#f97316", "#64748b"
    };
    return QColor(QString::fromUtf8(colors[index % 8]));
}

//----------------------------------------------

static void paintChartFrame(QPainter &painter, const QRect &area, const QString &title)
{
    painter.setPen(QColor(QString::fromUtf8("#d0d7e2")));
    painter.setBrush(QColor(QString::fromUtf8("#f8fafc")));
    painter.drawRoundedRect(area.adjusted(0, 0, -1, -1), 8, 8);
    if (title.isEmpty())
        return;
    painter.setPen(QColor(QString::fromUtf8("#1f2937")));
    QFont titleFont(QString::fromUtf8("Segoe UI"), 11);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    const QString shown = QFontMetrics(titleFont).elidedText(
        title, Qt::ElideRight, area.width() - 24);
    painter.drawText(QRect(area.left() + 12, area.top() + 8, area.width() - 24, 22),
                     Qt::AlignLeft | Qt::AlignVCenter, shown);
}

//----------------------------------------------

static QImage renderChartImage(const QString &body)
{
    QString type;
    QString title;
    QList<ChartPoint> points;
    if (!parseChart(body, &type, &title, &points))
        return QImage();

    const int n = points.size();
    double minValue = 0.0;
    double maxValue = 0.0;
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double value = points.at(i).value;
        if (i == 0 || value < minValue)
            minValue = value;
        if (i == 0 || value > maxValue)
            maxValue = value;
        if (value > 0.0)
            sum += value;
    }
    if (minValue > 0.0)
        minValue = 0.0;
    if (maxValue < 0.0)
        maxValue = 0.0;
    if (qAbs(maxValue - minValue) < 1e-9)
        maxValue = minValue + 1.0;

    const int top = title.isEmpty() ? 16 : 40;
    QImage image;
    if (type == QString::fromUtf8("pie"))
    {
        if (sum <= 0.0)
            return QImage();
        image = QImage(560, qMax(280, top + 36 + n * 22), QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(QString::fromUtf8("#f8fafc")));
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        paintChartFrame(painter, QRect(0, 0, image.width(), image.height()), title);
        const QRect pieRect(28, top + 8, 220, 220);
        QList<int> spans;
        int used = 0;
        int lastPositive = -1;
        for (int i = 0; i < n; ++i)
        {
            if (points.at(i).value <= 0.0)
            {
                spans.append(0);
                continue;
            }
            const int span = int((points.at(i).value / sum) * 360.0 * 16.0);
            spans.append(span);
            used += span;
            lastPositive = i;
        }
        if (lastPositive >= 0)
            spans[lastPositive] += 360 * 16 - used;
        int angle = 90 * 16;
        for (int i = 0; i < n; ++i)
        {
            if (spans.at(i) == 0)
                continue;
            painter.setPen(Qt::NoPen);
            painter.setBrush(chartColor(i));
            painter.drawPie(pieRect, angle, spans.at(i));
            angle += spans.at(i);
        }
        QFont labelFont(QString::fromUtf8("Segoe UI"), 9);
        painter.setFont(labelFont);
        int y = top + 12;
        for (int i = 0; i < n; ++i)
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(chartColor(i));
            painter.drawRoundedRect(268, y, 12, 12, 2, 2);
            painter.setPen(QColor(QString::fromUtf8("#1f2937")));
            const QString row = points.at(i).label + QString::fromUtf8("  ")
                                + QString::number(points.at(i).value, 'g', 6);
            painter.drawText(QRect(288, y - 2, 250, 18),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             QFontMetrics(labelFont).elidedText(row, Qt::ElideRight, 250));
            y += 22;
        }
        return image;
    }

    const int plotW = qBound(280, n * 52, 680);
    const int plotH = 200;
    const int left = 52;
    const int bottom = 48;
    const int width = left + plotW + 16;
    const int height = top + plotH + bottom;
    image = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(QString::fromUtf8("#f8fafc")));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    paintChartFrame(painter, QRect(0, 0, width, height), title);

    const double span = maxValue - minValue;
    const int baseY = top + plotH - int(((0.0 - minValue) / span) * plotH);
    painter.setPen(QColor(QString::fromUtf8("#e5e7eb")));
    for (int g = 0; g <= 4; ++g)
    {
        const int y = top + (plotH * g) / 4;
        painter.drawLine(left, y, left + plotW, y);
    }
    painter.setPen(QColor(QString::fromUtf8("#94a3b8")));
    painter.drawLine(left, top, left, top + plotH);
    painter.drawLine(left, baseY, left + plotW, baseY);

    QFont axisFont(QString::fromUtf8("Segoe UI"), 8);
    painter.setFont(axisFont);
    painter.setPen(QColor(QString::fromUtf8("#64748b")));
    painter.drawText(QRect(0, top - 8, left - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                     QString::number(maxValue, 'g', 4));
    painter.drawText(QRect(0, top + plotH - 8, left - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                     QString::number(minValue, 'g', 4));

    QFont labelFont(QString::fromUtf8("Segoe UI"), 8);
    const int slot = plotW / n;
    if (type == QString::fromUtf8("line"))
    {
        QPolygonF poly;
        for (int i = 0; i < n; ++i)
        {
            const double t = (points.at(i).value - minValue) / span;
            const double x = left + slot * i + slot / 2.0;
            const double y = top + plotH - t * plotH;
            poly << QPointF(x, y);
        }
        painter.setPen(QPen(chartColor(0), 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(poly);
        painter.setBrush(chartColor(0));
        painter.setPen(Qt::NoPen);
        for (int i = 0; i < poly.size(); ++i)
            painter.drawEllipse(poly.at(i), 3.5, 3.5);
    }
    else
    {
        const int barW = qMax(8, int(slot * 0.62));
        for (int i = 0; i < n; ++i)
        {
            const double t = (points.at(i).value - minValue) / span;
            const int x = left + slot * i + (slot - barW) / 2;
            const int y = top + plotH - int(t * plotH);
            const int y0 = baseY;
            QRect bar(x, qMin(y, y0), barW, qMax(1, qAbs(y - y0)));
            painter.setPen(Qt::NoPen);
            painter.setBrush(chartColor(i));
            painter.drawRoundedRect(bar, 3, 3);
        }
    }

    painter.setFont(labelFont);
    painter.setPen(QColor(QString::fromUtf8("#334155")));
    for (int i = 0; i < n; ++i)
    {
        const QString shown = QFontMetrics(labelFont).elidedText(
            points.at(i).label, Qt::ElideRight, slot - 4);
        painter.drawText(QRect(left + slot * i, top + plotH + 6, slot, 28),
                         Qt::AlignHCenter | Qt::AlignTop, shown);
    }
    return image;
}

//----------------------------------------------

struct DiagramNode
{
    QString name;
    int     rank;
    int     row;
    QRect   box;
};

struct DiagramEdge
{
    int     from;
    int     to;
    QString label;
};

static int diagramNode(QList<DiagramNode> *nodes, QHash<QString, int> *index, const QString &name)
{
    const QString key = name.trimmed();
    if (key.isEmpty())
        return -1;
    if (index->contains(key))
        return index->value(key);
    if (nodes->size() >= kMediaMaxNodes)
        return -1;
    DiagramNode node;
    node.name = key;
    node.rank = 0;
    node.row = 0;
    nodes->append(node);
    const int at = nodes->size() - 1;
    index->insert(key, at);
    return at;
}

//----------------------------------------------

static QImage renderDiagramImage(const QString &body)
{
    QList<DiagramNode> nodes;
    QHash<QString, int> index;
    QList<DiagramEdge> edges;
    bool overflow = false;
    foreach (QString line, body.split(QChar('\n')))
    {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        int at = line.indexOf(QString::fromUtf8("-->"));
        int arrowLen = 3;
        if (at < 0)
        {
            at = line.indexOf(QString::fromUtf8("->"));
            arrowLen = 2;
        }
        if (at < 0)
        {
            at = line.indexOf(QString::fromUtf8("→"));
            arrowLen = 1;
        }
        if (at < 0)
        {
            if (diagramNode(&nodes, &index, line) < 0)
                overflow = true;
            continue;
        }
        const QString left = line.left(at).trimmed();
        QString right = line.mid(at + arrowLen).trimmed();
        QString label;
        const int colon = right.indexOf(QLatin1Char(':'));
        if (colon >= 0)
        {
            label = right.mid(colon + 1).trimmed();
            right = right.left(colon).trimmed();
        }
        const int from = diagramNode(&nodes, &index, left);
        const int to = diagramNode(&nodes, &index, right);
        if (from < 0 || to < 0)
        {
            overflow = true;
            continue;
        }
        DiagramEdge edge;
        edge.from = from;
        edge.to = to;
        edge.label = label;
        edges.append(edge);
    }
    if (nodes.isEmpty() || overflow)
        return QImage();

    const int n = nodes.size();
    for (int pass = 0; pass < n; ++pass)
    {
        bool changed = false;
        for (int i = 0; i < edges.size(); ++i)
        {
            const int from = edges.at(i).from;
            const int to = edges.at(i).to;
            const int next = nodes.at(from).rank + 1;
            if (next < n && next > nodes.at(to).rank)
            {
                nodes[to].rank = next;
                changed = true;
            }
        }
        if (!changed)
            break;
    }
    QHash<int, int> rowsUsed;
    int maxRank = 0;
    int maxRow = 0;
    for (int i = 0; i < n; ++i)
    {
        const int row = rowsUsed.value(nodes.at(i).rank, 0);
        nodes[i].row = row;
        rowsUsed.insert(nodes.at(i).rank, row + 1);
        if (nodes.at(i).rank > maxRank)
            maxRank = nodes.at(i).rank;
        if (row > maxRow)
            maxRow = row;
    }

    const int boxW = 148;
    const int boxH = 36;
    const int gapX = 64;
    const int gapY = 22;
    const int margin = 18;
    const int width = margin * 2 + (maxRank + 1) * boxW + maxRank * gapX;
    const int height = margin * 2 + (maxRow + 1) * boxH + maxRow * gapY;
    for (int i = 0; i < n; ++i)
    {
        const int x = margin + nodes.at(i).rank * (boxW + gapX);
        const int y = margin + nodes.at(i).row * (boxH + gapY);
        nodes[i].box = QRect(x, y, boxW, boxH);
    }

    QImage image(qMax(width, 80), qMax(height, 60), QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(QString::fromUtf8("#f8fafc")));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QColor(QString::fromUtf8("#d0d7e2")));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRect(0, 0, image.width(), image.height()).adjusted(0, 0, -1, -1),
                            8, 8);

    QFont labelFont(QString::fromUtf8("Segoe UI"), 8);
    painter.setFont(labelFont);
    for (int i = 0; i < edges.size(); ++i)
    {
        const QRect from = nodes.at(edges.at(i).from).box;
        const QRect to = nodes.at(edges.at(i).to).box;
        QPointF a;
        QPointF b;
        if (from.right() < to.left())
        {
            a = QPointF(from.right(), from.center().y());
            b = QPointF(to.left(), to.center().y());
        }
        else
        {
            a = QPointF(from.center().x(), from.bottom());
            b = QPointF(to.center().x(), to.top());
        }
        painter.setPen(QPen(QColor(QString::fromUtf8("#475569")), 1.6));
        painter.drawLine(a, b);
        const double angle = std::atan2(b.y() - a.y(), b.x() - a.x());
        const double ah = 8.0;
        const QPointF p1(b.x() - ah * std::cos(angle - 0.4),
                         b.y() - ah * std::sin(angle - 0.4));
        const QPointF p2(b.x() - ah * std::cos(angle + 0.4),
                         b.y() - ah * std::sin(angle + 0.4));
        painter.drawLine(b, p1);
        painter.drawLine(b, p2);
        if (!edges.at(i).label.isEmpty())
        {
            const QPointF mid((a.x() + b.x()) / 2.0, (a.y() + b.y()) / 2.0);
            const QString shown = QFontMetrics(labelFont).elidedText(
                edges.at(i).label, Qt::ElideRight, 120);
            const int tw = QFontMetrics(labelFont).width(shown) + 8;
            const QRect chip(int(mid.x()) - tw / 2, int(mid.y()) - 8, tw, 16);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(QString::fromUtf8("#f8fafc")));
            painter.drawRoundedRect(chip, 3, 3);
            painter.setPen(QColor(QString::fromUtf8("#334155")));
            painter.drawText(chip, Qt::AlignCenter, shown);
        }
    }

    QFont nodeFont(QString::fromUtf8("Segoe UI"), 9);
    painter.setFont(nodeFont);
    for (int i = 0; i < n; ++i)
    {
        const QRect box = nodes.at(i).box;
        painter.setPen(QColor(QString::fromUtf8("#4a6fa5")));
        painter.setBrush(QColor(QString::fromUtf8("#eef4ff")));
        painter.drawRoundedRect(box, 8, 8);
        painter.setPen(QColor(QString::fromUtf8("#1f2937")));
        const QString shown = QFontMetrics(nodeFont).elidedText(
            nodes.at(i).name, Qt::ElideRight, box.width() - 12);
        painter.drawText(box.adjusted(6, 0, -6, 0), Qt::AlignCenter, shown);
    }
    return image;
}

//----------------------------------------------

static QString mediaSnippet(const QImage &image, const QByteArray &key)
{
    if (image.isNull())
        return QString();
    const QString path = saveMediaPng(image, key);
    if (path.isEmpty())
        return QString();
    return figureHtml(imageTag(path), QString());
}

//----------------------------------------------

static QString fenceTag(const QString &line)
{
    QString tag = line.mid(3).trimmed().toLower();
    const int space = tag.indexOf(QRegExp(QString::fromUtf8("\\s")));
    if (space > 0)
        tag = tag.left(space);
    return tag;
}

//----------------------------------------------

static QStringList splitTableCells(QString line)
{
    line = line.trimmed();
    if (line.startsWith(QLatin1Char('|')))
        line = line.mid(1);
    if (line.endsWith(QLatin1Char('|')))
        line.chop(1);
    QStringList cells;
    foreach (const QString &cell, line.split(QLatin1Char('|')))
        cells.append(cell.trimmed());
    return cells;
}

//----------------------------------------------

static bool tableSeparator(const QString &line, QList<int> *align)
{
    if (!line.contains(QLatin1Char('|')) || !line.contains(QLatin1Char('-')))
        return false;
    const QStringList cells = splitTableCells(line);
    if (cells.isEmpty())
        return false;
    align->clear();
    QRegExp dash(QString::fromUtf8("^:?-{3,}:?$"));
    foreach (const QString &cell, cells)
    {
        if (!dash.exactMatch(cell))
            return false;
        int mode = 0;
        if (cell.startsWith(QLatin1Char(':')) && cell.endsWith(QLatin1Char(':')))
            mode = 1;
        else if (cell.endsWith(QLatin1Char(':')))
            mode = 2;
        align->append(mode);
    }
    return true;
}

//----------------------------------------------

static QString alignAttr(int mode)
{
    if (mode == 1)
        return QString::fromUtf8(" align=\"center\"");
    if (mode == 2)
        return QString::fromUtf8(" align=\"right\"");
    return QString::fromUtf8(" align=\"left\"");
}

//----------------------------------------------

QString MarkdownFormatter::richText(const QString &raw, QStringList *snippets,
                                    const QString &baseDir)
{
    return formatInline(escapeHtml(pullImages(raw, snippets, baseDir)));
}

//----------------------------------------------

QString MarkdownFormatter::toHtml(QString markdown, QString imageBaseDir)
{
    if (markdown.isEmpty())
        return QString();

    QStringList codeBlocks;
    QStringList snippets;
    QStringList lines = markdown.split(QChar('\n'));
    QString rebuilt;
    bool inFence = false;
    QString fenceBuf;
    QString tag;
    foreach (const QString &line, lines)
    {
        if (line.startsWith(QString::fromUtf8("```")))
        {
            if (inFence)
            {
                const bool chart = (tag == QString::fromUtf8("chart"));
                const bool diagram = (tag == QString::fromUtf8("diagram")
                                      || tag == QString::fromUtf8("scheme"));
                QString special;
                if (chart || diagram)
                {
                    const QByteArray key = QString::fromUtf8(chart ? "chart-v1\n" : "diagram-v1\n")
                                               .toUtf8()
                                           + fenceBuf.toUtf8();
                    const QByteArray hash = QCryptographicHash::hash(
                        key, QCryptographicHash::Sha1);
                    const QImage image = chart ? renderChartImage(fenceBuf)
                                               : renderDiagramImage(fenceBuf);
                    special = mediaSnippet(image, hash);
                }
                if (!special.isEmpty())
                {
                    rebuilt += QString::fromUtf8("@@IMG%1@@").arg(snippets.size());
                    rebuilt += QChar('\n');
                    snippets.append(special);
                }
                else
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
                }
                fenceBuf.clear();
                tag.clear();
                inFence = false;
            }
            else
            {
                inFence = true;
                fenceBuf.clear();
                tag = fenceTag(line);
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

    // Close open lists and the current paragraph before a block element.
    struct ListCloser
    {
        QString *html;
        bool    *inList;
        bool    *inOl;
        bool    *inPara;
        void close()
        {
            if (*inPara)
            {
                *html += QString::fromUtf8("</p>");
                *inPara = false;
            }
            if (*inList)
            {
                *html += QString::fromUtf8("</ul>");
                *inList = false;
            }
            if (*inOl)
            {
                *html += QString::fromUtf8("</ol>");
                *inOl = false;
            }
        }
    };
    ListCloser closer;
    closer.html = &html;
    closer.inList = &inList;
    closer.inOl = &inOl;
    closer.inPara = &inPara;

    for (int i = 0; i < lines.size(); ++i)
    {
        const QString raw = lines.at(i);
        if (raw.startsWith(QString::fromUtf8("@@CODE"))
            || raw.startsWith(QString::fromUtf8("@@IMG")))
        {
            closer.close();
            html += raw;
            continue;
        }

        QList<int> align;
        if (raw.contains(QLatin1Char('|')) && i + 1 < lines.size()
            && tableSeparator(lines.at(i + 1), &align))
        {
            closer.close();
            const QStringList header = splitTableCells(raw);
            QString table = QString::fromUtf8(
                "<table align=\"center\" border=\"1\" cellpadding=\"6\" cellspacing=\"0\">");
            table += QString::fromUtf8("<tr>");
            for (int c = 0; c < header.size(); ++c)
            {
                const int mode = (c < align.size()) ? align.at(c) : 0;
                table += QString::fromUtf8("<th%1>%2</th>")
                             .arg(alignAttr(mode), richText(header.at(c), &snippets, imageBaseDir));
            }
            table += QString::fromUtf8("</tr>");
            i += 2;
            while (i < lines.size() && lines.at(i).contains(QLatin1Char('|'))
                   && !lines.at(i).trimmed().isEmpty()
                   && !lines.at(i).startsWith(QString::fromUtf8("@@")))
            {
                const QStringList cells = splitTableCells(lines.at(i));
                table += QString::fromUtf8("<tr>");
                const int count = qMax(header.size(), cells.size());
                for (int c = 0; c < count; ++c)
                {
                    const int mode = (c < align.size()) ? align.at(c) : 0;
                    const QString cell = (c < cells.size()) ? cells.at(c) : QString();
                    table += QString::fromUtf8("<td%1>%2</td>")
                                 .arg(alignAttr(mode), richText(cell, &snippets, imageBaseDir));
                }
                table += QString::fromUtf8("</tr>");
                ++i;
            }
            --i;
            table += QString::fromUtf8("</table>");
            html += table;
            continue;
        }

        QRegExp imageLine(QString::fromUtf8("^\\s*!\\[([^\\]]*)\\]\\(([^)]+)\\)\\s*$"));
        if (imageLine.exactMatch(raw))
        {
            closer.close();
            html += imageHtml(imageLine.cap(1), imageLine.cap(2), imageBaseDir);
            continue;
        }

        if (raw.startsWith(QString::fromUtf8("### ")))
        {
            closer.close();
            html += QString::fromUtf8("<h4>")
                    + richText(raw.mid(4), &snippets, imageBaseDir)
                    + QString::fromUtf8("</h4>");
            continue;
        }
        if (raw.startsWith(QString::fromUtf8("## ")))
        {
            closer.close();
            html += QString::fromUtf8("<h3>")
                    + richText(raw.mid(3), &snippets, imageBaseDir)
                    + QString::fromUtf8("</h3>");
            continue;
        }
        if (raw.startsWith(QString::fromUtf8("# ")))
        {
            closer.close();
            html += QString::fromUtf8("<h2>")
                    + richText(raw.mid(2), &snippets, imageBaseDir)
                    + QString::fromUtf8("</h2>");
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
            html += QString::fromUtf8("<li>")
                    + richText(ul.cap(1), &snippets, imageBaseDir)
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
            html += QString::fromUtf8("<li>")
                    + richText(ol.cap(1), &snippets, imageBaseDir)
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
        html += richText(raw, &snippets, imageBaseDir);
    }
    closer.close();

    for (int i = 0; i < codeBlocks.size(); ++i)
    {
        const QString token = QString::fromUtf8("@@CODE%1@@").arg(i);
        html.replace(token, codeBlocks.at(i));
    }
    for (int i = 0; i < snippets.size(); ++i)
    {
        const QString token = QString::fromUtf8("@@IMG%1@@").arg(i);
        html.replace(token, snippets.at(i));
    }
    return html;
}
