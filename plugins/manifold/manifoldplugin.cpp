// Solid tools on top of Manifold (Apache-2.0, https://github.com/elalish/manifold).
// This file is built with a 32-bit GCC 13 and linked statically. The host
// compiler cannot build Manifold, and this DLL must not use Qt.

#include "jsonmin.h"
#include "meshio.h"
#include "manifold.h"
#include "../../src/toolplugin.h"

//----------------------------------------------

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <map>
#include <tuple>
#include <sstream>
#include <string>
#include <vector>

//----------------------------------------------

#if defined(_WIN32)
#  define MANIFOLD_API __declspec(dllexport)
#else
#  define MANIFOLD_API __attribute__((visibility("default")))
#endif

//----------------------------------------------

using manifold::CrossSection;
using manifold::Manifold;
using manifold::OpType;
using manifold::Polygons;
using manifold::SimplePolygon;

//----------------------------------------------

std::string fnum(double value)
{
    char buf[64];
    const std::to_chars_result formatted = std::to_chars(
        buf, buf + sizeof(buf), value, std::chars_format::general, 8);
    if (formatted.ec != std::errc())
        return "0";
    return std::string(buf, formatted.ptr);
}

std::string inum(int value)
{
    char buf[32];
    const std::to_chars_result formatted = std::to_chars(buf, buf + sizeof(buf), value);
    if (formatted.ec != std::errc())
        return "0";
    return std::string(buf, formatted.ptr);
}

int reply(char *output, int capacity, int *ok, bool success, const std::string &text)
{
    if (!output || (int)text.size() + 1 > capacity)
        return (int)text.size() + 1;
    std::memcpy(output, text.data(), text.size());
    output[text.size()] = 0;
    if (ok)
        *ok = success ? 1 : 0;
    return 0;
}

int failCall(char *output, int capacity, int *ok, const std::string &text)
{
    return reply(output, capacity, ok, false, text.empty() ? "Manifold failed." : text);
}

//----------------------------------------------

const char *statusName(Manifold::Error status)
{
    switch (status)
    {
    case Manifold::Error::NoError:
        return "NoError";
    case Manifold::Error::NonFiniteVertex:
        return "NonFiniteVertex";
    case Manifold::Error::NotManifold:
        return "NotManifold";
    case Manifold::Error::VertexOutOfBounds:
        return "VertexOutOfBounds";
    case Manifold::Error::PropertiesWrongLength:
        return "PropertiesWrongLength";
    case Manifold::Error::MissingPositionProperties:
        return "MissingPositionProperties";
    case Manifold::Error::MergeVectorsDifferentLengths:
        return "MergeVectorsDifferentLengths";
    case Manifold::Error::MergeIndexOutOfBounds:
        return "MergeIndexOutOfBounds";
    case Manifold::Error::TransformWrongLength:
        return "TransformWrongLength";
    case Manifold::Error::RunIndexWrongLength:
        return "RunIndexWrongLength";
    case Manifold::Error::FaceIDWrongLength:
        return "FaceIDWrongLength";
    case Manifold::Error::InvalidConstruction:
        return "InvalidConstruction";
    }
    return "Unknown";
}

bool finiteNumber(double value)
{
    return std::isfinite(value);
}

//----------------------------------------------

const Json *optionalField(const Json &root, const char *key)
{
    const Json *value = root.field(key);
    if (!value || value->kind == Json::Null)
        return 0;
    return value;
}

bool readNumberValue(const Json &value, double *out, std::string *error, const char *what)
{
    if (value.kind == Json::Number && finiteNumber(value.number))
    {
        *out = value.number;
        return true;
    }
    if (value.kind == Json::String)
    {
        const char *begin = value.text.c_str();
        const char *end = begin + value.text.size();
        const std::from_chars_result parsed = std::from_chars(begin, end, *out);
        if (parsed.ec == std::errc() && parsed.ptr == end && finiteNumber(*out))
            return true;
    }
    if (error)
        *error = std::string(what ? what : "value") + " must be a number.";
    return false;
}

bool readNumber(const Json &root, const char *key, double *out, bool *present, std::string *error)
{
    if (present)
        *present = false;
    const Json *value = optionalField(root, key);
    if (!value)
        return true;
    if (!readNumberValue(*value, out, error, key))
        return false;
    if (present)
        *present = true;
    return true;
}

bool readInt(const Json &root, const char *key, int *out, bool *present, std::string *error)
{
    double number = 0;
    if (!readNumber(root, key, &number, present, error))
        return false;
    if (present && !*present)
        return true;
    if (number < -2000000000 || number > 2000000000 || number != (double)(int)number)
    {
        if (error)
            *error = std::string(key) + " must be an integer.";
        return false;
    }
    *out = (int)number;
    return true;
}

bool readBool(const Json &root, const char *key, bool *out, std::string *error)
{
    const Json *value = optionalField(root, key);
    if (!value)
        return true;
    if (value->kind == Json::Bool)
    {
        *out = value->boolean;
        return true;
    }
    if (error)
        *error = std::string(key) + " must be true or false.";
    return false;
}

bool readText(const Json &root, const char *key, std::string *out, bool *present, std::string *error)
{
    if (present)
        *present = false;
    const Json *value = optionalField(root, key);
    if (!value)
        return true;
    if (value->kind != Json::String)
    {
        if (error)
            *error = std::string(key) + " must be a string.";
        return false;
    }
    *out = value->text;
    if (present)
        *present = true;
    return true;
}

bool readVec3Value(const Json &value, glm::vec3 *out, std::string *error, const char *what)
{
    if (value.kind != Json::Array || value.items.size() != 3)
    {
        if (error)
            *error = std::string(what ? what : "vector") + " must be [x, y, z].";
        return false;
    }
    double xyz[3];
    for (int i = 0; i < 3; ++i)
    {
        if (!readNumberValue(value.items[i], &xyz[i], error, what))
            return false;
    }
    *out = glm::vec3((float)xyz[0], (float)xyz[1], (float)xyz[2]);
    return true;
}

bool readVec2Value(const Json &value, glm::vec2 *out, std::string *error)
{
    if (value.kind != Json::Array || value.items.size() != 2)
    {
        if (error)
            *error = "Expected a point [x, y].";
        return false;
    }
    double xy[2];
    for (int i = 0; i < 2; ++i)
    {
        if (!readNumberValue(value.items[i], &xy[i], error, "point"))
            return false;
    }
    *out = glm::vec2((float)xy[0], (float)xy[1]);
    return true;
}

bool readSize3(const Json &root, glm::vec3 *out, std::string *error)
{
    *out = glm::vec3(1);
    const Json *value = optionalField(root, "size");
    if (!value)
        return true;
    if (value->kind == Json::Number || value->kind == Json::String)
    {
        double number = 1;
        if (!readNumberValue(*value, &number, error, "size"))
            return false;
        *out = glm::vec3((float)number);
        return true;
    }
    return readVec3Value(*value, out, error, "size");
}

bool readScale(const Json &root, glm::vec3 *out, std::string *error)
{
    *out = glm::vec3(1);
    const Json *value = optionalField(root, "scale");
    if (!value)
        return true;
    if (value->kind == Json::Number || value->kind == Json::String)
    {
        double number = 1;
        if (!readNumberValue(*value, &number, error, "scale"))
            return false;
        *out = glm::vec3((float)number);
        return true;
    }
    return readVec3Value(*value, out, error, "scale");
}

bool isAbsolutePath(const std::string &path)
{
    if (path.size() >= 2 && path[1] == ':')
        return true;
    if (!path.empty() && (path[0] == '/' || path[0] == '\\'))
        return true;
    return false;
}

std::string resolvePath(const Json &root, const std::string &path)
{
    if (path.empty() || isAbsolutePath(path))
        return path;
    const Json *base = root.field("project_root");
    if (!base || base->kind != Json::String || base->text.empty())
        return path;
    std::string full = base->text;
    const char last = full[full.size() - 1];
    if (last != '/' && last != '\\')
        full.push_back('\\');
    full += path;
    return full;
}

bool requirePath(const Json &root, const char *key, std::string *out, std::string *error)
{
    std::string text;
    bool present = false;
    if (!readText(root, key, &text, &present, error))
        return false;
    if (!present || text.empty())
    {
        if (error)
            *error = std::string(key) + " is required.";
        return false;
    }
    *out = resolvePath(root, text);
    return true;
}

OpType readOp(const Json &root, std::string *error)
{
    std::string text = "union";
    bool present = false;
    if (!readText(root, "op", &text, &present, error))
        return OpType::Add;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] >= 'A' && text[i] <= 'Z')
            text[i] = (char)(text[i] - 'A' + 'a');
    }
    if (text == "union" || text == "add")
        return OpType::Add;
    if (text == "difference" || text == "subtract")
        return OpType::Subtract;
    if (text == "intersection" || text == "intersect")
        return OpType::Intersect;
    if (error)
        *error = "op must be union, difference, or intersection.";
    return OpType::Add;
}

//----------------------------------------------

bool soupToSolid(const Soup &soup, Manifold *body, std::string *error)
{
    if (soup.count() <= 0)
    {
        *body = Manifold();
        return true;
    }
    manifold::MeshGL mesh;
    mesh.numProp = 3;
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint32_t> welded;
    mesh.triVerts.reserve((size_t)soup.count() * 3);
    for (int i = 0; i < soup.count() * 3; ++i)
    {
        const float x = soup.xyz[(size_t)i * 3];
        const float y = soup.xyz[(size_t)i * 3 + 1];
        const float z = soup.xyz[(size_t)i * 3 + 2];
        uint32_t bits[3];
        std::memcpy(&bits[0], &x, 4);
        std::memcpy(&bits[1], &y, 4);
        std::memcpy(&bits[2], &z, 4);
        const std::tuple<uint32_t, uint32_t, uint32_t> key(bits[0], bits[1], bits[2]);
        const std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint32_t>::iterator found =
            welded.find(key);
        if (found != welded.end())
        {
            mesh.triVerts.push_back(found->second);
            continue;
        }
        const uint32_t index = (uint32_t)(mesh.vertProperties.size() / 3);
        mesh.vertProperties.push_back(x);
        mesh.vertProperties.push_back(y);
        mesh.vertProperties.push_back(z);
        welded.insert(std::make_pair(key, index));
        mesh.triVerts.push_back(index);
    }
    *body = Manifold(mesh);
    if (body->Status() != Manifold::Error::NoError)
    {
        if (error)
            *error = std::string("status=") + statusName(body->Status());
        return false;
    }
    return true;
}

bool solidToSoup(const Manifold &body, Soup *soup, std::string *error)
{
    soup->xyz.clear();
    if (body.IsEmpty())
        return true;
    const manifold::MeshGL mesh = body.GetMeshGL();
    if (mesh.numProp < 3)
    {
        if (error)
            *error = "Mesh is missing positions.";
        return false;
    }
    const int triangles = (int)(mesh.triVerts.size() / 3);
    if (triangles > kMaxTriangles)
    {
        if (error)
            *error = "Mesh has too many triangles (limit 2000000).";
        return false;
    }
    soup->xyz.reserve((size_t)triangles * 9);
    for (int tri = 0; tri < triangles; ++tri)
    {
        for (int corner = 0; corner < 3; ++corner)
        {
            const uint32_t index = mesh.triVerts[(size_t)tri * 3 + corner];
            const size_t base = (size_t)index * mesh.numProp;
            if (base + 2 >= mesh.vertProperties.size())
            {
                if (error)
                    *error = "Mesh index is out of range.";
                return false;
            }
            soup->xyz.push_back(mesh.vertProperties[base]);
            soup->xyz.push_back(mesh.vertProperties[base + 1]);
            soup->xyz.push_back(mesh.vertProperties[base + 2]);
        }
    }
    return true;
}

bool loadSolid(const std::string &path, Manifold *body, std::string *error)
{
    Soup soup;
    if (!readMeshFile(path, &soup, error))
        return false;
    return soupToSolid(soup, body, error);
}

std::string solidFacts(const Manifold &body)
{
    std::string text = "triangles=" + inum(body.NumTri());
    text += " vertices=" + inum(body.NumVert());
    text += " status=" + std::string(statusName(body.Status()));
    if (body.IsEmpty() || body.Status() != Manifold::Error::NoError)
    {
        text += " empty=1 volume=0 area=0";
        return text;
    }
    const manifold::Properties props = body.GetProperties();
    text += " volume=" + fnum(props.volume);
    text += " area=" + fnum(props.surfaceArea);
    text += " genus=" + inum(body.Genus());
    const manifold::Box box = body.BoundingBox();
    if (box.IsFinite())
    {
        text += " bbox_min=" + fnum(box.min.x) + "," + fnum(box.min.y) + "," + fnum(box.min.z);
        text += " bbox_max=" + fnum(box.max.x) + "," + fnum(box.max.y) + "," + fnum(box.max.z);
    }
    return text;
}

bool saveSolid(const Manifold &body, const std::string &path, std::string *summary, std::string *error)
{
    if (body.Status() != Manifold::Error::NoError)
    {
        if (error)
            *error = std::string("status=") + statusName(body.Status());
        return false;
    }
    Soup soup;
    if (!solidToSoup(body, &soup, error))
        return false;
    if (!writeMeshFile(path, soup, error))
        return false;
    if (summary)
        *summary = solidFacts(body);
    return true;
}

int finishSolid(const Manifold &body, const std::string &path,
                char *output, int capacity, int *ok, bool warnShells = false)
{
    std::string summary;
    std::string error;
    if (!saveSolid(body, path, &summary, &error))
        return failCall(output, capacity, ok, error);
    if (warnShells && !body.IsEmpty() && body.Status() == Manifold::Error::NoError)
    {
        const int shells = (int)body.Decompose().size();
        summary += " shells=" + inum(shells);
        if (shells > 1)
            summary += " pieces do not intersect and stayed separate";
    }
    return reply(output, capacity, ok, true, "ok output=" + path + " " + summary);
}

//----------------------------------------------

bool readContour(const Json &value, SimplePolygon *contour, std::string *error)
{
    contour->clear();
    if (value.kind != Json::Array || value.items.size() < 3)
    {
        if (error)
            *error = "A contour needs at least 3 points [x, y].";
        return false;
    }
    if ((int)value.items.size() > kMaxTriangles)
    {
        if (error)
            *error = "Contour has too many points.";
        return false;
    }
    contour->reserve(value.items.size());
    for (size_t i = 0; i < value.items.size(); ++i)
    {
        glm::vec2 point(0);
        if (!readVec2Value(value.items[i], &point, error))
            return false;
        contour->push_back(point);
    }
    return true;
}

bool readPolygonsValue(const Json &value, Polygons *polygons, std::string *error)
{
    if (value.kind != Json::Array || value.items.empty())
    {
        if (error)
            *error = "polygons must be a list of contours.";
        return false;
    }
    const Json &first = value.items[0];
    if (first.kind == Json::Array && !first.items.empty() && first.items[0].kind == Json::Array)
    {
        for (size_t i = 0; i < value.items.size(); ++i)
        {
            SimplePolygon contour;
            if (!readContour(value.items[i], &contour, error))
                return false;
            polygons->push_back(contour);
        }
        return true;
    }
    SimplePolygon contour;
    if (!readContour(value, &contour, error))
        return false;
    polygons->push_back(contour);
    return true;
}

bool readPolygonsObject(const Json &root, Polygons *polygons, std::string *error)
{
    const Json *many = optionalField(root, "polygons");
    const Json *one = optionalField(root, "polygon");
    if (many)
    {
        if (!readPolygonsValue(*many, polygons, error))
            return false;
    }
    if (one)
    {
        SimplePolygon contour;
        if (!readContour(*one, &contour, error))
            return false;
        polygons->push_back(contour);
    }
    const Json *path = optionalField(root, "path");
    if (path && path->kind == Json::String && !path->text.empty())
    {
        const std::string file = resolvePath(root, path->text);
        if (lowerExt(file) == ".json")
        {
            std::vector<char> bytes;
            if (!readBytes(file, &bytes, error))
                return false;
            bytes.push_back(0);
            Json profile;
            if (!parseJson(bytes.data(), &profile, error))
                return false;
            const Json *fileMany = optionalField(profile, "polygons");
            const Json *fileOne = optionalField(profile, "polygon");
            if (!fileMany && !fileOne)
            {
                if (error)
                    *error = "Profile JSON needs polygon or polygons.";
                return false;
            }
            if (fileMany && !readPolygonsValue(*fileMany, polygons, error))
                return false;
            if (fileOne)
            {
                SimplePolygon contour;
                if (!readContour(*fileOne, &contour, error))
                    return false;
                polygons->push_back(contour);
            }
        }
        else if (!many && !one)
        {
            if (error)
                *error = "path for a contour must be a .json profile.";
            return false;
        }
    }
    return true;
}

bool polygonsText(const Polygons &polygons, std::string *text)
{
    std::string out = "{\"polygons\":[";
    for (size_t c = 0; c < polygons.size(); ++c)
    {
        if (c)
            out += ",";
        out += "[";
        for (size_t i = 0; i < polygons[c].size(); ++i)
        {
            if (i)
                out += ",";
            out += "[";
            out += fnum(polygons[c][i].x);
            out += ",";
            out += fnum(polygons[c][i].y);
            out += "]";
        }
        out += "]";
    }
    out += "]}";
    text->swap(out);
    return true;
}

bool writeTextFile(const std::string &path, const std::string &text, std::string *error)
{
    if (!ensureParent(path, error))
        return false;
    std::ofstream out(fsPath(path), std::ios::binary);
    if (!out)
    {
        if (error)
            *error = "Cannot write " + path;
        return false;
    }
    out.write(text.data(), (std::streamsize)text.size());
    if (!out)
    {
        if (error)
            *error = "Cannot write " + path;
        return false;
    }
    return true;
}

bool writeSvg(const std::string &path, const Polygons &polygons, std::string *error)
{
    float minX = 0;
    float minY = 0;
    float maxX = 1;
    float maxY = 1;
    bool any = false;
    for (size_t c = 0; c < polygons.size(); ++c)
    {
        for (size_t i = 0; i < polygons[c].size(); ++i)
        {
            const float x = polygons[c][i].x;
            const float y = polygons[c][i].y;
            if (!any)
            {
                minX = maxX = x;
                minY = maxY = y;
                any = true;
            }
            else
            {
                if (x < minX)
                    minX = x;
                if (y < minY)
                    minY = y;
                if (x > maxX)
                    maxX = x;
                if (y > maxY)
                    maxY = y;
            }
        }
    }
    const float width = maxX > minX ? maxX - minX : 1;
    const float height = maxY > minY ? maxY - minY : 1;
    std::string text = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"";
    text += fnum(minX) + " " + fnum(minY) + " " + fnum(width) + " " + fnum(height);
    text += "\">\n";
    for (size_t c = 0; c < polygons.size(); ++c)
    {
        text += "<polygon points=\"";
        for (size_t i = 0; i < polygons[c].size(); ++i)
        {
            if (i)
                text += " ";
            text += fnum(polygons[c][i].x);
            text += ",";
            text += fnum(maxY - (polygons[c][i].y - minY));
        }
        text += "\" fill=\"none\" stroke=\"black\"/>\n";
    }
    text += "</svg>\n";
    return writeTextFile(path, text, error);
}

bool writeProfile(const std::string &path, const Polygons &polygons, std::string *error)
{
    const std::string ext = lowerExt(path);
    if (ext == ".svg")
        return writeSvg(path, polygons, error);
    if (ext != ".json" && !ext.empty())
    {
        if (error)
            *error = "Contour output must end in .json or .svg.";
        return false;
    }
    std::string text;
    polygonsText(polygons, &text);
    const std::string dest = ext.empty() ? path + ".json" : path;
    return writeTextFile(dest, text, error);
}

bool sectionFromPolygons(const Polygons &polygons, CrossSection *section, std::string *error)
{
    if (polygons.empty())
    {
        if (error)
            *error = "Contour is empty.";
        return false;
    }
    *section = CrossSection(polygons, CrossSection::FillRule::EvenOdd);
    if (section->IsEmpty())
    {
        if (error)
            *error = "Contour is empty after cleanup.";
        return false;
    }
    return true;
}

std::string profileFacts(const CrossSection &section)
{
    const manifold::Rect bounds = section.Bounds();
    std::string text = "area=" + fnum(section.Area());
    text += " contours=" + inum(section.NumContour());
    text += " vertices=" + inum(section.NumVert());
    text += " bbox_min=" + fnum(bounds.min.x) + "," + fnum(bounds.min.y);
    text += " bbox_max=" + fnum(bounds.max.x) + "," + fnum(bounds.max.y);
    return text;
}

bool collectPolygons(const Json &root, Polygons *polygons, std::string *error)
{
    if (!readPolygonsObject(root, polygons, error))
        return false;
    const Json *inputs = optionalField(root, "inputs");
    if (inputs)
    {
        if (inputs->kind != Json::Array)
        {
            if (error)
                *error = "inputs must be a list of .json profiles.";
            return false;
        }
        for (size_t i = 0; i < inputs->items.size(); ++i)
        {
            if (inputs->items[i].kind != Json::String)
            {
                if (error)
                    *error = "inputs must be a list of .json profiles.";
                return false;
            }
            Json wrapper;
            wrapper.kind = Json::Object;
            Json path;
            path.kind = Json::String;
            path.text = inputs->items[i].text;
            wrapper.fields.push_back(std::make_pair(std::string("path"), path));
            const Json *base = root.field("project_root");
            if (base)
                wrapper.fields.push_back(std::make_pair(std::string("project_root"), *base));
            if (!readPolygonsObject(wrapper, polygons, error))
                return false;
        }
    }
    if (polygons->empty())
    {
        if (error)
            *error = "polygon, polygons, path, or inputs is required.";
        return false;
    }
    return true;
}

//----------------------------------------------

struct Call
{
    Json        root;
    std::string error;
};

bool beginCall(const char *json, Call *call)
{
    if (!parseJson(json, &call->root, &call->error))
        return false;
    return true;
}

int invokeCube(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        glm::vec3 size(1);
        if (!readSize3(call.root, &size, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (size.x <= 0 || size.y <= 0 || size.z <= 0)
            return failCall(output, capacity, ok, "size components must be positive.");
        bool center = false;
        if (!readBool(call.root, "center", &center, &call.error))
            return failCall(output, capacity, ok, call.error);
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        return finishSolid(Manifold::Cube(size, center), dest, output, capacity, ok);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

int invokeSphere(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        double radius = 1;
        bool present = false;
        if (!readNumber(call.root, "radius", &radius, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (!present || radius <= 0)
            return failCall(output, capacity, ok, "radius must be positive.");
        int segments = 0;
        present = false;
        if (!readInt(call.root, "segments", &segments, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (segments < 0)
            return failCall(output, capacity, ok, "segments must be >= 0.");
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        return finishSolid(Manifold::Sphere((float)radius, segments), dest, output, capacity, ok);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

int invokeCylinder(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        double height = 0;
        double radius = 0;
        double radiusHigh = -1;
        bool present = false;
        if (!readNumber(call.root, "height", &height, &present, &call.error) || !present)
            return failCall(output, capacity, ok, present ? call.error : "height is required.");
        present = false;
        if (!readNumber(call.root, "radius", &radius, &present, &call.error) || !present)
            return failCall(output, capacity, ok, present ? call.error : "radius is required.");
        present = false;
        if (!readNumber(call.root, "radius_high", &radiusHigh, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (!present)
            radiusHigh = -1;
        if (height == 0 || radius < 0 || (present && radiusHigh < 0))
            return failCall(output, capacity, ok, "height must be non-zero and radii must be >= 0.");
        int segments = 0;
        present = false;
        if (!readInt(call.root, "segments", &segments, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (segments < 0)
            return failCall(output, capacity, ok, "segments must be >= 0.");
        bool center = false;
        if (!readBool(call.root, "center", &center, &call.error))
            return failCall(output, capacity, ok, call.error);
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        return finishSolid(
            Manifold::Cylinder((float)height, (float)radius, (float)radiusHigh, segments, center),
            dest, output, capacity, ok);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

int invokeTetra(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        glm::vec3 size(1);
        if (!readSize3(call.root, &size, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (size.x <= 0 || size.y <= 0 || size.z <= 0)
            return failCall(output, capacity, ok, "size components must be positive.");
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        Manifold body = Manifold::Tetrahedron().Scale(size);
        return finishSolid(body, dest, output, capacity, ok);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

bool readScaleTop(const Json &root, glm::vec2 *out, std::string *error)
{
    *out = glm::vec2(1);
    const Json *value = optionalField(root, "scale_top");
    if (!value)
        return true;
    if (value->kind == Json::Number || value->kind == Json::String)
    {
        double number = 1;
        if (!readNumberValue(*value, &number, error, "scale_top"))
            return false;
        *out = glm::vec2((float)number);
        return true;
    }
    return readVec2Value(*value, out, error);
}

int invokeExtrude(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        Polygons polygons;
        if (!collectPolygons(call.root, &polygons, &call.error))
            return failCall(output, capacity, ok, call.error);
        CrossSection section;
        if (!sectionFromPolygons(polygons, &section, &call.error))
            return failCall(output, capacity, ok, call.error);
        double height = 0;
        bool present = false;
        if (!readNumber(call.root, "height", &height, &present, &call.error) || !present || height == 0)
            return failCall(output, capacity, ok, present ? "height must be non-zero." : "height is required.");
        int divisions = 0;
        present = false;
        if (!readInt(call.root, "divisions", &divisions, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (divisions < 0)
            return failCall(output, capacity, ok, "divisions must be >= 0.");
        double twist = 0;
        present = false;
        if (!readNumber(call.root, "twist_deg", &twist, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        glm::vec2 scaleTop(1);
        if (!readScaleTop(call.root, &scaleTop, &call.error))
            return failCall(output, capacity, ok, call.error);
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        const Manifold body = Manifold::Extrude(
            section, (float)height, divisions, (float)twist, scaleTop);
        return finishSolid(body, dest, output, capacity, ok);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

int invokeRevolve(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        Polygons polygons;
        if (!collectPolygons(call.root, &polygons, &call.error))
            return failCall(output, capacity, ok, call.error);
        CrossSection section;
        if (!sectionFromPolygons(polygons, &section, &call.error))
            return failCall(output, capacity, ok, call.error);
        int segments = 0;
        bool present = false;
        if (!readInt(call.root, "segments", &segments, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (segments < 0)
            return failCall(output, capacity, ok, "segments must be >= 0.");
        double degrees = 360;
        present = false;
        if (!readNumber(call.root, "degrees", &degrees, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (degrees == 0)
            return failCall(output, capacity, ok, "degrees must be non-zero.");
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        const Manifold body = Manifold::Revolve(section, segments, (float)degrees);
        return finishSolid(body, dest, output, capacity, ok);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

bool loadInputList(const Json &root, std::vector<Manifold> *bodies, std::string *error)
{
    const Json *inputs = optionalField(root, "inputs");
    if (!inputs || inputs->kind != Json::Array || inputs->items.empty())
    {
        if (error)
            *error = "inputs must be a list of STL, OBJ, or tessellated STEP paths.";
        return false;
    }
    if (inputs->items.size() > 128)
    {
        if (error)
            *error = "Too many inputs (limit 128).";
        return false;
    }
    for (size_t i = 0; i < inputs->items.size(); ++i)
    {
        if (inputs->items[i].kind != Json::String || inputs->items[i].text.empty())
        {
            if (error)
                *error = "inputs must be a list of STL, OBJ, or tessellated STEP paths.";
            return false;
        }
        Manifold body;
        if (!loadSolid(resolvePath(root, inputs->items[i].text), &body, error))
            return false;
        bodies->push_back(body);
    }
    return true;
}

int invokeBoolean(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        const OpType op = readOp(call.root, &call.error);
        if (!call.error.empty())
            return failCall(output, capacity, ok, call.error);
        std::vector<Manifold> bodies;
        if (!loadInputList(call.root, &bodies, &call.error))
            return failCall(output, capacity, ok, call.error);
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        const Manifold body = Manifold::BatchBoolean(bodies, op);
        return finishSolid(body, dest, output, capacity, ok, op == OpType::Add);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

int invokeHull(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        std::vector<glm::vec3> points;
        const Json *raw = optionalField(call.root, "points");
        if (raw)
        {
            if (raw->kind != Json::Array || raw->items.empty())
                return failCall(output, capacity, ok, "points must be a list of [x, y, z].");
            for (size_t i = 0; i < raw->items.size(); ++i)
            {
                glm::vec3 point(0);
                if (!readVec3Value(raw->items[i], &point, &call.error, "points"))
                    return failCall(output, capacity, ok, call.error);
                points.push_back(point);
            }
        }
        std::vector<Manifold> bodies;
        if (points.empty() && optionalField(call.root, "inputs"))
        {
            if (!loadInputList(call.root, &bodies, &call.error))
                return failCall(output, capacity, ok, call.error);
        }
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        Manifold body;
        if (!points.empty())
            body = Manifold::Hull(points);
        else if (bodies.size() == 1)
            body = bodies[0].Hull();
        else if (bodies.size() > 1)
            body = Manifold::Hull(bodies);
        else
            return failCall(output, capacity, ok, "inputs or points is required.");
        return finishSolid(body, dest, output, capacity, ok);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

int invokePlace(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        std::string source;
        if (!requirePath(call.root, "path", &source, &call.error))
            return failCall(output, capacity, ok, call.error);
        Manifold body;
        if (!loadSolid(source, &body, &call.error))
            return failCall(output, capacity, ok, call.error);
        glm::vec3 translate(0);
        const Json *shift = optionalField(call.root, "translate");
        if (shift && !readVec3Value(*shift, &translate, &call.error, "translate"))
            return failCall(output, capacity, ok, call.error);
        glm::vec3 rotate(0);
        const Json *spin = optionalField(call.root, "rotate_deg");
        if (spin && !readVec3Value(*spin, &rotate, &call.error, "rotate_deg"))
            return failCall(output, capacity, ok, call.error);
        glm::vec3 scale(1);
        if (!readScale(call.root, &scale, &call.error))
            return failCall(output, capacity, ok, call.error);
        std::string center = "origin";
        bool present = false;
        if (!readText(call.root, "center", &center, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        for (size_t i = 0; i < center.size(); ++i)
        {
            if (center[i] >= 'A' && center[i] <= 'Z')
                center[i] = (char)(center[i] - 'A' + 'a');
        }
        if (center != "origin" && center != "bbox")
            return failCall(output, capacity, ok, "center must be origin or bbox.");
        glm::vec3 pivot(0);
        const bool aroundBox = center == "bbox" && !body.IsEmpty();
        if (aroundBox)
        {
            pivot = body.BoundingBox().Center();
            body = body.Translate(-pivot);
        }
        body = body.Scale(scale).Rotate(rotate.x, rotate.y, rotate.z);
        if (aroundBox)
            body = body.Translate(pivot);
        body = body.Translate(translate);
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        return finishSolid(body, dest, output, capacity, ok);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

int invokeMirror(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        std::string source;
        if (!requirePath(call.root, "path", &source, &call.error))
            return failCall(output, capacity, ok, call.error);
        Manifold body;
        if (!loadSolid(source, &body, &call.error))
            return failCall(output, capacity, ok, call.error);
        glm::vec3 normal(1, 0, 0);
        const Json *value = optionalField(call.root, "normal");
        if (!value)
            return failCall(output, capacity, ok, "normal is required.");
        if (!readVec3Value(*value, &normal, &call.error, "normal"))
            return failCall(output, capacity, ok, call.error);
        if (glm::length(normal) == 0)
            return failCall(output, capacity, ok, "normal must not be zero.");
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        return finishSolid(body.Mirror(normal), dest, output, capacity, ok);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

int finishProfile(const CrossSection &section, const Json &root,
                  char *output, int capacity, int *ok)
{
    double height = 0;
    bool hasHeight = false;
    std::string error;
    if (!readNumber(root, "height", &height, &hasHeight, &error))
        return failCall(output, capacity, ok, error);
    std::string dest;
    if (!requirePath(root, "output", &dest, &error))
        return failCall(output, capacity, ok, error);
    if (hasHeight)
    {
        if (height == 0)
            return failCall(output, capacity, ok, "height must be non-zero.");
        int divisions = 0;
        bool present = false;
        if (!readInt(root, "divisions", &divisions, &present, &error))
            return failCall(output, capacity, ok, error);
        double twist = 0;
        if (!readNumber(root, "twist_deg", &twist, &present, &error))
            return failCall(output, capacity, ok, error);
        glm::vec2 scaleTop(1);
        if (!readScaleTop(root, &scaleTop, &error))
            return failCall(output, capacity, ok, error);
        const Manifold body = Manifold::Extrude(
            section, (float)height, divisions, (float)twist, scaleTop);
        return finishSolid(body, dest, output, capacity, ok);
    }
    const Polygons polygons = section.ToPolygons();
    if (!writeProfile(dest, polygons, &error))
        return failCall(output, capacity, ok, error);
    return reply(output, capacity, ok, true,
                 "ok output=" + dest + " " + profileFacts(section));
}

int invokeSlice(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        std::string source;
        if (!requirePath(call.root, "path", &source, &call.error))
            return failCall(output, capacity, ok, call.error);
        Manifold body;
        if (!loadSolid(source, &body, &call.error))
            return failCall(output, capacity, ok, call.error);
        double height = 0;
        bool present = false;
        if (!readNumber(call.root, "height", &height, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        const CrossSection section = body.Slice((float)height);
        const Polygons polygons = section.ToPolygons();
        if (!writeProfile(dest, polygons, &call.error))
            return failCall(output, capacity, ok, call.error);
        std::string text = "ok output=" + dest + " " + profileFacts(section);
        if (section.IsEmpty())
            text += " empty=1";
        return reply(output, capacity, ok, true, text);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

int invokeProject(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        std::string source;
        if (!requirePath(call.root, "path", &source, &call.error))
            return failCall(output, capacity, ok, call.error);
        Manifold body;
        if (!loadSolid(source, &body, &call.error))
            return failCall(output, capacity, ok, call.error);
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        const CrossSection section = body.Project();
        const Polygons polygons = section.ToPolygons();
        if (!writeProfile(dest, polygons, &call.error))
            return failCall(output, capacity, ok, call.error);
        return reply(output, capacity, ok, true,
                     "ok output=" + dest + " " + profileFacts(section));
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

int invokeRefine(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        std::string source;
        if (!requirePath(call.root, "path", &source, &call.error))
            return failCall(output, capacity, ok, call.error);
        Manifold body;
        if (!loadSolid(source, &body, &call.error))
            return failCall(output, capacity, ok, call.error);
        std::string mode = "count";
        bool present = false;
        if (!readText(call.root, "mode", &mode, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        for (size_t i = 0; i < mode.size(); ++i)
        {
            if (mode[i] >= 'A' && mode[i] <= 'Z')
                mode[i] = (char)(mode[i] - 'A' + 'a');
        }
        double value = 0;
        present = false;
        if (!readNumber(call.root, "value", &value, &present, &call.error) || !present)
            return failCall(output, capacity, ok, present ? call.error : "value is required.");
        if (mode == "count")
        {
            if (value < 1 || value != (double)(int)value)
                return failCall(output, capacity, ok, "value must be an integer >= 1.");
            body = body.Refine((int)value);
        }
        else if (mode == "length")
        {
            if (value <= 0)
                return failCall(output, capacity, ok, "value must be positive.");
            body = body.RefineToLength((float)value);
        }
        else
            return failCall(output, capacity, ok, "mode must be count or length.");
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        return finishSolid(body, dest, output, capacity, ok);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

int invokeSmooth(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        std::string source;
        if (!requirePath(call.root, "path", &source, &call.error))
            return failCall(output, capacity, ok, call.error);
        Manifold body;
        if (!loadSolid(source, &body, &call.error))
            return failCall(output, capacity, ok, call.error);
        double angle = 60;
        bool present = false;
        if (!readNumber(call.root, "angle_deg", &angle, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        double smoothness = 0;
        if (!readNumber(call.root, "smoothness", &smoothness, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (smoothness < 0 || smoothness > 1)
            return failCall(output, capacity, ok, "smoothness must be from 0 to 1.");
        int refine = 2;
        present = false;
        if (!readInt(call.root, "refine", &refine, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (refine < 1)
            return failCall(output, capacity, ok, "refine must be >= 1.");
        body = body.SmoothOut((float)angle, (float)smoothness);
        if (refine > 1)
            body = body.Refine(refine);
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        return finishSolid(body, dest, output, capacity, ok);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

int invokeSolid(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        std::string source;
        if (!requirePath(call.root, "path", &source, &call.error))
            return failCall(output, capacity, ok, call.error);
        Manifold body;
        if (!loadSolid(source, &body, &call.error))
            return failCall(output, capacity, ok, call.error);
        std::string text = "ok " + solidFacts(body);
        std::string otherPath;
        bool present = false;
        if (!readText(call.root, "other", &otherPath, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (present && !otherPath.empty())
        {
            Manifold other;
            if (!loadSolid(resolvePath(call.root, otherPath), &other, &call.error))
                return failCall(output, capacity, ok, call.error);
            double search = 0;
            bool hasSearch = false;
            if (!readNumber(call.root, "search", &search, &hasSearch, &call.error))
                return failCall(output, capacity, ok, call.error);
            if (!hasSearch || search <= 0)
            {
                const glm::vec3 a = body.BoundingBox().Size();
                const glm::vec3 b = other.BoundingBox().Size();
                search = (double)glm::length(a) + (double)glm::length(b);
                if (search <= 0)
                    search = 1;
            }
            text += " gap=" + fnum(body.MinGap(other, (float)search));
        }
        return reply(output, capacity, ok, true, text);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

int invokeTrim(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        std::string source;
        if (!requirePath(call.root, "path", &source, &call.error))
            return failCall(output, capacity, ok, call.error);
        Manifold body;
        if (!loadSolid(source, &body, &call.error))
            return failCall(output, capacity, ok, call.error);
        glm::vec3 normal(0, 0, 1);
        const Json *value = optionalField(call.root, "normal");
        if (value && !readVec3Value(*value, &normal, &call.error, "normal"))
            return failCall(output, capacity, ok, call.error);
        if (glm::length(normal) == 0)
            return failCall(output, capacity, ok, "normal must not be zero.");
        double offset = 0;
        bool present = false;
        if (!readNumber(call.root, "offset", &offset, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        std::string mode = "trim";
        if (!readText(call.root, "mode", &mode, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        for (size_t i = 0; i < mode.size(); ++i)
        {
            if (mode[i] >= 'A' && mode[i] <= 'Z')
                mode[i] = (char)(mode[i] - 'A' + 'a');
        }
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (mode == "split")
        {
            const std::pair<Manifold, Manifold> parts =
                body.SplitByPlane(normal, (float)offset);
            std::string second;
            bool hasSecond = false;
            if (!readText(call.root, "output_b", &second, &hasSecond, &call.error))
                return failCall(output, capacity, ok, call.error);
            if (!hasSecond || second.empty())
            {
                const std::string ext = lowerExt(dest);
                const size_t dot = ext.empty() ? std::string::npos : dest.size() - ext.size();
                second = dest.substr(0, dot) + "_b" + (ext.empty() ? ".stl" : ext);
            }
            else
                second = resolvePath(call.root, second);
            std::string summaryA;
            std::string summaryB;
            if (!saveSolid(parts.first, dest, &summaryA, &call.error))
                return failCall(output, capacity, ok, call.error);
            if (!saveSolid(parts.second, second, &summaryB, &call.error))
                return failCall(output, capacity, ok, call.error);
            return reply(output, capacity, ok, true,
                         "ok output=" + dest + " " + summaryA
                         + " output_b=" + second + " " + summaryB);
        }
        if (mode != "trim")
            return failCall(output, capacity, ok, "mode must be trim or split.");
        return finishSolid(body.TrimByPlane(normal, (float)offset), dest, output, capacity, ok);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

CrossSection::JoinType readJoin(const Json &root, std::string *error)
{
    std::string text = "miter";
    bool present = false;
    if (!readText(root, "join", &text, &present, error))
        return CrossSection::JoinType::Miter;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] >= 'A' && text[i] <= 'Z')
            text[i] = (char)(text[i] - 'A' + 'a');
    }
    if (text == "miter")
        return CrossSection::JoinType::Miter;
    if (text == "round")
        return CrossSection::JoinType::Round;
    if (text == "square")
        return CrossSection::JoinType::Square;
    if (error)
        *error = "join must be miter, round, or square.";
    return CrossSection::JoinType::Miter;
}

int invokeOffset(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        Polygons polygons;
        if (!collectPolygons(call.root, &polygons, &call.error))
            return failCall(output, capacity, ok, call.error);
        CrossSection section;
        if (!sectionFromPolygons(polygons, &section, &call.error))
            return failCall(output, capacity, ok, call.error);
        double delta = 0;
        bool present = false;
        if (!readNumber(call.root, "delta", &delta, &present, &call.error) || !present)
            return failCall(output, capacity, ok, present ? call.error : "delta is required.");
        const CrossSection::JoinType join = readJoin(call.root, &call.error);
        if (!call.error.empty())
            return failCall(output, capacity, ok, call.error);
        double miter = 2;
        if (!readNumber(call.root, "miter_limit", &miter, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        int segments = 0;
        present = false;
        if (!readInt(call.root, "segments", &segments, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (segments < 0)
            return failCall(output, capacity, ok, "segments must be >= 0.");
        section = section.Offset(delta, join, miter, segments);
        if (section.IsEmpty())
            return failCall(output, capacity, ok, "Offset removed the contour.");
        return finishProfile(section, call.root, output, capacity, ok);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

int invokeProfile(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        const OpType op = readOp(call.root, &call.error);
        if (!call.error.empty())
            return failCall(output, capacity, ok, call.error);
        std::vector<Polygons> groups;
        const Json *inputs = optionalField(call.root, "inputs");
        if (inputs && inputs->kind == Json::Array)
        {
            for (size_t i = 0; i < inputs->items.size(); ++i)
            {
                if (inputs->items[i].kind != Json::String)
                    return failCall(output, capacity, ok, "inputs must be .json profiles.");
                Json wrapper;
                wrapper.kind = Json::Object;
                Json path;
                path.kind = Json::String;
                path.text = inputs->items[i].text;
                wrapper.fields.push_back(std::make_pair(std::string("path"), path));
                const Json *base = call.root.field("project_root");
                if (base)
                    wrapper.fields.push_back(std::make_pair(std::string("project_root"), *base));
                Polygons one;
                if (!readPolygonsObject(wrapper, &one, &call.error))
                    return failCall(output, capacity, ok, call.error);
                groups.push_back(one);
            }
        }
        Polygons direct;
        const Json *many = optionalField(call.root, "polygons");
        const Json *one = optionalField(call.root, "polygon");
        const Json *other = optionalField(call.root, "polygons_b");
        if (many || one)
        {
            if (!readPolygonsObject(call.root, &direct, &call.error))
                return failCall(output, capacity, ok, call.error);
            if (!direct.empty())
                groups.insert(groups.begin(), direct);
        }
        if (other)
        {
            Polygons second;
            if (!readPolygonsValue(*other, &second, &call.error))
                return failCall(output, capacity, ok, call.error);
            groups.push_back(second);
        }
        if (groups.empty())
            return failCall(output, capacity, ok, "polygons or inputs is required.");
        std::vector<CrossSection> sections;
        for (size_t i = 0; i < groups.size(); ++i)
        {
            CrossSection section;
            if (!sectionFromPolygons(groups[i], &section, &call.error))
                return failCall(output, capacity, ok, call.error);
            sections.push_back(section);
        }
        CrossSection result = CrossSection::BatchBoolean(sections, op);
        double epsilon = 0;
        bool present = false;
        if (!readNumber(call.root, "epsilon", &epsilon, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (present && epsilon > 0)
            result = result.Simplify(epsilon);
        if (result.IsEmpty())
            return failCall(output, capacity, ok, "Contour boolean is empty.");
        return finishProfile(result, call.root, output, capacity, ok);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

std::string indexedOutput(const std::string &path, int index)
{
    const std::string ext = lowerExt(path);
    const size_t cut = ext.empty() ? path.size() : path.size() - ext.size();
    return path.substr(0, cut) + "_" + inum(index) + (ext.empty() ? ".stl" : ext);
}

int invokeDecompose(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        std::string source;
        if (!requirePath(call.root, "path", &source, &call.error))
            return failCall(output, capacity, ok, call.error);
        Manifold body;
        if (!loadSolid(source, &body, &call.error))
            return failCall(output, capacity, ok, call.error);
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        const std::vector<Manifold> parts = body.Decompose();
        if (parts.empty())
            return failCall(output, capacity, ok, "Body has no separate shells.");
        if ((int)parts.size() > 256)
            return failCall(output, capacity, ok, "Too many shells (limit 256).");
        std::string text = "ok parts=" + inum((int)parts.size());
        for (size_t i = 0; i < parts.size(); ++i)
        {
            const std::string file = indexedOutput(dest, (int)i);
            std::string summary;
            if (!saveSolid(parts[i], file, &summary, &call.error))
                return failCall(output, capacity, ok, call.error);
            text += " output_" + inum((int)i) + "=" + file;
        }
        return reply(output, capacity, ok, true, text);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

int invokeCircle(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        double radius = 0;
        bool present = false;
        if (!readNumber(call.root, "radius", &radius, &present, &call.error) || !present || radius <= 0)
            return failCall(output, capacity, ok, "radius must be positive.");
        int segments = 0;
        if (!readInt(call.root, "segments", &segments, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (segments < 0)
            return failCall(output, capacity, ok, "segments must be >= 0.");
        return finishProfile(CrossSection::Circle((float)radius, segments),
                             call.root, output, capacity, ok);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

int invokeSquare(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        glm::vec2 size(1);
        const Json *value = optionalField(call.root, "size");
        if (value)
        {
            if (value->kind == Json::Number || value->kind == Json::String)
            {
                double number = 1;
                if (!readNumberValue(*value, &number, &call.error, "size"))
                    return failCall(output, capacity, ok, call.error);
                size = glm::vec2((float)number);
            }
            else if (!readVec2Value(*value, &size, &call.error))
                return failCall(output, capacity, ok, call.error);
        }
        if (size.x <= 0 || size.y <= 0)
            return failCall(output, capacity, ok, "size components must be positive.");
        bool center = false;
        if (!readBool(call.root, "center", &center, &call.error))
            return failCall(output, capacity, ok, call.error);
        return finishProfile(CrossSection::Square(size, center), call.root, output, capacity, ok);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

//----------------------------------------------

static const double kPi = 3.14159265358979323846;

static glm::vec3 rotateAround(glm::vec3 vector, glm::vec3 axis, float radians)
{
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return vector * cosine
        + glm::cross(axis, vector) * sine
        + axis * glm::dot(axis, vector) * (1.0f - cosine);
}

static void pushTri(Soup *soup, const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c)
{
    const glm::vec3 corner[3] = {a, b, c};
    for (int i = 0; i < 3; ++i)
    {
        soup->xyz.push_back(corner[i].x);
        soup->xyz.push_back(corner[i].y);
        soup->xyz.push_back(corner[i].z);
    }
}

static bool gearTooth(int teeth, double module, double pressureDeg, double addendum,
                      double dedendum, double backlash, int flankSteps,
                      SimplePolygon *contour, std::string *error)
{
    const double alpha = pressureDeg * kPi / 180.0;
    const double pitchRadius = module * (double)teeth / 2.0;
    const double baseRadius = pitchRadius * std::cos(alpha);
    const double tipRadius = pitchRadius + module * addendum;
    const double rootRadius = pitchRadius - module * dedendum;
    if (rootRadius <= module * 0.05)
    {
        *error = "root radius is not positive. Lower dedendum or raise the tooth count.";
        return false;
    }
    if (tipRadius <= rootRadius)
    {
        *error = "addendum and dedendum leave no tooth.";
        return false;
    }
    const double halfThick = kPi / (2.0 * (double)teeth) - backlash / (2.0 * pitchRadius);
    if (halfThick <= 0.0)
    {
        *error = "backlash removes the whole tooth.";
        return false;
    }
    const double involuteAlpha = std::tan(alpha) - alpha;
    const double flankStart = rootRadius < baseRadius ? baseRadius : rootRadius;
    auto involute = [&](double radius) -> double
    {
        const double phi = std::acos(baseRadius / radius);
        return std::tan(phi) - phi;
    };
    auto flankAngle = [&](double radius) -> double
    {
        return halfThick - (involute(radius) - involuteAlpha);
    };
    if (flankAngle(tipRadius) <= 0.0)
    {
        *error = "gear profile is self-intersecting.";
        return false;
    }
    const double rootAngle = flankAngle(flankStart);
    contour->clear();
    auto push = [&](double radius, double angle)
    {
        const glm::vec2 point((float)(radius * std::cos(angle)),
                              (float)(radius * std::sin(angle)));
        if (!contour->empty())
        {
            const glm::vec2 last = contour->back();
            const float dx = point.x - last.x;
            const float dy = point.y - last.y;
            if (dx * dx + dy * dy < 1e-12f)
                return;
        }
        contour->push_back(point);
    };
    const int steps = flankSteps;
    push(rootRadius, -rootAngle);
    if (rootRadius < baseRadius - 1e-9)
        push(flankStart, -rootAngle);
    for (int i = 0; i <= steps; ++i)
    {
        const double t = (double)i / (double)steps;
        const double radius = flankStart + (tipRadius - flankStart) * t;
        push(radius, -flankAngle(radius));
    }
    const int tipSteps = 4;
    const double tipLeft = -flankAngle(tipRadius);
    const double tipRight = flankAngle(tipRadius);
    for (int i = 1; i < tipSteps; ++i)
    {
        const double t = (double)i / (double)tipSteps;
        push(tipRadius, tipLeft + (tipRight - tipLeft) * t);
    }
    for (int i = steps; i >= 0; --i)
    {
        const double t = (double)i / (double)steps;
        const double radius = flankStart + (tipRadius - flankStart) * t;
        push(radius, flankAngle(radius));
    }
    if (rootRadius < baseRadius - 1e-9)
        push(flankStart, rootAngle);
    push(rootRadius, rootAngle);
    const int rootSteps = 3;
    for (int i = 1; i < rootSteps; ++i)
    {
        const double t = (double)i / (double)rootSteps;
        push(rootRadius, rootAngle - 2.0 * rootAngle * t);
    }
    if ((int)contour->size() < 3)
    {
        *error = "gear profile is self-intersecting.";
        return false;
    }
    double area = 0;
    for (size_t i = 0; i < contour->size(); ++i)
    {
        const glm::vec2 a = (*contour)[i];
        const glm::vec2 b = (*contour)[(i + 1) % contour->size()];
        area += (double)a.x * (double)b.y - (double)b.x * (double)a.y;
    }
    if (area < 0)
        std::reverse(contour->begin(), contour->end());
    if (std::fabs(area) < 1e-8)
    {
        *error = "gear profile is self-intersecting.";
        return false;
    }
    return true;
}

int invokeGear(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        double module = 0;
        bool present = false;
        if (!readNumber(call.root, "module", &module, &present, &call.error) || !present || module <= 0)
            return failCall(output, capacity, ok, "module must be positive.");
        int teeth = 0;
        if (!readInt(call.root, "teeth", &teeth, &present, &call.error) || !present)
            return failCall(output, capacity, ok, "teeth is required.");
        if (teeth < 4 || teeth > 200)
            return failCall(output, capacity, ok, "teeth must be from 4 to 200.");
        double width = 0;
        if (!readNumber(call.root, "width", &width, &present, &call.error) || !present || width <= 0)
            return failCall(output, capacity, ok, "width must be positive.");
        double pressure = 20;
        if (!readNumber(call.root, "pressure_deg", &pressure, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (pressure <= 0 || pressure >= 80)
            return failCall(output, capacity, ok, "pressure_deg must be between 0 and 80.");
        double addendum = 1;
        if (!readNumber(call.root, "addendum", &addendum, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (addendum <= 0 || addendum > 5)
            return failCall(output, capacity, ok, "addendum coefficient must be greater than 0 and at most 5.");
        double dedendum = 1.25;
        if (!readNumber(call.root, "dedendum", &dedendum, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (dedendum <= 0 || dedendum > 5)
            return failCall(output, capacity, ok, "dedendum coefficient must be greater than 0 and at most 5.");
        double backlash = 0;
        if (!readNumber(call.root, "backlash", &backlash, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (backlash < 0)
            return failCall(output, capacity, ok, "backlash must be >= 0.");
        double helix = 0;
        if (!readNumber(call.root, "helix_deg", &helix, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (helix < -720 || helix > 720)
            return failCall(output, capacity, ok, "helix_deg must be from -720 to 720.");
        double bevel = 0;
        if (!readNumber(call.root, "bevel_deg", &bevel, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (bevel < 0 || bevel >= 90)
            return failCall(output, capacity, ok, "bevel_deg must be from 0 up to but not including 90.");
        double bore = 0;
        if (!readNumber(call.root, "bore", &bore, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (bore < 0)
            return failCall(output, capacity, ok, "bore must be >= 0.");
        int flankSteps = 6;
        if (!readInt(call.root, "flank_steps", &flankSteps, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (!present)
            flankSteps = 6;
        if (flankSteps < 2 || flankSteps > 24)
            return failCall(output, capacity, ok, "flank_steps must be from 2 to 24.");
        const double pitchRadius = module * (double)teeth / 2.0;
        double scale = 1;
        if (bevel > 0)
        {
            const double cone = pitchRadius / std::sin(bevel * kPi / 180.0);
            scale = (cone - width) / cone;
            if (scale <= 0.05)
                return failCall(output, capacity, ok, "width is too large for this cone.");
        }
        const double rootRadius = pitchRadius - module * dedendum;
        if (bore > 0 && bore >= rootRadius * (scale < 1 ? scale : 1) * 0.98)
            return failCall(output, capacity, ok, "bore does not fit inside the root.");
        SimplePolygon contour;
        if (!gearTooth(teeth, module, pressure, addendum, dedendum, backlash, flankSteps,
                       &contour, &call.error))
            return failCall(output, capacity, ok, call.error);
        const CrossSection section(contour, CrossSection::FillRule::Positive);
        if (section.IsEmpty() || section.Area() <= 0)
            return failCall(output, capacity, ok, "gear profile is self-intersecting.");
        int divisions = 0;
        bool divisionsSet = false;
        if (!readInt(call.root, "divisions", &divisions, &divisionsSet, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (!divisionsSet && std::fabs(helix) > 0.01)
            divisions = 8;
        if (divisions < 0 || divisions > 64)
            return failCall(output, capacity, ok, "divisions must be from 0 to 64.");
        const Manifold tooth = Manifold::Extrude(
            section, (float)width, divisions, (float)helix, glm::vec2((float)scale));
        const double overlap = module * 0.15;
        const float rootLow = (float)(rootRadius + overlap);
        const float rootTop = (float)((rootRadius + overlap) * scale);
        Manifold gear = Manifold::Cylinder((float)width, rootLow, rootTop, 0, false);
        for (int i = 0; i < teeth; ++i)
        {
            const float angle = 360.0f * (float)i / (float)teeth;
            gear = gear + tooth.Rotate(0, 0, angle);
        }
        if (bore > 0)
            gear = gear - Manifold::Cylinder((float)width, (float)bore, (float)bore, 0, false);
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        return finishSolid(gear, dest, output, capacity, ok);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

int invokePattern(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        std::string source;
        if (!requirePath(call.root, "path", &source, &call.error))
            return failCall(output, capacity, ok, call.error);
        Manifold body;
        if (!loadSolid(source, &body, &call.error))
            return failCall(output, capacity, ok, call.error);
        int count = 0;
        bool present = false;
        if (!readInt(call.root, "count", &count, &present, &call.error) || !present)
            return failCall(output, capacity, ok, "count is required.");
        if (count < 2 || count > 360)
            return failCall(output, capacity, ok, "count must be from 2 to 360.");
        std::string axis = "z";
        if (!readText(call.root, "axis", &axis, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        for (size_t i = 0; i < axis.size(); ++i)
        {
            if (axis[i] >= 'A' && axis[i] <= 'Z')
                axis[i] = (char)(axis[i] - 'A' + 'a');
        }
        if (axis != "x" && axis != "y" && axis != "z")
            return failCall(output, capacity, ok, "axis must be x, y, or z.");
        double angle = 360;
        if (!readNumber(call.root, "angle_deg", &angle, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (angle == 0)
            return failCall(output, capacity, ok, "angle_deg must not be 0.");
        glm::vec3 center(0);
        const Json *centerValue = optionalField(call.root, "center");
        if (centerValue && !readVec3Value(*centerValue, &center, &call.error, "center"))
            return failCall(output, capacity, ok, call.error);
        bool unite = true;
        if (!readBool(call.root, "union", &unite, &call.error))
            return failCall(output, capacity, ok, call.error);
        const double step = angle / (double)count;
        std::vector<Manifold> copies;
        copies.reserve((size_t)count);
        for (int i = 0; i < count; ++i)
        {
            const float turn = (float)(step * (double)i);
            const float rx = axis == "x" ? turn : 0;
            const float ry = axis == "y" ? turn : 0;
            const float rz = axis == "z" ? turn : 0;
            copies.push_back(body.Translate(-center).Rotate(rx, ry, rz).Translate(center));
        }
        const Manifold result = unite
            ? Manifold::BatchBoolean(copies, OpType::Add)
            : Manifold::Compose(copies);
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        return finishSolid(result, dest, output, capacity, ok, unite);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

int invokeSweep(const char *argumentsJson, char *output, int capacity, int *ok)
{
    try
    {
        Call call;
        if (!beginCall(argumentsJson, &call))
            return failCall(output, capacity, ok, call.error);
        const Json *pathValue = optionalField(call.root, "path");
        double helixRadius = 0;
        bool helixPresent = false;
        if (!readNumber(call.root, "helix_radius", &helixRadius, &helixPresent, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (pathValue && helixPresent)
            return failCall(output, capacity, ok, "Give path or a helix, not both.");
        int segments = 24;
        bool present = false;
        if (!readInt(call.root, "segments", &segments, &present, &call.error))
            return failCall(output, capacity, ok, call.error);
        if (!present || segments == 0)
            segments = 24;
        if (segments < 3 || segments > 256)
            return failCall(output, capacity, ok, "segments must be from 3 to 256.");
        std::vector<glm::vec3> points;
        if (pathValue)
        {
            if (pathValue->kind != Json::Array || pathValue->items.size() < 2)
                return failCall(output, capacity, ok, "path needs at least 2 points [x, y, z].");
            if ((int)pathValue->items.size() > 4000)
                return failCall(output, capacity, ok, "path has too many points (limit 4000).");
            for (size_t i = 0; i < pathValue->items.size(); ++i)
            {
                glm::vec3 point(0);
                if (!readVec3Value(pathValue->items[i], &point, &call.error, "path"))
                    return failCall(output, capacity, ok, call.error);
                if (!points.empty() && glm::length(point - points.back()) < 1e-6f)
                    continue;
                points.push_back(point);
            }
        }
        else if (helixPresent)
        {
            if (helixRadius <= 0)
                return failCall(output, capacity, ok, "helix_radius must be positive.");
            double pitch = 0;
            if (!readNumber(call.root, "pitch", &pitch, &present, &call.error) || !present)
                return failCall(output, capacity, ok, "pitch is required with helix_radius.");
            double turns = 0;
            if (!readNumber(call.root, "turns", &turns, &present, &call.error) || !present || turns <= 0)
                return failCall(output, capacity, ok, "turns must be positive.");
            if (turns > 64)
                return failCall(output, capacity, ok, "turns must be at most 64.");
            const int count = (int)(turns * (double)segments) + 1;
            if (count < 2 || count > 4000)
                return failCall(output, capacity, ok, "helix has too many points (limit 4000).");
            points.reserve((size_t)count);
            for (int i = 0; i < count; ++i)
            {
                const double t = turns * (double)i / (double)(count - 1);
                const double angle = 2.0 * kPi * t;
                points.push_back(glm::vec3((float)(helixRadius * std::cos(angle)),
                                           (float)(helixRadius * std::sin(angle)),
                                           (float)(pitch * t)));
            }
        }
        else
            return failCall(output, capacity, ok, "path or helix_radius is required.");
        if (points.size() < 2)
            return failCall(output, capacity, ok, "path needs at least 2 distinct points.");
        bool closed = glm::length(points.back() - points.front()) < 1e-5f;
        if (closed)
        {
            points.pop_back();
            if (points.size() < 3)
                return failCall(output, capacity, ok, "a closed path needs at least 3 points.");
        }
        double profileRadius = 0;
        bool radiusPresent = false;
        if (!readNumber(call.root, "profile_radius", &profileRadius, &radiusPresent, &call.error))
            return failCall(output, capacity, ok, call.error);
        const Json *polygon = optionalField(call.root, "polygon");
        if (radiusPresent && polygon)
            return failCall(output, capacity, ok, "Give polygon or profile_radius, not both.");
        std::vector<glm::vec2> profile;
        if (radiusPresent)
        {
            if (profileRadius <= 0)
                return failCall(output, capacity, ok, "profile_radius must be positive.");
            profile.reserve((size_t)segments);
            for (int i = 0; i < segments; ++i)
            {
                const double angle = 2.0 * kPi * (double)i / (double)segments;
                profile.push_back(glm::vec2((float)(profileRadius * std::cos(angle)),
                                            (float)(profileRadius * std::sin(angle))));
            }
        }
        else if (polygon)
        {
            SimplePolygon contour;
            if (!readContour(*polygon, &contour, &call.error))
                return failCall(output, capacity, ok, call.error);
            if ((int)contour.size() > 256)
                return failCall(output, capacity, ok, "polygon has too many points (limit 256).");
            profile.assign(contour.begin(), contour.end());
        }
        else
            return failCall(output, capacity, ok, "polygon or profile_radius is required.");
        const int rings = (int)points.size();
        const int around = (int)profile.size();
        const int wall = (closed ? rings : rings - 1) * around * 2;
        const int caps = closed ? 0 : around * 2;
        if (wall + caps > kMaxTriangles)
            return failCall(output, capacity, ok, "Sweep has too many triangles (limit 2000000).");
        std::vector<glm::vec3> tangent((size_t)rings);
        if (closed)
        {
            for (int i = 0; i < rings; ++i)
            {
                const glm::vec3 next = points[(size_t)((i + 1) % rings)];
                glm::vec3 dir = next - points[(size_t)i];
                if (glm::length(dir) < 1e-6f)
                    return failCall(output, capacity, ok, "path has a zero-length span.");
                tangent[(size_t)i] = glm::normalize(dir);
            }
        }
        else
        {
            for (int i = 0; i < rings - 1; ++i)
            {
                glm::vec3 dir = points[(size_t)i + 1] - points[(size_t)i];
                if (glm::length(dir) < 1e-6f)
                    return failCall(output, capacity, ok, "path has a zero-length span.");
                tangent[(size_t)i] = glm::normalize(dir);
            }
            tangent[(size_t)rings - 1] = tangent[(size_t)rings - 2];
        }
        std::vector<glm::vec3> normal((size_t)rings);
        std::vector<glm::vec3> binormal((size_t)rings);
        glm::vec3 helper(0, 0, 1);
        if (std::fabs(glm::dot(tangent[0], helper)) > 0.9f)
            helper = glm::vec3(1, 0, 0);
        glm::vec3 current = glm::normalize(glm::cross(tangent[0], helper));
        for (int i = 0; i < rings; ++i)
        {
            if (i > 0)
            {
                const glm::vec3 axisRaw = glm::cross(tangent[(size_t)i - 1], tangent[(size_t)i]);
                const float axisLength = glm::length(axisRaw);
                if (axisLength > 1e-6f)
                {
                    const float turn = std::atan2(axisLength,
                                                   glm::dot(tangent[(size_t)i - 1], tangent[(size_t)i]));
                    current = rotateAround(current, axisRaw / axisLength, turn);
                }
                current = current - tangent[(size_t)i] * glm::dot(current, tangent[(size_t)i]);
                if (glm::length(current) < 1e-6f)
                    return failCall(output, capacity, ok, "path frame collapsed.");
                current = glm::normalize(current);
            }
            normal[(size_t)i] = current;
            binormal[(size_t)i] = glm::cross(tangent[(size_t)i], current);
        }
        std::vector<glm::vec3> ring((size_t)rings * (size_t)around);
        for (int i = 0; i < rings; ++i)
        {
            for (int s = 0; s < around; ++s)
            {
                const glm::vec2 offset = profile[(size_t)s];
                ring[(size_t)i * (size_t)around + (size_t)s] =
                    points[(size_t)i]
                    + normal[(size_t)i] * offset.x
                    + binormal[(size_t)i] * offset.y;
            }
        }
        Soup soup;
        soup.xyz.reserve((size_t)(wall + caps) * 9);
        const int spans = closed ? rings : rings - 1;
        for (int i = 0; i < spans; ++i)
        {
            const int next = (i + 1) % rings;
            for (int s = 0; s < around; ++s)
            {
                const int sn = (s + 1) % around;
                const glm::vec3 p00 = ring[(size_t)i * (size_t)around + (size_t)s];
                const glm::vec3 p01 = ring[(size_t)i * (size_t)around + (size_t)sn];
                const glm::vec3 p10 = ring[(size_t)next * (size_t)around + (size_t)s];
                const glm::vec3 p11 = ring[(size_t)next * (size_t)around + (size_t)sn];
                pushTri(&soup, p00, p01, p11);
                pushTri(&soup, p00, p11, p10);
            }
        }
        if (!closed)
        {
            glm::vec3 start(0);
            glm::vec3 end(0);
            for (int s = 0; s < around; ++s)
            {
                start += ring[(size_t)s];
                end += ring[(size_t)(rings - 1) * (size_t)around + (size_t)s];
            }
            start /= (float)around;
            end /= (float)around;
            for (int s = 0; s < around; ++s)
            {
                const int sn = (s + 1) % around;
                pushTri(&soup, start,
                        ring[(size_t)sn],
                        ring[(size_t)s]);
                pushTri(&soup, end,
                        ring[(size_t)(rings - 1) * (size_t)around + (size_t)s],
                        ring[(size_t)(rings - 1) * (size_t)around + (size_t)sn]);
            }
        }
        Manifold body;
        if (!soupToSolid(soup, &body, &call.error))
            return failCall(output, capacity, ok, call.error);
        std::string dest;
        if (!requirePath(call.root, "output", &dest, &call.error))
            return failCall(output, capacity, ok, call.error);
        return finishSolid(body, dest, output, capacity, ok);
    }
    catch (const std::exception &ex)
    {
        return failCall(output, capacity, ok, ex.what());
    }
}

//----------------------------------------------

const char kSchemaCube[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"size\":{\"description\":\"Edge length, or [x, y, z]. Default 1.\"},"
    "\"center\":{\"type\":\"boolean\",\"description\":\"Center the solid on the origin.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .stl, .obj, .step, or .stp file.\"}"
    "},\"required\":[\"output\"]}";

const char kSchemaSphere[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"radius\":{\"type\":\"number\"},"
    "\"segments\":{\"type\":\"integer\",\"description\":\"Segments around a diameter. 0 picks a default.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .stl, .obj, .step, or .stp file.\"}"
    "},\"required\":[\"radius\",\"output\"]}";

const char kSchemaCylinder[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"height\":{\"type\":\"number\"},"
    "\"radius\":{\"type\":\"number\",\"description\":\"Radius at the bottom.\"},"
    "\"radius_high\":{\"type\":\"number\",\"description\":\"Radius at the top. Default matches radius. 0 makes a cone.\"},"
    "\"segments\":{\"type\":\"integer\",\"description\":\"Segments around the circle. 0 picks a default.\"},"
    "\"center\":{\"type\":\"boolean\",\"description\":\"Center the solid along Z.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .stl, .obj, .step, or .stp file.\"}"
    "},\"required\":[\"height\",\"radius\",\"output\"]}";

const char kSchemaTetra[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"size\":{\"description\":\"Scale of the unit tetrahedron, a number or [x, y, z].\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .stl, .obj, .step, or .stp file.\"}"
    "},\"required\":[\"output\"]}";

const char kSchemaExtrude[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"polygon\":{\"type\":\"array\",\"description\":\"One contour [[x, y], ...].\"},"
    "\"polygons\":{\"type\":\"array\",\"description\":\"Contours. An inner contour becomes a hole.\"},"
    "\"path\":{\"type\":\"string\",\"description\":\"JSON profile with polygon or polygons.\"},"
    "\"height\":{\"type\":\"number\",\"description\":\"Extrusion length along Z.\"},"
    "\"divisions\":{\"type\":\"integer\",\"description\":\"Extra cross-sections. Default 0.\"},"
    "\"twist_deg\":{\"type\":\"number\",\"description\":\"Twist of the top, degrees.\"},"
    "\"scale_top\":{\"description\":\"Top scale, a number or [x, y]. [0, 0] makes a cone.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .stl, .obj, .step, or .stp file.\"}"
    "},\"required\":[\"height\",\"output\"]}";

const char kSchemaRevolve[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"polygon\":{\"type\":\"array\",\"description\":\"Contour in XY. Positive X is the radius.\"},"
    "\"polygons\":{\"type\":\"array\"},"
    "\"path\":{\"type\":\"string\",\"description\":\"JSON profile.\"},"
    "\"degrees\":{\"type\":\"number\",\"description\":\"Revolution angle. Default 360.\"},"
    "\"segments\":{\"type\":\"integer\",\"description\":\"Segments around the circle. 0 picks a default.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .stl, .obj, .step, or .stp file.\"}"
    "},\"required\":[\"output\"]}";

const char kSchemaBoolean[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"op\":{\"type\":\"string\",\"description\":\"union, difference, or intersection.\"},"
    "\"inputs\":{\"type\":\"array\",\"description\":\"STL, OBJ, or tessellated STEP paths. Difference subtracts every file after the first.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .stl, .obj, .step, or .stp file.\"}"
    "},\"required\":[\"inputs\",\"output\"]}";

const char kSchemaHull[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"inputs\":{\"type\":\"array\",\"description\":\"STL, OBJ, or tessellated STEP paths.\"},"
    "\"points\":{\"type\":\"array\",\"description\":\"Points [x, y, z]. Used instead of inputs when present.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .stl, .obj, .step, or .stp file.\"}"
    "},\"required\":[\"output\"]}";

const char kSchemaPlace[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"STL, OBJ, or tessellated STEP solid.\"},"
    "\"translate\":{\"type\":\"array\",\"description\":\"Shift [x, y, z] applied last.\"},"
    "\"rotate_deg\":{\"type\":\"array\",\"description\":\"Degrees [x, y, z]: X, then Y, then Z.\"},"
    "\"scale\":{\"description\":\"One number or [x, y, z].\"},"
    "\"center\":{\"type\":\"string\",\"description\":\"origin or bbox. bbox rotates and scales about the bounding-box center.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .stl, .obj, .step, or .stp file.\"}"
    "},\"required\":[\"path\",\"output\"]}";

const char kSchemaMirror[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\"},"
    "\"normal\":{\"type\":\"array\",\"description\":\"Normal of the mirror plane through the origin.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .stl, .obj, .step, or .stp file.\"}"
    "},\"required\":[\"path\",\"normal\",\"output\"]}";

const char kSchemaSlice[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"STL, OBJ, or tessellated STEP solid.\"},"
    "\"height\":{\"type\":\"number\",\"description\":\"Z of the cutting plane. Default 0.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .json or .svg contour.\"}"
    "},\"required\":[\"path\",\"output\"]}";

const char kSchemaProject[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .json or .svg silhouette on XY.\"}"
    "},\"required\":[\"path\",\"output\"]}";

const char kSchemaRefine[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\"},"
    "\"mode\":{\"type\":\"string\",\"description\":\"count splits every edge into value pieces. length keeps pieces about value long.\"},"
    "\"value\":{\"type\":\"number\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .stl, .obj, .step, or .stp file.\"}"
    "},\"required\":[\"path\",\"value\",\"output\"]}";

const char kSchemaSmooth[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\"},"
    "\"angle_deg\":{\"type\":\"number\",\"description\":\"Edges sharper than this stay sharp. Default 60.\"},"
    "\"smoothness\":{\"type\":\"number\",\"description\":\"0 to 1, applied to sharp edges. Default 0.\"},"
    "\"refine\":{\"type\":\"integer\",\"description\":\"Subdivide each edge into this many pieces so the rounding is visible. Default 2.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .stl, .obj, .step, or .stp file.\"}"
    "},\"required\":[\"path\",\"output\"]}";

const char kSchemaSolid[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"STL, OBJ, or tessellated STEP solid.\"},"
    "\"other\":{\"type\":\"string\",\"description\":\"Second solid. When set, gap is the separation, or 0 when the solids overlap.\"},"
    "\"search\":{\"type\":\"number\",\"description\":\"Largest gap to measure. Default is the sum of the bounding-box diagonals.\"}"
    "},\"required\":[\"path\"]}";

const char kSchemaTrim[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\"},"
    "\"normal\":{\"type\":\"array\",\"description\":\"Plane normal. Default [0, 0, 1].\"},"
    "\"offset\":{\"type\":\"number\",\"description\":\"Distance of the plane from the origin along the normal.\"},"
    "\"mode\":{\"type\":\"string\",\"description\":\"trim keeps the positive side. split writes both sides.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .stl, .obj, .step, or .stp file for the positive side.\"},"
    "\"output_b\":{\"type\":\"string\",\"description\":\"Negative side for split. Default adds _b before the extension.\"}"
    "},\"required\":[\"path\",\"output\"]}";

const char kSchemaOffset[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"polygon\":{\"type\":\"array\"},"
    "\"polygons\":{\"type\":\"array\"},"
    "\"path\":{\"type\":\"string\",\"description\":\"JSON profile.\"},"
    "\"delta\":{\"type\":\"number\",\"description\":\"Offset distance. Negative insets.\"},"
    "\"join\":{\"type\":\"string\",\"description\":\"miter, round, or square. Default miter.\"},"
    "\"miter_limit\":{\"type\":\"number\",\"description\":\"Default 2.\"},"
    "\"segments\":{\"type\":\"integer\",\"description\":\"Segments for round joins. 0 picks a default.\"},"
    "\"height\":{\"type\":\"number\",\"description\":\"When set, extrude the offset contour.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\".json or .svg, or .stl, .obj, .step, or .stp when height is set.\"}"
    "},\"required\":[\"delta\",\"output\"]}";

const char kSchemaProfile[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"op\":{\"type\":\"string\",\"description\":\"union, difference, or intersection.\"},"
    "\"polygons\":{\"type\":\"array\"},"
    "\"polygons_b\":{\"type\":\"array\",\"description\":\"Second contour list.\"},"
    "\"inputs\":{\"type\":\"array\",\"description\":\"JSON profile paths.\"},"
    "\"epsilon\":{\"type\":\"number\",\"description\":\"Simplify tolerance. 0 keeps the exact contour.\"},"
    "\"height\":{\"type\":\"number\",\"description\":\"When set, extrude the result.\"},"
    "\"output\":{\"type\":\"string\"}"
    "},\"required\":[\"output\"]}";

const char kSchemaDecompose[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"STL, OBJ, or tessellated STEP solid.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"File stem. Parts are written as stem_0, stem_1, and so on.\"}"
    "},\"required\":[\"path\",\"output\"]}";

const char kSchemaCircle[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"radius\":{\"type\":\"number\"},"
    "\"segments\":{\"type\":\"integer\",\"description\":\"0 picks a default.\"},"
    "\"height\":{\"type\":\"number\",\"description\":\"When set, extrude the circle.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\".json or .svg, or a mesh when height is set.\"}"
    "},\"required\":[\"radius\",\"output\"]}";

const char kSchemaSquare[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"size\":{\"description\":\"Side, or [width, height]. Default 1.\"},"
    "\"center\":{\"type\":\"boolean\"},"
    "\"height\":{\"type\":\"number\",\"description\":\"When set, extrude the rectangle.\"},"
    "\"output\":{\"type\":\"string\"}"
    "},\"required\":[\"output\"]}";

const char kSchemaGear[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"module\":{\"type\":\"number\",\"description\":\"Module. Tooth size is module times the coefficients.\"},"
    "\"teeth\":{\"type\":\"integer\",\"description\":\"External tooth count, from 4 to 200.\"},"
    "\"width\":{\"type\":\"number\",\"description\":\"Face width along +Z. The base sits at Z=0.\"},"
    "\"pressure_deg\":{\"type\":\"number\",\"description\":\"Pressure angle in degrees. Default 20.\"},"
    "\"addendum\":{\"type\":\"number\",\"description\":\"Addendum coefficient, fraction of module. Default 1. Not a length.\"},"
    "\"dedendum\":{\"type\":\"number\",\"description\":\"Dedendum coefficient, fraction of module. Default 1.25. Not a length.\"},"
    "\"backlash\":{\"type\":\"number\",\"description\":\"Arc backlash at the pitch circle. Default 0.\"},"
    "\"helix_deg\":{\"type\":\"number\",\"description\":\"Twist of the tooth over the face width, in degrees. Default 0.\"},"
    "\"bevel_deg\":{\"type\":\"number\",\"description\":\"Pitch-cone angle. 0 is a spur gear.\"},"
    "\"bore\":{\"type\":\"number\",\"description\":\"Hole radius. 0 leaves a solid hub.\"},"
    "\"flank_steps\":{\"type\":\"integer\",\"description\":\"Points along one involute flank. Default 6.\"},"
    "\"divisions\":{\"type\":\"integer\",\"description\":\"Extra sections along the width. Default 8 when helix_deg is set.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .stl, .obj, .step, or .stp file. STEP is a triangle solid.\"}"
    "},\"required\":[\"module\",\"teeth\",\"width\",\"output\"]}";

const char kSchemaPattern[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"path\":{\"type\":\"string\",\"description\":\"STL, OBJ, or tessellated STEP solid.\"},"
    "\"count\":{\"type\":\"integer\",\"description\":\"Number of copies, from 2 to 360.\"},"
    "\"axis\":{\"type\":\"string\",\"description\":\"x, y, or z. Default z.\"},"
    "\"angle_deg\":{\"type\":\"number\",\"description\":\"Total angle. Default 360. The copy at the full turn is not repeated.\"},"
    "\"center\":{\"description\":\"Rotation center [x, y, z]. Default 0.\"},"
    "\"union\":{\"type\":\"boolean\",\"description\":\"Fuse the copies into one solid. Default true.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .stl, .obj, .step, or .stp file.\"}"
    "},\"required\":[\"path\",\"count\",\"output\"]}";

const char kSchemaSweep[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"polygon\":{\"type\":\"array\",\"description\":\"Profile [[x, y], ...] in the plane normal to the path start. x is the normal, y is the binormal.\"},"
    "\"profile_radius\":{\"type\":\"number\",\"description\":\"Circle profile. Do not set it together with polygon.\"},"
    "\"path\":{\"type\":\"array\",\"description\":\"Polyline [[x, y, z], ...], at least 2 points. A repeated first point closes it.\"},"
    "\"helix_radius\":{\"type\":\"number\",\"description\":\"Helix radius. Do not set it together with path.\"},"
    "\"pitch\":{\"type\":\"number\",\"description\":\"Rise per turn. Required with helix_radius.\"},"
    "\"turns\":{\"type\":\"number\",\"description\":\"Number of turns. Required with helix_radius.\"},"
    "\"segments\":{\"type\":\"integer\",\"description\":\"Circle segments, or points per helix turn. Default 24.\"},"
    "\"output\":{\"type\":\"string\",\"description\":\"New .stl, .obj, .step, or .stp file.\"}"
    "},\"required\":[\"output\"]}";

struct ToolDef
{
    const char         *id;
    const char         *title;
    const char         *description;
    const char         *schema;
    UniqueToolInvokeFn invoke;
};

const ToolDef kTools[] = {
    { "cad_cube", "Куб",
      "Строит прямоугольный параллелепипед ядром Manifold и записывает STL, OBJ или мозаичный STEP. "
      "size — одно число или [x, y, z], по умолчанию 1. center=true ставит центр в ноль, "
      "иначе минимальный угол лежит в нуле. output — новый файл.",
      kSchemaCube, invokeCube },
    { "cad_sphere", "Сфера",
      "Строит сферу. radius — радиус. segments — число сегментов по диаметру, 0 выбирает само. "
      "output — новый STL, OBJ или мозаичный STEP.",
      kSchemaSphere, invokeSphere },
    { "cad_cylinder", "Цилиндр",
      "Строит цилиндр или конус вдоль Z. height — высота. radius — нижний радиус. "
      "radius_high — верхний радиус, по умолчанию равен нижнему, 0 даёт конус. "
      "segments — сегменты окружности, 0 выбирает само. center=true центрирует тело по высоте.",
      kSchemaCylinder, invokeCylinder },
    { "cad_tetra", "Тетраэдр",
      "Строит тетраэдр Manifold и масштабирует его. size — число или [x, y, z], по умолчанию 1.",
      kSchemaTetra, invokeTetra },
    { "cad_extrude", "Выдавливание",
      "Выдавливает плоский контур вдоль Z. polygon — один контур [[x, y], ...], polygons — несколько "
      "контуров, внутренний контур становится отверстием. path может указать JSON с этими полями. "
      "height — высота. divisions — дополнительные сечения. twist_deg — закрутка верха в градусах. "
      "scale_top — масштаб верха, число или [x, y]; [0, 0] даёт конус. Контуры не должны накладываться.",
      kSchemaExtrude, invokeExtrude },
    { "cad_revolve", "Вращение",
      "Вращает контур вокруг оси Y и делает эту ось осью Z готового тела. Контур лежит в плоскости XY, "
      "радиус — положительный X. Часть левее оси Y отбрасывается. degrees — угол, по умолчанию 360. "
      "segments — сегменты по окружности, 0 выбирает само.",
      kSchemaRevolve, invokeRevolve },
    { "cad_boolean", "Булева операция",
      "Объединяет, вычитает или пересекает несколько твёрдых тел. op: union, difference или intersection. "
      "inputs — список STL, OBJ или мозаичного STEP, до 128 файлов. При difference из первого тела вычитаются остальные. "
      "Пустое пересечение даёт пустое тело. Если в ответе shells больше 1, детали не пересекаются и остались отдельными: "
      "сдвинь их так, чтобы они входили друг в друга, и повтори. Это не поломка ядра. output — новый файл.",
      kSchemaBoolean, invokeBoolean },
    { "cad_hull", "Выпуклая оболочка",
      "Строит выпуклую оболочку сеток из inputs или точек points как списка [x, y, z]. "
      "Если заданы points, оболочки сеток не используются. output — новый STL, OBJ или мозаичный STEP.",
      kSchemaHull, invokeHull },
    { "cad_place", "Положение тела",
      "Переносит, поворачивает и масштабирует твёрдое тело. translate — сдвиг [x, y, z], применяется последним. "
      "rotate_deg — градусы [x, y, z]: сначала X, затем Y, затем Z. scale — число или [x, y, z]. "
      "center=bbox вращает и масштабирует вокруг центра габарита, center=origin — вокруг нуля. "
      "Исходный файл не меняется.",
      kSchemaPlace, invokePlace },
    { "cad_mirror", "Отражение",
      "Отражает тело относительно плоскости, проходящей через ноль. normal — нормаль этой плоскости. "
      "Нулевая нормаль недопустима.",
      kSchemaMirror, invokeMirror },
    { "cad_slice", "Слой",
      "Пересекает тело плоскостью постоянной Z и записывает контуры. height — высота плоскости, по умолчанию 0. "
      "output с окончанием .json хранит полигоны, .svg — чертёж. В ответе площадь и число контуров.",
      kSchemaSlice, invokeSlice },
    { "cad_project", "Проекция",
      "Проецирует тело на плоскость XY и записывает внешний контур. output — .json или .svg.",
      kSchemaProject, invokeProject },
    { "cad_refine", "Уточнение сетки",
      "Делит рёбра сетки. mode=count и value=n делят каждое ребро на n частей. "
      "mode=length и value задают примерную длину куска. Если у тела уже есть сглаживание, новые вершины лежат на сглаженной поверхности.",
      kSchemaRefine, invokeRefine },
    { "cad_smooth", "Сглаживание",
      "Скругляет рёбра и затем уточняет сетку, чтобы скругление стало видно. "
      "angle_deg — угол между нормалями, острее которого ребро остаётся острым, по умолчанию 60. "
      "smoothness от 0 до 1 задаёт скругление острых рёбер. refine — на сколько частей делить ребро, по умолчанию 2.",
      kSchemaSmooth, invokeSmooth },
    { "cad_solid", "Свойства тела",
      "Сообщает объём, площадь, род поверхности, габарит, число треугольников и статус тела. "
      "other — второе тело: тогда gap — зазор между ними, а 0 означает пересечение или касание. "
      "search — наибольший зазор, который ещё измеряется.",
      kSchemaSolid, invokeSolid },
    { "cad_trim", "Обрезка плоскостью",
      "mode=trim оставляет часть тела со стороны нормали. mode=split пишет обе части: output и output_b. "
      "normal — нормаль плоскости, по умолчанию [0, 0, 1]. offset — расстояние плоскости от нуля вдоль нормали.",
      kSchemaTrim, invokeTrim },
    { "cad_offset", "Смещение контура",
      "Смещает плоский контур на delta. Отрицательное delta уменьшает контур. join: miter, round или square, "
      "по умолчанию miter. miter_limit по умолчанию 2. polygon, polygons или path — исходный контур. "
      "Если задан height, результат выдавливается в тело, иначе пишется .json или .svg.",
      kSchemaOffset, invokeOffset },
    { "cad_profile", "Контур",
      "Булева операция плоских контуров. op: union, difference или intersection. "
      "Можно дать polygons и polygons_b либо список inputs из JSON-профилей. epsilon упрощает контур. "
      "height выдавливает результат в тело, иначе пишется контур.",
      kSchemaProfile, invokeProfile },
    { "cad_decompose", "Разбор тела",
      "Разделяет несвязанные оболочки на отдельные файлы. output — путь-основа: части получают суффиксы _0, _1 "
      "перед расширением. В ответе число частей.",
      kSchemaDecompose, invokeDecompose },
    { "cad_circle", "Круг",
      "Строит круг. radius — радиус, segments — сегменты, 0 выбирает само. "
      "Если задан height, круг выдавливается в цилиндр и пишется сетка. Иначе output — .json или .svg.",
      kSchemaCircle, invokeCircle },
    { "cad_square", "Прямоугольник",
      "Строит прямоугольник. size — сторона или [ширина, высота], center ставит его в ноль. "
      "Если задан height, прямоугольник выдавливается в призму. Иначе пишется контур.",
      kSchemaSquare, invokeSquare },
    { "cad_gear", "Шестерня",
      "Строит внешнюю эвольвентную шестерню. module — модуль. teeth — число зубьев, от 4 до 200. "
      "width — ширина вдоль +Z, основание в нуле. pressure_deg — угол зацепления, по умолчанию 20. "
      "addendum и dedendum — коэффициенты головки и ножки в долях модуля, по умолчанию 1 и 1.25, это не длины. "
      "backlash — зазор по дуге делительной окружности. helix_deg — закрутка зуба на ширине. "
      "bevel_deg — угол делительного конуса, 0 даёт цилиндрическую шестерню. bore — радиус отверстия, 0 без отверстия. "
      "output — новый STL, OBJ или мозаичный STEP. Файл .step — сетка треугольников, его читают эти функции. "
      "cad_inspect читает STL, OBJ и DXF.",
      kSchemaGear, invokeGear },
    { "cad_pattern", "Круговой массив",
      "Копирует тело по окружности вокруг оси. path — исходная сетка. count — число копий, от 2. "
      "axis — x, y или z, по умолчанию z. angle_deg — полный угол, по умолчанию 360, копия на полном обороте не повторяется. "
      "center — центр [x, y, z]. union — объединить копии в одно тело, по умолчанию true. "
      "Если копии не касаются, shells больше 1: они остались отдельными. "
      "output — STL, OBJ или мозаичный STEP.",
      kSchemaPattern, invokePattern },
    { "cad_sweep", "Протяжка",
      "Протягивает профиль вдоль пути и закрывает торцы. polygon — контур [[x, y], ...] в плоскости, "
      "перпендикулярной началу пути: x вдоль нормали, y вдоль бинормали. profile_radius — круг вместо контура. "
      "path — ломаная [[x, y, z], ...]. Вместо path можно задать helix_radius, pitch (подъём за оборот) и turns. "
      "Одновременно путь и спираль задавать нельзя. segments — сегменты круга или точки на оборот, по умолчанию 24. "
      "output — STL, OBJ или мозаичный STEP.",
      kSchemaSweep, invokeSweep },
};

//----------------------------------------------

extern "C" MANIFOLD_API int unique_plugin_abi(void)
{
    return UNIQUE_TOOL_PLUGIN_ABI;
}

extern "C" MANIFOLD_API const char *unique_plugin_id(void)
{
    return "manifold";
}

extern "C" MANIFOLD_API const char *unique_plugin_title(void)
{
    return "CAD";
}

extern "C" MANIFOLD_API int unique_plugin_tool_count(void)
{
    return (int)(sizeof(kTools) / sizeof(kTools[0]));
}

extern "C" MANIFOLD_API int unique_plugin_tool(int index, UniqueToolSpec *spec)
{
    if (!spec || index < 0 || index >= unique_plugin_tool_count())
        return 0;
    spec->id = kTools[index].id;
    spec->title = kTools[index].title;
    spec->description = kTools[index].description;
    spec->schemaJson = kTools[index].schema;
    spec->invoke = kTools[index].invoke;
    return 1;
}
