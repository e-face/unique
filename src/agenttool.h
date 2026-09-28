#ifndef AGENTTOOL_H
#define AGENTTOOL_H

#include <QJsonObject>
#include <QObject>
#include <QString>

//----------------------------------------------

/**
 * @brief Инструмент (функция), который агент может выполнить по запросу модели.
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
     * @brief Выполнить функцию.
     * @param argumentsJson JSON-объект аргументов от модели.
     * @param ok false, если вызов завершился ошибкой (текст ошибки — в возврате).
     */
    virtual QString invoke(QString argumentsJson, bool *ok) = 0;

    /**
     * @brief Прервать зависший вызов (команда, вложенный цикл).
     */
    virtual void interrupt()
    {
    }
};

#endif // AGENTTOOL_H
