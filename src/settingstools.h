#ifndef SETTINGSTOOLS_H
#define SETTINGSTOOLS_H

#include "agenttool.h"

class Worker;

//----------------------------------------------

/**
 * @brief Read program or project settings, without API keys.
 *
 * The user confirms every read.
 */
class ReadSettingsTool : public AgentTool
{
public:
    explicit ReadSettingsTool(Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    Worker *_owner;
};

//----------------------------------------------

/**
 * @brief Change program or project settings: limits, action control,
 *        model profiles (except the API key), and worker presets.
 *
 * The full change list is shown to the user and applied only
 * after confirmation.
 */
class EditSettingsTool : public AgentTool
{
public:
    explicit EditSettingsTool(Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    Worker *_owner;
};

#endif // SETTINGSTOOLS_H
