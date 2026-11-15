#ifndef DEBUGAGENTTOOLS_H
#define DEBUGAGENTTOOLS_H

#include "agenttool.h"

//----------------------------------------------

/**
 * @brief Returns the current date and time.
 */
class DateTimeAgentTool : public AgentTool
{
public:
    explicit DateTimeAgentTool(QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);
};

//----------------------------------------------

/**
 * @brief System information, for checking function calls.
 */
class SystemInfoAgentTool : public AgentTool
{
public:
    explicit SystemInfoAgentTool(QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);
};

#endif // DEBUGAGENTTOOLS_H
