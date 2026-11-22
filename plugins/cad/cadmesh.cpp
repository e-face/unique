#include "cadmesh.h"
//----------------------------------------------
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRegExp>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSet>
#include <QTextStream>
#include <QtGlobal>
//----------------------------------------------
#include <cstring>

//----------------------------------------------

const qint64 kMaxFileBytes = 64 * 1024 * 1024;
const int    kMaxTriangles = 2000000;
const int    kMaxAssemblyDepth = 4;

//----------------------------------------------

struct WeldKey
{
    qint64 x;
    qint64 y;
    qint64 z;

    bool operator==(const WeldKey &other) const
    {
        return x == other.x && y == other.y && z == other.z;
    }
};

uint qHash(const WeldKey &key, uint seed = 0)
{
    return ::qHash(key.x, seed) ^ ::qHash(key.y, seed + 1) ^ ::qHash(key.z, seed + 2);
}

//----------------------------------------------

QString suffixOf(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

float readF32le(const uchar *bytes)
{
    uchar native[4];
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    native[0] = bytes[3];
    native[1] = bytes[2];
    native[2] = bytes[1];
    native[3] = bytes[0];
#else
    native[0] = bytes[0];
    native[1] = bytes[1];
    native[2] = bytes[2];
    native[3] = bytes[3];
#endif
    float value;
    memcpy(&value, native, 4);
    return value;
}

void writeF32le(QFile *file, float value)
{
    uchar native[4];
    memcpy(native, &value, 4);
    uchar bytes[4];
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    bytes[0] = native[3];
    bytes[1] = native[2];
    bytes[2] = native[1];
    bytes[3] = native[0];
#else
    bytes[0] = native[0];
    bytes[1] = native[1];
    bytes[2] = native[2];
    bytes[3] = native[3];
#endif
    file->write(reinterpret_cast<const char *>(bytes), 4);
}

void writeU32le(QFile *file, quint32 value)
{
    char bytes[4];
    bytes[0] = char(value & 255);
    bytes[1] = char((value >> 8) & 255);
    bytes[2] = char((value >> 16) & 255);
    bytes[3] = char((value >> 24) & 255);
    file->write(bytes, 4);
}

void writeU16le(QFile *file, quint16 value)
{
    char bytes[2];
    bytes[0] = char(value & 255);
    bytes[1] = char((value >> 8) & 255);
    file->write(bytes, 2);
}

//----------------------------------------------

void boundsOf(const QVector<Vec3> &vertices, Vec3 *minimum, Vec3 *maximum, bool *any)
{
    if (!any)
        return;
    *any = !vertices.isEmpty();
    if (!*any)
        return;
    Vec3 lo = vertices.at(0);
    Vec3 hi = vertices.at(0);
    for (int i = 1; i < vertices.size(); ++i)
    {
        const Vec3 &v = vertices.at(i);
        lo.x = qMin(lo.x, v.x);
        lo.y = qMin(lo.y, v.y);
        lo.z = qMin(lo.z, v.z);
        hi.x = qMax(hi.x, v.x);
        hi.y = qMax(hi.y, v.y);
        hi.z = qMax(hi.z, v.z);
    }
    if (minimum)
        *minimum = lo;
    if (maximum)
        *maximum = hi;
}

double weldTolerance(const QVector<Vec3> &vertices)
{
    Vec3 lo;
    Vec3 hi;
    bool any = false;
    boundsOf(vertices, &lo, &hi, &any);
    if (!any)
        return 1e-9;
    const double diag = length(hi - lo);
    return qMax(1e-9, diag * 1e-9);
}

void weldMesh(Mesh *mesh)
{
    if (!mesh || mesh->vertices.isEmpty() || mesh->triangles.isEmpty())
        return;
    const double tol = weldTolerance(mesh->vertices);
    QHash<WeldKey, int> indexOf;
    QVector<int> remap(mesh->vertices.size());
    QVector<Vec3> fresh;
    fresh.reserve(mesh->vertices.size());
    for (int i = 0; i < mesh->vertices.size(); ++i)
    {
        const Vec3 &v = mesh->vertices.at(i);
        WeldKey key;
        key.x = qRound64(v.x / tol);
        key.y = qRound64(v.y / tol);
        key.z = qRound64(v.z / tol);
        const QHash<WeldKey, int>::const_iterator found = indexOf.constFind(key);
        if (found == indexOf.constEnd())
        {
            const int id = fresh.size();
            fresh.append(v);
            indexOf.insert(key, id);
            remap[i] = id;
        }
        else
            remap[i] = found.value();
    }
    QVector<Tri> faces;
    faces.reserve(mesh->triangles.size());
    for (int i = 0; i < mesh->triangles.size(); ++i)
    {
        Tri tri = mesh->triangles.at(i);
        tri.a = remap.at(tri.a);
        tri.b = remap.at(tri.b);
        tri.c = remap.at(tri.c);
        if (tri.a == tri.b || tri.b == tri.c || tri.c == tri.a)
            continue;
        faces.append(tri);
    }
    mesh->vertices = fresh;
    mesh->triangles = faces;
}

bool tooMany(const Mesh &mesh, QString *error)
{
    if (mesh.triangles.size() <= kMaxTriangles)
        return false;
    if (error)
        *error = QString::fromLatin1("Mesh has too many triangles (limit %1).")
                     .arg(kMaxTriangles);
    return true;
}

//----------------------------------------------

quint64 edgeKey(int a, int b)
{
    const quint32 lo = quint32(a < b ? a : b);
    const quint32 hi = quint32(a < b ? b : a);
    return (quint64(lo) << 32) | quint64(hi);
}

//----------------------------------------------

bool readAll(const QString &path, QByteArray *data, QString *error)
{
    QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return fail(error, QString::fromLatin1("File not found: %1").arg(path));
    if (info.size() > kMaxFileBytes)
        return fail(error, QString::fromLatin1("File is larger than %1 bytes.")
                               .arg(qint64(kMaxFileBytes)));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QString::fromLatin1("Cannot open %1").arg(path));
    *data = file.readAll();
    return true;
}

bool ensureParentDir(const QString &path, QString *error)
{
    const QString dir = QFileInfo(path).absolutePath();
    if (dir.isEmpty() || QDir().mkpath(dir))
        return true;
    return fail(error, QString::fromLatin1("Cannot create folder %1").arg(dir));
}

//----------------------------------------------

bool loadStl(const QByteArray &data, Mesh *mesh, QString *error)
{
    mesh->vertices.clear();
    mesh->triangles.clear();
    const int size = data.size();
    if (size >= 84)
    {
        const uchar *bytes = reinterpret_cast<const uchar *>(data.constData());
        const quint32 count = quint32(bytes[80])
                              | (quint32(bytes[81]) << 8)
                              | (quint32(bytes[82]) << 16)
                              | (quint32(bytes[83]) << 24);
        const qint64 expect = 84 + qint64(count) * 50;
        if (count > 0 && expect == size)
        {
            if (int(count) > kMaxTriangles)
                return fail(error, QString::fromLatin1(
                                       "Mesh has too many triangles (limit %1).")
                                       .arg(kMaxTriangles));
            mesh->vertices.reserve(int(count) * 3);
            mesh->triangles.reserve(int(count));
            for (quint32 i = 0; i < count; ++i)
            {
                const uchar *tri = bytes + 84 + int(i) * 50;
                int index[3];
                for (int v = 0; v < 3; ++v)
                {
                    const uchar *xyz = tri + 12 + v * 12;
                    index[v] = mesh->vertices.size();
                    mesh->vertices.append(Vec3(readF32le(xyz),
                                               readF32le(xyz + 4),
                                               readF32le(xyz + 8)));
                }
                Tri face;
                face.a = index[0];
                face.b = index[1];
                face.c = index[2];
                mesh->triangles.append(face);
            }
            weldMesh(mesh);
            return true;
        }
    }

    QTextStream in(data);
    in.setLocale(QLocale::c());
    in.setCodec("UTF-8");
    QVector<Vec3> corners;
    while (!in.atEnd())
    {
        QString token;
        in >> token;
        if (token.compare(QString::fromLatin1("vertex"), Qt::CaseInsensitive) != 0)
            continue;
        double x = 0;
        double y = 0;
        double z = 0;
        in >> x >> y >> z;
        if (in.status() != QTextStream::Ok)
            return fail(error, QString::fromLatin1("STL text has a broken vertex."));
        corners.append(Vec3(x, y, z));
        if (corners.size() > kMaxTriangles * 3)
            return fail(error, QString::fromLatin1(
                                   "Mesh has too many triangles (limit %1).")
                                   .arg(kMaxTriangles));
    }
    if (corners.isEmpty() || (corners.size() % 3) != 0)
        return fail(error, QString::fromLatin1("STL text has no complete triangles."));
    mesh->vertices = corners;
    for (int i = 0; i < corners.size(); i += 3)
    {
        Tri face;
        face.a = i;
        face.b = i + 1;
        face.c = i + 2;
        mesh->triangles.append(face);
    }
    weldMesh(mesh);
    return true;
}

//----------------------------------------------

bool loadObj(const QByteArray &data, QList<Part> *parts, QString *error)
{
    QTextStream in(data);
    in.setLocale(QLocale::c());
    in.setCodec("UTF-8");
    QVector<Vec3> vertices;
    struct RawPart
    {
        QString    name;
        QVector<Tri> faces;
    };
    QList<RawPart> raw;
    RawPart current;

    while (!in.atEnd())
    {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList bits = line.split(QRegExp(QString::fromLatin1("\\s+")),
                                            QString::SkipEmptyParts);
        if (bits.isEmpty())
            continue;
        const QString cmd = bits.at(0);
        if (cmd == QString::fromLatin1("v") && bits.size() >= 4)
        {
            vertices.append(Vec3(bits.at(1).toDouble(),
                                 bits.at(2).toDouble(),
                                 bits.at(3).toDouble()));
            continue;
        }
        if (cmd == QString::fromLatin1("o") || cmd == QString::fromLatin1("g"))
        {
            QString name;
            for (int i = 1; i < bits.size(); ++i)
            {
                if (!name.isEmpty())
                    name += QLatin1Char(' ');
                name += bits.at(i);
            }
            if (current.faces.isEmpty())
            {
                if (!name.isEmpty())
                    current.name = name;
            }
            else
            {
                raw.append(current);
                current = RawPart();
                current.name = name;
            }
            continue;
        }
        if (cmd != QString::fromLatin1("f") || bits.size() < 4)
            continue;
        QVector<int> poly;
        for (int i = 1; i < bits.size(); ++i)
        {
            const QString cell = bits.at(i).section(QLatin1Char('/'), 0, 0);
            bool ok = false;
            int index = cell.toInt(&ok);
            if (!ok || index == 0)
                return fail(error, QString::fromLatin1("OBJ face index is invalid."));
            if (index < 0)
                index = vertices.size() + index;
            else
                index -= 1;
            if (index < 0 || index >= vertices.size())
                return fail(error, QString::fromLatin1("OBJ face index is out of range."));
            poly.append(index);
        }
        for (int i = 1; i + 1 < poly.size(); ++i)
        {
            Tri face;
            face.a = poly.at(0);
            face.b = poly.at(i);
            face.c = poly.at(i + 1);
            current.faces.append(face);
            if (current.faces.size() > kMaxTriangles)
                return fail(error, QString::fromLatin1(
                                       "Mesh has too many triangles (limit %1).")
                                       .arg(kMaxTriangles));
        }
    }
    if (!current.faces.isEmpty() || raw.isEmpty())
        raw.append(current);
    if (vertices.isEmpty() || raw.isEmpty())
        return fail(error, QString::fromLatin1("OBJ file has no faces."));

    parts->clear();
    for (int p = 0; p < raw.size(); ++p)
    {
        if (raw.at(p).faces.isEmpty())
            continue;
        QHash<int, int> remap;
        Part part;
        part.name = raw.at(p).name;
        for (int f = 0; f < raw.at(p).faces.size(); ++f)
        {
            const Tri src = raw.at(p).faces.at(f);
            const int id[3] = { src.a, src.b, src.c };
            Tri face;
            int *dst[3] = { &face.a, &face.b, &face.c };
            for (int k = 0; k < 3; ++k)
            {
                const QHash<int, int>::const_iterator found = remap.constFind(id[k]);
                if (found == remap.constEnd())
                {
                    const int fresh = part.mesh.vertices.size();
                    part.mesh.vertices.append(vertices.at(id[k]));
                    remap.insert(id[k], fresh);
                    *dst[k] = fresh;
                }
                else
                    *dst[k] = found.value();
            }
            part.mesh.triangles.append(face);
        }
        weldMesh(&part.mesh);
        parts->append(part);
    }
    if (parts->isEmpty())
        return fail(error, QString::fromLatin1("OBJ file has no faces."));
    return true;
}

//----------------------------------------------

void includePoint(Vec3 point, Vec3 *lo, Vec3 *hi, bool *any)
{
    if (!*any)
    {
        *lo = point;
        *hi = point;
        *any = true;
        return;
    }
    lo->x = qMin(lo->x, point.x);
    lo->y = qMin(lo->y, point.y);
    lo->z = qMin(lo->z, point.z);
    hi->x = qMax(hi->x, point.x);
    hi->y = qMax(hi->y, point.y);
    hi->z = qMax(hi->z, point.z);
}

double jsonNumber(const QJsonValue &value, bool *ok)
{
    if (value.isDouble())
    {
        *ok = true;
        return value.toDouble();
    }
    if (value.isString())
    {
        bool local = false;
        const double number = QLocale::c().toDouble(value.toString().trimmed(), &local);
        *ok = local;
        return number;
    }
    *ok = false;
    return 0;
}

bool readVec3Value(const QJsonValue &value, Vec3 *out, bool *present, QString *error)
{
    if (present)
        *present = false;
    if (value.isUndefined() || value.isNull())
        return true;
    if (!value.isArray() || value.toArray().size() != 3)
        return fail(error, QString::fromLatin1("Expected three numbers [x, y, z]."));
    const QJsonArray array = value.toArray();
    double xyz[3];
    for (int i = 0; i < 3; ++i)
    {
        bool ok = false;
        xyz[i] = jsonNumber(array.at(i), &ok);
        if (!ok)
            return fail(error, QString::fromLatin1("Expected three numbers [x, y, z]."));
    }
    *out = Vec3(xyz[0], xyz[1], xyz[2]);
    if (present)
        *present = true;
    return true;
}

bool readScaleValue(const QJsonValue &value, Vec3 *out, QString *error)
{
    if (value.isUndefined() || value.isNull())
    {
        *out = Vec3(1, 1, 1);
        return true;
    }
    if (value.isDouble() || value.isString())
    {
        bool ok = false;
        const double number = jsonNumber(value, &ok);
        if (!ok)
            return fail(error, QString::fromLatin1("scale must be a number or [x, y, z]."));
        *out = Vec3(number, number, number);
        return true;
    }
    return readVec3Value(value, out, 0, error);
}

//----------------------------------------------

bool loadDxf(const QByteArray &data, Model *model, QString *error)
{
    QTextStream in(data);
    in.setLocale(QLocale::c());
    in.setCodec("UTF-8");
    if (data.startsWith("AutoCAD Binary DXF"))
        return fail(error, QString::fromLatin1("Binary DXF is not supported."));

    bool inEntities = false;
    QString entity;
    QString layer;
    double num[16];
    bool has[16];
    QVector<Vec3> poly;
    bool inPoly = false;
    bool polyClosed = false;
    Mesh faces;

    for (int i = 0; i < 16; ++i)
    {
        num[i] = 0;
        has[i] = false;
    }

    auto clearEntity = [&]() {
        for (int i = 0; i < 16; ++i)
        {
            num[i] = 0;
            has[i] = false;
        }
        layer.clear();
    };
    auto dxfPoint = [&](int slot) {
        const int base = slot * 3;
        return Vec3(has[base] ? num[base] : 0,
                    has[base + 1] ? num[base + 1] : 0,
                    has[base + 2] ? num[base + 2] : 0);
    };
    auto noteLayer = [&]() {
        const QString name = layer.isEmpty() ? QString::fromLatin1("0") : layer;
        if (!model->layers.contains(name))
            model->layers.append(name);
    };
    auto addLength = [&](double segment) {
        if (segment > 0)
            model->dxfLength += segment;
    };

    auto flush = [&]() {
        if (entity == QString::fromLatin1("LINE") && has[0] && has[3])
        {
            const Vec3 a = dxfPoint(0);
            const Vec3 b = dxfPoint(1);
            includePoint(a, &model->dxfMin, &model->dxfMax, &model->hasDxfBounds);
            includePoint(b, &model->dxfMin, &model->dxfMax, &model->hasDxfBounds);
            addLength(length(b - a));
            model->dxfLines += 1;
            noteLayer();
        }
        else if (entity == QString::fromLatin1("CIRCLE") && has[0] && has[10])
        {
            const Vec3 c = dxfPoint(0);
            const double r = num[10];
            includePoint(c + Vec3(r, r, 0), &model->dxfMin, &model->dxfMax, &model->hasDxfBounds);
            includePoint(c - Vec3(r, r, 0), &model->dxfMin, &model->dxfMax, &model->hasDxfBounds);
            addLength(2.0 * std::acos(-1.0) * qAbs(r));
            model->dxfCircles += 1;
            noteLayer();
        }
        else if (entity == QString::fromLatin1("ARC") && has[0] && has[10] && has[11] && has[12])
        {
            const Vec3 c = dxfPoint(0);
            const double r = qAbs(num[10]);
            double a0 = num[11];
            double a1 = num[12];
            double sweep = a1 - a0;
            if (sweep < 0)
                sweep += 360.0;
            addLength(r * sweep * std::acos(-1.0) / 180.0);
            includePoint(c + Vec3(r, r, 0), &model->dxfMin, &model->dxfMax, &model->hasDxfBounds);
            includePoint(c - Vec3(r, r, 0), &model->dxfMin, &model->dxfMax, &model->hasDxfBounds);
            model->dxfArcs += 1;
            noteLayer();
        }
        else if (entity == QString::fromLatin1("LWPOLYLINE") && poly.size() >= 2)
        {
            for (int i = 0; i < poly.size(); ++i)
                includePoint(poly.at(i), &model->dxfMin, &model->dxfMax, &model->hasDxfBounds);
            const int last = polyClosed ? poly.size() : poly.size() - 1;
            for (int i = 0; i < last; ++i)
                addLength(length(poly.at((i + 1) % poly.size()) - poly.at(i)));
            model->dxfPolylines += 1;
            noteLayer();
        }
        else if (entity == QString::fromLatin1("3DFACE"))
        {
            QVector<Vec3> corners;
            for (int slot = 0; slot < 4; ++slot)
            {
                if (!has[slot * 3] && !has[slot * 3 + 1])
                    continue;
                const Vec3 point = dxfPoint(slot);
                if (!corners.isEmpty() && length(point - corners.last()) < 1e-12)
                    continue;
                corners.append(point);
                includePoint(point, &model->dxfMin, &model->dxfMax, &model->hasDxfBounds);
            }
            if (corners.size() >= 3)
            {
                const int base = faces.vertices.size();
                for (int i = 0; i < corners.size(); ++i)
                    faces.vertices.append(corners.at(i));
                Tri tri;
                tri.a = base;
                tri.b = base + 1;
                tri.c = base + 2;
                faces.triangles.append(tri);
                if (corners.size() >= 4)
                {
                    tri.a = base;
                    tri.b = base + 2;
                    tri.c = base + 3;
                    faces.triangles.append(tri);
                }
                model->dxfFaces += 1;
                noteLayer();
            }
        }
        poly.clear();
        polyClosed = false;
        clearEntity();
    };

    auto finishPoly = [&]() {
        if (!inPoly)
            return;
        if (poly.size() >= 2)
        {
            for (int i = 0; i < poly.size(); ++i)
                includePoint(poly.at(i), &model->dxfMin, &model->dxfMax, &model->hasDxfBounds);
            const int last = polyClosed ? poly.size() : poly.size() - 1;
            for (int i = 0; i < last; ++i)
                addLength(length(poly.at((i + 1) % poly.size()) - poly.at(i)));
            model->dxfPolylines += 1;
            noteLayer();
        }
        poly.clear();
        inPoly = false;
        polyClosed = false;
    };

    while (!in.atEnd())
    {
        const QString codeLine = in.readLine();
        if (codeLine.isNull())
            break;
        const QString valueLine = in.readLine();
        if (valueLine.isNull())
            break;
        bool codeOk = false;
        const int code = codeLine.trimmed().toInt(&codeOk);
        if (!codeOk && entity.isEmpty() && !inEntities)
            return fail(error, QString::fromLatin1("Binary DXF is not supported."));
        const QString value = valueLine.trimmed();
        if (code == 0)
        {
            if (value == QString::fromLatin1("VERTEX") && inPoly)
            {
                entity = value;
                clearEntity();
                continue;
            }
            if (value == QString::fromLatin1("SEQEND"))
            {
                finishPoly();
                entity.clear();
                continue;
            }
            if (inPoly && value != QString::fromLatin1("VERTEX"))
                finishPoly();
            if (inEntities)
                flush();
            entity = value;
            if (value == QString::fromLatin1("SECTION"))
                entity = value;
            if (value == QString::fromLatin1("ENDSEC"))
                inEntities = false;
            if (value == QString::fromLatin1("POLYLINE") && inEntities)
                inPoly = true;
            if (value == QString::fromLatin1("EOF"))
                break;
            continue;
        }
        if (entity == QString::fromLatin1("SECTION") && code == 2)
        {
            inEntities = (value.compare(QString::fromLatin1("ENTITIES"), Qt::CaseInsensitive) == 0);
            continue;
        }
        if (!inEntities)
            continue;
        if (entity == QString::fromLatin1("VERTEX"))
        {
            if (code == 10 || code == 20 || code == 30)
            {
                const int axis = code / 10;
                if (axis == 0)
                    poly.append(Vec3());
                if (!poly.isEmpty())
                {
                    Vec3 &point = poly.last();
                    const double number = value.toDouble();
                    if (axis == 0)
                        point.x = number;
                    else if (axis == 1)
                        point.y = number;
                    else
                        point.z = number;
                }
            }
            continue;
        }
        if (code == 8)
            layer = value;
        else if (code == 70 && (entity == QString::fromLatin1("LWPOLYLINE")
                                || entity == QString::fromLatin1("POLYLINE")))
            polyClosed = (value.toInt() & 1) != 0;
        else if (entity == QString::fromLatin1("LWPOLYLINE") && (code == 10 || code == 20))
        {
            if (code == 10)
                poly.append(Vec3(value.toDouble(), 0, 0));
            else if (!poly.isEmpty())
                poly.last().y = value.toDouble();
        }
        else if (code >= 10 && code <= 39)
        {
            const int slot = code % 10;
            const int axis = code / 10 - 1;
            if (slot >= 0 && slot <= 3 && axis >= 0 && axis <= 2)
            {
                const int index = slot * 3 + axis;
                num[index] = value.toDouble();
                has[index] = true;
            }
        }
        else if (code == 40 || code == 50 || code == 51)
        {
            const int index = (code == 40) ? 10 : (code == 50 ? 11 : 12);
            num[index] = value.toDouble();
            has[index] = true;
        }
    }
    if (inEntities)
        flush();
    finishPoly();

    if (!faces.triangles.isEmpty())
    {
        weldMesh(&faces);
        if (tooMany(faces, error))
            return false;
        Part part;
        part.name = QFileInfo(model->sourcePath).completeBaseName();
        part.sourceFile = model->sourcePath;
        part.mesh = faces;
        model->parts.append(part);
    }
    const bool any2d = model->dxfLines || model->dxfCircles || model->dxfArcs
                       || model->dxfPolylines;
    if (!any2d && faces.triangles.isEmpty())
        return fail(error, QString::fromLatin1(
                               "DXF has no LINE, CIRCLE, ARC, polyline, or 3DFACE entities."));
    model->format = QString::fromLatin1("dxf");
    return true;
}

//----------------------------------------------

bool loadModelAt(const QString &path, const QString &projectRoot, Model *model,
                 QString *error, int depth);

bool loadAssembly(const QString &path, const QByteArray &data, const QString &projectRoot,
                  Model *model, QString *error, int depth)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fail(error, QString::fromLatin1("Assembly JSON is invalid."));
    const QJsonObject root = doc.object();
    const QJsonValue partsValue = root.value(QString::fromLatin1("parts"));
    if (!partsValue.isArray() || partsValue.toArray().isEmpty())
        return fail(error, QString::fromLatin1("Assembly JSON needs a parts array."));
    const QString units = root.value(QString::fromLatin1("units")).toString().trimmed();
    if (!units.isEmpty())
        model->units = units;
    const QString beside = QFileInfo(path).absolutePath();
    const QJsonArray parts = partsValue.toArray();
    for (int i = 0; i < parts.size(); ++i)
    {
        if (!parts.at(i).isObject())
            return fail(error, QString::fromLatin1("Assembly part %1 is not an object.").arg(i + 1));
        const QJsonObject obj = parts.at(i).toObject();
        const QString file = obj.value(QString::fromLatin1("file")).toString().trimmed();
        if (file.isEmpty())
            return fail(error, QString::fromLatin1("Assembly part %1 has no file.").arg(i + 1));
        Placement place;
        if (!readVec3Value(obj.value(QString::fromLatin1("translate")), &place.translate, 0, error))
            return false;
        if (!readVec3Value(obj.value(QString::fromLatin1("rotate_deg")), &place.rotateDeg, 0, error))
            return false;
        if (!readScaleValue(obj.value(QString::fromLatin1("scale")), &place.scale, error))
            return false;
        const QString resolved = resolveInputPath(file, projectRoot, beside);
        Model child;
        if (!loadModelAt(resolved, projectRoot, &child, error, depth + 1))
            return false;
        if (child.triangleCount() == 0)
            return fail(error, QString::fromLatin1("Assembly part has no triangles: %1").arg(file));
        Part part;
        part.name = obj.value(QString::fromLatin1("name")).toString().trimmed();
        if (part.name.isEmpty())
            part.name = QFileInfo(file).completeBaseName();
        part.sourceFile = file;
        part.mesh = child.combined();
        transformVertices(&part.mesh.vertices, place, Vec3());
        model->parts.append(part);
    }
    model->format = QString::fromLatin1("assembly");
    return true;
}

bool loadModelAt(const QString &path, const QString &projectRoot, Model *model,
                 QString *error, int depth)
{
    if (!model)
        return fail(error, QString::fromLatin1("No model."));
    if (depth > kMaxAssemblyDepth)
        return fail(error, QString::fromLatin1("Assembly nesting is too deep."));
    const QString suffix = suffixOf(path);
    if (suffix == QString::fromLatin1("step") || suffix == QString::fromLatin1("stp")
        || suffix == QString::fromLatin1("iges") || suffix == QString::fromLatin1("igs"))
    {
        return fail(error, QString::fromLatin1(
                               "STEP and IGES are not read. Save the model as STL, OBJ, or DXF."));
    }
    QByteArray data;
    if (!readAll(path, &data, error))
        return false;
    *model = Model();
    model->sourcePath = path;
    model->units = QString::fromLatin1("mm");

    if (suffix == QString::fromLatin1("stl"))
    {
        Part part;
        part.name = QFileInfo(path).completeBaseName();
        part.sourceFile = path;
        if (!loadStl(data, &part.mesh, error))
            return false;
        model->parts.append(part);
        model->format = QString::fromLatin1("stl");
        return true;
    }
    if (suffix == QString::fromLatin1("obj"))
    {
        if (!loadObj(data, &model->parts, error))
            return false;
        for (int i = 0; i < model->parts.size(); ++i)
        {
            if (model->parts[i].name.isEmpty())
                model->parts[i].name = QFileInfo(path).completeBaseName();
            model->parts[i].sourceFile = path;
        }
        model->format = QString::fromLatin1("obj");
        return true;
    }
    if (suffix == QString::fromLatin1("json"))
        return loadAssembly(path, data, projectRoot, model, error, depth);
    if (suffix == QString::fromLatin1("dxf"))
        return loadDxf(data, model, error);
    return fail(error, QString::fromLatin1(
                           "Unsupported file type. Use .stl, .obj, .dxf, or an assembly .json."));
}

//----------------------------------------------

bool writeStl(const QString &path, const Mesh &mesh, bool binary, QString *error)
{
    if (!ensureParentDir(path, error))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(error, QString::fromLatin1("Cannot write %1").arg(path));
    const QString solid = QFileInfo(path).completeBaseName();
    if (!binary)
    {
        QTextStream out(&file);
        out.setLocale(QLocale::c());
        out.setCodec("UTF-8");
        out << "solid " << solid << "\n";
        for (int i = 0; i < mesh.triangles.size(); ++i)
        {
            const Tri &tri = mesh.triangles.at(i);
            const Vec3 a = mesh.vertices.at(tri.a);
            const Vec3 b = mesh.vertices.at(tri.b);
            const Vec3 c = mesh.vertices.at(tri.c);
            const Vec3 n = normalized(cross(b - a, c - a));
            out << "  facet normal " << n.x << " " << n.y << " " << n.z << "\n";
            out << "    outer loop\n";
            out << "      vertex " << a.x << " " << a.y << " " << a.z << "\n";
            out << "      vertex " << b.x << " " << b.y << " " << b.z << "\n";
            out << "      vertex " << c.x << " " << c.y << " " << c.z << "\n";
            out << "    endloop\n";
            out << "  endfacet\n";
        }
        out << "endsolid " << solid << "\n";
        return true;
    }

    QByteArray header(80, '\0');
    const QByteArray title = solid.toUtf8().left(70);
    memcpy(header.data(), title.constData(), size_t(title.size()));
    file.write(header);
    writeU32le(&file, quint32(mesh.triangles.size()));
    for (int i = 0; i < mesh.triangles.size(); ++i)
    {
        const Tri &tri = mesh.triangles.at(i);
        const Vec3 a = mesh.vertices.at(tri.a);
        const Vec3 b = mesh.vertices.at(tri.b);
        const Vec3 c = mesh.vertices.at(tri.c);
        const Vec3 n = normalized(cross(b - a, c - a));
        writeF32le(&file, float(n.x));
        writeF32le(&file, float(n.y));
        writeF32le(&file, float(n.z));
        const Vec3 v[3] = { a, b, c };
        for (int k = 0; k < 3; ++k)
        {
            writeF32le(&file, float(v[k].x));
            writeF32le(&file, float(v[k].y));
            writeF32le(&file, float(v[k].z));
        }
        writeU16le(&file, 0);
    }
    return true;
}

bool writeObj(const QString &path, const QList<Part> &parts, QString *error)
{
    if (!ensureParentDir(path, error))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(error, QString::fromLatin1("Cannot write %1").arg(path));
    QTextStream out(&file);
    out.setLocale(QLocale::c());
    out.setCodec("UTF-8");
    out << "# Unique CAD\n";
    int base = 1;
    for (int p = 0; p < parts.size(); ++p)
    {
        const Part &part = parts.at(p);
        QString name = part.name.trimmed();
        if (name.isEmpty())
            name = QString::fromLatin1("part_%1").arg(p + 1);
        name.replace(QLatin1Char('\n'), QLatin1Char(' '));
        out << "o " << name << "\n";
        for (int i = 0; i < part.mesh.vertices.size(); ++i)
        {
            const Vec3 &v = part.mesh.vertices.at(i);
            out << "v " << v.x << " " << v.y << " " << v.z << "\n";
        }
        for (int i = 0; i < part.mesh.triangles.size(); ++i)
        {
            const Tri &tri = part.mesh.triangles.at(i);
            out << "f " << (base + tri.a) << " " << (base + tri.b) << " "
                << (base + tri.c) << "\n";
        }
        base += part.mesh.vertices.size();
    }
    return true;
}

Vec3 rotateXYZ(Vec3 v, Vec3 degrees)
{
    const double rx = degrees.x * std::acos(-1.0) / 180.0;
    const double ry = degrees.y * std::acos(-1.0) / 180.0;
    const double rz = degrees.z * std::acos(-1.0) / 180.0;
    {
        const double c = std::cos(rx);
        const double s = std::sin(rx);
        v = Vec3(v.x, c * v.y - s * v.z, s * v.y + c * v.z);
    }
    {
        const double c = std::cos(ry);
        const double s = std::sin(ry);
        v = Vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
    }
    {
        const double c = std::cos(rz);
        const double s = std::sin(rz);
        v = Vec3(c * v.x - s * v.y, s * v.x + c * v.y, v.z);
    }
    return v;
}

bool nearPoint(Vec3 a, Vec3 b, double tol)
{
    return length(a - b) <= tol;
}

//----------------------------------------------

QString cadNum(double value)
{
    QString text = QLocale::c().toString(value, 'f', 6);
    if (text.contains(QLatin1Char('.')))
    {
        while (text.endsWith(QLatin1Char('0')))
            text.chop(1);
        if (text.endsWith(QLatin1Char('.')))
            text.chop(1);
    }
    if (text == QString::fromLatin1("-0") || text.isEmpty())
        text = QString::fromLatin1("0");
    return text;
}

QString cadVec(Vec3 value)
{
    return cadNum(value.x) + QLatin1Char(' ') + cadNum(value.y) + QLatin1Char(' ')
           + cadNum(value.z);
}

//----------------------------------------------

QString resolveInputPath(const QString &path, const QString &projectRoot,
                         const QString &besideDir)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return trimmed;
    const QFileInfo direct(trimmed);
    if (direct.isAbsolute())
        return QDir::cleanPath(direct.absoluteFilePath());
    if (!besideDir.isEmpty())
    {
        const QString beside = QDir::cleanPath(QDir(besideDir).absoluteFilePath(trimmed));
        if (QFileInfo(beside).exists())
            return beside;
    }
    if (!projectRoot.isEmpty())
        return QDir::cleanPath(QDir(projectRoot).absoluteFilePath(trimmed));
    return QDir::cleanPath(direct.absoluteFilePath());
}

QString resolveOutputPath(const QString &path, const QString &projectRoot)
{
    const QString trimmed = path.trimmed();
    const QFileInfo info(trimmed);
    if (info.isAbsolute() || projectRoot.trimmed().isEmpty())
        return QDir::cleanPath(info.absoluteFilePath());
    return QDir::cleanPath(QDir(projectRoot).absoluteFilePath(trimmed));
}

//----------------------------------------------

int Model::triangleCount() const
{
    int count = 0;
    for (int i = 0; i < parts.size(); ++i)
        count += parts.at(i).mesh.triangles.size();
    return count;
}

int Model::vertexCount() const
{
    int count = 0;
    for (int i = 0; i < parts.size(); ++i)
        count += parts.at(i).mesh.vertices.size();
    return count;
}

Mesh Model::combined() const
{
    Mesh out;
    for (int p = 0; p < parts.size(); ++p)
    {
        const Mesh &mesh = parts.at(p).mesh;
        const int base = out.vertices.size();
        out.vertices += mesh.vertices;
        for (int i = 0; i < mesh.triangles.size(); ++i)
        {
            Tri tri = mesh.triangles.at(i);
            tri.a += base;
            tri.b += base;
            tri.c += base;
            out.triangles.append(tri);
        }
    }
    return out;
}

//----------------------------------------------

bool loadModel(const QString &path, const QString &projectRoot, Model *model, QString *error)
{
    return loadModelAt(resolveInputPath(path, projectRoot, QString()), projectRoot,
                       model, error, 0);
}

//----------------------------------------------

bool saveParts(const QString &path, const QList<Part> &parts, bool binaryStl, QString *error)
{
    if (parts.isEmpty())
        return fail(error, QString::fromLatin1("Nothing to write."));
    const QString suffix = suffixOf(path);
    if (suffix == QString::fromLatin1("obj"))
        return writeObj(path, parts, error);
    if (suffix == QString::fromLatin1("stl"))
    {
        Mesh mesh;
        for (int p = 0; p < parts.size(); ++p)
        {
            const int base = mesh.vertices.size();
            mesh.vertices += parts.at(p).mesh.vertices;
            for (int i = 0; i < parts.at(p).mesh.triangles.size(); ++i)
            {
                Tri tri = parts.at(p).mesh.triangles.at(i);
                tri.a += base;
                tri.b += base;
                tri.c += base;
                mesh.triangles.append(tri);
            }
        }
        return writeStl(path, mesh, binaryStl, error);
    }
    return fail(error, QString::fromLatin1("Output must be .stl or .obj."));
}

//----------------------------------------------

bool writeAssemblyJson(const QString &path, const QString &units,
                       const QList<AssemblyPartSpec> &parts, QString *error)
{
    if (parts.isEmpty())
        return fail(error, QString::fromLatin1("Assembly has no parts."));
    if (!ensureParentDir(path, error))
        return false;
    QJsonArray array;
    for (int i = 0; i < parts.size(); ++i)
    {
        const AssemblyPartSpec &part = parts.at(i);
        QJsonObject obj;
        if (!part.name.trimmed().isEmpty())
            obj.insert(QString::fromLatin1("name"), part.name.trimmed());
        obj.insert(QString::fromLatin1("file"), part.file);
        QJsonArray translate;
        translate.append(part.place.translate.x);
        translate.append(part.place.translate.y);
        translate.append(part.place.translate.z);
        QJsonArray rotate;
        rotate.append(part.place.rotateDeg.x);
        rotate.append(part.place.rotateDeg.y);
        rotate.append(part.place.rotateDeg.z);
        obj.insert(QString::fromLatin1("translate"), translate);
        obj.insert(QString::fromLatin1("rotate_deg"), rotate);
        const bool uniform = part.place.scale.x == part.place.scale.y
                             && part.place.scale.y == part.place.scale.z;
        if (uniform)
            obj.insert(QString::fromLatin1("scale"), part.place.scale.x);
        else
        {
            QJsonArray scale;
            scale.append(part.place.scale.x);
            scale.append(part.place.scale.y);
            scale.append(part.place.scale.z);
            obj.insert(QString::fromLatin1("scale"), scale);
        }
        array.append(obj);
    }
    QJsonObject root;
    root.insert(QString::fromLatin1("units"),
                units.trimmed().isEmpty() ? QString::fromLatin1("mm") : units.trimmed());
    root.insert(QString::fromLatin1("parts"), array);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(error, QString::fromLatin1("Cannot write %1").arg(path));
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

//----------------------------------------------

MeshStats measureMesh(const Mesh &mesh)
{
    MeshStats stats;
    boundsOf(mesh.vertices, &stats.minimum, &stats.maximum, &stats.any);
    QHash<quint64, int> edges;
    double signedVolume = 0;
    for (int i = 0; i < mesh.triangles.size(); ++i)
    {
        const Tri &tri = mesh.triangles.at(i);
        if (tri.a < 0 || tri.b < 0 || tri.c < 0
            || tri.a >= mesh.vertices.size() || tri.b >= mesh.vertices.size()
            || tri.c >= mesh.vertices.size())
            continue;
        const Vec3 a = mesh.vertices.at(tri.a);
        const Vec3 b = mesh.vertices.at(tri.b);
        const Vec3 c = mesh.vertices.at(tri.c);
        stats.area += length(cross(b - a, c - a)) * 0.5;
        signedVolume += dot(a, cross(b, c));
        const int id[3] = { tri.a, tri.b, tri.c };
        for (int e = 0; e < 3; ++e)
        {
            const int u = id[e];
            const int v = id[(e + 1) % 3];
            if (u == v)
                continue;
            const quint64 key = edgeKey(u, v);
            edges[key] = edges.value(key) + 1;
        }
    }
    signedVolume /= 6.0;
    stats.flipped = signedVolume < 0;
    stats.volume = std::fabs(signedVolume);
    QHash<quint64, int>::const_iterator it = edges.constBegin();
    for (; it != edges.constEnd(); ++it)
    {
        if (it.value() == 1)
            stats.openEdges += 1;
        else if (it.value() > 2)
            stats.nonManifold += 1;
    }
    stats.watertight = mesh.triangles.size() > 0 && stats.openEdges == 0
                       && stats.nonManifold == 0;
    return stats;
}

//----------------------------------------------

void transformVertices(QVector<Vec3> *vertices, const Placement &place, Vec3 center)
{
    if (!vertices)
        return;
    for (int i = 0; i < vertices->size(); ++i)
    {
        Vec3 point = vertices->at(i);
        if (place.aroundBbox)
            point = point - center;
        point = Vec3(point.x * place.scale.x, point.y * place.scale.y, point.z * place.scale.z);
        point = rotateXYZ(point, place.rotateDeg);
        if (place.aroundBbox)
            point = point + center;
        vertices->replace(i, point + place.translate);
    }
}

//----------------------------------------------

void modelBounds(const Model &model, Vec3 *minimum, Vec3 *maximum, bool *any)
{
    QVector<Vec3> all;
    for (int i = 0; i < model.parts.size(); ++i)
        all += model.parts.at(i).mesh.vertices;
    if (model.hasDxfBounds)
    {
        all.append(model.dxfMin);
        all.append(model.dxfMax);
    }
    boundsOf(all, minimum, maximum, any);
}

//----------------------------------------------

bool sectionMesh(const Mesh &mesh, Vec3 point, Vec3 normal, QList<SectionLoop> *loops,
                 QString *error)
{
    if (!loops)
        return fail(error, QString::fromLatin1("No section result."));
    loops->clear();
    const Vec3 n = normalized(normal);
    if (length(n) < 0.5)
        return fail(error, QString::fromLatin1("normal must not be zero."));
    if (mesh.triangles.isEmpty())
        return fail(error, QString::fromLatin1("Mesh has no triangles."));
    Vec3 lo;
    Vec3 hi;
    bool any = false;
    boundsOf(mesh.vertices, &lo, &hi, &any);
    const double diag = any ? length(hi - lo) : 1.0;
    const double eps = qMax(1e-9, diag * 1e-9);
    const double join = qMax(1e-7, diag * 1e-7);

    struct Seg
    {
        Vec3 a;
        Vec3 b;
    };
    QVector<Seg> segs;
    for (int i = 0; i < mesh.triangles.size(); ++i)
    {
        const Tri &tri = mesh.triangles.at(i);
        const Vec3 p[3] = {
            mesh.vertices.at(tri.a),
            mesh.vertices.at(tri.b),
            mesh.vertices.at(tri.c)
        };
        double dist[3];
        int onPlane = 0;
        for (int k = 0; k < 3; ++k)
        {
            dist[k] = dot(p[k] - point, n);
            if (std::fabs(dist[k]) <= eps)
                onPlane += 1;
        }
        QVector<Vec3> hits;
        if (onPlane == 3)
        {
            hits.append(p[0]);
            hits.append(p[1]);
            hits.append(p[2]);
        }
        else
        {
            for (int k = 0; k < 3; ++k)
            {
                const int j = (k + 1) % 3;
                const bool zk = std::fabs(dist[k]) <= eps;
                const bool zj = std::fabs(dist[j]) <= eps;
                if (zk && zj)
                {
                    hits.append(p[k]);
                    hits.append(p[j]);
                }
                else if (zk)
                    hits.append(p[k]);
                else if ((dist[k] > eps && dist[j] < -eps) || (dist[k] < -eps && dist[j] > eps))
                {
                    const double t = dist[k] / (dist[k] - dist[j]);
                    hits.append(p[k] + (p[j] - p[k]) * t);
                }
            }
        }
        QVector<Vec3> unique;
        for (int h = 0; h < hits.size(); ++h)
        {
            bool seen = false;
            for (int u = 0; u < unique.size(); ++u)
            {
                if (nearPoint(unique.at(u), hits.at(h), join))
                {
                    seen = true;
                    break;
                }
            }
            if (!seen)
                unique.append(hits.at(h));
        }
        if (onPlane == 3 && unique.size() >= 3)
        {
            segs.append(Seg{ unique.at(0), unique.at(1) });
            segs.append(Seg{ unique.at(1), unique.at(2) });
            segs.append(Seg{ unique.at(2), unique.at(0) });
        }
        else if (unique.size() >= 2)
            segs.append(Seg{ unique.at(0), unique.at(1) });
    }
    if (segs.isEmpty())
        return fail(error, QString::fromLatin1("The plane does not cut the mesh."));

    QVector<bool> used(segs.size(), false);
    for (int i = 0; i < segs.size(); ++i)
    {
        if (used.at(i))
            continue;
        used[i] = true;
        QList<Vec3> poly;
        poly.append(segs.at(i).a);
        poly.append(segs.at(i).b);
        bool grew = true;
        while (grew)
        {
            grew = false;
            const Vec3 tail = poly.last();
            for (int j = 0; j < segs.size(); ++j)
            {
                if (used.at(j))
                    continue;
                if (nearPoint(segs.at(j).a, tail, join))
                {
                    poly.append(segs.at(j).b);
                    used[j] = true;
                    grew = true;
                    break;
                }
                if (nearPoint(segs.at(j).b, tail, join))
                {
                    poly.append(segs.at(j).a);
                    used[j] = true;
                    grew = true;
                    break;
                }
            }
        }
        grew = true;
        while (grew)
        {
            grew = false;
            const Vec3 head = poly.first();
            for (int j = 0; j < segs.size(); ++j)
            {
                if (used.at(j))
                    continue;
                if (nearPoint(segs.at(j).b, head, join))
                {
                    poly.prepend(segs.at(j).a);
                    used[j] = true;
                    grew = true;
                    break;
                }
                if (nearPoint(segs.at(j).a, head, join))
                {
                    poly.prepend(segs.at(j).b);
                    used[j] = true;
                    grew = true;
                    break;
                }
            }
        }
        SectionLoop loop;
        loop.points = poly;
        loop.closed = poly.size() >= 4 && nearPoint(poly.first(), poly.last(), join);
        if (loop.closed)
            loop.points.removeLast();
        loops->append(loop);
    }
    return true;
}
