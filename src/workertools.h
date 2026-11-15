#ifndef WORKERTOOLS_H
#define WORKERTOOLS_H

#include "agenttool.h"

class ProjectWorkspace;
class Worker;
class WorkerManager;

//----------------------------------------------

/**
 * @brief Assign a task to a child worker. The result arrives after every child.
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
 * @brief The executor's own plan: what to do itself, what to hand off, and what to turn into a tool.
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
 * @brief Ask the user about a missing detail and wait for the answer.
 */
class AskUserTool : public AgentTool
{
public:
    explicit AskUserTool(Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    Worker *_owner;
};

//----------------------------------------------
/**
 * @brief List of the owner's direct child workers.
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
 * @brief Full report of a child worker on request. Context receives a truncated copy.
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
 * @brief Delete a temporary child worker together with its descendants.
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
 * @brief Create a tool: a shared reusable specialist. An executor calls it.
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
 * @brief List of the project's tools and whether they are busy.
 */
class ListToolsTool : public AgentTool
{
public:
    explicit ListToolsTool(WorkerManager *manager, QObject *parent = 0);

    void setOwner(Worker *owner);

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
 * @brief Worker presets an executor may create.
 */
class ListWorkerPresetsTool : public AgentTool
{
public:
    ListWorkerPresetsTool(WorkerManager *manager, Worker *owner, QObject *parent = 0);

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
 * @brief Run a tool and wait for its reply. The queue runs one at a time.
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
 * @brief A command in the project directory. The user confirms every run.
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
 * @brief Return the result to the parent and stop the agent loop.
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
 * @brief Ask the user for permission to act in a child chat.
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
 * @brief Add a peer to the group by a preset_id from settings.
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
 * @brief A message to the direct parent or child (not for a group).
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
 * @brief Wait for your turn to speak, to vote, or for the group dialogue to end.
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
 * @brief The current speaker's line, broadcast to every group member.
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
 * @brief Pass the speaker role to the next member.
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
 * @brief Propose the group's final result for a unanimous vote.
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
 * @brief A vote for or against the group's current proposal.
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
 * @brief List of members of your own group.
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
