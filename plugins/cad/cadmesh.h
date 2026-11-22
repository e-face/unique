#ifndef CADMESH_H
#define CADMESH_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <cmath>

//----------------------------------------------

struct Vec3
{
    double x;
    double y;
    double z;

    Vec3()
        : x(0)
        , y(0)
        , z(0)
    {
    }

    Vec3(double ax, double ay, double az)
        : x(ax)
        , y(ay)
        , z(az)
    {
    }
};

inline Vec3 operator+(Vec3 a, Vec3 b)
{
    return Vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

inline Vec3 operator-(Vec3 a, Vec3 b)
{
    return Vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

inline Vec3 operator*(Vec3 a, double s)
{
    return Vec3(a.x * s, a.y * s, a.z * s);
}

inline double dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(Vec3 a, Vec3 b)
{
    return Vec3(a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x);
}

inline double length(Vec3 a)
{
    return std::sqrt(dot(a, a));
}

inline Vec3 normalized(Vec3 a)
{
    const double n = length(a);
    if (n < 1e-15)
        return Vec3();
    return a * (1.0 / n);
}

//----------------------------------------------

struct Tri
{
    int a;
    int b;
    int c;
};

struct Mesh
{
    QVector<Vec3> vertices;
    QVector<Tri>  triangles;
};

struct Part
{
    QString name;
    QString sourceFile;
    Mesh    mesh;
};

/**
 * One loaded file: a mesh, an assembly of meshes, or a 2D DXF.
 * Triangle parts and DXF statistics can both be set when a DXF has 3D faces.
 */
struct Model
{
    QString     format;
    QString     units;
    QString     sourcePath;
    QList<Part> parts;

    int         dxfLines;
    int         dxfCircles;
    int         dxfArcs;
    int         dxfPolylines;
    int         dxfFaces;
    double      dxfLength;
    bool        hasDxfBounds;
    Vec3        dxfMin;
    Vec3        dxfMax;
    QStringList layers;

    Model()
        : dxfLines(0)
        , dxfCircles(0)
        , dxfArcs(0)
        , dxfPolylines(0)
        , dxfFaces(0)
        , dxfLength(0)
        , hasDxfBounds(false)
    {
    }

    int triangleCount() const;
    int vertexCount() const;
    Mesh combined() const;
};

//----------------------------------------------

struct Placement
{
    Vec3 translate;
    Vec3 rotateDeg;
    Vec3 scale;
    bool aroundBbox;

    Placement()
        : scale(1, 1, 1)
        , aroundBbox(false)
    {
    }
};

struct AssemblyPartSpec
{
    QString   name;
    QString   file;
    Placement place;
};

struct MeshStats
{
    Vec3   minimum;
    Vec3   maximum;
    bool   any;
    double area;
    double volume;
    bool   watertight;
    bool   flipped;
    int    openEdges;
    int    nonManifold;

    MeshStats()
        : any(false)
        , area(0)
        , volume(0)
        , watertight(false)
        , flipped(false)
        , openEdges(0)
        , nonManifold(0)
    {
    }
};

struct SectionLoop
{
    QList<Vec3> points;
    bool        closed;

    SectionLoop()
        : closed(false)
    {
    }
};

//----------------------------------------------

QString cadNum(double value);
QString cadVec(Vec3 value);

QString resolveInputPath(const QString &path, const QString &projectRoot,
                         const QString &besideDir);
QString resolveOutputPath(const QString &path, const QString &projectRoot);

bool loadModel(const QString &path, const QString &projectRoot, Model *model,
               QString *error);
bool saveParts(const QString &path, const QList<Part> &parts, bool binaryStl,
               QString *error);
bool writeAssemblyJson(const QString &path, const QString &units,
                       const QList<AssemblyPartSpec> &parts, QString *error);

MeshStats measureMesh(const Mesh &mesh);
void transformVertices(QVector<Vec3> *vertices, const Placement &place, Vec3 center);
bool sectionMesh(const Mesh &mesh, Vec3 point, Vec3 normal,
                 QList<SectionLoop> *loops, QString *error);

void modelBounds(const Model &model, Vec3 *minimum, Vec3 *maximum, bool *any);

#endif // CADMESH_H
