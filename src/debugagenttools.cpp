#include "debugagenttools.h"
//----------------------------------------------
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSysInfo>
#include <QtGlobal>

//----------------------------------------------

DateTimeAgentTool::DateTimeAgentTool(QObject *parent)
    : AgentTool(parent)
{
}

//----------------------------------------------

QString DateTimeAgentTool::name() const
{
    return QString::fromUtf8("get_current_datetime");
}

//----------------------------------------------

QString DateTimeAgentTool::description() const
{
    return QString::fromUtf8(
        "Returns the current local date and time on the user's computer.");
}

//----------------------------------------------

QJsonObject DateTimeAgentTool::parametersSchema() const
{
    QJsonObject format;
    format.insert(QString::fromUtf8("type"), QString::fromUtf8("string"));
    format.insert(QString::fromUtf8("description"),
                  QString::fromUtf8("Qt datetime format, e.g. yyyy-MM-dd HH:mm:ss. "
                                      "If omitted, ISO 8601 is used."));

    QJsonObject properties;
    properties.insert(QString::fromUtf8("format"), format);

    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), properties);
    return schema;
}

//----------------------------------------------

QString DateTimeAgentTool::invoke(QString argumentsJson, bool *ok)
{
    QString format;
    if (!argumentsJson.trimmed().isEmpty())
    {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(argumentsJson.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
        {
            if (ok)
                *ok = false;
            return QString::fromUtf8("Invalid JSON arguments.");
        }
        format = document.object().value(QString::fromUtf8("format")).toString();
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QString text = format.isEmpty()
                         ? now.toString(Qt::ISODate)
                         : now.toString(format);
    if (ok)
        *ok = true;
    return text;
}

//----------------------------------------------

SystemInfoAgentTool::SystemInfoAgentTool(QObject *parent)
    : AgentTool(parent)
{
}

//----------------------------------------------

QString SystemInfoAgentTool::name() const
{
    return QString::fromUtf8("get_system_info");
}

//----------------------------------------------

QString SystemInfoAgentTool::description() const
{
    return QString::fromUtf8(
        "Returns OS, architecture, Qt version and the current working directory.");
}

//----------------------------------------------

QJsonObject SystemInfoAgentTool::parametersSchema() const
{
    QJsonObject schema;
    schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
    schema.insert(QString::fromUtf8("properties"), QJsonObject());
    return schema;
}

//----------------------------------------------

QString SystemInfoAgentTool::invoke(QString argumentsJson, bool *ok)
{
    Q_UNUSED(argumentsJson);

    QJsonObject info;
    info.insert(QString::fromUtf8("prettyProductName"), QSysInfo::prettyProductName());
    info.insert(QString::fromUtf8("kernelType"), QSysInfo::kernelType());
    info.insert(QString::fromUtf8("kernelVersion"), QSysInfo::kernelVersion());
    info.insert(QString::fromUtf8("currentCpuArchitecture"), QSysInfo::currentCpuArchitecture());
    info.insert(QString::fromUtf8("qtVersion"), QString::fromUtf8(qVersion()));
    info.insert(QString::fromUtf8("currentPath"), QDir::currentPath());

    if (ok)
        *ok = true;
    return QString::fromUtf8(QJsonDocument(info).toJson(QJsonDocument::Compact));
}
