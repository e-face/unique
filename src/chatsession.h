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
 * @brief Снимок открытого чата для записи на диск.
 */
struct ChatSnapshot
{
    ChatSnapshot()
        : maximized(false)
        , minimized(false)
        , autoTitle(true)
    {
    }

    QString id;
    QString title;
    QRect   geometry;
    bool    maximized;
    bool    minimized;
    bool    autoTitle;
    QString workerType;
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
 * @brief Одна беседа: клиенты провайдеров, агент и виджет вкладки.
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
     * @brief Снимок для записи. @p sealing — закрытие приложения: незавершённые
     *        вызовы инструментов помечаются прерванными.
     */
    ChatSnapshot snapshot(bool sealing = false) const;
    void applySnapshot(const ChatSnapshot &shot);
    bool contentLoaded() const;
    void ensureContentLoaded();

    /**
     * @brief Содержимое (история/лента) изменилось с последней записи на диск.
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
 * @brief Состояние вкладок и окна для проекта.
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
 * @brief Запись и чтение списка открытых чатов.
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
