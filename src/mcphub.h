#ifndef MCPHUB_H
#define MCPHUB_H

//----------------------------------------------
#include "toolcatalog.h"
//----------------------------------------------
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class Agent;

//----------------------------------------------

/**
 * @brief One MCP server the user configured.
 * transport is "stdio" (a local program) or "http" (one endpoint URL).
 */
struct McpServerConfig
{
    McpServerConfig()
        : transport(QString::fromUtf8("stdio"))
        , enabled(true)
    {
    }

    QString id;
    QString name;
    QString transport;
    QString command;
    QString arguments;
    QString workDir;
    QString environment;
    QString url;
    QString headers;
    bool    enabled;
};

//----------------------------------------------

/**
 * @brief MCP client: stdio and HTTP servers, their tools in the worker catalog.
 *
 * A tool id is mcp_<server>_<tool>. The worker specialization has to include
 * that id before the model can call it.
 */
class McpHub : public QObject
{
    Q_OBJECT
public:
    static McpHub *instance();

    QList<McpServerConfig> servers() const;
    /**
     * @brief Short status for the settings list. Already translated.
     */
    QString statusText(const QString &serverId) const;

    QString addServer(McpServerConfig config);
    void updateServer(const McpServerConfig &config);
    void removeServer(const QString &serverId);
    /** Drop connections and connect every enabled server again. */
    void reconnectAll();

    QList<ToolOffer> toolOffers() const;
    void addTools(Agent *agent, const QStringList &enabledIds);

    QString call(const QString &serverId, const QString &mcpName,
                 const QString &argumentsJson, bool *ok);
    void interrupt(const QString &serverId);

signals:
    void changed();

public slots:
    /** Load mcp.ini and connect. Called once at startup. */
    void sltConnectAll();

private:
    explicit McpHub(QObject *parent = 0);
    virtual ~McpHub();
    void load();
    void save() const;
    void connectServer(const QString &serverId);

    class McpSession;
    McpSession *findSession(const QString &serverId) const;

    struct McpBound
    {
        QString     serverId;
        QString     mcpName;
        QString     description;
        QJsonObject schema;
    };

    QList<McpServerConfig>       _servers;
    QList<McpSession *>          _sessions;
    mutable QHash<QString, McpBound> _bound;
    bool                         _loading;
};

#endif // MCPHUB_H
