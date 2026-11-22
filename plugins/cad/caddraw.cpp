#include "caddraw.h"
//----------------------------------------------
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QTextStream>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

//----------------------------------------------

namespace
{

struct Vec2
{
    double x;
    double y;
    Vec2()
        : x(0)
        , y(0)
    {
    }
    Vec2(double ax, double ay)
        : x(ax)
        , y(ay)
    {
    }
};

struct PaperLine
{
    double x1, y1, x2, y2;
    bool   hidden;
};

struct PaperText
{
    double  x;
    double  y;
    double  height;
    QString text;
};

struct ViewDef
{
    const char *name;
    Vec3        right;
    Vec3        up;
    Vec3        toward;
    int         col;
    int         row;
};

//----------------------------------------------

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QString xmlEscape(const QString &text)
{
    QString out = text;
    out.replace(QLatin1Char('&'), QString::fromLatin1("&amp;"));
    out.replace(QLatin1Char('<'), QString::fromLatin1("&lt;"));
    out.replace(QLatin1Char('>'), QString::fromLatin1("&gt;"));
    return out;
}

QString paperNum(double value)
{
    return QLocale::c().toString(value, 'f', 4);
}

bool sheetSize(const QString &name, const QString &orientation, double *width, double *height,
               QString *error)
{
    const QString key = name.trimmed().toUpper();
    double shortSide = 210;
    double longSide = 297;
    if (key == QString::fromLatin1("A4"))
    {
        shortSide = 210;
        longSide = 297;
    }
    else if (key == QString::fromLatin1("A3"))
    {
        shortSide = 297;
        longSide = 420;
    }
    else if (key == QString::fromLatin1("A2"))
    {
        shortSide = 420;
        longSide = 594;
    }
    else if (key == QString::fromLatin1("A1"))
    {
        shortSide = 594;
        longSide = 841;
    }
    else
        return fail(error, QString::fromLatin1("sheet must be A4, A3, A2, or A1."));
    const bool landscape = orientation.trimmed().compare(QString::fromLatin1("portrait"),
                                                          Qt::CaseInsensitive) != 0;
    if (!orientation.trimmed().isEmpty()
        && orientation.trimmed().compare(QString::fromLatin1("landscape"), Qt::CaseInsensitive) != 0
        && orientation.trimmed().compare(QString::fromLatin1("portrait"), Qt::CaseInsensitive) != 0)
        return fail(error, QString::fromLatin1("orientation must be landscape or portrait."));
    *width = landscape ? longSide : shortSide;
    *height = landscape ? shortSide : longSide;
    return true;
}

bool writePaper(const QString &path, double width, double height,
                const QVector<PaperLine> &lines, const QVector<PaperText> &texts,
                QString *error)
{
    const QString dir = QFileInfo(path).absolutePath();
    if (!dir.isEmpty() && !QDir().mkpath(dir))
        return fail(error, QString::fromLatin1("Cannot create folder %1").arg(dir));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(error, QString::fromLatin1("Cannot write %1").arg(path));
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QString::fromLatin1("svg"))
    {
        QTextStream out(&file);
        out.setLocale(QLocale::c());
        out.setCodec("UTF-8");
        out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << paperNum(width)
            << "mm\" height=\"" << paperNum(height) << "mm\" viewBox=\"0 0 "
            << paperNum(width) << " " << paperNum(height) << "\">\n";
        out << "<rect x=\"0\" y=\"0\" width=\"" << paperNum(width) << "\" height=\""
            << paperNum(height) << "\" fill=\"#fff\" stroke=\"#222\" stroke-width=\"0.3\"/>\n";
        for (int i = 0; i < lines.size(); ++i)
        {
            const PaperLine &line = lines.at(i);
            const double y1 = height - line.y1;
            const double y2 = height - line.y2;
            out << "<line x1=\"" << paperNum(line.x1) << "\" y1=\"" << paperNum(y1)
                << "\" x2=\"" << paperNum(line.x2) << "\" y2=\"" << paperNum(y2)
                << "\" fill=\"none\" stroke=\"" << (line.hidden ? "#888" : "#111")
                << "\" stroke-width=\"" << (line.hidden ? "0.15" : "0.25") << "\"";
            if (line.hidden)
                out << " stroke-dasharray=\"1.5 1\"";
            out << "/>\n";
        }
        for (int i = 0; i < texts.size(); ++i)
        {
            const PaperText &text = texts.at(i);
            out << "<text x=\"" << paperNum(text.x) << "\" y=\""
                << paperNum(height - text.y) << "\" font-size=\"" << paperNum(text.height)
                << "\" fill=\"#222\" font-family=\"sans-serif\">"
                << xmlEscape(text.text) << "</text>\n";
        }
        out << "</svg>\n";
        return true;
    }
    if (suffix == QString::fromLatin1("dxf"))
    {
        QTextStream out(&file);
        out.setLocale(QLocale::c());
        out.setCodec("UTF-8");
        out << "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n";
        out << "0\nSECTION\n2\nENTITIES\n";
        out << "0\nLINE\n8\nSHEET\n10\n0\n20\n0\n11\n" << paperNum(width)
            << "\n21\n0\n0\nLINE\n8\nSHEET\n10\n" << paperNum(width) << "\n20\n0\n11\n"
            << paperNum(width) << "\n21\n" << paperNum(height)
            << "\n0\nLINE\n8\nSHEET\n10\n" << paperNum(width) << "\n20\n" << paperNum(height)
            << "\n11\n0\n21\n" << paperNum(height)
            << "\n0\nLINE\n8\nSHEET\n10\n0\n20\n" << paperNum(height) << "\n11\n0\n21\n0\n";
        for (int i = 0; i < lines.size(); ++i)
        {
            const PaperLine &line = lines.at(i);
            out << "0\nLINE\n8\n" << (line.hidden ? "HIDDEN" : "VISIBLE")
                << "\n10\n" << paperNum(line.x1) << "\n20\n" << paperNum(line.y1)
                << "\n11\n" << paperNum(line.x2) << "\n21\n" << paperNum(line.y2) << "\n";
        }
        for (int i = 0; i < texts.size(); ++i)
        {
            const PaperText &text = texts.at(i);
            QString label = text.text;
            label.replace(QLatin1Char('\n'), QLatin1Char(' '));
            out << "0\nTEXT\n8\nTEXT\n10\n" << paperNum(text.x) << "\n20\n" << paperNum(text.y)
                << "\n40\n" << paperNum(text.height) << "\n1\n" << label << "\n";
        }
        out << "0\nENDSEC\n0\nEOF\n";
        return true;
    }
    return fail(error, QString::fromLatin1("Drawing output must be .svg or .dxf."));
}

//----------------------------------------------

struct Feat
{
    int  a;
    int  b;
    int  f0;
    int  f1;
    bool boundary;
    bool silhouette;
    bool crease;
};

struct Seg2
{
    Vec2 a;
    Vec2 b;
    bool hidden;
};

bool pointInTri(Vec2 p, Vec2 a, Vec2 b, Vec2 c, double *alpha, double *beta, double *gamma)
{
    const Vec2 v0(b.x - a.x, b.y - a.y);
    const Vec2 v1(c.x - a.x, c.y - a.y);
    const Vec2 v2(p.x - a.x, p.y - a.y);
    const double den = v0.x * v1.y - v1.x * v0.y;
    if (std::fabs(den) < 1e-14)
        return false;
    const double bWeight = (v2.x * v1.y - v1.x * v2.y) / den;
    const double cWeight = (v0.x * v2.y - v2.x * v0.y) / den;
    const double aWeight = 1.0 - bWeight - cWeight;
    *alpha = aWeight;
    *beta = bWeight;
    *gamma = cWeight;
    return aWeight >= -1e-5 && bWeight >= -1e-5 && cWeight >= -1e-5;
}

const ViewDef kViews[] = {
    { "front",  Vec3(1, 0, 0),  Vec3(0, 0, 1), Vec3(0, -1, 0), 1, 1 },
    { "back",   Vec3(-1, 0, 0), Vec3(0, 0, 1), Vec3(0, 1, 0),  3, 1 },
    { "top",    Vec3(1, 0, 0),  Vec3(0, 1, 0), Vec3(0, 0, 1),  1, 2 },
    { "bottom", Vec3(1, 0, 0),  Vec3(0, -1, 0), Vec3(0, 0, -1), 1, 0 },
    { "right",  Vec3(0, 1, 0),  Vec3(0, 0, 1), Vec3(1, 0, 0),  2, 1 },
    { "left",   Vec3(0, -1, 0), Vec3(0, 0, 1), Vec3(-1, 0, 0), 0, 1 }
};

const ViewDef *findView(const QString &name)
{
    const QString key = name.trimmed().toLower();
    const int count = int(sizeof(kViews) / sizeof(kViews[0]));
    for (int i = 0; i < count; ++i)
    {
        if (key == QString::fromLatin1(kViews[i].name))
            return &kViews[i];
    }
    return 0;
}

quint64 edgeKey(int a, int b)
{
    const quint32 lo = quint32(qMin(a, b));
    const quint32 hi = quint32(qMax(a, b));
    return (quint64(lo) << 32) | quint64(hi);
}

} // namespace

//----------------------------------------------

bool writeOrthographic(const QString &output, const Mesh &mesh, const DrawOptions &options,
                       QString *summary, QString *error)
{
    if (mesh.triangles.isEmpty())
        return fail(error, QString::fromLatin1("Mesh has no triangles to draw."));
    if (mesh.triangles.size() > 200000)
        return fail(error, QString::fromLatin1(
                               "Mesh is too large to draw (limit 200000 triangles)."));
    double sheetW = 0;
    double sheetH = 0;
    if (!sheetSize(options.sheet, options.orientation, &sheetW, &sheetH, error))
        return false;

    QStringList wanted = options.views;
    if (wanted.isEmpty())
    {
        wanted << QString::fromLatin1("front") << QString::fromLatin1("top")
               << QString::fromLatin1("right");
    }
    QVector<const ViewDef *> views;
    for (int i = 0; i < wanted.size(); ++i)
    {
        const ViewDef *view = findView(wanted.at(i));
        if (!view)
            return fail(error, QString::fromLatin1(
                                   "Unknown view. Use front, back, top, bottom, left, or right."));
        views.append(view);
    }
    const bool firstAngle = options.projection.trimmed().compare(
                                QString::fromLatin1("first"), Qt::CaseInsensitive) == 0;
    if (!options.projection.trimmed().isEmpty() && !firstAngle
        && options.projection.trimmed().compare(QString::fromLatin1("third"),
                                                 Qt::CaseInsensitive) != 0)
        return fail(error, QString::fromLatin1("projection must be third or first."));

    QVector<Vec3> faceNormal(mesh.triangles.size());
    for (int i = 0; i < mesh.triangles.size(); ++i)
    {
        const Tri &tri = mesh.triangles.at(i);
        const Vec3 a = mesh.vertices.at(tri.a);
        const Vec3 b = mesh.vertices.at(tri.b);
        const Vec3 c = mesh.vertices.at(tri.c);
        faceNormal[i] = cross(b - a, c - a);
    }

    struct EdgeUse
    {
        int f0;
        int f1;
        EdgeUse()
            : f0(-1)
            , f1(-1)
        {
        }
    };
    QHash<quint64, EdgeUse> edges;
    for (int i = 0; i < mesh.triangles.size(); ++i)
    {
        const Tri &tri = mesh.triangles.at(i);
        const int id[3] = { tri.a, tri.b, tri.c };
        for (int e = 0; e < 3; ++e)
        {
            const quint64 key = edgeKey(id[e], id[(e + 1) % 3]);
            EdgeUse use = edges.value(key);
            if (use.f0 < 0)
                use.f0 = i;
            else if (use.f1 < 0)
                use.f1 = i;
            edges.insert(key, use);
        }
    }

    QVector<Feat> feats;
    const double pi = std::acos(-1.0);
    QHash<quint64, EdgeUse>::const_iterator it = edges.constBegin();
    for (; it != edges.constEnd(); ++it)
    {
        Feat feat;
        feat.a = int(it.key() >> 32);
        feat.b = int(it.key() & 0xffffffffu);
        feat.f0 = it.value().f0;
        feat.f1 = it.value().f1;
        feat.boundary = feat.f1 < 0;
        feat.silhouette = false;
        feat.crease = false;
        if (feat.f0 >= 0 && feat.f1 >= 0)
        {
            const Vec3 n0 = normalized(faceNormal.at(feat.f0));
            const Vec3 n1 = normalized(faceNormal.at(feat.f1));
            double cosine = dot(n0, n1);
            cosine = qBound(-1.0, cosine, 1.0);
            const double degrees = std::acos(cosine) * 180.0 / pi;
            feat.crease = degrees >= options.creaseDeg;
        }
        feats.append(feat);
    }

    Vec3 lo = mesh.vertices.at(0);
    Vec3 hi = lo;
    for (int i = 1; i < mesh.vertices.size(); ++i)
    {
        const Vec3 &v = mesh.vertices.at(i);
        lo.x = qMin(lo.x, v.x);
        lo.y = qMin(lo.y, v.y);
        lo.z = qMin(lo.z, v.z);
        hi.x = qMax(hi.x, v.x);
        hi.y = qMax(hi.y, v.y);
        hi.z = qMax(hi.z, v.z);
    }
    const double diag = qMax(1e-9, length(hi - lo));
    const bool doHidden = options.hidden && mesh.triangles.size() <= 8000;

    struct LaidView
    {
        QString     name;
        int         col;
        int         row;
        double      width;
        double      height;
        QVector<Seg2> segs;
    };
    QVector<LaidView> laid;
    int featureLines = 0;
    for (int v = 0; v < views.size(); ++v)
    {
        const ViewDef *view = views.at(v);
        LaidView item;
        item.name = QString::fromLatin1(view->name);
        item.col = view->col;
        item.row = view->row;
        if (firstAngle)
        {
            if (item.name == QString::fromLatin1("top"))
                item.row = 0;
            else if (item.name == QString::fromLatin1("bottom"))
                item.row = 2;
            else if (item.name == QString::fromLatin1("left"))
                item.col = 2;
            else if (item.name == QString::fromLatin1("right"))
                item.col = 0;
        }
        QVector<Feat> visible = feats;
        for (int f = 0; f < visible.size(); ++f)
        {
            const double side0 = (visible.at(f).f0 >= 0)
                                     ? dot(normalized(faceNormal.at(visible.at(f).f0)), view->toward)
                                     : 0;
            const double side1 = (visible.at(f).f1 >= 0)
                                     ? dot(normalized(faceNormal.at(visible.at(f).f1)), view->toward)
                                     : 0;
            const bool opposite = side0 * side1 < 0;
            const bool grazes = (std::fabs(side0) < 1e-8) != (std::fabs(side1) < 1e-8);
            if ((opposite || grazes) && visible.at(f).f1 >= 0)
                visible[f].silhouette = true;
        }
        double minX = 0;
        double minY = 0;
        double maxX = 0;
        double maxY = 0;
        bool any = false;
        QVector<Seg2> raw;
        for (int f = 0; f < visible.size(); ++f)
        {
            if (!visible.at(f).boundary && !visible.at(f).crease && !visible.at(f).silhouette)
                continue;
            const Vec3 a = mesh.vertices.at(visible.at(f).a);
            const Vec3 b = mesh.vertices.at(visible.at(f).b);
            Seg2 seg;
            seg.a = Vec2(dot(a, view->right), dot(a, view->up));
            seg.b = Vec2(dot(b, view->right), dot(b, view->up));
            seg.hidden = false;
            const double span = std::sqrt((seg.a.x - seg.b.x) * (seg.a.x - seg.b.x)
                                          + (seg.a.y - seg.b.y) * (seg.a.y - seg.b.y));
            if (span < diag * 1e-8)
                continue;
            if (doHidden)
            {
                const Vec3 mid = (a + b) * 0.5;
                const double depth = dot(mid, view->toward);
                const Vec2 point((seg.a.x + seg.b.x) * 0.5, (seg.a.y + seg.b.y) * 0.5);
                for (int t = 0; t < mesh.triangles.size(); ++t)
                {
                    if (t == visible.at(f).f0 || t == visible.at(f).f1)
                        continue;
                    const Tri &tri = mesh.triangles.at(t);
                    const Vec3 ta = mesh.vertices.at(tri.a);
                    const Vec3 tb = mesh.vertices.at(tri.b);
                    const Vec3 tc = mesh.vertices.at(tri.c);
                    double alpha = 0;
                    double beta = 0;
                    double gamma = 0;
                    const Vec2 pa(dot(ta, view->right), dot(ta, view->up));
                    const Vec2 pb(dot(tb, view->right), dot(tb, view->up));
                    const Vec2 pc(dot(tc, view->right), dot(tc, view->up));
                    if (!pointInTri(point, pa, pb, pc, &alpha, &beta, &gamma))
                        continue;
                    const double triDepth = alpha * dot(ta, view->toward)
                                            + beta * dot(tb, view->toward)
                                            + gamma * dot(tc, view->toward);
                    if (triDepth > depth + diag * 1e-6)
                    {
                        seg.hidden = true;
                        break;
                    }
                }
            }
            raw.append(seg);
            const Vec2 ends[2] = { seg.a, seg.b };
            for (int e = 0; e < 2; ++e)
            {
                if (!any)
                {
                    minX = maxX = ends[e].x;
                    minY = maxY = ends[e].y;
                    any = true;
                }
                else
                {
                    minX = qMin(minX, ends[e].x);
                    minY = qMin(minY, ends[e].y);
                    maxX = qMax(maxX, ends[e].x);
                    maxY = qMax(maxY, ends[e].y);
                }
            }
        }
        if (!any)
            continue;
        for (int s = 0; s < raw.size(); ++s)
        {
            raw[s].a.x -= minX;
            raw[s].a.y -= minY;
            raw[s].b.x -= minX;
            raw[s].b.y -= minY;
        }
        item.segs = raw;
        item.width = qMax(1e-9, maxX - minX);
        item.height = qMax(1e-9, maxY - minY);
        featureLines += raw.size();
        laid.append(item);
    }
    if (laid.isEmpty())
        return fail(error, QString::fromLatin1("The views have no edges to draw."));

    QVector<int> cols;
    QVector<int> rows;
    for (int i = 0; i < laid.size(); ++i)
    {
        if (!cols.contains(laid.at(i).col))
            cols.append(laid.at(i).col);
        if (!rows.contains(laid.at(i).row))
            rows.append(laid.at(i).row);
    }
    std::sort(cols.begin(), cols.end());
    std::sort(rows.begin(), rows.end());
    QVector<double> colW(cols.size(), 0);
    QVector<double> rowH(rows.size(), 0);
    for (int i = 0; i < laid.size(); ++i)
    {
        const int col = cols.indexOf(laid.at(i).col);
        const int row = rows.indexOf(laid.at(i).row);
        colW[col] = qMax(colW.at(col), laid.at(i).width);
        rowH[row] = qMax(rowH.at(row), laid.at(i).height);
    }
    double sumW = 0;
    double sumH = 0;
    for (int i = 0; i < colW.size(); ++i)
        sumW += colW.at(i);
    for (int i = 0; i < rowH.size(); ++i)
        sumH += rowH.at(i);

    const double margin = 12;
    const double gap = 14;
    const double label = 5;
    const double title = 10;
    const double usableW = sheetW - margin * 2 - gap * qMax(0, cols.size() - 1);
    const double usableH = sheetH - margin * 2 - title - label * rows.size()
                           - gap * qMax(0, rows.size() - 1);
    double scale = options.scale;
    if (scale < 0)
        return fail(error, QString::fromLatin1("scale must be zero or positive."));
    if (scale == 0)
    {
        if (sumW <= 0 || sumH <= 0 || usableW <= 0 || usableH <= 0)
            return fail(error, QString::fromLatin1("The sheet is too small for these views."));
        scale = qMin(usableW / sumW, usableH / sumH);
    }

    QVector<double> colX(cols.size(), margin);
    QVector<double> rowY(rows.size(), margin + title);
    for (int i = 1; i < cols.size(); ++i)
        colX[i] = colX.at(i - 1) + colW.at(i - 1) * scale + gap;
    for (int i = 1; i < rows.size(); ++i)
        rowY[i] = rowY.at(i - 1) + rowH.at(i - 1) * scale + label + gap;

    QVector<PaperLine> lines;
    QVector<PaperText> texts;
    for (int i = 0; i < laid.size(); ++i)
    {
        const int col = cols.indexOf(laid.at(i).col);
        const int row = rows.indexOf(laid.at(i).row);
        const double ox = colX.at(col);
        const double oy = rowY.at(row);
        for (int s = 0; s < laid.at(i).segs.size(); ++s)
        {
            const Seg2 &seg = laid.at(i).segs.at(s);
            PaperLine line;
            line.x1 = ox + seg.a.x * scale;
            line.y1 = oy + seg.a.y * scale;
            line.x2 = ox + seg.b.x * scale;
            line.y2 = oy + seg.b.y * scale;
            line.hidden = seg.hidden;
            lines.append(line);
        }
        PaperText caption;
        caption.x = ox;
        caption.y = oy + laid.at(i).height * scale + 0.5;
        caption.height = 3.2;
        caption.text = laid.at(i).name;
        texts.append(caption);
    }
    PaperText footer;
    footer.x = margin;
    footer.y = 4;
    footer.height = 3.5;
    footer.text = QString::fromLatin1("%1  %2  scale %3")
                      .arg(QFileInfo(output).fileName(),
                           options.units.trimmed().isEmpty()
                               ? QString::fromLatin1("mm")
                               : options.units.trimmed(),
                           cadNum(scale));
    texts.append(footer);

    if (!writePaper(output, sheetW, sheetH, lines, texts, error))
        return false;
    if (summary)
    {
        QStringList names;
        for (int i = 0; i < laid.size(); ++i)
            names << laid.at(i).name;
        *summary = QString::fromLatin1("views=%1 scale=%2 lines=%3 sheet=%4 hidden=%5")
                       .arg(names.join(QLatin1Char(',')))
                       .arg(cadNum(scale))
                       .arg(featureLines)
                       .arg(options.sheet.trimmed().toUpper())
                       .arg(doHidden ? QString::fromLatin1("yes")
                                     : (options.hidden ? QString::fromLatin1("skipped")
                                                       : QString::fromLatin1("no")));
    }
    return true;
}

//----------------------------------------------

bool writeSectionDrawing(const QString &output, const QList<SectionLoop> &loops,
                         Vec3 planePoint, Vec3 planeNormal, const DrawOptions &options,
                         QString *summary, QString *error)
{
    if (loops.isEmpty())
        return fail(error, QString::fromLatin1("Section has no curves."));
    double sheetW = 0;
    double sheetH = 0;
    if (!sheetSize(options.sheet, options.orientation, &sheetW, &sheetH, error))
        return false;
    const Vec3 normal = normalized(planeNormal);
    const Vec3 helper = std::fabs(normal.x) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    const Vec3 axisU = normalized(cross(helper, normal));
    const Vec3 axisV = normalized(cross(normal, axisU));

    QVector<PaperLine> local;
    double minX = 0;
    double minY = 0;
    double maxX = 0;
    double maxY = 0;
    bool any = false;
    int closed = 0;
    int open = 0;
    double curveLength = 0;
    for (int i = 0; i < loops.size(); ++i)
    {
        const SectionLoop &loop = loops.at(i);
        if (loop.points.size() < 2)
            continue;
        if (loop.closed)
            closed += 1;
        else
            open += 1;
        const int segments = loop.closed ? loop.points.size() : loop.points.size() - 1;
        for (int s = 0; s < segments; ++s)
        {
            const Vec3 a = loop.points.at(s);
            const Vec3 b = loop.points.at((s + 1) % loop.points.size());
            curveLength += length(b - a);
            PaperLine line;
            line.x1 = dot(a - planePoint, axisU);
            line.y1 = dot(a - planePoint, axisV);
            line.x2 = dot(b - planePoint, axisU);
            line.y2 = dot(b - planePoint, axisV);
            line.hidden = false;
            local.append(line);
            const double xs[2] = { line.x1, line.x2 };
            const double ys[2] = { line.y1, line.y2 };
            for (int k = 0; k < 2; ++k)
            {
                if (!any)
                {
                    minX = maxX = xs[k];
                    minY = maxY = ys[k];
                    any = true;
                }
                else
                {
                    minX = qMin(minX, xs[k]);
                    minY = qMin(minY, ys[k]);
                    maxX = qMax(maxX, xs[k]);
                    maxY = qMax(maxY, ys[k]);
                }
            }
        }
    }
    if (!any)
        return fail(error, QString::fromLatin1("Section has no curves."));
    const double modelW = qMax(1e-9, maxX - minX);
    const double modelH = qMax(1e-9, maxY - minY);
    const double margin = 16;
    const double usableW = sheetW - margin * 2;
    const double usableH = sheetH - margin * 2 - 10;
    double scale = options.scale;
    if (scale < 0)
        return fail(error, QString::fromLatin1("scale must be zero or positive."));
    if (scale == 0)
        scale = qMin(usableW / modelW, usableH / modelH);
    QVector<PaperLine> lines;
    for (int i = 0; i < local.size(); ++i)
    {
        PaperLine line = local.at(i);
        line.x1 = margin + (line.x1 - minX) * scale;
        line.y1 = margin + 8 + (line.y1 - minY) * scale;
        line.x2 = margin + (line.x2 - minX) * scale;
        line.y2 = margin + 8 + (line.y2 - minY) * scale;
        lines.append(line);
    }
    QVector<PaperText> texts;
    PaperText footer;
    footer.x = margin;
    footer.y = 4;
    footer.height = 3.5;
    footer.text = QString::fromLatin1("section  length %1  %2  scale %3")
                      .arg(cadNum(curveLength),
                           options.units.trimmed().isEmpty()
                               ? QString::fromLatin1("mm")
                               : options.units.trimmed(),
                           cadNum(scale));
    texts.append(footer);
    if (!writePaper(output, sheetW, sheetH, lines, texts, error))
        return false;
    if (summary)
    {
        *summary = QString::fromLatin1("loops=%1 open=%2 length=%3 scale=%4 lines=%5")
                       .arg(closed)
                       .arg(open)
                       .arg(cadNum(curveLength))
                       .arg(cadNum(scale))
                       .arg(lines.size());
    }
    return true;
}
