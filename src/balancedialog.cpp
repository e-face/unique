#include "balancedialog.h"
//----------------------------------------------
#include "appsettings.h"
#include "llmnetworkworker.h"
//----------------------------------------------
#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

//----------------------------------------------

BalanceDialog::BalanceDialog(QWidget *parent)
    : QDialog(parent)
    , _tree(0)
    , _refreshButton(0)
{
    setWindowTitle(tr("Баланс"));
    resize(560, 360);

    _tree = new QTreeWidget(this);
    _tree->setColumnCount(2);
    _tree->setHeaderLabels(QStringList() << tr("Профиль") << tr("Баланс / статус"));
    _tree->setRootIsDecorated(false);
    _tree->setUniformRowHeights(true);
    _tree->setAlternatingRowColors(true);
    _tree->setSelectionMode(QAbstractItemView::NoSelection);
    _tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _tree->header()->setStretchLastSection(true);
    _tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    _refreshButton = new QPushButton(tr("Обновить"), this);
    connect(_refreshButton, SIGNAL(clicked()), this, SLOT(sltRefresh()));

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    if (QPushButton *closeButton = buttons->button(QDialogButtonBox::Close))
        closeButton->setText(tr("Закрыть"));
    buttons->addButton(_refreshButton, QDialogButtonBox::ActionRole);
    connect(buttons, SIGNAL(rejected()), this, SLOT(reject()));
    connect(buttons, SIGNAL(accepted()), this, SLOT(accept()));

    QVBoxLayout *root = new QVBoxLayout(this);
    root->addWidget(new QLabel(tr("Баланс по профилям моделей:"), this));
    root->addWidget(_tree, 1);
    root->addWidget(buttons);

    connect(LlmNetworkWorker::instance(),
            SIGNAL(replyReady(int,QByteArray,int,QString)),
            this, SLOT(sltReplyReady(int,QByteArray,int,QString)));
}

//----------------------------------------------

void BalanceDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    sltRefresh();
}

//----------------------------------------------

QString BalanceDialog::resolveUrl(QString baseUrl, QString balanceUrl)
{
    const QString path = balanceUrl.trimmed();
    if (path.isEmpty())
        return QString();
    if (path.startsWith(QString::fromUtf8("http://"), Qt::CaseInsensitive)
        || path.startsWith(QString::fromUtf8("https://"), Qt::CaseInsensitive))
        return path;

    const QUrl base(baseUrl.trimmed());
    if (!base.isValid() || base.host().isEmpty())
        return QString();

    QString rel = path;
    if (!rel.startsWith(QChar('/')))
        rel.prepend(QChar('/'));

    // The balance path is always from the host root, not from /compatible-mode/v1.
    QUrl out;
    out.setScheme(base.scheme().isEmpty() ? QString::fromUtf8("https") : base.scheme());
    out.setHost(base.host());
    if (base.port() > 0)
        out.setPort(base.port());
    out.setPath(rel);
    return out.toString();
}

//----------------------------------------------

static bool isDashScopeProfile(const LlmProfile &profile)
{
    const QString u = profile.baseUrl.toLower();
    const QString id = profile.id.toLower();
    return u.contains(QString::fromUtf8("dashscope"))
           || u.contains(QString::fromUtf8("aliyuncs"))
           || id.contains(QString::fromUtf8("qwen"))
           || profile.name.toLower().contains(QString::fromUtf8("qwen"));
}

//----------------------------------------------

static bool looksLikeHtmlOrConsoleUrl(QString url)
{
    const QString u = url.toLower();
    if (u.contains(QString::fromUtf8("/home/")))
        return true;
    if (u.contains(QString::fromUtf8("/billing/overview")))
        return true;
    if (u.contains(QString::fromUtf8("qianwenai.com")))
        return true;
    if (u.contains(QString::fromUtf8("console.alibabacloud.com")))
        return true;
    if (u.contains(QString::fromUtf8("modelstudio.console")))
        return true;
    return false;
}

//----------------------------------------------

static QString dashScopeQuotasUrl(const LlmProfile &profile)
{
    const QUrl base(profile.baseUrl.trimmed());
    QString host = base.host();
    if (host.isEmpty())
        host = QString::fromUtf8("dashscope-intl.aliyuncs.com");
    return QString::fromUtf8("https://%1/api/v1/quotas").arg(host);
}

//----------------------------------------------

static QString effectiveBalanceUrl(const LlmProfile &profile)
{
    QString url = profile.balanceUrl.trimmed();
    if (isDashScopeProfile(profile))
    {
        if (url.isEmpty() || looksLikeHtmlOrConsoleUrl(url)
            || url.contains(QString::fromUtf8("/user/balance"), Qt::CaseInsensitive))
            return dashScopeQuotasUrl(profile);
    }
    return url;
}

//----------------------------------------------

static QJsonValue walkJsonPath(QJsonValue root, QString path)
{
    QJsonValue cur = root;
    const QStringList parts = path.split(QChar('.'), QString::SkipEmptyParts);
    foreach (const QString &part, parts)
    {
        bool isIndex = false;
        const int index = part.toInt(&isIndex);
        if (cur.isArray() && isIndex)
        {
            const QJsonArray arr = cur.toArray();
            if (index < 0 || index >= arr.size())
                return QJsonValue();
            cur = arr.at(index);
        }
        else if (cur.isObject())
            cur = cur.toObject().value(part);
        else
            return QJsonValue();
    }
    return cur;
}

//----------------------------------------------

static QString valueToBalanceText(QJsonValue value, QString currencyHint)
{
    if (value.isDouble())
        return QString::number(value.toDouble(), 'f', 2)
               + (currencyHint.isEmpty() ? QString()
                                         : (QString::fromUtf8(" ") + currencyHint));
    if (value.isString())
    {
        const QString s = value.toString().trimmed();
        if (s.isEmpty())
            return QString();
        if (!currencyHint.isEmpty() && !s.contains(currencyHint))
            return s + QString::fromUtf8(" ") + currencyHint;
        return s;
    }
    return QString();
}

//----------------------------------------------

QString BalanceDialog::parseBalance(QByteArray data, QString jsonPath, QString *failReason)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError)
    {
        if (failReason)
        {
            QByteArray sample = data.trimmed().left(120);
            sample.replace('\n', ' ');
            sample.replace('\r', ' ');
            if (data.trimmed().startsWith('<') || sample.toLower().contains("html"))
            {
                *failReason = tr(
                    "ответ HTML (нужен API баланса, не страница кабинета). "
                    "Для DashScope/Qwen: /api/v1/quotas");
            }
            else
            {
                *failReason = tr("ответ не JSON: %1 (%2)")
                                  .arg(err.errorString(),
                                       QString::fromUtf8(sample));
            }
        }
        return QString();
    }

    const QJsonValue root = doc.isObject() ? QJsonValue(doc.object())
                                           : (doc.isArray() ? QJsonValue(doc.array())
                                                            : QJsonValue());
    if (root.isObject())
    {
        const QJsonObject obj = root.toObject();
        const QString code = obj.value(QString::fromUtf8("code")).toString();
        if (!code.isEmpty() && code.compare(QString::fromUtf8("Success"), Qt::CaseInsensitive) != 0
            && code != QString::fromUtf8("0"))
        {
            const QString msg = obj.value(QString::fromUtf8("message")).toString();
            if (failReason)
                *failReason = msg.isEmpty()
                                  ? QString::fromUtf8("API code=%1").arg(code)
                                  : QString::fromUtf8("%1 (%2)").arg(msg, code);
            // Still try to pull the balance below: sometimes code and data arrive together.
        }
    }

    if (!jsonPath.trimmed().isEmpty())
    {
        const QString text = valueToBalanceText(walkJsonPath(root, jsonPath.trimmed()),
                                                QString());
        if (!text.isEmpty())
            return text;
        if (failReason)
            *failReason = tr("в JSON нет значения по пути «%1»").arg(jsonPath.trimmed());
        return QString();
    }

    if (root.isObject())
    {
        const QJsonObject obj = root.toObject();
        const QJsonArray infos = obj.value(QString::fromUtf8("balance_infos")).toArray();
        if (!infos.isEmpty())
        {
            QStringList parts;
            for (int i = 0; i < infos.size(); ++i)
            {
                const QJsonObject info = infos.at(i).toObject();
                const QString total = info.value(QString::fromUtf8("total_balance")).toString();
                const QString cur = info.value(QString::fromUtf8("currency")).toString();
                if (total.isEmpty())
                    continue;
                parts.append(cur.isEmpty() ? total : (total + QString::fromUtf8(" ") + cur));
            }
            if (!parts.isEmpty())
                return parts.join(QString::fromUtf8(", "));
        }

        const char *keys[] = {
            "total_balance", "balance", "credits", "available", "remaining", "total_credits",
            "limit_remaining", 0
        };
        for (int i = 0; keys[i]; ++i)
        {
            const QString text = valueToBalanceText(obj.value(QString::fromUtf8(keys[i])),
                                                    QString());
            if (!text.isEmpty())
                return text;
        }

        const QJsonObject dataObj = obj.value(QString::fromUtf8("data")).toObject();
        if (!dataObj.isEmpty())
        {
            // DashScope /api/v1/quotas: data.credits, data.available
            QStringList parts;
            const QString credits = valueToBalanceText(
                dataObj.value(QString::fromUtf8("credits")), QString::fromUtf8("USD"));
            const QString available = valueToBalanceText(
                dataObj.value(QString::fromUtf8("available")), QString::fromUtf8("USD"));
            if (!credits.isEmpty())
                parts.append(QString::fromUtf8("credits ") + credits);
            if (!available.isEmpty() && available != credits)
                parts.append(QString::fromUtf8("available ") + available);
            if (!parts.isEmpty())
                return parts.join(QString::fromUtf8(", "));

            for (int i = 0; keys[i]; ++i)
            {
                const QString text =
                    valueToBalanceText(dataObj.value(QString::fromUtf8(keys[i])), QString());
                if (!text.isEmpty())
                    return text;
            }
            const QJsonValue total = dataObj.value(QString::fromUtf8("total_credits"));
            const QJsonValue usage = dataObj.value(QString::fromUtf8("total_usage"));
            if (total.isDouble() || total.isString())
            {
                QString text = valueToBalanceText(total, QString());
                if (usage.isDouble() || usage.isString())
                    text += QString::fromUtf8(" (used ")
                            + valueToBalanceText(usage, QString())
                            + QChar(')');
                return text;
            }
        }
    }
    if (failReason && failReason->isEmpty())
        *failReason = tr("не удалось распознать баланс в ответе API "
                         "(задайте JSON путь баланса в профиле)");
    return QString();
}

//----------------------------------------------

void BalanceDialog::rebuildRows()
{
    _tree->clear();
    const QList<LlmProfile> profiles = AppSettings::instance()->profiles();
    foreach (const LlmProfile &profile, profiles)
    {
        QString name = profile.name.trimmed();
        if (name.isEmpty())
            name = profile.id;
        QTreeWidgetItem *item = new QTreeWidgetItem(_tree);
        item->setData(0, Qt::UserRole, profile.id);
        item->setText(0, name);
        const RowState state = _states.value(profile.id);
        updateItem(item, state);
    }
    if (_tree->topLevelItemCount() == 0)
    {
        QTreeWidgetItem *empty = new QTreeWidgetItem(_tree);
        empty->setText(0, tr("(нет профилей)"));
        empty->setText(1, tr("Добавьте профиль в настройках"));
    }
}

//----------------------------------------------

void BalanceDialog::updateItem(QTreeWidgetItem *item, const RowState &state)
{
    if (!item)
        return;
    if (state.status.isEmpty())
        item->setText(1, tr("—"));
    else
        item->setText(1, state.status);
    QColor color;
    if (state.pending)
        color = QColor(QString::fromUtf8("#6b7280"));
    else if (state.ok)
        color = QColor(QString::fromUtf8("#15803d"));
    else
        color = QColor(QString::fromUtf8("#b45309"));
    item->setForeground(1, color);
}

//----------------------------------------------

QTreeWidgetItem *BalanceDialog::itemForProfile(QString profileId) const
{
    for (int i = 0; i < _tree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *item = _tree->topLevelItem(i);
        if (item && item->data(0, Qt::UserRole).toString() == profileId)
            return item;
    }
    return 0;
}

//----------------------------------------------

void BalanceDialog::setRowStatus(QString profileId, QString status, bool ok, bool pending)
{
    RowState state;
    state.status = status;
    state.ok = ok;
    state.pending = pending;
    _states.insert(profileId, state);
    updateItem(itemForProfile(profileId), state);
}

//----------------------------------------------

void BalanceDialog::sltRefresh()
{
    _requestProfileId.clear();
    _states.clear();
    rebuildRows();

    const QList<LlmProfile> profiles = AppSettings::instance()->profiles();
    foreach (const LlmProfile &profile, profiles)
    {
        const QString configured = profile.balanceUrl.trimmed();
        const QString effective = effectiveBalanceUrl(profile);
        if (effective.isEmpty())
        {
            setRowStatus(profile.id,
                         tr("URL баланса не задан в профиле"),
                         false, false);
            continue;
        }
        if (profile.apiKey.trimmed().isEmpty())
        {
            setRowStatus(profile.id,
                         tr("не задан токен API"),
                         false, false);
            continue;
        }
        if (profile.baseUrl.trimmed().isEmpty()
            && !effective.startsWith(QString::fromUtf8("http"), Qt::CaseInsensitive))
        {
            setRowStatus(profile.id,
                         tr("не задан адрес API (base URL)"),
                         false, false);
            continue;
        }

        const QString url = resolveUrl(profile.baseUrl, effective);
        if (url.isEmpty())
        {
            setRowStatus(profile.id, tr("не удалось собрать URL запроса"), false, false);
            continue;
        }

        setRowStatus(profile.id, tr("запрос…"), false, true);
        QHash<QByteArray, QByteArray> headers;
        headers.insert("Authorization",
                       QByteArray("Bearer ") + profile.apiKey.toUtf8());
        // DashScope quotas sometimes require this header.
        if (isDashScopeProfile(profile))
            headers.insert("Content-Type", "application/json");
        const int id = LlmNetworkWorker::instance()->get(url, headers);
        _requestProfileId.insert(id, profile.id);
        Q_UNUSED(configured);
    }
}

//----------------------------------------------

void BalanceDialog::sltReplyReady(int requestId, QByteArray data, int httpStatus,
                                  QString errorText)
{
    if (!_requestProfileId.contains(requestId))
        return;
    const QString profileId = _requestProfileId.take(requestId);
    const LlmProfile profile = AppSettings::instance()->profileById(profileId);

    if (!errorText.trimmed().isEmpty())
    {
        setRowStatus(profileId,
                     tr("сетевая ошибка: %1").arg(errorText.trimmed()),
                     false, false);
        return;
    }
    if (httpStatus < 200 || httpStatus >= 300)
    {
        QString detail = QString::fromUtf8(data.trimmed().left(180));
        detail.replace(QChar('\n'), QChar(' '));
        if (detail.isEmpty())
            setRowStatus(profileId,
                         tr("HTTP %1").arg(httpStatus),
                         false, false);
        else
            setRowStatus(profileId,
                         tr("HTTP %1: %2").arg(httpStatus).arg(detail),
                         false, false);
        return;
    }

    QString failReason;
    const QString text = parseBalance(data, profile.balanceJsonPath, &failReason);
    if (!text.isEmpty())
        setRowStatus(profileId, text, true, false);
    else
        setRowStatus(profileId,
                     failReason.isEmpty() ? tr("баланс не найден в ответе") : failReason,
                     false, false);
}
