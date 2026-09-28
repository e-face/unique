#ifndef PROJECTFILETOOLS_H
#define PROJECTFILETOOLS_H

#include "agenttool.h"

class ProjectWorkspace;
class Worker;

//----------------------------------------------

class CreateFileAgentTool : public AgentTool
{
public:
    explicit CreateFileAgentTool(ProjectWorkspace *workspace, Worker *owner = 0,
                                 QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    QString createOne(QString relPath, QString content, bool *ok);

    ProjectWorkspace *_workspace;
    Worker           *_owner;
};

//----------------------------------------------

class WriteFileAgentTool : public AgentTool
{
public:
    explicit WriteFileAgentTool(ProjectWorkspace *workspace, Worker *owner = 0,
                                QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    QString writeOne(QString relPath, QString content, bool *ok);

    ProjectWorkspace *_workspace;
    Worker           *_owner;
};

//----------------------------------------------

class ReadFileAgentTool : public AgentTool
{
public:
    explicit ReadFileAgentTool(ProjectWorkspace *workspace, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    QString readOne(QString relPath, int offset, int limit, bool *ok) const;

    ProjectWorkspace *_workspace;
};

//----------------------------------------------

class DeleteFileAgentTool : public AgentTool
{
public:
    explicit DeleteFileAgentTool(ProjectWorkspace *workspace, Worker *owner = 0,
                                 QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    ProjectWorkspace *_workspace;
    Worker           *_owner;
};

//----------------------------------------------

class RenameFileAgentTool : public AgentTool
{
public:
    explicit RenameFileAgentTool(ProjectWorkspace *workspace, Worker *owner = 0,
                                 QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    ProjectWorkspace *_workspace;
    Worker           *_owner;
};

//----------------------------------------------

class ListDirectoryAgentTool : public AgentTool
{
public:
    explicit ListDirectoryAgentTool(ProjectWorkspace *workspace, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    ProjectWorkspace *_workspace;
};

#endif // PROJECTFILETOOLS_H
