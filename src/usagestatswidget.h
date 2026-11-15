#ifndef USAGESTATSWIDGET_H
#define USAGESTATSWIDGET_H

#include "appsettings.h"
//----------------------------------------------
#include <QStringList>
#include <QWidget>

//----------------------------------------------

/**
 * @brief Visual summary of UsageStats: KPIs, token mix, and shares by role.
 */
class UsageStatsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit UsageStatsWidget(QWidget *parent = 0);

    void setStats(const UsageStats &stats);
    /**
     * @brief Extra rows under the chart ("key", "value").
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
