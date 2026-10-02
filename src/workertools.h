#ifndef WORKERTOOLS_H
#define WORKERTOOLS_H

#include "agenttool.h"

class ProjectWorkspace;
class Worker;
class WorkerManager;

//----------------------------------------------

/**
 * @brief Поручить задачу дочернему воркеру; результат придёт после всех детей.
 */
class DelegateTaskTool : public AgentTool
{
public:
    DelegateTaskTool(WorkerManager *manager, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    WorkerManager *_manager;
    Worker        *_owner;
};

//----------------------------------------------

/**
 * @brief Архитектура и список подзадач; обязателен до delegate_task у менеджера.
 */
class PlanWorkTool : public AgentTool
{
public:
    explicit PlanWorkTool(Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    Worker *_owner;
};

//----------------------------------------------
/**
 * @brief Список прямых дочерних воркеров владельца.
 */
class ListChildWorkersTool : public AgentTool
{
public:
    explicit ListChildWorkersTool(Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    Worker *_owner;
};

//----------------------------------------------

/**
 * @brief Полный отчёт дочернего воркера по запросу (в контекст попадает обрезанный).
 */
class GetChildResultTool : public AgentTool
{
public:
    explicit GetChildResultTool(Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    Worker *_owner;
};

//----------------------------------------------

/**
 * @brief Удалить временного дочернего воркера вместе с его потомками.
 */
class DismissChildWorkerTool : public AgentTool
{
public:
    DismissChildWorkerTool(WorkerManager *manager, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    WorkerManager *_manager;
    Worker        *_owner;
};

//----------------------------------------------

/**
 * @brief Создать инструмент: общий многоразовый исполнитель (только менеджер).
 */
class CreateToolTool : public AgentTool
{
public:
    CreateToolTool(WorkerManager *manager, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    WorkerManager *_manager;
    Worker        *_owner;
};

//----------------------------------------------

/**
 * @brief Список инструментов проекта с их занятостью.
 */
class ListToolsTool : public AgentTool
{
public:
    explicit ListToolsTool(WorkerManager *manager, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    WorkerManager *_manager;
};

//----------------------------------------------

/**
 * @brief Запустить инструмент и дождаться его ответа (очередь — по одному).
 */
class RunToolTool : public AgentTool
{
public:
    RunToolTool(WorkerManager *manager, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    WorkerManager *_manager;
    Worker        *_owner;
};

//----------------------------------------------

/**
 * @brief Команда в каталоге проекта; каждое выполнение подтверждает пользователь.
 */
class QEventLoop;
class QProcess;

class RunCommandAgentTool : public AgentTool
{
public:
    RunCommandAgentTool(ProjectWorkspace *workspace, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);
    virtual void        interrupt();

private:
    static void killProcessTree(QProcess *process);
    QString runOne(QJsonObject args, bool *ok);

    ProjectWorkspace *_workspace;
    Worker           *_owner;
    QProcess         *_runningProcess;
    QEventLoop       *_runningLoop;
    bool              _interrupted;
};

//----------------------------------------------

/**
 * @brief Вернуть результат родителю и остановить цикл агента.
 */
class CompleteTaskTool : public AgentTool
{
public:
    CompleteTaskTool(Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    Worker *_owner;
};

//----------------------------------------------

/**
 * @brief Запросить у пользователя разрешение на действие в дочернем чате.
 */
class RequestPermissionTool : public AgentTool
{
public:
    RequestPermissionTool(Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    Worker *_owner;
};

//----------------------------------------------

/**
 * @brief Добавить собеседника в группу по preset_id из настроек.
 */
class AddGroupMemberTool : public AgentTool
{
public:
    AddGroupMemberTool(WorkerManager *manager, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    WorkerManager *_manager;
    Worker        *_owner;
};

//----------------------------------------------

/**
 * @brief Сообщение прямому родителю/ребёнку (не для группы).
 */
class TalkToTool : public AgentTool
{
public:
    TalkToTool(WorkerManager *manager, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    WorkerManager *_manager;
    Worker        *_owner;
};

//----------------------------------------------

/**
 * @brief Ждать своей очереди оратора / голосования / конца диалога группы.
 */
class GroupAwaitTool : public AgentTool
{
public:
    GroupAwaitTool(WorkerManager *manager, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    WorkerManager *_manager;
    Worker        *_owner;
};

//----------------------------------------------

/**
 * @brief Реплика текущего оратора — рассылается всем членам группы.
 */
class GroupSpeakTool : public AgentTool
{
public:
    GroupSpeakTool(WorkerManager *manager, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    WorkerManager *_manager;
    Worker        *_owner;
};

//----------------------------------------------

/**
 * @brief Передать роль оратора следующему участнику.
 */
class GroupEndTurnTool : public AgentTool
{
public:
    GroupEndTurnTool(WorkerManager *manager, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    WorkerManager *_manager;
    Worker        *_owner;
};

//----------------------------------------------

/**
 * @brief Предложить итоговый результат группы на единогласное голосование.
 */
class GroupProposeTool : public AgentTool
{
public:
    GroupProposeTool(WorkerManager *manager, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    WorkerManager *_manager;
    Worker        *_owner;
};

//----------------------------------------------

/**
 * @brief Голос за/против текущего предложения группы.
 */
class GroupVoteTool : public AgentTool
{
public:
    GroupVoteTool(WorkerManager *manager, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    WorkerManager *_manager;
    Worker        *_owner;
};

//----------------------------------------------

/**
 * @brief Список участников своей группы.
 */
class ListGroupMembersTool : public AgentTool
{
public:
    ListGroupMembersTool(WorkerManager *manager, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    WorkerManager *_manager;
    Worker        *_owner;
};

#endif // WORKERTOOLS_H
