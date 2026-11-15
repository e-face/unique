#ifndef MODALITYTOOLS_H
#define MODALITYTOOLS_H

#include "agenttool.h"
#include "appsettings.h"

class ProjectWorkspace;
class Worker;

//----------------------------------------------

class GenerateImageTool : public AgentTool
{
public:
    GenerateImageTool(ProjectWorkspace *workspace, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    ProjectWorkspace *_workspace;
    Worker           *_owner;
};

//----------------------------------------------

class TextToSpeechTool : public AgentTool
{
public:
    TextToSpeechTool(ProjectWorkspace *workspace, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    ProjectWorkspace *_workspace;
    Worker           *_owner;
};

//----------------------------------------------

class SpeechToTextTool : public AgentTool
{
public:
    SpeechToTextTool(ProjectWorkspace *workspace, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    ProjectWorkspace *_workspace;
    Worker           *_owner;
};

//----------------------------------------------

class GenerateAudioTool : public AgentTool
{
public:
    GenerateAudioTool(ProjectWorkspace *workspace, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    ProjectWorkspace *_workspace;
    Worker           *_owner;
};

//----------------------------------------------

class GenerateVideoTool : public AgentTool
{
public:
    GenerateVideoTool(ProjectWorkspace *workspace, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    ProjectWorkspace *_workspace;
    Worker           *_owner;
};

//----------------------------------------------

class EmbedTextsTool : public AgentTool
{
public:
    EmbedTextsTool(ProjectWorkspace *workspace, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    ProjectWorkspace *_workspace;
    Worker           *_owner;
};

//----------------------------------------------

/**
 * @brief Record a decision (options, choice, rationale) into a project file.
 */
class ProposeDecisionTool : public AgentTool
{
public:
    ProposeDecisionTool(ProjectWorkspace *workspace, Worker *owner, QObject *parent = 0);

    virtual QString     name() const;
    virtual QString     description() const;
    virtual QJsonObject parametersSchema() const;
    virtual QString     invoke(QString argumentsJson, bool *ok);

private:
    ProjectWorkspace *_workspace;
    Worker           *_owner;
};

//----------------------------------------------

LlmProfile modalityProfileForWorker(Worker *owner);

/** text_to_speech arguments: text is only the line to speak, not task metadata. */
QJsonObject buildTextToSpeechToolArgs(const QString &taskText,
                                      const QJsonObject *toolArgs = 0);

#endif // MODALITYTOOLS_H
