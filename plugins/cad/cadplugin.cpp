#include "caddraw.h"
#include "toolplugin.h"
//----------------------------------------------
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegExp>
//----------------------------------------------
#include <cstring>

//----------------------------------------------

#if defined(_WIN32)
#  define CAD_API __declspec(dllexport)
#else
#  define CAD_API __attribute__((visibility("default")))
#endif

//----------------------------------------------

struct Call
{
    QJsonObject obj;
    QString     root;
    QString     units;
};

int reply(char *output, int capacity, int *ok, bool success, const QString &text)
{
    const QByteArray bytes = text.toUtf8();
    if (!output || bytes.size() + 1 > capacity)
        return bytes.size() + 1;
    memcpy(output, bytes.constData(), size_t(bytes.size()));
    output[bytes.size()] = 0;
    if (ok)
        *ok = success ? 1 : 0;
    return 0;
}

int failCall(char *output, int capacity, int *ok, const QString &text)
{
    return reply(output, capacity, ok, false, text);
}

bool parseCall(const char *json, Call *call, QString *error)
{
    QJsonParseError parseError;
    const QByteArray raw(json ? json : "");
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        *error = QString::fromLatin1("Arguments must be a JSON object.");
        return false;
    }
    call->obj = doc.object();
    call->root = call->obj.value(QString::fromLatin1("project_root")).toString();
    call->units = call->obj.value(QString::fromLatin1("units")).toString().trimmed();
    if (call->units.isEmpty())
        call->units = QString::fromLatin1("mm");
    return true;
}

bool readNumber(const QJsonValue &value, double *out)
{
    if (value.isDouble())
    {
        *out = value.toDouble();
        return true;
    }
    if (value.isString())
    {
        bool ok = false;
        *out = QLocale::c().toDouble(value.toString().trimmed(), &ok);
        return ok;
    }
    return false;
}

bool readVec3(const QJsonValue &value, Vec3 *out, bool *present, QString *error)
{
    if (present)
        *present = false;
    if (value.isUndefined() || value.isNull())
        return true;
    if (!value.isArray() || value.toArray().size() != 3)
    {
        *error = QString::fromLatin1("Expected three numbers [x, y, z].");
        return false;
    }
    const QJsonArray array = value.toArray();
    double xyz[3];
    for (int i = 0; i < 3; ++i)
    {
        if (!readNumber(array.at(i), &xyz[i]))
        {
            *error = QString::fromLatin1("Expected three numbers [x, y, z].");
            return false;
        }
    }
    *out = Vec3(xyz[0], xyz[1], xyz[2]);
    if (present)
        *present = true;
    return true;
}

bool readScale(const QJsonValue &value, Vec3 *out, QString *error)
{
    if (value.isUndefined() || value.isNull())
    {
        *out = Vec3(1, 1, 1);
        return true;
    }
    if (value.isDouble() || value.isString())
    {
        double number = 1;
        if (!readNumber(value, &number))
        {
            *error = QString::fromLatin1("scale must be a number or [x, y, z].");
            return false;
        }
        *out = Vec3(number, number, number);
        return true;
    }
    bool present = false;
    if (!readVec3(value, out, &present, error))
        return false;
    if (!present)
        *out = Vec3(1, 1, 1);
    return true;
}

bool readPlacement(const QJsonObject &obj, Placement *place, QString *error)
{
    *place = Placement();
    if (!readVec3(obj.value(QString::fromLatin1("translate")), &place->translate, 0, error))
        return false;
    if (!readVec3(obj.value(QString::fromLatin1("rotate_deg")), &place->rotateDeg, 0, error))
        return false;
    if (!readScale(obj.value(QString::fromLatin1("scale")), &place->scale, error))
        return false;
    const QString center = obj.value(QString::fromLatin1("center")).toString().trimmed().toLower();
    if (center.isEmpty() || center == QString::fromLatin1("origin"))
        place->aroundBbox = false;
    else if (center == QString::fromLatin1("bbox"))
        place->aroundBbox = true;
    else
    {
        *error = QString::fromLatin1("center must be origin or bbox.");
        return false;
    }
    return true;
}

bool flag(const Call &call, const char *key, bool fallback)
{
    const QJsonValue value = call.obj.value(QString::fromLatin1(key));
    if (value.isBool())
        return value.toBool();
    if (value.isDouble())
        return value.toDouble() != 0;
    if (!value.isString())
        return fallback;
    const QString text = value.toString().trimmed().toLower();
    if (text == QString::fromLatin1("true") || text == QString::fromLatin1("1")
        || text == QString::fromLatin1("yes"))
        return true;
    if (text == QString::fromLatin1("false") || text == QString::fromLatin1("0")
        || text == QString::fromLatin1("no"))
        return false;
    return fallback;
}

QStringList textList(const Call &call, const char *key)
{
    QStringList items;
    const QJsonValue value = call.obj.value(QString::fromLatin1(key));
    if (value.isArray())
    {
        const QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i)
        {
            const QString text = array.at(i).toString().trimmed();
            if (!text.isEmpty())
                items.append(text);
        }
    }
    else if (value.isString())
    {
        items = value.toString().split(QRegExp(QString::fromLatin1("[,\\s]+")),
                                       QString::SkipEmptyParts);
    }
    return items;
}

QString argText(const Call &call, const char *key)
{
    return call.obj.value(QString::fromLatin1(key)).toString().trimmed();
}

bool loadAt(const Call &call, const QString &path, Model *model, QString *error)
{
    const QString resolved = resolveInputPath(path, call.root, QString());
    if (!loadModel(resolved, call.root, model, error))
        return false;
    if (model->format != QString::fromLatin1("assembly"))
        model->units = call.units;
    else if (model->units.trimmed().isEmpty())
        model->units = call.units;
    return true;
}

QString reportModel(const Model &model)
{
    QStringList lines;
    lines << QString::fromLatin1("ok format=%1 parts=%2 triangles=%3 vertices=%4")
                 .arg(model.format)
                 .arg(model.parts.size())
                 .arg(model.triangleCount())
                 .arg(model.vertexCount());
    lines << QString::fromLatin1("units=%1").arg(model.units);
    const bool drawing = model.dxfLines || model.dxfCircles || model.dxfArcs
                         || model.dxfPolylines || model.dxfFaces;
    if (drawing)
    {
        lines << QString::fromLatin1(
                     "dxf lines=%1 circles=%2 arcs=%3 polylines=%4 faces=%5 length=%6")
                     .arg(model.dxfLines)
                     .arg(model.dxfCircles)
                     .arg(model.dxfArcs)
                     .arg(model.dxfPolylines)
                     .arg(model.dxfFaces)
                     .arg(cadNum(model.dxfLength));
        if (!model.layers.isEmpty())
            lines << QString::fromLatin1("layers=%1").arg(model.layers.join(QLatin1Char(',')));
    }
    Vec3 minimum;
    Vec3 maximum;
    bool any = false;
    modelBounds(model, &minimum, &maximum, &any);
    if (any)
    {
        const Vec3 size = maximum - minimum;
        lines << QString::fromLatin1("bbox_min=%1").arg(cadVec(minimum));
        lines << QString::fromLatin1("bbox_max=%1").arg(cadVec(maximum));
        lines << QString::fromLatin1("size=%1").arg(cadVec(size));
        lines << QString::fromLatin1("diagonal=%1").arg(cadNum(length(size)));
    }
    if (model.triangleCount() > 0)
    {
        double area = 0;
        double volume = 0;
        int openEdges = 0;
        int nonManifold = 0;
        bool watertight = true;
        const int limit = qMin(model.parts.size(), 40);
        for (int i = 0; i < model.parts.size(); ++i)
        {
            const MeshStats stats = measureMesh(model.parts.at(i).mesh);
            area += stats.area;
            volume += stats.volume;
            openEdges += stats.openEdges;
            nonManifold += stats.nonManifold;
            watertight = watertight && stats.watertight;
            if (i < limit)
            {
                const Vec3 size = stats.any ? (stats.maximum - stats.minimum) : Vec3();
                lines << QString::fromLatin1(
                             "part %1 name=%2 triangles=%3 size=%4 watertight=%5")
                             .arg(i + 1)
                             .arg(model.parts.at(i).name)
                             .arg(model.parts.at(i).mesh.triangles.size())
                             .arg(cadVec(size))
                             .arg(stats.watertight ? QString::fromLatin1("yes")
                                                  : QString::fromLatin1("no"));
            }
        }
        if (model.parts.size() > limit)
            lines << QString::fromLatin1("parts_omitted=%1").arg(model.parts.size() - limit);
        lines << QString::fromLatin1("area=%1 volume=%2 watertight=%3 open_edges=%4 nonmanifold=%5")
                     .arg(cadNum(area))
                     .arg(cadNum(volume))
                     .arg(watertight ? QString::fromLatin1("yes") : QString::fromLatin1("no"))
                     .arg(openEdges)
                     .arg(nonManifold);
        if (!watertight)
            lines << QString::fromLatin1("volume_is_approximate=yes");
    }
    return lines.join(QLatin1Char('\n'));
}

bool readPartSpecs(const QJsonValue &value, QList<AssemblyPartSpec> *parts, QString *error)
{
    if (!value.isArray())
    {
        *error = QString::fromLatin1("parts must be an array.");
        return false;
    }
    const QJsonArray array = value.toArray();
    if (array.isEmpty())
    {
        *error = QString::fromLatin1("parts is empty.");
        return false;
    }
    for (int i = 0; i < array.size(); ++i)
    {
        if (!array.at(i).isObject())
        {
            *error = QString::fromLatin1("Assembly part %1 is not an object.").arg(i + 1);
            return false;
        }
        const QJsonObject obj = array.at(i).toObject();
        AssemblyPartSpec spec;
        spec.file = obj.value(QString::fromLatin1("file")).toString().trimmed();
        spec.name = obj.value(QString::fromLatin1("name")).toString().trimmed();
        if (spec.file.isEmpty())
        {
            *error = QString::fromLatin1("Assembly part %1 has no file.").arg(i + 1);
            return false;
        }
        if (!readPlacement(obj, &spec.place, error))
            return false;
        spec.place.aroundBbox = false;
        parts->append(spec);
    }
    return true;
}

bool bakeSpecs(const Call &call, const QList<AssemblyPartSpec> &specs, const QString &beside,
               Model *model, QString *error)
{
    model->format = QString::fromLatin1("assembly");
    model->units = call.units;
    for (int i = 0; i < specs.size(); ++i)
    {
        const QString resolved = resolveInputPath(specs.at(i).file, call.root, beside);
        Model child;
        if (!loadModel(resolved, call.root, &child, error))
            return false;
        if (child.triangleCount() == 0)
        {
            *error = QString::fromLatin1("Assembly part has no triangles: %1")
                         .arg(specs.at(i).file);
            return false;
        }
        Part part;
        part.name = specs.at(i).name.trimmed().isEmpty()
                        ? QFileInfo(specs.at(i).file).completeBaseName()
                        : specs.at(i).name.trimmed();
        part.sourceFile = specs.at(i).file;
        part.mesh = child.combined();
        transformVertices(&part.mesh.vertices, specs.at(i).place, Vec3());
        model->parts.append(part);
    }
    return true;
}

QString requirePath(const Call &call, const char *key, QString *error)
{
    const QString path = argText(call, key);
    if (path.isEmpty())
        *error = QString::fromLatin1("%1 is required.").arg(QString::fromLatin1(key));
    return path;
}

//----------------------------------------------

int invokeInspect(const char *argumentsJson, char *output, int outputCapacity, int *ok)
{
    Call call;
    QString error;
    if (!parseCall(argumentsJson, &call, &error))
        return failCall(output, outputCapacity, ok, error);
    const QString path = requirePath(call, "path", &error);
    if (path.isEmpty())
        return failCall(output, outputCapacity, ok, error);
    Model model;
    if (!loadAt(call, path, &model, &error))
        return failCall(output, outputCapacity, ok, error);
    return reply(output, outputCapacity, ok, true, reportModel(model));
}

int invokeMeasure(const char *argumentsJson, char *output, int outputCapacity, int *ok)
{
    Call call;
    QString error;
    if (!parseCall(argumentsJson, &call, &error))
        return failCall(output, outputCapacity, ok, error);
    Vec3 a;
    Vec3 b;
    bool hasA = false;
    bool hasB = false;
    if (!readVec3(call.obj.value(QString::fromLatin1("a")), &a, &hasA, &error))
        return failCall(output, outputCapacity, ok, error);
    if (!readVec3(call.obj.value(QString::fromLatin1("b")), &b, &hasB, &error))
        return failCall(output, outputCapacity, ok, error);
    const QString path = argText(call, "path");
    if (path.isEmpty() && !(hasA && hasB))
        return failCall(output, outputCapacity, ok,
                        QString::fromLatin1("Give path, or both points a and b."));
    QStringList lines;
    lines << QString::fromLatin1("ok");
    if (!path.isEmpty())
    {
        Model model;
        if (!loadAt(call, path, &model, &error))
            return failCall(output, outputCapacity, ok, error);
        lines << reportModel(model);
        if (model.dxfLength > 0)
            lines << QString::fromLatin1("curve_length=%1").arg(cadNum(model.dxfLength));
    }
    if (hasA && hasB)
    {
        lines << QString::fromLatin1("distance=%1").arg(cadNum(length(b - a)));
        lines << QString::fromLatin1("delta=%1").arg(cadVec(b - a));
    }
    return reply(output, outputCapacity, ok, true, lines.join(QLatin1Char('\n')));
}

int invokeTransform(const char *argumentsJson, char *output, int outputCapacity, int *ok)
{
    Call call;
    QString error;
    if (!parseCall(argumentsJson, &call, &error))
        return failCall(output, outputCapacity, ok, error);
    const QString path = requirePath(call, "path", &error);
    if (path.isEmpty())
        return failCall(output, outputCapacity, ok, error);
    const QString outPath = requirePath(call, "output", &error);
    if (outPath.isEmpty())
        return failCall(output, outputCapacity, ok, error);
    Placement place;
    if (!readPlacement(call.obj, &place, &error))
        return failCall(output, outputCapacity, ok, error);
    Model model;
    if (!loadAt(call, path, &model, &error))
        return failCall(output, outputCapacity, ok, error);
    if (model.triangleCount() == 0)
        return failCall(output, outputCapacity, ok, QString::fromLatin1("Mesh has no triangles."));
    Vec3 minimum;
    Vec3 maximum;
    bool any = false;
    modelBounds(model, &minimum, &maximum, &any);
    const Vec3 center((minimum.x + maximum.x) * 0.5,
                      (minimum.y + maximum.y) * 0.5,
                      (minimum.z + maximum.z) * 0.5);
    for (int i = 0; i < model.parts.size(); ++i)
        transformVertices(&model.parts[i].mesh.vertices, place, center);
    const QString dest = resolveOutputPath(outPath, call.root);
    if (!saveParts(dest, model.parts, flag(call, "binary", true), &error))
        return failCall(output, outputCapacity, ok, error);
    modelBounds(model, &minimum, &maximum, &any);
    return reply(output, outputCapacity, ok, true,
                 QString::fromLatin1("ok output=%1 triangles=%2 bbox_min=%3 bbox_max=%4")
                     .arg(dest)
                     .arg(model.triangleCount())
                     .arg(cadVec(minimum))
                     .arg(cadVec(maximum)));
}

int invokeConvert(const char *argumentsJson, char *output, int outputCapacity, int *ok)
{
    Call call;
    QString error;
    if (!parseCall(argumentsJson, &call, &error))
        return failCall(output, outputCapacity, ok, error);
    const QString path = requirePath(call, "path", &error);
    if (path.isEmpty())
        return failCall(output, outputCapacity, ok, error);
    const QString outPath = requirePath(call, "output", &error);
    if (outPath.isEmpty())
        return failCall(output, outputCapacity, ok, error);
    Model model;
    if (!loadAt(call, path, &model, &error))
        return failCall(output, outputCapacity, ok, error);
    if (model.triangleCount() == 0)
        return failCall(output, outputCapacity, ok, QString::fromLatin1("Mesh has no triangles."));
    const QString dest = resolveOutputPath(outPath, call.root);
    if (!saveParts(dest, model.parts, flag(call, "binary", true), &error))
        return failCall(output, outputCapacity, ok, error);
    return reply(output, outputCapacity, ok, true,
                 QString::fromLatin1("ok output=%1 format=%2 triangles=%3")
                     .arg(dest, QFileInfo(dest).suffix().toLower())
                     .arg(model.triangleCount()));
}

int invokeAssembly(const char *argumentsJson, char *output, int outputCapacity, int *ok)
{
    Call call;
    QString error;
    if (!parseCall(argumentsJson, &call, &error))
        return failCall(output, outputCapacity, ok, error);
    const QString path = argText(call, "path");
    const QString outPath = argText(call, "output");
    QList<AssemblyPartSpec> specs;
    const bool hasParts = call.obj.contains(QString::fromLatin1("parts"));
    if (hasParts && !readPartSpecs(call.obj.value(QString::fromLatin1("parts")), &specs, &error))
        return failCall(output, outputCapacity, ok, error);
    if (!hasParts && path.isEmpty())
        return failCall(output, outputCapacity, ok,
                        QString::fromLatin1("Give path or parts."));

    const QString dest = outPath.isEmpty() ? QString() : resolveOutputPath(outPath, call.root);
    const QString destSuffix = QFileInfo(dest).suffix().toLower();
    if (hasParts && destSuffix == QString::fromLatin1("json"))
    {
        for (int i = 0; i < specs.size(); ++i)
        {
            const QString resolved = resolveInputPath(specs.at(i).file, call.root, QString());
            if (!QFileInfo(resolved).isFile())
                return failCall(output, outputCapacity, ok,
                                QString::fromLatin1("File not found: %1").arg(specs.at(i).file));
        }
        if (!writeAssemblyJson(dest, call.units, specs, &error))
            return failCall(output, outputCapacity, ok, error);
        return reply(output, outputCapacity, ok, true,
                     QString::fromLatin1("ok output=%1 parts=%2 units=%3")
                         .arg(dest)
                         .arg(specs.size())
                         .arg(call.units));
    }

    Model model;
    if (hasParts)
    {
        const QString beside = dest.isEmpty() ? QString() : QFileInfo(dest).absolutePath();
        if (!bakeSpecs(call, specs, beside, &model, &error))
            return failCall(output, outputCapacity, ok, error);
    }
    else if (!loadAt(call, path, &model, &error))
        return failCall(output, outputCapacity, ok, error);

    if (!dest.isEmpty())
    {
        if (model.triangleCount() == 0)
            return failCall(output, outputCapacity, ok, QString::fromLatin1("Mesh has no triangles."));
        if (!saveParts(dest, model.parts, flag(call, "binary", true), &error))
            return failCall(output, outputCapacity, ok, error);
    }
    QString text = reportModel(model);
    if (!dest.isEmpty())
        text += QString::fromLatin1("\noutput=%1").arg(dest);
    return reply(output, outputCapacity, ok, true, text);
}

int invokeDrawing(const char *argumentsJson, char *output, int outputCapacity, int *ok)
{
    Call call;
    QString error;
    if (!parseCall(argumentsJson, &call, &error))
        return failCall(output, outputCapacity, ok, error);
    const QString path = requirePath(call, "path", &error);
    if (path.isEmpty())
        return failCall(output, outputCapacity, ok, error);
    const QString outPath = requirePath(call, "output", &error);
    if (outPath.isEmpty())
        return failCall(output, outputCapacity, ok, error);
    Model model;
    if (!loadAt(call, path, &model, &error))
        return failCall(output, outputCapacity, ok, error);
    DrawOptions options;
    options.views = textList(call, "views");
    const QString sheet = argText(call, "sheet");
    const QString orientation = argText(call, "orientation");
    const QString projection = argText(call, "projection");
    if (!sheet.isEmpty())
        options.sheet = sheet;
    if (!orientation.isEmpty())
        options.orientation = orientation;
    if (!projection.isEmpty())
        options.projection = projection;
    options.units = model.units;
    options.hidden = flag(call, "hidden", false);
    double scale = 0;
    if (call.obj.contains(QString::fromLatin1("scale"))
        && !readNumber(call.obj.value(QString::fromLatin1("scale")), &scale))
        return failCall(output, outputCapacity, ok, QString::fromLatin1("scale must be a number."));
    options.scale = scale;
    double crease = 30;
    if (call.obj.contains(QString::fromLatin1("crease_deg"))
        && !readNumber(call.obj.value(QString::fromLatin1("crease_deg")), &crease))
        return failCall(output, outputCapacity, ok,
                        QString::fromLatin1("crease_deg must be a number."));
    options.creaseDeg = crease;
    QString summary;
    const QString dest = resolveOutputPath(outPath, call.root);
    if (!writeOrthographic(dest, model.combined(), options, &summary, &error))
        return failCall(output, outputCapacity, ok, error);
    return reply(output, outputCapacity, ok, true,
                 QString::fromLatin1("ok output=%1 %2").arg(dest, summary));
}

int invokeSection(const char *argumentsJson, char *output, int outputCapacity, int *ok)
{
    Call call;
    QString error;
    if (!parseCall(argumentsJson, &call, &error))
        return failCall(output, outputCapacity, ok, error);
    const QString path = requirePath(call, "path", &error);
    if (path.isEmpty())
        return failCall(output, outputCapacity, ok, error);
    const QString outPath = requirePath(call, "output", &error);
    if (outPath.isEmpty())
        return failCall(output, outputCapacity, ok, error);
    Vec3 point;
    Vec3 normal(0, 0, 1);
    bool hasPoint = false;
    if (!readVec3(call.obj.value(QString::fromLatin1("point")), &point, &hasPoint, &error))
        return failCall(output, outputCapacity, ok, error);
    if (!hasPoint)
        return failCall(output, outputCapacity, ok, QString::fromLatin1("point is required."));
    bool hasNormal = false;
    if (!readVec3(call.obj.value(QString::fromLatin1("normal")), &normal, &hasNormal, &error))
        return failCall(output, outputCapacity, ok, error);
    if (!hasNormal)
        normal = Vec3(0, 0, 1);
    Model model;
    if (!loadAt(call, path, &model, &error))
        return failCall(output, outputCapacity, ok, error);
    QList<SectionLoop> loops;
    if (!sectionMesh(model.combined(), point, normal, &loops, &error))
        return failCall(output, outputCapacity, ok, error);
    DrawOptions options;
    options.units = model.units;
    const QString sheet = argText(call, "sheet");
    const QString orientation = argText(call, "orientation");
    if (!sheet.isEmpty())
        options.sheet = sheet;
    if (!orientation.isEmpty())
        options.orientation = orientation;
    double scale = 0;
    if (call.obj.contains(QString::fromLatin1("scale"))
        && !readNumber(call.obj.value(QString::fromLatin1("scale")), &scale))
        return failCall(output, outputCapacity, ok, QString::fromLatin1("scale must be a number."));
    options.scale = scale;
    QString summary;
    const QString dest = resolveOutputPath(outPath, call.root);
    if (!writeSectionDrawing(dest, loops, point, normal, options, &summary, &error))
        return failCall(output, outputCapacity, ok, error);
    return reply(output, outputCapacity, ok, true,
                 QString::fromLatin1("ok output=%1 %2").arg(dest, summary));
}

//----------------------------------------------

const char kSchemaInspect[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"STL, OBJ, assembly JSON, or DXF path.\"},"
    "\"units\":{\"type\":\"string\",\"description\":\"Unit label, default mm.\"}"
    "},\"required\":[\"path\"]}";

const char kSchemaMeasure[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"Model, assembly, or DXF to measure.\"},"
    "\"a\":{\"type\":\"array\",\"description\":\"First point [x, y, z].\"},"
    "\"b\":{\"type\":\"array\",\"description\":\"Second point [x, y, z].\"},"
    "\"units\":{\"type\":\"string\",\"description\":\"Unit label, default mm.\"}"
    "}}";

const char kSchemaTransform[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"Source STL, OBJ, or assembly JSON.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .stl or .obj file.\"},"
    "\"translate\":{\"type\":\"array\",\"description\":\"Shift [x, y, z] applied last.\"},"
    "\"rotate_deg\":{\"type\":\"array\",\"description\":\"Degrees [x, y, z], applied X then Y then Z.\"},"
    "\"scale\":{\"description\":\"One number or [x, y, z].\"},"
    "\"center\":{\"type\":\"string\",\"description\":\"origin or bbox.\"},"
    "\"binary\":{\"type\":\"boolean\",\"description\":\"Write binary STL when output is .stl.\"}"
    "},\"required\":[\"path\",\"output\"]}";

const char kSchemaConvert[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"Source STL, OBJ, or assembly JSON.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .stl or .obj file.\"},"
    "\"binary\":{\"type\":\"boolean\",\"description\":\"Write binary STL when output is .stl. Default true.\"}"
    "},\"required\":[\"path\",\"output\"]}";

const char kSchemaAssembly[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"Existing assembly JSON or OBJ with groups.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .json, .stl, or .obj file.\"},"
    "\"parts\":{\"type\":\"array\",\"description\":\"Parts {file, name, translate, rotate_deg, scale}.\"},"
    "\"units\":{\"type\":\"string\",\"description\":\"Unit label stored in a JSON assembly.\"},"
    "\"binary\":{\"type\":\"boolean\",\"description\":\"Write binary STL when output is .stl.\"}"
    "}}";

const char kSchemaDrawing[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"STL, OBJ, or assembly to draw.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .svg or .dxf file.\"},"
    "\"views\":{\"type\":\"array\",\"description\":\"front, top, right, left, back, bottom.\"},"
    "\"projection\":{\"type\":\"string\",\"description\":\"third or first. Default third.\"},"
    "\"sheet\":{\"type\":\"string\",\"description\":\"A4, A3, A2, or A1.\"},"
    "\"orientation\":{\"type\":\"string\",\"description\":\"landscape or portrait.\"},"
    "\"scale\":{\"type\":\"number\",\"description\":\"Millimetres per model unit. 0 fits the sheet.\"},"
    "\"crease_deg\":{\"type\":\"number\",\"description\":\"Draw an interior edge at or above this angle. Default 30.\"},"
    "\"hidden\":{\"type\":\"boolean\",\"description\":\"Dashed hidden edges when the mesh has at most 8000 triangles.\"}"
    "},\"required\":[\"path\",\"output\"]}";

const char kSchemaSection[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"STL, OBJ, or assembly to cut.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .svg or .dxf file.\"},"
    "\"point\":{\"type\":\"array\",\"description\":\"A point on the plane [x, y, z].\"},"
    "\"normal\":{\"type\":\"array\",\"description\":\"Plane normal [x, y, z]. Default [0, 0, 1].\"},"
    "\"sheet\":{\"type\":\"string\",\"description\":\"A4, A3, A2, or A1.\"},"
    "\"orientation\":{\"type\":\"string\",\"description\":\"landscape or portrait.\"},"
    "\"scale\":{\"type\":\"number\",\"description\":\"Millimetres per model unit. 0 fits the sheet.\"}"
    "},\"required\":[\"path\",\"output\",\"point\"]}";

struct ToolDef
{
    const char        *id;
    const char        *title;
    const char        *description;
    const char        *schema;
    UniqueToolInvokeFn invoke;
};

const ToolDef kTools[] = {
    { "cad_inspect", "Обмер модели",
      "Читает сетку STL или OBJ, сборку JSON или чертёж DXF. path — путь в проекте или абсолютный. "
      "Для сетки сообщает детали, число треугольников, габарит, размеры по осям, площадь, объём "
      "замкнутого тела и открытые рёбра. Для DXF сообщает отрезки, окружности, дуги, полилинии, слои, "
      "габарит и длину линий. STEP и IGES не читаются: модель нужно заранее сохранить как STL, OBJ или DXF. "
      "units только подписывает единицы, по умолчанию mm.",
      kSchemaInspect, invokeInspect },
    { "cad_measure", "Размеры",
      "Считает габарит dx, dy, dz и диагональ. Точки a и b задаются как [x, y, z] и дают расстояние "
      "между ними; для одной такой дистанции файл не нужен. У чертежа DXF дополнительно считает "
      "суммарную длину линий, дуг и окружностей.",
      kSchemaMeasure, invokeMeasure },
    { "cad_transform", "Преобразование",
      "Переносит, поворачивает и масштабирует сетку и записывает новый STL или OBJ. "
      "translate — сдвиг [x, y, z]. rotate_deg — градусы [x, y, z]: сначала поворот вокруг X, "
      "затем вокруг Y, затем вокруг Z. scale — число или [x, y, z]. center=bbox вращает и "
      "масштабирует вокруг центра габарита, center=origin — вокруг нуля. Сдвиг применяется последним. "
      "output — новый файл, исходный не меняется.",
      kSchemaTransform, invokeTransform },
    { "cad_convert", "Конвертация",
      "Переводит сетку между STL и OBJ. Расширение output выбирает формат. binary по умолчанию true "
      "и пишет двоичный STL; false пишет текстовый STL. Несколько деталей в OBJ сохраняются группами. "
      "В STL группы сливаются в одно тело.",
      kSchemaConvert, invokeConvert },
    { "cad_assembly", "Сборка",
      "Собирает детали из STL и OBJ. parts — список объектов с полями file, name, translate, "
      "rotate_deg и scale; поворот и масштаб идут вокруг нуля детали, затем сдвиг. Если output "
      "кончается на .json, записывается только состав. Если output — .stl или .obj, детали ставятся "
      "в сборку и записывается сетка. path к готовому JSON или OBJ с группами показывает состав и "
      "общий габарит, а вместе с output ещё и выпекает сетку. Файл детали ищется рядом со сборкой, "
      "затем в проекте.",
      kSchemaAssembly, invokeAssembly },
    { "cad_drawing", "Чертёж",
      "Строит ортогональный чертёж сетки: контур, острые рёбра и границы. views — список front, top, "
      "right, left, back, bottom; по умолчанию front, top и right. projection=third или first. "
      "sheet — A4, A3, A2 или A1. orientation — landscape или portrait. scale=0 вписывает виды в лист, "
      "иначе это миллиметры листа на единицу модели. crease_deg — угол между нормалями, с которого "
      "внутреннее ребро рисуется; по умолчанию 30. hidden=true рисует невидимые рёбра пунктиром, если "
      "треугольников не больше 8000. output — файл .svg или .dxf.",
      kSchemaDrawing, invokeDrawing },
    { "cad_section", "Сечение",
      "Режет сетку плоскостью и записывает контур в SVG или DXF. point — точка на плоскости [x, y, z], "
      "normal — нормаль. В ответе длина контура, число замкнутых петель и незамкнутых цепочек.",
      kSchemaSection, invokeSection }
};

const int kToolCount = int(sizeof(kTools) / sizeof(kTools[0]));

//----------------------------------------------

extern "C" CAD_API int unique_plugin_abi(void)
{
    return UNIQUE_TOOL_PLUGIN_ABI;
}

extern "C" CAD_API const char *unique_plugin_id(void)
{
    return "cad";
}

extern "C" CAD_API const char *unique_plugin_title(void)
{
    return "CAD";
}

extern "C" CAD_API int unique_plugin_tool_count(void)
{
    return kToolCount;
}

extern "C" CAD_API int unique_plugin_tool(int index, UniqueToolSpec *spec)
{
    if (!spec || index < 0 || index >= kToolCount)
        return 0;
    spec->id = kTools[index].id;
    spec->title = kTools[index].title;
    spec->description = kTools[index].description;
    spec->schemaJson = kTools[index].schema;
    spec->invoke = kTools[index].invoke;
    return 1;
}
