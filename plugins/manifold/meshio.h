#ifndef MESHIO_H
#define MESHIO_H

//----------------------------------------------

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

//----------------------------------------------

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

//----------------------------------------------

struct Soup
{
    std::vector<float> xyz;

    int count() const
    {
        return (int)(xyz.size() / 9);
    }
};

//----------------------------------------------

static const int kMaxTriangles = 2000000;
static const std::uintmax_t kMaxFileBytes = 64 * 1024 * 1024;

//----------------------------------------------

inline std::filesystem::path fsPath(const std::string &utf8)
{
#ifdef _WIN32
    if (utf8.empty())
        return std::filesystem::path();
    const int count = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, 0, 0);
    if (count <= 1)
        return std::filesystem::path();
    std::wstring wide((size_t)count - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], count);
    return std::filesystem::path(wide);
#else
    return std::filesystem::path(utf8);
#endif
}

//----------------------------------------------

inline std::string lowerExt(const std::string &path)
{
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return std::string();
    std::string ext = path.substr(dot);
    for (size_t i = 0; i < ext.size(); ++i)
    {
        if (ext[i] >= 'A' && ext[i] <= 'Z')
            ext[i] = (char)(ext[i] - 'A' + 'a');
    }
    return ext;
}

//----------------------------------------------

inline bool readBytes(const std::string &path, std::vector<char> *data, std::string *error)
{
    std::error_code code;
    const std::filesystem::path file = fsPath(path);
    if (!std::filesystem::is_regular_file(file, code))
    {
        if (error)
            *error = "File not found: " + path;
        return false;
    }
    const std::uintmax_t size = std::filesystem::file_size(file, code);
    if (code || size > kMaxFileBytes)
    {
        if (error)
            *error = "File is larger than 67108864 bytes.";
        return false;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        if (error)
            *error = "Cannot open " + path;
        return false;
    }
    data->resize((size_t)size);
    if (size && !in.read(data->data(), (std::streamsize)size))
    {
        if (error)
            *error = "Cannot read " + path;
        return false;
    }
    return true;
}

//----------------------------------------------

inline bool ensureParent(const std::string &path, std::string *error)
{
    const std::filesystem::path file = fsPath(path);
    const std::filesystem::path parent = file.parent_path();
    if (parent.empty())
        return true;
    std::error_code code;
    std::filesystem::create_directories(parent, code);
    if (code)
    {
        if (error)
            *error = "Cannot create folder for " + path;
        return false;
    }
    return true;
}

//----------------------------------------------

inline float readF32(const unsigned char *bytes)
{
    float value = 0;
    std::memcpy(&value, bytes, 4);
    return value;
}

inline void writeF32(std::ostream &out, float value)
{
    out.write((const char *)&value, 4);
}

//----------------------------------------------

inline bool readStl(const std::vector<char> &data, Soup *soup, std::string *error)
{
    soup->xyz.clear();
    const int size = (int)data.size();
    if (size >= 84)
    {
        const unsigned char *bytes = (const unsigned char *)data.data();
        const uint32_t count = (uint32_t)bytes[80]
                               | ((uint32_t)bytes[81] << 8)
                               | ((uint32_t)bytes[82] << 16)
                               | ((uint32_t)bytes[83] << 24);
        const long long expect = 84ll + (long long)count * 50ll;
        if (expect == size && (count > 0 || size == 84))
        {
            if (count == 0)
                return true;
            if ((int)count > kMaxTriangles)
            {
                if (error)
                    *error = "Mesh has too many triangles (limit 2000000).";
                return false;
            }
            soup->xyz.reserve((size_t)count * 9);
            for (uint32_t i = 0; i < count; ++i)
            {
                const unsigned char *tri = bytes + 84 + (int)i * 50;
                for (int v = 0; v < 3; ++v)
                {
                    const unsigned char *xyz = tri + 12 + v * 12;
                    soup->xyz.push_back(readF32(xyz));
                    soup->xyz.push_back(readF32(xyz + 4));
                    soup->xyz.push_back(readF32(xyz + 8));
                }
            }
            return true;
        }
    }

    std::string text(data.begin(), data.end());
    std::stringstream in(text);
    std::string token;
    int count = 0;
    while (in >> token)
    {
        for (size_t i = 0; i < token.size(); ++i)
        {
            if (token[i] >= 'A' && token[i] <= 'Z')
                token[i] = (char)(token[i] - 'A' + 'a');
        }
        if (token != "vertex")
            continue;
        float xyz[3];
        for (int i = 0; i < 3; ++i)
        {
            if (!(in >> xyz[i]))
            {
                if (error)
                    *error = "STL vertex is incomplete.";
                return false;
            }
        }
        soup->xyz.push_back(xyz[0]);
        soup->xyz.push_back(xyz[1]);
        soup->xyz.push_back(xyz[2]);
        ++count;
        if (count / 3 > kMaxTriangles)
        {
            if (error)
                *error = "Mesh has too many triangles (limit 2000000).";
            return false;
        }
    }
    if (soup->xyz.size() < 9 || (soup->xyz.size() % 9) != 0)
    {
        if (error)
            *error = "STL has no triangles.";
        return false;
    }
    return true;
}

//----------------------------------------------

inline bool parseLooseDouble(const std::string &token, double *out)
{
    const char *begin = token.c_str();
    const char *end = begin + token.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, *out);
    return parsed.ec == std::errc() && parsed.ptr == end;
}

inline bool readObj(const std::vector<char> &data, Soup *soup, std::string *error)
{
    soup->xyz.clear();
    std::vector<float> verts;
    std::stringstream in(std::string(data.begin(), data.end()));
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::stringstream row(line);
        std::string head;
        if (!(row >> head))
            continue;
        if (head == "v")
        {
            double x = 0;
            double y = 0;
            double z = 0;
            std::string sx;
            std::string sy;
            std::string sz;
            if (!(row >> sx >> sy >> sz)
                || !parseLooseDouble(sx, &x)
                || !parseLooseDouble(sy, &y)
                || !parseLooseDouble(sz, &z))
            {
                if (error)
                    *error = "OBJ vertex is incomplete.";
                return false;
            }
            verts.push_back((float)x);
            verts.push_back((float)y);
            verts.push_back((float)z);
            continue;
        }
        if (head != "f")
            continue;
        std::vector<int> face;
        std::string corner;
        while (row >> corner)
        {
            const size_t slash = corner.find('/');
            const std::string indexText = slash == std::string::npos
                                              ? corner
                                              : corner.substr(0, slash);
            int index = 0;
            const std::from_chars_result parsed = std::from_chars(
                indexText.c_str(), indexText.c_str() + indexText.size(), index);
            if (parsed.ec != std::errc() || index == 0)
            {
                if (error)
                    *error = "OBJ face index is invalid.";
                return false;
            }
            if (index < 0)
                index = (int)(verts.size() / 3) + index + 1;
            face.push_back(index);
        }
        if (face.size() < 3)
        {
            if (error)
                *error = "OBJ face has fewer than 3 vertices.";
            return false;
        }
        for (size_t i = 1; i + 1 < face.size(); ++i)
        {
            const int ids[3] = { face[0], face[i], face[i + 1] };
            if (soup->count() + 1 > kMaxTriangles)
            {
                if (error)
                    *error = "Mesh has too many triangles (limit 2000000).";
                return false;
            }
            for (int v = 0; v < 3; ++v)
            {
                const int index = ids[v];
                if (index < 1 || (size_t)index > verts.size() / 3)
                {
                    if (error)
                        *error = "OBJ face index is out of range.";
                    return false;
                }
                const size_t base = (size_t)(index - 1) * 3;
                soup->xyz.push_back(verts[base]);
                soup->xyz.push_back(verts[base + 1]);
                soup->xyz.push_back(verts[base + 2]);
            }
        }
    }
    if (soup->xyz.size() < 9)
    {
        if (error)
            *error = "OBJ has no triangles.";
        return false;
    }
    return true;
}

//----------------------------------------------

inline bool readStep(const std::vector<char> &data, Soup *soup, std::string *error);

//----------------------------------------------

inline bool readMeshFile(const std::string &path, Soup *soup, std::string *error)
{
    const std::string ext = lowerExt(path);
    if (ext != ".stl" && ext != ".obj" && ext != ".step" && ext != ".stp")
    {
        if (error)
            *error = "Mesh path must end in .stl, .obj, .step, or .stp.";
        return false;
    }
    std::vector<char> data;
    if (!readBytes(path, &data, error))
        return false;
    if (ext == ".stl")
        return readStl(data, soup, error);
    if (ext == ".step" || ext == ".stp")
        return readStep(data, soup, error);
    return readObj(data, soup, error);
}

//----------------------------------------------

inline bool writeStl(const std::string &path, const Soup &soup, std::string *error)
{
    if (soup.count() > kMaxTriangles)
    {
        if (error)
            *error = "Mesh has too many triangles (limit 2000000).";
        return false;
    }
    if (!ensureParent(path, error))
        return false;
    std::ofstream out(fsPath(path), std::ios::binary);
    if (!out)
    {
        if (error)
            *error = "Cannot write " + path;
        return false;
    }
    char header[80];
    std::memset(header, 0, sizeof(header));
    std::memcpy(header, "manifold", 8);
    out.write(header, 80);
    const uint32_t count = (uint32_t)soup.count();
    out.write((const char *)&count, 4);
    const unsigned char pad[2] = { 0, 0 };
    for (int i = 0; i < soup.count(); ++i)
    {
        const float *p = &soup.xyz[(size_t)i * 9];
        const float ux = p[3] - p[0];
        const float uy = p[4] - p[1];
        const float uz = p[5] - p[2];
        const float vx = p[6] - p[0];
        const float vy = p[7] - p[1];
        const float vz = p[8] - p[2];
        writeF32(out, uy * vz - uz * vy);
        writeF32(out, uz * vx - ux * vz);
        writeF32(out, ux * vy - uy * vx);
        for (int k = 0; k < 9; ++k)
            writeF32(out, p[k]);
        out.write((const char *)pad, 2);
    }
    if (!out)
    {
        if (error)
            *error = "Cannot write " + path;
        return false;
    }
    return true;
}

//----------------------------------------------

inline bool writeObj(const std::string &path, const Soup &soup, std::string *error)
{
    if (soup.count() > kMaxTriangles)
    {
        if (error)
            *error = "Mesh has too many triangles (limit 2000000).";
        return false;
    }
    if (!ensureParent(path, error))
        return false;
    std::ofstream out(fsPath(path), std::ios::binary);
    if (!out)
    {
        if (error)
            *error = "Cannot write " + path;
        return false;
    }
    out << "o solid\n";
    for (int i = 0; i < soup.count(); ++i)
    {
        const float *p = &soup.xyz[(size_t)i * 9];
        for (int v = 0; v < 3; ++v)
        {
            char line[128];
            char *end = line + sizeof(line);
            char *cursor = line;
            const char *prefix = "v ";
            while (*prefix)
                *cursor++ = *prefix++;
            for (int k = 0; k < 3; ++k)
            {
                if (k)
                    *cursor++ = ' ';
                const std::to_chars_result formatted = std::to_chars(
                    cursor, end - 2, (double)p[v * 3 + k],
                    std::chars_format::general, 8);
                if (formatted.ec != std::errc())
                {
                    if (error)
                        *error = "Cannot format OBJ vertex.";
                    return false;
                }
                cursor = formatted.ptr;
            }
            *cursor++ = '\n';
            out.write(line, (std::streamsize)(cursor - line));
        }
        const int base = i * 3 + 1;
        out << "f " << base << " " << (base + 1) << " " << (base + 2) << "\n";
    }
    if (!out)
    {
        if (error)
            *error = "Cannot write " + path;
        return false;
    }
    return true;
}

//----------------------------------------------

inline bool writeStep(const std::string &path, const Soup &soup, std::string *error)
{
    if (soup.count() <= 0)
    {
        if (error)
            *error = "Mesh has no triangles.";
        return false;
    }
    if (soup.count() > kMaxTriangles)
    {
        if (error)
            *error = "Mesh has too many triangles (limit 2000000).";
        return false;
    }
    if (!ensureParent(path, error))
        return false;

    struct Key
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
        bool operator<(const Key &other) const
        {
            if (x != other.x)
                return x < other.x;
            if (y != other.y)
                return y < other.y;
            return z < other.z;
        }
    };
    std::vector<float> xyz;
    std::vector<int> tris;
    tris.reserve((size_t)soup.count() * 3);
    std::map<Key, int> welded;
    for (int i = 0; i < soup.count() * 3; ++i)
    {
        const float x = soup.xyz[(size_t)i * 3];
        const float y = soup.xyz[(size_t)i * 3 + 1];
        const float z = soup.xyz[(size_t)i * 3 + 2];
        Key key;
        std::memcpy(&key.x, &x, 4);
        std::memcpy(&key.y, &y, 4);
        std::memcpy(&key.z, &z, 4);
        const std::map<Key, int>::iterator found = welded.find(key);
        if (found != welded.end())
        {
            tris.push_back(found->second);
            continue;
        }
        const int index = (int)(xyz.size() / 3) + 1;
        xyz.push_back(x);
        xyz.push_back(y);
        xyz.push_back(z);
        welded.insert(std::make_pair(key, index));
        tris.push_back(index);
    }

    std::ofstream out(fsPath(path), std::ios::binary);
    if (!out)
    {
        if (error)
            *error = "Cannot write " + path;
        return false;
    }
    out << "ISO-10303-21;\nHEADER;\n";
    out << "FILE_DESCRIPTION(('unique-tessellated'),'2;1');\n";
    out << "FILE_NAME('part.step','',(''),(''),'Unique','Unique','');\n";
    out << "FILE_SCHEMA(('AP242_MANAGED_MODEL_BASED_3D_ENGINEERING_MIM_LF { 1 0 10303 442 1 1 4 }'));\n";
    out << "ENDSEC;\nDATA;\n";
    out << "#1=APPLICATION_CONTEXT('automotive design');\n";
    out << "#2=APPLICATION_PROTOCOL_DEFINITION('international standard','automotive_design',2000,#1);\n";
    out << "#3=PRODUCT_CONTEXT('',#1,'mechanical');\n";
    out << "#4=PRODUCT('part','part','',(#3));\n";
    out << "#5=PRODUCT_DEFINITION_FORMATION('', '',#4);\n";
    out << "#6=PRODUCT_DEFINITION_CONTEXT('part definition',#1,'design');\n";
    out << "#7=PRODUCT_DEFINITION('design','',#5,#6);\n";
    out << "#8=PRODUCT_DEFINITION_SHAPE('','',#7);\n";
    out << "#9=UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-7),#11,"
           "'distance_accuracy_value','confusion accuracy');\n";
    out << "#10=CARTESIAN_POINT_LIST_3D('',(";
    const int vertices = (int)(xyz.size() / 3);
    for (int i = 0; i < vertices; ++i)
    {
        if (i)
            out << ',';
        out << '(' << xyz[(size_t)i * 3] << ',' << xyz[(size_t)i * 3 + 1]
            << ',' << xyz[(size_t)i * 3 + 2] << ')';
    }
    out << "));\n";
    out << "#11=(LENGTH_UNIT()NAMED_UNIT(*)SI_UNIT(.MILLI.,.METRE.));\n";
    out << "#12=(NAMED_UNIT(*)PLANE_ANGLE_UNIT()SI_UNIT($,.RADIAN.));\n";
    out << "#13=(NAMED_UNIT(*)SI_UNIT($,.STERADIAN.)SOLID_ANGLE_UNIT());\n";
    out << "#14=(GEOMETRIC_REPRESENTATION_CONTEXT(3)"
           "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#9))"
           "GLOBAL_UNIT_ASSIGNED_CONTEXT((#11,#12,#13))"
           "REPRESENTATION_CONTEXT('','3D'));\n";
    out << "#15=TRIANGULATED_SURFACE_SET('',#10,$,$,$,(";
    const int triangles = (int)(tris.size() / 3);
    for (int i = 0; i < triangles; ++i)
    {
        if (i)
            out << ',';
        out << '(' << tris[(size_t)i * 3] << ',' << tris[(size_t)i * 3 + 1]
            << ',' << tris[(size_t)i * 3 + 2] << ')';
    }
    out << "));\n";
    out << "#16=TESSELLATED_SHAPE_REPRESENTATION('',(#15),#14);\n";
    out << "#17=SHAPE_REPRESENTATION_RELATIONSHIP('','',#16,#18);\n";
    out << "#18=SHAPE_REPRESENTATION('',(#19),#14);\n";
    out << "#19=AXIS2_PLACEMENT_3D('',#20,#21,#22);\n";
    out << "#20=CARTESIAN_POINT('',(0.,0.,0.));\n";
    out << "#21=DIRECTION('',(0.,0.,1.));\n";
    out << "#22=DIRECTION('',(1.,0.,0.));\n";
    out << "#23=SHAPE_DEFINITION_REPRESENTATION(#8,#16);\n";
    out << "ENDSEC;\nEND-ISO-10303-21;\n";
    if (!out)
    {
        if (error)
            *error = "Cannot write " + path;
        return false;
    }
    return true;
}

//----------------------------------------------

inline bool readStep(const std::vector<char> &data, Soup *soup, std::string *error)
{
    const std::string text(data.begin(), data.end());
    const std::string marker = "CARTESIAN_POINT_LIST_3D";
    const size_t at = text.find(marker);
    const size_t triAt = text.find("TRIANGULATED_SURFACE_SET");
    if (at == std::string::npos || triAt == std::string::npos)
    {
        if (error)
            *error = "STEP reader accepts only a tessellated mesh written by this tool.";
        return false;
    }
    const size_t coords = text.find("((", at);
    const size_t coordsEnd = text.find("))", coords);
    const size_t faces = text.find("((", triAt);
    const size_t facesEnd = text.find("))", faces == std::string::npos ? 0 : faces);
    if (coords == std::string::npos || coordsEnd == std::string::npos
        || faces == std::string::npos || facesEnd == std::string::npos)
    {
        if (error)
            *error = "STEP mesh is missing coordinates or triangles.";
        return false;
    }
    std::vector<float> xyz;
    const char *cursor = text.c_str() + coords + 2;
    const char *limit = text.c_str() + coordsEnd;
    while (cursor < limit)
    {
        while (cursor < limit
               && !((*cursor >= '0' && *cursor <= '9') || *cursor == '-' || *cursor == '+'
                    || *cursor == '.'))
            ++cursor;
        if (cursor >= limit)
            break;
        char *end = 0;
        const double value = std::strtod(cursor, &end);
        if (end == cursor)
            break;
        xyz.push_back((float)value);
        cursor = end;
    }
    if (xyz.size() < 9 || xyz.size() % 3 != 0)
    {
        if (error)
            *error = "STEP mesh is missing coordinates or triangles.";
        return false;
    }
    const int vertices = (int)(xyz.size() / 3);
    std::vector<long> index;
    cursor = text.c_str() + faces + 2;
    limit = text.c_str() + facesEnd;
    while (cursor < limit)
    {
        while (cursor < limit
               && !((*cursor >= '0' && *cursor <= '9') || *cursor == '-' || *cursor == '+'))
            ++cursor;
        if (cursor >= limit)
            break;
        char *end = 0;
        const long value = std::strtol(cursor, &end, 10);
        if (end == cursor)
            break;
        index.push_back(value);
        cursor = end;
    }
    if (index.size() < 3 || index.size() % 3 != 0)
    {
        if (error)
            *error = "STEP mesh has no triangles.";
        return false;
    }
    soup->xyz.clear();
    for (size_t i = 0; i < index.size(); ++i)
    {
        const long vertex = index[i];
        if (vertex < 1 || vertex > vertices)
        {
            if (error)
                *error = "STEP triangle index is out of range.";
            return false;
        }
        soup->xyz.push_back(xyz[(size_t)(vertex - 1) * 3]);
        soup->xyz.push_back(xyz[(size_t)(vertex - 1) * 3 + 1]);
        soup->xyz.push_back(xyz[(size_t)(vertex - 1) * 3 + 2]);
        if (soup->count() > kMaxTriangles)
        {
            if (error)
                *error = "Mesh has too many triangles (limit 2000000).";
            return false;
        }
    }
    if (soup->count() <= 0)
    {
        if (error)
            *error = "STEP mesh has no triangles.";
        return false;
    }
    return true;
}

//----------------------------------------------

inline bool writeMeshFile(const std::string &path, const Soup &soup, std::string *error)
{
    const std::string ext = lowerExt(path);
    if (ext == ".stl")
        return writeStl(path, soup, error);
    if (ext == ".obj")
        return writeObj(path, soup, error);
    if (ext == ".step" || ext == ".stp")
        return writeStep(path, soup, error);
    if (error)
        *error = "output must end in .stl, .obj, .step, or .stp.";
    return false;
}

//----------------------------------------------

#endif // MESHIO_H
