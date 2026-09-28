#ifndef DEBUGAGENTTOOLS_H
#define DEBUGAGENTTOOLS_H

#include "agenttool.h"

//----------------------------------------------

/**
 * @brief Возвращает текущие дату и время.
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
 * @brief Сведения о системе — для проверки вызова функций.
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
