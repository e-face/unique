#ifndef LOOKUPTOOLS_H
#define LOOKUPTOOLS_H

#include "agenttool.h"

class ProjectWorkspace;
class Worker;
class QNetworkReply;
class QEventLoop;

//----------------------------------------------

class SearchFilesAgentTool : public AgentTool
{
public:
    explicit SearchFilesAgentTool(ProjectWorkspace *workspace, Worker *owner = 0,
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

class FindInFilesAgentTool : public AgentTool
{
public:
    explicit FindInFilesAgentTool(ProjectWorkspace *workspace, Worker *owner = 0,
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

class WebSearchAgentTool : public AgentTool
{
public:
    explicit WebSearchAgentTool(Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);
    virtual void        interrupt();

private:
    Worker         *_owner;
    QNetworkReply  *_reply;
    QEventLoop     *_loop;
    bool            _interrupted;
};

//----------------------------------------------

class ReadPageAgentTool : public AgentTool
{
public:
    explicit ReadPageAgentTool(Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);
    virtual void        interrupt();

private:
    Worker         *_owner;
    QNetworkReply  *_reply;
    QEventLoop     *_loop;
    bool            _interrupted;
};

//----------------------------------------------

class ReadPdfAgentTool : public AgentTool
{
public:
    explicit ReadPdfAgentTool(ProjectWorkspace *workspace, Worker *owner = 0,
                              QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);
    virtual void        interrupt();

private:
    ProjectWorkspace *_workspace;
    Worker           *_owner;
    QNetworkReply    *_reply;
    QEventLoop       *_loop;
    bool              _interrupted;
};

//----------------------------------------------

class PdfPageImageAgentTool : public AgentTool
{
public:
    explicit PdfPageImageAgentTool(ProjectWorkspace *workspace, Worker *owner = 0,
                                   QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);
    virtual void        interrupt();

private:
    ProjectWorkspace *_workspace;
    Worker           *_owner;
    bool              _interrupted;
};

//----------------------------------------------

class AttachImageAgentTool : public AgentTool
{
public:
    explicit AttachImageAgentTool(ProjectWorkspace *workspace, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);
    virtual QJsonArray  takeResultParts();

private:
    ProjectWorkspace *_workspace;
    QJsonArray        _parts;
};

#endif // LOOKUPTOOLS_H
