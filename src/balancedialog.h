#ifndef BALANCEDIALOG_H
#define BALANCEDIALOG_H

#include <QDialog>
#include <QHash>
#include <QMap>
#include <QString>

class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

//----------------------------------------------

/**
 * @brief Диалог баланса по всем профилям моделей.
 *        При открытии и по кнопке «Обновить» запрашивает баланс;
 *        если получить нельзя — показывает причину.
 */
class BalanceDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BalanceDialog(QWidget *parent = 0);

protected:
    virtual void showEvent(QShowEvent *event);

private slots:
    void sltRefresh();
    void sltReplyReady(int requestId, QByteArray data, int httpStatus, QString errorText);

private:
    struct RowState
    {
        QString status;
        bool    ok;
        bool    pending;
    };

    static QString resolveUrl(QString baseUrl, QString balanceUrl);
    static QString parseBalance(QByteArray data, QString jsonPath, QString *failReason);
    void rebuildRows();
    void setRowStatus(QString profileId, QString status, bool ok, bool pending);
    void updateItem(QTreeWidgetItem *item, const RowState &state);
    QTreeWidgetItem *itemForProfile(QString profileId) const;

    QTreeWidget         *_tree;
    QPushButton         *_refreshButton;
    QHash<int, QString>  _requestProfileId;
    QMap<QString, RowState> _states;
};

#endif // BALANCEDIALOG_H
