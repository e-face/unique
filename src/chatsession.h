#ifndef CHATSESSION_H
#define CHATSESSION_H

#include "conversationwidget.h"
#include "llmtypes.h"
//----------------------------------------------
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QRect>
#include <QString>
#include <QStringList>

class Agent;
class LlmClient;
class QWidget;
class Worker;

//----------------------------------------------

/**
 * @brief Snapshot of the open chat for writing to disk.
 */
struct ChatSnapshot
{
    ChatSnapshot()
        : maximized(false)
        , minimized(false)
        , autoTitle(true)
        , checkOnly(false)
    {
    }

    QString id;
    QString title;
    QRect   geometry;
    bool    maximized;
    bool    minimized;
    bool    autoTitle;
    QString workerType;
    bool    checkOnly;
    QString parentId;
    QString toolName;
    QString toolPurpose;
    QString task;
    QString workerTitle;
    QString personName;
    QString groupId;
    QString groupRole;
    QString llmProfileId;
    QString result;
    QString workerState;
    QString lastError;
    QString lastAssistant;
    QString permissionAction;
    QStringList alwaysCommands;
    QStringList changedFiles;
    QList<LlmMessage>      history;
    QList<ChatDisplayItem> display;
};

//----------------------------------------------

/**
 * @brief One conversation: provider clients, the agent, and the tab widget.
 */
class ChatSession : public QObject
{
    Q_OBJECT
public:
    explicit ChatSession(QObject *parent = 0);
    virtual ~ChatSession();

    QString id() const;
    void setId(QString id);

    QString title() const;
    void setTitle(QString title);

    bool autoTitle() const;
    void setAutoTitle(bool enabled);

    Agent *agent() const;
    ConversationWidget *widget() const;
    void setWidget(ConversationWidget *widget);

    LlmClient *client() const;

    bool hasRestoreGeometry() const;
    QRect restoreGeometry() const;
    bool restoreMaximized() const;
    bool restoreMinimized() const;

    /**
     * @brief Snapshot for writing. @p sealing means application shutdown: unfinished
     *        tool calls are marked interrupted.
     */
    ChatSnapshot snapshot(bool sealing = false) const;
    void applySnapshot(const ChatSnapshot &shot);
    bool contentLoaded() const;
    void ensureContentLoaded();

    /**
     * @brief Content (history or feed) has changed since the last write to disk.
     */
    bool contentDirty() const;
    void markContentDirty();
    void clearContentDirty();

    Worker *worker() const;
    void setWorker(Worker *worker);

private slots:
    void sltMarkDirty();

private:
    void applyContent(const ChatSnapshot &shot);
    static QString makeId();

    QString             _id;
    QString             _title;
    LlmClient          *_client;
    Agent              *_agent;
    ConversationWidget *_widget;
    Worker             *_worker;
    QRect               _restoreGeometry;
    bool                _restoreMaximized;
    bool                _restoreMinimized;
    bool                _hasRestoreGeometry;
    bool                _autoTitle;
    bool                _contentLoaded;
    bool                _contentDirty;
};

//----------------------------------------------

/**
 * @brief Tab and window state for the project.
 */
struct ProjectUiState
{
    QString     activePage;
    QStringList openTabs;
    QByteArray  geometry;
    QByteArray  windowState;
};

//----------------------------------------------

/**
 * @brief Writing and reading the list of open chats.
 */
class ChatStore
{
public:
    static QString filePath();
    static QString storeDir();
    static void setProjectRoot(QString root);
    static bool save(const QList<ChatSnapshot> &chats, QString activeId);
    static bool load(QList<ChatSnapshot> *chats, QString *activeId);
    static bool loadChat(QString id, ChatSnapshot *shot);
    static bool saveChat(const ChatSnapshot &shot);
    static void removeChat(QString id);
    static bool saveUiState(const ProjectUiState &ui);
    static bool loadUiState(ProjectUiState *ui);

private:
    static QString safeId(QString id);
    static QString indexPath();
    static QString uiStatePath();
    static QString chatsDir();
    static QString chatFilePath(QString id);
    static QJsonObject snapshotMetaToJson(const ChatSnapshot &shot);
    static ChatSnapshot snapshotMetaFromJson(const QJsonObject &object);
    static void fillContentFromJson(ChatSnapshot *shot, const QJsonObject &object);
    static bool writeJsonFile(QString path, const QJsonObject &root);
    static bool readJsonFile(QString path, QJsonObject *root);
    static bool migrateLegacyMonolith();
    static QJsonObject messageToJson(const LlmMessage &message);
    static LlmMessage messageFromJson(const QJsonObject &object);
    static QJsonObject displayToJson(const ChatDisplayItem &item);
    static ChatDisplayItem displayFromJson(const QJsonObject &object);
};

#endif // CHATSESSION_H
