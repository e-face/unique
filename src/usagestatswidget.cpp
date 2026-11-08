#include "usagestatswidget.h"
//----------------------------------------------
#include "applocale.h"
//----------------------------------------------
#include <QPainter>
#include <QPaintEvent>
#include <QtGlobal>

//----------------------------------------------

QString UsageStatsWidget::roleTitle(const QString &role) const
{
    if (role == QString::fromUtf8("root"))
        return tr("Исполнитель");
    if (role == QString::fromUtf8("manager")
        || role == QString::fromUtf8("executor")
        || role == QString::fromUtf8("tester"))
        return tr("Исполнители");
    if (role == QString::fromUtf8("tool"))
        return tr("Инструменты");
    if (role == QString::fromUtf8("other"))
        return tr("Прочее");
    return role;
}

//----------------------------------------------

static QColor roleColor(const QString &role, bool light)
{
    if (role == QString::fromUtf8("root") || role == QString::fromUtf8("manager"))
        return QColor(QString::fromUtf8(light ? "#3b82f6" : "#5aa2ff"));
    if (role == QString::fromUtf8("executor"))
        return QColor(QString::fromUtf8(light ? "#e07a5f" : "#e07a5f"));
    if (role == QString::fromUtf8("tester"))
        return QColor(QString::fromUtf8(light ? "#8b5cf6" : "#c586c0"));
    if (role == QString::fromUtf8("tool"))
        return QColor(QString::fromUtf8(light ? "#f59e0b" : "#e8a090"));
    return QColor(QString::fromUtf8(light ? "#6b7280" : "#8a8a8a"));
}

//----------------------------------------------

static QString formatCount(qint64 value)
{
    if (value >= 1000000)
        return QString::fromUtf8("%1M").arg(value / 1000000.0, 0, 'f', 1);
    if (value >= 10000)
        return QString::fromUtf8("%1k").arg(value / 1000.0, 0, 'f', 1);
    return QString::number(value);
}

//----------------------------------------------

UsageStatsWidget::UsageStatsWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    connect(AppLocale::instance(), SIGNAL(languageChanged(QString)),
            this, SLOT(retranslateUi()));
}

//----------------------------------------------

void UsageStatsWidget::retranslateUi()
{
    update();
}

//----------------------------------------------

void UsageStatsWidget::setStats(const UsageStats &stats)
{
    _stats = stats;
    updateGeometry();
    update();
}

//----------------------------------------------

void UsageStatsWidget::setExtraRows(const QStringList &labels, const QStringList &values)
{
    _extraLabels = labels;
    _extraValues = values;
    updateGeometry();
    update();
}

//----------------------------------------------

static int statsPaintHeight(int roles, int extras)
{
    // Совпадает с вертикальными шагами paintEvent, плюс нижний отступ.
    const int pad = 8;
    int y = pad;
    y += 62 + 14;
    y += 22;
    y += 18 + 6;
    y += 24;
    y += 22;
    if (roles <= 0)
        y += 24;
    else
        y += 28 * roles;
    if (extras > 0)
        y += 4 + 22 * extras;
    return y + pad;
}

//----------------------------------------------

QSize UsageStatsWidget::sizeHint() const
{
    const int extras = qMin(_extraLabels.size(), _extraValues.size());
    return QSize(520, statsPaintHeight(_stats.byRole.size(), extras));
}

//----------------------------------------------

QSize UsageStatsWidget::minimumSizeHint() const
{
    return QSize(360, statsPaintHeight(0, 0));
}

//----------------------------------------------

void UsageStatsWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    AppSettings *s = AppSettings::instance();
    const bool light = s->isLightTheme();
    const QColor text = s->textColor();
    const QColor muted = s->mutedTextColor();
    const QColor panel = light ? QColor(QString::fromUtf8("#ffffff"))
                               : s->panelColor();
    const QColor track = light ? QColor(QString::fromUtf8("#e8e8e8"))
                               : QColor(QString::fromUtf8("#3a3a3a"));
    const QColor promptC = QColor(QString::fromUtf8(light ? "#2563eb" : "#5aa2ff"));
    const QColor complC = QColor(QString::fromUtf8(light ? "#16a34a" : "#6a9955"));
    const QColor cacheC = QColor(QString::fromUtf8(light ? "#0ea5e9" : "#38bdf8"));

    const int w = width();
    const int pad = 8;
    int y = pad;

    // --- KPI cards ---
    const int cardGap = 8;
    const int cardH = 62;
    const int cardW = (w - pad * 2 - cardGap * 3) / 4;
    struct Kpi
    {
        QString title;
        QString value;
        QColor accent;
    };
    const double cachePct = (_stats.promptTokens > 0)
                                ? (100.0 * _stats.cachedPromptTokens / _stats.promptTokens)
                                : 0.0;
    const Kpi kpis[4] = {
        { tr("Запросы"), formatCount(_stats.requestCount), promptC },
        { tr("Prompt"), formatCount(_stats.promptTokens), promptC },
        { tr("Completion"), formatCount(_stats.completionTokens), complC },
        { tr("Всего"), formatCount(_stats.totalTokens),
          QColor(QString::fromUtf8(light ? "#7c3aed" : "#c586c0")) }
    };
    for (int i = 0; i < 4; ++i)
    {
        const int x = pad + i * (cardW + cardGap);
        QRect card(x, y, cardW, cardH);
        p.setPen(Qt::NoPen);
        p.setBrush(panel);
        p.drawRoundedRect(card, 8, 8);
        p.fillRect(QRect(x, y, 4, cardH), kpis[i].accent);

        QFont titleFont = font();
        titleFont.setPointSize(qMax(8, font().pointSize() - 1));
        p.setFont(titleFont);
        p.setPen(muted);
        p.drawText(QRect(x + 12, y + 8, cardW - 16, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   kpis[i].title);

        QFont valueFont = font();
        valueFont.setBold(true);
        valueFont.setPointSize(font().pointSize() + 4);
        p.setFont(valueFont);
        p.setPen(text);
        p.drawText(QRect(x + 12, y + 26, cardW - 16, 28), Qt::AlignLeft | Qt::AlignVCenter,
                   kpis[i].value);
    }
    y += cardH + 14;

    // --- Token mix bar ---
    p.setFont(font());
    p.setPen(muted);
    p.drawText(QRect(pad, y, w - pad * 2, 18), Qt::AlignLeft | Qt::AlignVCenter,
               tr("Состав токенов"));
    y += 22;

    const qint64 mixTotal = qMax(qint64(1), _stats.promptTokens + _stats.completionTokens);
    const int barH = 18;
    const int barW = w - pad * 2;
    QRect mixTrack(pad, y, barW, barH);
    p.setPen(Qt::NoPen);
    p.setBrush(track);
    p.drawRoundedRect(mixTrack, 6, 6);

    const int promptW = int(barW * double(_stats.promptTokens) / double(mixTotal));
    const int complW = barW - promptW;
    if (promptW > 0)
    {
        p.setBrush(promptC);
        p.drawRoundedRect(QRect(pad, y, qMax(4, promptW), barH), 6, 6);
        if (promptW > 8 && complW > 0)
            p.fillRect(QRect(pad + promptW - 6, y, 6, barH), promptC);
    }
    if (complW > 0 && _stats.completionTokens > 0)
    {
        p.setBrush(complC);
        const int cx = pad + promptW;
        p.drawRoundedRect(QRect(cx, y, qMax(4, complW), barH), 6, 6);
        if (promptW > 0)
            p.fillRect(QRect(cx, y, 6, barH), complC);
    }
    // Cache share of prompt as thin overlay on the left of prompt segment.
    if (_stats.promptTokens > 0 && _stats.cachedPromptTokens > 0)
    {
        const int cacheW = qMax(2, int(promptW * double(_stats.cachedPromptTokens)
                                       / double(_stats.promptTokens)));
        p.setBrush(cacheC);
        p.setOpacity(0.55);
        p.drawRoundedRect(QRect(pad, y, cacheW, barH), 6, 6);
        p.setOpacity(1.0);
    }
    y += barH + 6;

    QFont small = font();
    small.setPointSize(qMax(8, font().pointSize() - 1));
    p.setFont(small);
    p.setPen(muted);
    p.drawText(QRect(pad, y, barW, 16), Qt::AlignLeft | Qt::AlignVCenter,
               tr("prompt %1  ·  completion %2  ·  кеш префикса %3%")
                   .arg(formatCount(_stats.promptTokens))
                   .arg(formatCount(_stats.completionTokens))
                   .arg(cachePct, 0, 'f', 1));
    y += 24;

    // --- Roles ---
    p.setFont(font());
    p.setPen(muted);
    p.drawText(QRect(pad, y, w - pad * 2, 18), Qt::AlignLeft | Qt::AlignVCenter,
               tr("По ролям"));
    y += 22;

    qint64 maxRoleTokens = 1;
    for (QMap<QString, RoleUsageStats>::const_iterator it = _stats.byRole.constBegin();
         it != _stats.byRole.constEnd(); ++it)
    {
        const qint64 t = it.value().promptTokens + it.value().completionTokens;
        if (t > maxRoleTokens)
            maxRoleTokens = t;
    }

    if (_stats.byRole.isEmpty())
    {
        p.setPen(muted);
        p.drawText(QRect(pad, y, w - pad * 2, 20), Qt::AlignLeft | Qt::AlignVCenter,
                   tr("Пока нет данных"));
        y += 24;
    }
    else
    {
        const int labelW = 130;
        const int roleBarH = 12;
        for (QMap<QString, RoleUsageStats>::const_iterator it = _stats.byRole.constBegin();
             it != _stats.byRole.constEnd(); ++it)
        {
            const RoleUsageStats &role = it.value();
            const qint64 tokens = role.promptTokens + role.completionTokens;
            const QColor accent = roleColor(it.key(), light);

            p.setPen(text);
            p.setFont(small);
            p.drawText(QRect(pad, y, labelW - 6, 20), Qt::AlignLeft | Qt::AlignVCenter,
                       roleTitle(it.key()));

            const int barX = pad + labelW;
            const int avail = w - pad - barX - 88;
            QRect rTrack(barX, y + 4, avail, roleBarH);
            p.setPen(Qt::NoPen);
            p.setBrush(track);
            p.drawRoundedRect(rTrack, 4, 4);
            const int fillW = qMax(tokens > 0 ? 3 : 0,
                                   int(avail * double(tokens) / double(maxRoleTokens)));
            if (fillW > 0)
            {
                p.setBrush(accent);
                p.drawRoundedRect(QRect(barX, y + 4, fillW, roleBarH), 4, 4);
            }

            p.setPen(muted);
            p.drawText(QRect(barX + avail + 6, y, 82, 20), Qt::AlignLeft | Qt::AlignVCenter,
                       QString::fromUtf8("%1 · %2")
                           .arg(role.requests)
                           .arg(formatCount(tokens)));
            y += 28;
        }
    }

    // --- Extra rows ---
    const int n = qMin(_extraLabels.size(), _extraValues.size());
    if (n > 0)
    {
        y += 4;
        for (int i = 0; i < n; ++i)
        {
            p.setPen(muted);
            p.setFont(small);
            p.drawText(QRect(pad, y, w / 2, 18), Qt::AlignLeft | Qt::AlignVCenter,
                       _extraLabels.at(i));
            p.setPen(text);
            p.setFont(font());
            p.drawText(QRect(w / 2, y, w / 2 - pad, 18), Qt::AlignRight | Qt::AlignVCenter,
                       _extraValues.at(i));
            y += 22;
        }
    }
}
