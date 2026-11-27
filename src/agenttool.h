#ifndef AGENTTOOL_H
#define AGENTTOOL_H

#include <QJsonObject>
#include <QJsonArray>
#include <QObject>
#include <QString>

//----------------------------------------------

/**
 * @brief A tool (function) the agent can run when the model asks.
 */
class AgentTool : public QObject
{
public:
    explicit AgentTool(QObject *parent = 0)
        : QObject(parent)
    {
    }

    virtual ~AgentTool()
    {
    }

    virtual QString     name() const = 0;
    virtual QString     description() const = 0;
    virtual QJsonObject parametersSchema() const = 0;

    /**
     * @brief Run the function.
     * @param argumentsJson JSON object of arguments from the model.
     * @param ok false if the call failed (the error text is the return value).
     */
    virtual QString invoke(QString argumentsJson, bool *ok) = 0;

    /**
     * @brief Multimodal blocks for the tool message (text + image_url).
     *        Empty means the string from invoke() is the whole result.
     *        The array is taken once.
     */
    virtual QJsonArray takeResultParts()
    {
        return QJsonArray();
    }

    /**
     * @brief Interrupt a stuck call (a command or a nested loop).
     */
    virtual void interrupt()
    {
    }
};

#endif // AGENTTOOL_H
