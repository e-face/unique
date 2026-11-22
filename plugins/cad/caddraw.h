#ifndef CADDRAW_H
#define CADDRAW_H

#include "cadmesh.h"

//----------------------------------------------

struct DrawOptions
{
    QStringList views;
    QString     sheet;
    QString     orientation;
    QString     projection;
    QString     units;
    double      scale;
    double      creaseDeg;
    bool        hidden;

    DrawOptions()
        : sheet(QString::fromLatin1("A4"))
        , orientation(QString::fromLatin1("landscape"))
        , projection(QString::fromLatin1("third"))
        , units(QString::fromLatin1("mm"))
        , scale(0)
        , creaseDeg(30)
        , hidden(false)
    {
    }
};

//----------------------------------------------

bool writeOrthographic(const QString &output, const Mesh &mesh,
                       const DrawOptions &options, QString *summary, QString *error);
bool writeSectionDrawing(const QString &output, const QList<SectionLoop> &loops,
                         Vec3 planePoint, Vec3 planeNormal, const DrawOptions &options,
                         QString *summary, QString *error);

#endif // CADDRAW_H
