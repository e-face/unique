#include "mcphub.h"
//----------------------------------------------
#include "agent.h"
#include "agenttool.h"
#include "appsettings.h"
#include "appversion.h"
#include "secretbox.h"
//----------------------------------------------
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QUuid>

//----------------------------------------------

namespace
{

const int kConnectMs = 20000;
const int kCallMs    = 120000;
const int kMaxTools  = 200;
const int kMaxReply  = 200000;

QString slugText(const QString &text, int maxLen)
{
    QString out;
    const QString lower = text.toLower();
    for (int i = 0; i < lower.size(); ++i)
    {
        const QChar c = lower.at(i);
        if (c.isLetterOrNumber())
        {
            out += c;
            continue;
        }
        if (!out.isEmpty() && !out.endsWith(QLatin1Char('_')))
            out += QLatin1Char('_');
    }
    while (out.endsWith(QLatin1Char('_')))
        out.chop(1);
    if (out.isEmpty())
        out = QString::fromUtf8("x");
    if (out.at(0).isDigit())
        out.prepend(QLatin1Char('s'));
    if (out.size() > maxLen)
        out = out.left(maxLen);
    while (out.endsWith(QLatin1Char('_')))
        out.chop(1);
    return out;
}

QStringList splitArgs(const QString &line)
{
    QStringList out;
    QString cur;
    bool quote = false;
    QChar mark;
    for (int i = 0; i < line.size(); ++i)
    {
        const QChar c = line.at(i);
        if (!quote && (c == QLatin1Char('"') || c == QLatin1Char('\'')))
        {
            quote = true;
            mark = c;
            continue;
        }
        if (quote && c == mark)
        {
            quote = false;
            continue;
        }
        if (!quote && c.isSpace())
        {
            if (!cur.isEmpty())
            {
                out.append(cur);
                cur.clear();
            }
            continue;
        }
        cur += c;
    }
    if (!cur.isEmpty())
        out.append(cur);
    return out;
}

QString resolveProgram(const QString &program)
{
    if (program.contains(QLatin1Char('/')) || program.contains(QLatin1Char('\\'))
        || program.contains(QLatin1Char('.')))
        return program;
#ifdef Q_OS_WIN
    const QString path = QProcessEnvironment::systemEnvironment().value(
        QString::fromUtf8("PATH"));
    const QStringList dirs = path.split(QLatin1Char(';'), QString::SkipEmptyParts);
    const QStringList suffixes =
        QStringList() << QString::fromUtf8(".exe")
                      << QString::fromUtf8(".cmd")
                      << QString::fromUtf8(".bat");
    foreach (const QString &dir, dirs)
    {
        foreach (const QString &suffix, suffixes)
        {
            const QString candidate = QDir(dir).filePath(program + suffix);
            if (QFileInfo(candidate).isFile())
                return candidate;
        }
    }
#else
    Q_UNUSED(program);
#endif
    return program;
}

QString clipText(QString text, int limit)
{
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    if (text.size() <= limit)
        return text;
    return text.left(limit) + QString::fromUtf8("…");
}

int jsonId(const QJsonValue &value)
{
    if (value.isDouble())
        return int(value.toDouble());
    if (value.isString())
        return value.toString().toInt();
    return -1;
}

QJsonObject objectValue(const QJsonValue &value)
{
    if (value.isObject())
        return value.toObject();
    return QJsonObject();
}

//----------------------------------------------

class McpAgentTool : public AgentTool
{
public:
    McpAgentTool(const QString &id, const QString &serverId, const QString &mcpName,
                 const QString &description, const QJsonObject &schema, QObject *parent)
        : AgentTool(parent)
        , _id(id)
        , _serverId(serverId)
        , _mcpName(mcpName)
        , _description(description)
        , _schema(schema)
    {
    }

    virtual QString name() const
    {
        return _id;
    }

    virtual QString description() const
    {
        return _description;
    }

    virtual QJsonObject parametersSchema() const
    {
        return _schema;
    }

    virtual QString invoke(QString argumentsJson, bool *ok)
    {
        return McpHub::instance()->call(_serverId, _mcpName, argumentsJson, ok);
    }

    virtual void interrupt()
    {
        McpHub::instance()->interrupt(_serverId);
    }

private:
    QString     _id;
    QString     _serverId;
    QString     _mcpName;
    QString     _description;
    QJsonObject _schema;
};

}

//----------------------------------------------

class McpHub::McpSession : public QObject
{
public:
    struct ToolRec
    {
        QString     id;
        QString     mcpName;
        QString     title;
        QString     description;
        QJsonObject schema;
    };

    McpSession(const McpServerConfig &config, QObject *parent)
        : QObject(parent)
        , _config(config)
        , _process(0)
        , _network(0)
        , _nextId(0)
        , _callDepth(0)
        , _interrupt(false)
        , _dead(false)
        , _ready(false)
        , _activeReply(0)
        , _aborting(false)
    {
    }

    ~McpSession()
    {
        stopProcess();
    }

    QString serverId() const
    {
        return _config.id;
    }

    bool ready() const
    {
        return _ready && !_dead;
    }

    QString errorText() const
    {
        return _error;
    }

    QList<ToolRec> tools() const
    {
        return _tools;
    }

    void stopProcess()
    {
        _dead = true;
        _ready = false;
        if (_process)
        {
            if (_process->state() != QProcess::NotRunning)
            {
                _process->kill();
                _process->waitForFinished(300);
            }
            _process->deleteLater();
            _process = 0;
        }
    }

    void interruptCall()
    {
        _interrupt = true;
    }

    bool connectServer()
    {
        _error.clear();
        _tools.clear();
        _ready = false;
        _dead = false;
        _interrupt = false;
        _replies.clear();
        _buffer.clear();
        _stderr.clear();
        stopProcess();
        _dead = false;
        if (_config.transport == QString::fromUtf8("http"))
            return connectHttp();
        return connectStdio();
    }

    QString callTool(const QString &mcpName, const QJsonObject &args, bool *ok)
    {
        if (ok)
            *ok = false;
        if (!ready())
        {
            return _error.isEmpty()
                   ? QString::fromUtf8("MCP server is not connected.")
                   : _error;
        }
        QJsonObject params;
        params.insert(QString::fromUtf8("name"), mcpName);
        params.insert(QString::fromUtf8("arguments"), args);
        ++_callDepth;
        const QJsonObject result = rpc(QString::fromUtf8("tools/call"), params, kCallMs, false);
        --_callDepth;
        if (_callDepth == 0)
            _interrupt = false;
        if (_dead || result.contains(QString::fromUtf8("__error")))
        {
            const QString message = result.value(QString::fromUtf8("__error")).toString();
            return message.isEmpty()
                   ? QString::fromUtf8("MCP tool call failed.")
                   : message;
        }
        const bool isError = result.value(QString::fromUtf8("isError")).toBool(false);
        QString text = formatResult(result);
        if (text.size() > kMaxReply)
        {
            text = text.left(kMaxReply)
                   + QString::fromUtf8("\n… truncated");
        }
        if (ok)
            *ok = !isError;
        if (text.isEmpty())
            text = isError
                   ? QString::fromUtf8("MCP tool failed.")
                   : QString::fromUtf8("OK");
        return text;
    }

private:
    bool connectStdio()
    {
        const QString program = resolveProgram(_config.command.trimmed());
        if (program.isEmpty())
        {
            _error = McpHub::tr("Укажите команду.");
            _dead = true;
            return false;
        }
        _process = new QProcess(this);
        _process->setProcessChannelMode(QProcess::SeparateChannels);
        if (!_config.workDir.trimmed().isEmpty())
            _process->setWorkingDirectory(_config.workDir.trimmed());
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QStringList lines = _config.environment.split(QLatin1Char('\n'));
        foreach (QString line, lines)
        {
            line = line.trimmed();
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0)
                continue;
            env.insert(line.left(eq).trimmed(), line.mid(eq + 1));
        }
        _process->setProcessEnvironment(env);
        connect(_process, &QProcess::readyReadStandardOutput, this, [this]() {
            _buffer.append(_process->readAllStandardOutput());
            consume();
        });
        connect(_process, &QProcess::readyReadStandardError, this, [this]() {
            _stderr.append(_process->readAllStandardError());
            if (_stderr.size() > 4000)
                _stderr = _stderr.right(4000);
        });
        connect(_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            if (_process && _process->state() == QProcess::NotRunning)
                markDead();
        });
        connect(_process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                this, [this](int, QProcess::ExitStatus) { markDead(); });
        _process->start(program, splitArgs(_config.arguments));
        if (!_process->waitForStarted(8000))
        {
            _error = _process->errorString();
            markDead();
            return false;
        }
        const QStringList versions =
            QStringList() << QString::fromUtf8("2024-11-05")
                          << QString::fromUtf8("2025-03-26");
        bool inited = false;
        foreach (const QString &version, versions)
        {
            if (_dead)
                break;
            const QJsonObject result = rpc(QString::fromUtf8("initialize"),
                                           initParams(version), kConnectMs, false);
            if (!result.contains(QString::fromUtf8("__error")))
            {
                const QString agreed = result.value(QString::fromUtf8("protocolVersion")).toString();
                _protocol = agreed.isEmpty() ? version : agreed;
                inited = true;
                break;
            }
            _error = result.value(QString::fromUtf8("__error")).toString();
        }
        if (!inited || _dead)
        {
            if (_error.isEmpty())
                _error = stderrHint();
            markDead();
            return false;
        }
        notify(QString::fromUtf8("notifications/initialized"), QJsonObject());
        if (_dead || !loadTools())
        {
            markDead();
            return false;
        }
        _ready = true;
        _error.clear();
        return true;
    }

    bool connectHttp()
    {
        const QUrl url = QUrl::fromUserInput(_config.url.trimmed());
        if (!url.isValid()
            || (url.scheme() != QString::fromUtf8("http")
                && url.scheme() != QString::fromUtf8("https")))
        {
            _error = McpHub::tr("Укажите адрес http или https.");
            _dead = true;
            return false;
        }
        _httpUrl = url;
        if (!_network)
            _network = new QNetworkAccessManager(this);
        const QStringList versions =
            QStringList() << QString::fromUtf8("2024-11-05")
                          << QString::fromUtf8("2025-03-26");
        bool inited = false;
        foreach (const QString &version, versions)
        {
            _protocol = version;
            const QJsonObject result = rpc(QString::fromUtf8("initialize"),
                                           initParams(version), kConnectMs, false);
            if (!result.contains(QString::fromUtf8("__error")))
            {
                const QString agreed = result.value(QString::fromUtf8("protocolVersion")).toString();
                if (!agreed.isEmpty())
                    _protocol = agreed;
                inited = true;
                break;
            }
            _error = result.value(QString::fromUtf8("__error")).toString();
            _sessionId.clear();
        }
        if (!inited)
        {
            _dead = true;
            return false;
        }
        notify(QString::fromUtf8("notifications/initialized"), QJsonObject());
        if (_dead || !loadTools())
        {
            _dead = true;
            return false;
        }
        _ready = true;
        _error.clear();
        return true;
    }

    QJsonObject initParams(const QString &version) const
    {
        QJsonObject client;
        client.insert(QString::fromUtf8("name"), QString::fromUtf8(UNIQUE_APP_NAME));
        client.insert(QString::fromUtf8("version"), QString::fromUtf8(UNIQUE_APP_VERSION));
        QJsonObject caps;
        QJsonObject roots;
        roots.insert(QString::fromUtf8("listChanged"), false);
        caps.insert(QString::fromUtf8("roots"), roots);
        QJsonObject params;
        params.insert(QString::fromUtf8("protocolVersion"), version);
        params.insert(QString::fromUtf8("capabilities"), caps);
        params.insert(QString::fromUtf8("clientInfo"), client);
        return params;
    }

    bool loadTools()
    {
        _tools.clear();
        QString cursor;
        for (int page = 0; page < 20; ++page)
        {
            QJsonObject params;
            if (!cursor.isEmpty())
                params.insert(QString::fromUtf8("cursor"), cursor);
            const QJsonObject result = rpc(QString::fromUtf8("tools/list"), params, kConnectMs, false);
            if (result.contains(QString::fromUtf8("__error")))
            {
                _error = result.value(QString::fromUtf8("__error")).toString();
                return false;
            }
            const QJsonArray tools = result.value(QString::fromUtf8("tools")).toArray();
            foreach (const QJsonValue &item, tools)
            {
                if (_tools.size() >= kMaxTools)
                    break;
                const QJsonObject obj = item.toObject();
                const QString name = obj.value(QString::fromUtf8("name")).toString().trimmed();
                if (name.isEmpty())
                    continue;
                ToolRec rec;
                rec.mcpName = name;
                rec.title = obj.value(QString::fromUtf8("title")).toString().trimmed();
                if (rec.title.isEmpty())
                    rec.title = name;
                rec.description = obj.value(QString::fromUtf8("description")).toString().trimmed();
                rec.schema = objectValue(obj.value(QString::fromUtf8("inputSchema")));
                if (rec.schema.isEmpty())
                {
                    rec.schema.insert(QString::fromUtf8("type"), QString::fromUtf8("object"));
                    rec.schema.insert(QString::fromUtf8("additionalProperties"), true);
                }
                _tools.append(rec);
            }
            cursor = result.value(QString::fromUtf8("nextCursor")).toString();
            if (cursor.isEmpty() || _tools.size() >= kMaxTools)
                break;
        }
        return true;
    }

    void notify(const QString &method, const QJsonObject &params)
    {
        QJsonObject msg;
        msg.insert(QString::fromUtf8("jsonrpc"), QString::fromUtf8("2.0"));
        msg.insert(QString::fromUtf8("method"), method);
        if (!params.isEmpty())
            msg.insert(QString::fromUtf8("params"), params);
        post(msg, false);
    }

    QJsonObject rpc(const QString &method, const QJsonObject &params, int timeoutMs, bool /*note*/)
    {
        const int id = ++_nextId;
        QJsonObject msg;
        msg.insert(QString::fromUtf8("jsonrpc"), QString::fromUtf8("2.0"));
        msg.insert(QString::fromUtf8("id"), id);
        msg.insert(QString::fromUtf8("method"), method);
        msg.insert(QString::fromUtf8("params"), params);
        if (!post(msg, true))
        {
            QJsonObject err;
            err.insert(QString::fromUtf8("__error"),
                       _error.isEmpty() ? QString::fromUtf8("MCP transport failed.") : _error);
            return err;
        }
        if (!waitId(id, timeoutMs))
        {
            QJsonObject err;
            if (_interrupt)
                err.insert(QString::fromUtf8("__error"), QString::fromUtf8("MCP call interrupted."));
            else if (!_error.isEmpty() && _dead)
                err.insert(QString::fromUtf8("__error"), _error);
            else
                err.insert(QString::fromUtf8("__error"), QString::fromUtf8("MCP server timed out."));
            return err;
        }
        const QJsonObject reply = _replies.take(id);
        if (reply.contains(QString::fromUtf8("error")))
        {
            const QJsonObject errObj = reply.value(QString::fromUtf8("error")).toObject();
            QString message = errObj.value(QString::fromUtf8("message")).toString();
            if (message.isEmpty())
                message = QString::fromUtf8("MCP request failed.");
            QJsonObject err;
            err.insert(QString::fromUtf8("__error"), message);
            return err;
        }
        return objectValue(reply.value(QString::fromUtf8("result")));
    }

    bool post(const QJsonObject &msg, bool track)
    {
        const QByteArray json = QJsonDocument(msg).toJson(QJsonDocument::Compact);
        if (_config.transport != QString::fromUtf8("http"))
        {
            if (!_process || _process->state() == QProcess::NotRunning)
            {
                markDead();
                return false;
            }
            QByteArray frame = json;
            frame.append('\n');
            return _process->write(frame) == frame.size();
        }
        if (!_network)
            _network = new QNetworkAccessManager(this);
        QNetworkRequest req(_httpUrl);
        req.setHeader(QNetworkRequest::ContentTypeHeader,
                      QString::fromUtf8("application/json"));
        req.setRawHeader("Accept", "application/json, text/event-stream");
        if (!_protocol.isEmpty())
            req.setRawHeader("MCP-Protocol-Version", _protocol.toUtf8());
        if (!_sessionId.isEmpty())
            req.setRawHeader("Mcp-Session-Id", _sessionId.toUtf8());
        const QStringList headerLines = _config.headers.split(QLatin1Char('\n'));
        foreach (QString line, headerLines)
        {
            line = line.trimmed();
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon <= 0)
                continue;
            req.setRawHeader(line.left(colon).trimmed().toUtf8(),
                             line.mid(colon + 1).trimmed().toUtf8());
        }
        req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
        QNetworkReply *reply = _network->post(req, json);
        if (track)
            _activeReply = reply;
        connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
            ingestHttp(reply->readAll(),
                       reply->header(QNetworkRequest::ContentTypeHeader).toString());
            rememberSession(reply);
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, track]() {
            rememberSession(reply);
            const QByteArray rest = reply->readAll();
            if (!rest.isEmpty())
            {
                ingestHttp(rest, reply->header(QNetworkRequest::ContentTypeHeader).toString());
            }
            if (track
                && reply->error() != QNetworkReply::NoError
                && reply->error() != QNetworkReply::OperationCanceledError
                && _replies.isEmpty())
            {
                _error = reply->errorString();
            }
            if (reply == _activeReply)
                _activeReply = 0;
            reply->deleteLater();
        });
        if (!track)
            waitReply(reply, 8000);
        return true;
    }

    void waitReply(QNetworkReply *reply, int timeoutMs)
    {
        QElapsedTimer elapsed;
        elapsed.start();
        while (reply && !reply->isFinished() && !_interrupt && !_dead)
        {
            if (elapsed.elapsed() >= timeoutMs)
                return;
            pump(40);
        }
    }

    void rememberSession(QNetworkReply *reply)
    {
        if (!reply)
            return;
        const QByteArray id = reply->rawHeader("Mcp-Session-Id");
        if (!id.isEmpty())
            _sessionId = QString::fromUtf8(id);
    }

    void ingestHttp(const QByteArray &chunk, const QString &contentType)
    {
        if (chunk.isEmpty())
            return;
        const bool sse = contentType.contains(QString::fromUtf8("event-stream"))
                         || chunk.startsWith("data:")
                         || chunk.startsWith("event:");
        if (!sse)
        {
            handleMessage(chunk);
            return;
        }
        _sse.append(chunk);
        int split = -1;
        while ((split = _sse.indexOf("\n\n")) >= 0 || (split = _sse.indexOf("\r\n\r\n")) >= 0)
        {
            const int width = _sse.mid(split).startsWith("\r\n\r\n") ? 4 : 2;
            const QByteArray event = _sse.left(split);
            _sse.remove(0, split + width);
            QByteArray data;
            const QList<QByteArray> lines = event.split('\n');
            foreach (QByteArray line, lines)
            {
                if (line.endsWith('\r'))
                    line.chop(1);
                if (line.startsWith("data:"))
                {
                    QByteArray piece = line.mid(5);
                    if (piece.startsWith(' '))
                        piece.remove(0, 1);
                    if (!data.isEmpty())
                        data.append('\n');
                    data.append(piece);
                }
            }
            if (!data.isEmpty())
                handleMessage(data);
        }
    }

    bool waitId(int id, int timeoutMs)
    {
        QElapsedTimer elapsed;
        elapsed.start();
        while (!_replies.contains(id) && !_interrupt && !_dead)
        {
            if (elapsed.elapsed() >= timeoutMs)
                return false;
            pump(40);
        }
        return _replies.contains(id);
    }

    void pump(int ms)
    {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));
        timer.start(ms);
        loop.exec();
    }

    void markDead()
    {
        _dead = true;
        _ready = false;
        if (_error.isEmpty())
            _error = stderrHint();
        if (_error.isEmpty())
            _error = QString::fromUtf8("MCP server stopped.");
    }

    QString stderrHint() const
    {
        QString text = QString::fromUtf8(_stderr).trimmed();
        if (text.size() > 240)
            text = text.right(240);
        return text;
    }

    void consume()
    {
        while (!_buffer.isEmpty())
        {
            int i = 0;
            while (i < _buffer.size())
            {
                const char c = _buffer.at(i);
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
                    break;
                ++i;
            }
            if (i > 0)
                _buffer.remove(0, i);
            if (_buffer.isEmpty())
                return;
            if (_buffer.startsWith("Content-Length:") || _buffer.startsWith("content-length:"))
            {
                int headerEnd = _buffer.indexOf("\r\n\r\n");
                int sep = 4;
                if (headerEnd < 0)
                {
                    headerEnd = _buffer.indexOf("\n\n");
                    sep = 2;
                }
                if (headerEnd < 0)
                    return;
                const QByteArray header = _buffer.left(headerEnd);
                const int colon = header.indexOf(':');
                bool okLen = false;
                const int len = colon < 0
                                ? 0
                                : header.mid(colon + 1).trimmed().toInt(&okLen);
                if (!okLen || len < 0 || len > 8 * 1024 * 1024)
                {
                    _buffer.clear();
                    return;
                }
                if (_buffer.size() < headerEnd + sep + len)
                    return;
                const QByteArray body = _buffer.mid(headerEnd + sep, len);
                _buffer.remove(0, headerEnd + sep + len);
                handleMessage(body);
                continue;
            }
            const int nl = _buffer.indexOf('\n');
            if (nl < 0)
                return;
            QByteArray line = _buffer.left(nl);
            _buffer.remove(0, nl + 1);
            if (line.endsWith('\r'))
                line.chop(1);
            if (!line.isEmpty())
                handleMessage(line);
        }
    }

    void handleMessage(const QByteArray &body)
    {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            return;
        const QJsonObject obj = doc.object();
        const bool hasMethod = obj.contains(QString::fromUtf8("method"));
        const bool hasId = obj.contains(QString::fromUtf8("id"))
                           && !obj.value(QString::fromUtf8("id")).isNull();
        if (hasMethod && hasId)
        {
            answerServer(obj);
            return;
        }
        if (hasMethod)
            return;
        if (!hasId)
            return;
        const int id = jsonId(obj.value(QString::fromUtf8("id")));
        if (id > 0)
        {
            _replies.insert(id, obj);
            if (_activeReply && !_aborting && _activeReply->isRunning())
            {
                _aborting = true;
                _activeReply->abort();
                _aborting = false;
            }
        }
    }

    void answerServer(const QJsonObject &request)
    {
        const QString method = request.value(QString::fromUtf8("method")).toString();
        QJsonObject reply;
        reply.insert(QString::fromUtf8("jsonrpc"), QString::fromUtf8("2.0"));
        reply.insert(QString::fromUtf8("id"), request.value(QString::fromUtf8("id")));
        if (method == QString::fromUtf8("ping"))
        {
            reply.insert(QString::fromUtf8("result"), QJsonObject());
        }
        else if (method == QString::fromUtf8("roots/list"))
        {
            QJsonArray roots;
            const QString root = AppSettings::instance()->projectRoot().trimmed();
            if (!root.isEmpty())
            {
                QJsonObject one;
                one.insert(QString::fromUtf8("uri"), QUrl::fromLocalFile(root).toString());
                one.insert(QString::fromUtf8("name"), QString::fromUtf8("project"));
                roots.append(one);
            }
            QJsonObject result;
            result.insert(QString::fromUtf8("roots"), roots);
            reply.insert(QString::fromUtf8("result"), result);
        }
        else
        {
            QJsonObject err;
            err.insert(QString::fromUtf8("code"), -32601);
            err.insert(QString::fromUtf8("message"), QString::fromUtf8("Method not found"));
            reply.insert(QString::fromUtf8("error"), err);
        }
        post(reply, false);
    }

    static QString formatResult(const QJsonObject &result)
    {
        QString text;
        const QJsonArray content = result.value(QString::fromUtf8("content")).toArray();
        foreach (const QJsonValue &item, content)
        {
            const QJsonObject part = item.toObject();
            const QString type = part.value(QString::fromUtf8("type")).toString();
            if (!text.isEmpty())
                text += QLatin1Char('\n');
            if (type == QString::fromUtf8("text") || part.contains(QString::fromUtf8("text")))
            {
                text += part.value(QString::fromUtf8("text")).toString();
                continue;
            }
            if (type == QString::fromUtf8("image"))
            {
                const QString mime = part.value(QString::fromUtf8("mimeType")).toString();
                const int n = part.value(QString::fromUtf8("data")).toString().size();
                text += QString::fromUtf8("[image %1, %2 chars]").arg(mime).arg(n);
                continue;
            }
            text += QString::fromUtf8(QJsonDocument(part).toJson(QJsonDocument::Compact));
        }
        if (text.isEmpty() && result.contains(QString::fromUtf8("structuredContent")))
        {
            text = QString::fromUtf8(QJsonDocument(result.value(QString::fromUtf8("structuredContent")).toObject())
                                     .toJson(QJsonDocument::Compact));
        }
        return text;
    }

    McpServerConfig          _config;
    QProcess                *_process;
    QNetworkAccessManager   *_network;
    QUrl                     _httpUrl;
    QString                  _sessionId;
    QString                  _protocol;
    QByteArray               _buffer;
    QByteArray               _sse;
    QByteArray               _stderr;
    QHash<int, QJsonObject>  _replies;
    QList<ToolRec>           _tools;
    QString                  _error;
    int                      _nextId;
    int                      _callDepth;
    bool                     _interrupt;
    bool                     _dead;
    bool                     _ready;
    QNetworkReply           *_activeReply;
    bool                     _aborting;
};

//----------------------------------------------

McpHub *McpHub::instance()
{
    static McpHub *hub = new McpHub();
    return hub;
}

//----------------------------------------------

McpHub::McpHub(QObject *parent)
    : QObject(parent)
    , _loading(false)
{
}

//----------------------------------------------

McpHub::~McpHub()
{
    qDeleteAll(_sessions);
    _sessions.clear();
}

//----------------------------------------------

QList<McpServerConfig> McpHub::servers() const
{
    return _servers;
}

//----------------------------------------------

QString McpHub::statusText(const QString &serverId) const
{
    const McpSession *session = 0;
    foreach (const McpSession *item, _sessions)
    {
        if (item && item->serverId() == serverId)
        {
            session = item;
            break;
        }
    }
    bool enabled = true;
    foreach (const McpServerConfig &config, _servers)
    {
        if (config.id == serverId)
        {
            enabled = config.enabled;
            break;
        }
    }
    if (!enabled)
        return tr("выключен");
    if (!session)
        return tr("подключение…");
    if (session->ready())
    {
        const int count = session->tools().size();
        if (count == 0)
            return tr("нет инструментов");
        return tr("%1 функций").arg(count);
    }
    const QString error = clipText(session->errorText(), 180);
    if (error.isEmpty())
        return tr("подключение…");
    return tr("ошибка: %1").arg(error);
}

//----------------------------------------------

static QString plainSecret(const QString &stored, bool *wasPlain, bool *failed)
{
    if (wasPlain)
        *wasPlain = false;
    if (failed)
        *failed = false;
    if (stored.isEmpty())
        return QString();
    if (!SecretBox::isSealed(stored))
    {
        if (wasPlain)
            *wasPlain = true;
        return stored;
    }
    bool ok = true;
    const QString plain = SecretBox::open(stored, &ok);
    if (!ok && failed)
        *failed = true;
    return plain;
}

//----------------------------------------------

static QString sealSecret(const QString &id, const QString &plain,
                          QHash<QString, QString> *kept)
{
    if (plain.isEmpty() && kept && kept->contains(id))
        return kept->value(id);
    if (kept)
        kept->remove(id);
    return SecretBox::seal(plain);
}

//----------------------------------------------

void McpHub::load()
{
    _servers.clear();
    bool reseal = false;
    {
    QSettings settings(AppSettings::configDir() + QString::fromUtf8("/mcp.ini"),
                       QSettings::IniFormat);
    settings.setIniCodec("UTF-8");
    const int count = settings.beginReadArray(QString::fromUtf8("servers"));
    for (int i = 0; i < count; ++i)
    {
        settings.setArrayIndex(i);
        McpServerConfig config;
        config.id = settings.value(QString::fromUtf8("id")).toString().trimmed();
        config.name = settings.value(QString::fromUtf8("name")).toString().trimmed();
        config.transport = settings.value(QString::fromUtf8("transport"),
                                          QString::fromUtf8("stdio")).toString().trimmed();
        config.command = settings.value(QString::fromUtf8("command")).toString();
        config.arguments = settings.value(QString::fromUtf8("arguments")).toString();
        config.workDir = settings.value(QString::fromUtf8("workDir")).toString();
        const QString storedEnv = settings.value(QString::fromUtf8("environment")).toString();
        const QString storedHeaders = settings.value(QString::fromUtf8("headers")).toString();
        bool envPlain = false;
        bool envFailed = false;
        bool headersPlain = false;
        bool headersFailed = false;
        config.environment = plainSecret(storedEnv, &envPlain, &envFailed);
        config.url = settings.value(QString::fromUtf8("url")).toString();
        config.headers = plainSecret(storedHeaders, &headersPlain, &headersFailed);
        config.enabled = settings.value(QString::fromUtf8("enabled"), true).toBool();
        if (config.id.isEmpty() || config.name.isEmpty())
            continue;
        if (envFailed && !storedEnv.isEmpty())
            _keptSealedEnv.insert(config.id, storedEnv);
        else
            _keptSealedEnv.remove(config.id);
        if (headersFailed && !storedHeaders.isEmpty())
            _keptSealedHeaders.insert(config.id, storedHeaders);
        else
            _keptSealedHeaders.remove(config.id);
        if ((envPlain && !config.environment.isEmpty())
            || (headersPlain && !config.headers.isEmpty()))
            reseal = true;
        if (config.transport != QString::fromUtf8("http"))
            config.transport = QString::fromUtf8("stdio");
        _servers.append(config);
    }
    settings.endArray();
    }
    if (reseal)
        save();
}

//----------------------------------------------

void McpHub::save() const
{
    QDir().mkpath(AppSettings::configDir());
    QSettings settings(AppSettings::configDir() + QString::fromUtf8("/mcp.ini"),
                       QSettings::IniFormat);
    settings.setIniCodec("UTF-8");
    settings.remove(QString::fromUtf8("servers"));
    settings.beginWriteArray(QString::fromUtf8("servers"));
    for (int i = 0; i < _servers.size(); ++i)
    {
        settings.setArrayIndex(i);
        const McpServerConfig &config = _servers.at(i);
        settings.setValue(QString::fromUtf8("id"), config.id);
        settings.setValue(QString::fromUtf8("name"), config.name);
        settings.setValue(QString::fromUtf8("transport"), config.transport);
        settings.setValue(QString::fromUtf8("command"), config.command);
        settings.setValue(QString::fromUtf8("arguments"), config.arguments);
        settings.setValue(QString::fromUtf8("workDir"), config.workDir);
        settings.setValue(QString::fromUtf8("environment"),
                          sealSecret(config.id, config.environment, &_keptSealedEnv));
        settings.setValue(QString::fromUtf8("url"), config.url);
        settings.setValue(QString::fromUtf8("headers"),
                          sealSecret(config.id, config.headers, &_keptSealedHeaders));
        settings.setValue(QString::fromUtf8("enabled"), config.enabled);
    }
    settings.endArray();
    settings.sync();
}

//----------------------------------------------

void McpHub::connectServer(const QString &serverId)
{
    McpServerConfig config;
    bool found = false;
    foreach (const McpServerConfig &item, _servers)
    {
        if (item.id == serverId)
        {
            config = item;
            found = true;
            break;
        }
    }
    for (int i = _sessions.size() - 1; i >= 0; --i)
    {
        McpSession *session = _sessions.at(i);
        if (!session || session->serverId() != serverId)
            continue;
        _sessions.removeAt(i);
        session->stopProcess();
        session->deleteLater();
    }
    if (!found || !config.enabled)
    {
        emit changed();
        return;
    }
    McpSession *session = new McpSession(config, 0);
    _sessions.append(session);
    emit changed();
    session->connectServer();
    emit changed();
}

//----------------------------------------------

void McpHub::sltConnectAll()
{
    if (_loading)
        return;
    _loading = true;
    load();
    reconnectAll();
    _loading = false;
}

//----------------------------------------------

void McpHub::reconnectAll()
{
    qDeleteAll(_sessions);
    _sessions.clear();
    emit changed();
    foreach (const McpServerConfig &config, _servers)
    {
        if (!config.enabled)
            continue;
        McpSession *session = new McpSession(config, 0);
        _sessions.append(session);
        emit changed();
        session->connectServer();
        emit changed();
    }
}

//----------------------------------------------

static QString freshId()
{
    QString id = QUuid::createUuid().toString();
    id.remove(QLatin1Char('{'));
    id.remove(QLatin1Char('}'));
    return id;
}

QString McpHub::addServer(McpServerConfig config)
{
    if (config.id.trimmed().isEmpty())
        config.id = freshId();
    config.name = config.name.trimmed();
    _servers.append(config);
    save();
    connectServer(config.id);
    return config.id;
}

//----------------------------------------------

void McpHub::updateServer(const McpServerConfig &config)
{
    for (int i = 0; i < _servers.size(); ++i)
    {
        if (_servers.at(i).id != config.id)
            continue;
        _servers[i] = config;
        save();
        connectServer(config.id);
        return;
    }
}

//----------------------------------------------

void McpHub::removeServer(const QString &serverId)
{
    for (int i = _servers.size() - 1; i >= 0; --i)
    {
        if (_servers.at(i).id == serverId)
            _servers.removeAt(i);
    }
    save();
    connectServer(serverId);
}

//----------------------------------------------

McpHub::McpSession *McpHub::findSession(const QString &serverId) const
{
    foreach (McpSession *session, _sessions)
    {
        if (session && session->serverId() == serverId)
            return session;
    }
    return 0;
}

//----------------------------------------------

QList<ToolOffer> McpHub::toolOffers() const
{
    QList<ToolOffer> offers;
    QStringList used;
    _bound.clear();
    foreach (const McpSession *session, _sessions)
    {
        if (!session || !session->ready())
            continue;
        QString serverName;
        foreach (const McpServerConfig &config, _servers)
        {
            if (config.id == session->serverId())
            {
                serverName = config.name.trimmed();
                break;
            }
        }
        if (serverName.isEmpty())
            serverName = session->serverId();
        const QString serverSlug = slugText(serverName, 16);
        foreach (const McpSession::ToolRec &rec, session->tools())
        {
            QString id = QString::fromUtf8("mcp_%1_%2")
                             .arg(serverSlug, slugText(rec.mcpName, 32));
            if (id.size() > 64)
                id = id.left(64);
            QString unique = id;
            int n = 2;
            while (used.contains(unique))
            {
                unique = id.left(58) + QString::fromUtf8("_%1").arg(n);
                ++n;
            }
            used.append(unique);
            ToolOffer offer;
            offer.id = unique;
            offer.title = rec.title;
            offer.description = rec.description;
            offer.libraryId = QString::fromUtf8("mcp:") + session->serverId();
            offer.libraryTitle = QString::fromUtf8("MCP: ") + serverName;
            offer.builtin = false;
            offer.decisionsOnly = false;
            offers.append(offer);
            McpBound bound;
            bound.serverId = session->serverId();
            bound.mcpName = rec.mcpName;
            bound.description = rec.description.isEmpty() ? rec.title : rec.description;
            bound.schema = rec.schema;
            _bound.insert(unique, bound);
        }
    }
    return offers;
}

//----------------------------------------------

void McpHub::addTools(Agent *agent, const QStringList &enabledIds)
{
    if (!agent)
        return;
    toolOffers();
    foreach (const QString &enabled, enabledIds)
    {
        if (agent->hasTool(enabled))
            continue;
        if (!_bound.contains(enabled))
            continue;
        const McpBound bound = _bound.value(enabled);
        agent->addTool(new McpAgentTool(enabled, bound.serverId, bound.mcpName,
                                        bound.description, bound.schema, agent));
    }
}

//----------------------------------------------

QString McpHub::call(const QString &serverId, const QString &mcpName,
                     const QString &argumentsJson, bool *ok)
{
    if (ok)
        *ok = false;
    McpSession *session = findSession(serverId);
    if (!session || !session->ready())
        return QString::fromUtf8("MCP server is not connected.");
    QJsonObject args;
    const QString trimmed = argumentsJson.trimmed();
    if (!trimmed.isEmpty())
    {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            return QString::fromUtf8("MCP arguments must be a JSON object.");
        args = doc.object();
    }
    return session->callTool(mcpName, args, ok);
}

//----------------------------------------------

void McpHub::interrupt(const QString &serverId)
{
    McpSession *session = findSession(serverId);
    if (session)
        session->interruptCall();
}

//----------------------------------------------
