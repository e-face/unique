#ifndef USAGESTATSWIDGET_H
#define USAGESTATSWIDGET_H

#include "appsettings.h"
//----------------------------------------------
#include <QStringList>
#include <QWidget>

//----------------------------------------------

/**
 * @brief Визуальная сводка UsageStats: KPI, состав токенов, доли по ролям.
 */
class UsageStatsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit UsageStatsWidget(QWidget *parent = 0);

    void setStats(const UsageStats &stats);
    /**
     * @brief Доп. строки под графиком («ключ», «значение»).
     */
    void setExtraRows(const QStringList &labels, const QStringList &values);

    virtual QSize sizeHint() const;
    virtual QSize minimumSizeHint() const;

public slots:
    void retranslateUi();

protected:
    virtual void paintEvent(QPaintEvent *event);

private:
    QString roleTitle(const QString &role) const;

    UsageStats  _stats;
    QStringList _extraLabels;
    QStringList _extraValues;
};

#endif // USAGESTATSWIDGET_H
