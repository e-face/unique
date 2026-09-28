#ifndef JOURNALWIDGET_H
#define JOURNALWIDGET_H

#include "conversationwidget.h"
//----------------------------------------------
#include <QHash>
#include <QPointer>
#include <QString>

class Agent;

//----------------------------------------------

/**
 * @brief Журнал: сводная лента всех чатов за время работы программы.
 *
 * Сообщения пользователя и ассистента добавляются навсегда (развёрнуты).
 * Остальное (вызовы функций, делегирование, заметки, ошибки) — временные
 * свёрнутые записи: пока последняя запись временная, новая её замещает.
 * Между запусками не сохраняется.
 */
class JournalWidget : public ConversationWidget
{
    Q_OBJECT
public:
    explicit JournalWidget(QWidget *parent = 0);

    void attachAgent(Agent *agent, QString label);
    void detachAgent(Agent *agent);
    void setAgentLabel(Agent *agent, QString label);

public slots:
    void addPermanent(const QString &source, const QString &title,
                      const QString &text, bool fromUser);
    void addTransient(const QString &source, const QString &title,
                      const QString &text, const QString &kind = QString());

private slots:
    void sltJournalUser(const QString &text);
    void sltJournalAssistant(const QString &text);
    void sltJournalToolStarted(const QString &name, const QString &arguments);
    void sltJournalToolFinished(const QString &name, const QString &result, bool ok);
    void sltJournalNotice(const QString &text);
    void sltJournalFailed(const QString &error);
    void sltAgentDestroyed(QObject *object);

private:
    QString labelFor(QObject *sender) const;
    static QString shortArgs(QString arguments);

    QHash<QObject *, QString> _labels;
    QHash<QObject *, QString> _pendingArgs;
    QPointer<QWidget>         _transient;
};

#endif // JOURNALWIDGET_H
