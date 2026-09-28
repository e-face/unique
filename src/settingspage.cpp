#include "settingspage.h"
//----------------------------------------------
#include "appsettings.h"
#include "openaicompatibleclient.h"
#include "workertypes.h"
//----------------------------------------------
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

//----------------------------------------------

SettingsPage::SettingsPage(QWidget *parent)
    : QScrollArea(parent)
    , _profileList(0)
    , _profileNameEdit(0)
    , _profileUrlEdit(0)
    , _profileModelEdit(0)
    , _profileTokenEdit(0)
    , _profileBalanceUrlEdit(0)
    , _profileBalancePathEdit(0)
    , _addProfileButton(0)
    , _removeProfileButton(0)
    , _maxDepthSpin(0)
    , _maxConcurrentSpin(0)
    , _llmTimeoutSpin(0)
    , _pendingModelWindowRole(-1)
    , _projectPathLabel(0)
    , _inspectCheck(0)
    , _shellControlCheck(0)
    , _outsideFilesControlCheck(0)
    , _systemSettingsControlCheck(0)
    , _permissionsList(0)
    , _fontSpin(0)
    , _textColorButton(0)
    , _windowColorButton(0)
    , _panelColorButton(0)
    , _accentColorButton(0)
    , _chatUserColorButton(0)
    , _chatAssistantColorButton(0)
    , _chatOtherColorButton(0)
    , _updatingProfiles(false)
{
    for (int i = 0; i < 3; ++i)
        _typeProfile[i] = 0;
    for (int i = 0; i < 4; ++i)
    {
        _roleIterSpin[i] = 0;
        _roleCapSpin[i] = 0;
        _roleCapHint[i] = 0;
        _roleCapModelButton[i] = 0;
    }

    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);

    QWidget *host = new QWidget(this);
    QVBoxLayout *root = new QVBoxLayout(host);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(14);
    root->addWidget(buildApiBox());
    root->addWidget(buildWorkersBox());
    root->addWidget(buildProjectBox());
    root->addWidget(buildSecurityBox());
    root->addWidget(buildAppearanceBox());
    root->addStretch(1);
    setWidget(host);
    loadAppearance();
    loadControlFlags();
    refreshPermissionsList();
    loadProfiles();
    loadWorkerLimits();

    AppSettings *settings = AppSettings::instance();
    connect(settings, SIGNAL(alwaysCommandsChanged()),
            this, SLOT(refreshPermissionsList()));
    connect(OpenAiCompatibleClient::catalogHub(), SIGNAL(contextWindowChanged()),
            this, SLOT(sltCatalogWindowsUpdated()));
    requestContextCatalogs();
}

//----------------------------------------------

void SettingsPage::loadWorkerLimits()
{
    AppSettings *s = AppSettings::instance();
    if (_maxDepthSpin)
    {
        const QSignalBlocker blocker(_maxDepthSpin);
        _maxDepthSpin->setValue(s->maxWorkerDepth());
    }
    if (_maxConcurrentSpin)
    {
        const QSignalBlocker blocker(_maxConcurrentSpin);
        _maxConcurrentSpin->setValue(s->maxConcurrentWorkers());
    }
    if (_llmTimeoutSpin)
    {
        const QSignalBlocker blocker(_llmTimeoutSpin);
        _llmTimeoutSpin->setValue(s->llmTimeoutSec());
    }
    static const WorkerType roles[4] = {
        WorkerTypeManager, WorkerTypeExecutor, WorkerTypeTester, WorkerTypeTool
    };
    for (int i = 0; i < 4; ++i)
    {
        if (_roleIterSpin[i])
        {
            const QSignalBlocker blocker(_roleIterSpin[i]);
            _roleIterSpin[i]->setValue(s->roleToolIterations(roles[i]));
        }
        if (_roleCapSpin[i])
        {
            const QSignalBlocker blocker(_roleCapSpin[i]);
            _roleCapSpin[i]->setValue(s->roleContextCap(roles[i]));
        }
    }
    updateRoleCapHints();
}

//----------------------------------------------

void SettingsPage::sltWorkerLimitsChanged()
{
    AppSettings *s = AppSettings::instance();
    if (_maxDepthSpin)
        s->setMaxWorkerDepth(_maxDepthSpin->value());
    if (_maxConcurrentSpin)
        s->setMaxConcurrentWorkers(_maxConcurrentSpin->value());
    if (_llmTimeoutSpin)
        s->setLlmTimeoutSec(_llmTimeoutSpin->value());
    static const WorkerType roles[4] = {
        WorkerTypeManager, WorkerTypeExecutor, WorkerTypeTester, WorkerTypeTool
    };
    for (int i = 0; i < 4; ++i)
    {
        if (_roleIterSpin[i])
            s->setRoleToolIterations(roles[i], _roleIterSpin[i]->value());
        if (_roleCapSpin[i])
            s->setRoleContextCap(roles[i], _roleCapSpin[i]->value());
    }
}

//----------------------------------------------

void SettingsPage::updateRoleCapHints()
{
    static const WorkerType roles[4] = {
        WorkerTypeManager, WorkerTypeExecutor, WorkerTypeTester, WorkerTypeTool
    };
    AppSettings *s = AppSettings::instance();
    for (int i = 0; i < 4; ++i)
    {
        if (!_roleCapHint[i])
            continue;
        const LlmProfile profile = s->profileForWorkerType(roles[i]);
        const int window = OpenAiCompatibleClient::resolvedContextWindow(
                               profile.baseUrl, profile.model);
        QString model = profile.model.trimmed();
        if (model.isEmpty())
            model = tr("модель не задана");
        const bool fromApi = OpenAiCompatibleClient::hasApiContextWindow(
                                 profile.baseUrl, profile.model);
        if (fromApi)
        {
            _roleCapHint[i]->setText(tr("окно %1: %2 ток. (из API /models)")
                                     .arg(model)
                                     .arg(window));
        }
        else
        {
            _roleCapHint[i]->setText(tr("окно %1: %2 ток. (оценка: API не отдаёт размер окна)")
                                     .arg(model)
                                     .arg(window));
        }
        if (_roleCapSpin[i])
        {
            _roleCapSpin[i]->setToolTip(
                tr("0 — полное окно модели (%1 ток.). "
                   "Можно указать точное число, в том числе равное окну. "
                   "Иначе история сжимается раньше, по этому потолку.")
                    .arg(window));
        }
        if (_roleCapModelButton[i])
        {
            _roleCapModelButton[i]->setToolTip(
                tr("Запросить GET /models и подставить окно %1 (%2 ток.).")
                    .arg(model)
                    .arg(window));
        }
    }
    requestContextCatalogs();
}

//----------------------------------------------

void SettingsPage::sltUseModelContext()
{
    QObject *src = sender();
    commitCurrentProfile();
    for (int i = 0; i < 4; ++i)
    {
        if (_roleCapModelButton[i] != src || !_roleCapSpin[i])
            continue;
        const WorkerType roles[4] = {
            WorkerTypeManager, WorkerTypeExecutor, WorkerTypeTester, WorkerTypeTool
        };
        const LlmProfile profile = AppSettings::instance()->profileForWorkerType(roles[i]);
        _pendingModelWindowRole = i;
        OpenAiCompatibleClient::fetchContextCatalog(profile.baseUrl, profile.apiKey,
                                                    profile.model);
        const int window = OpenAiCompatibleClient::resolvedContextWindow(
                               profile.baseUrl, profile.model);
        if (window > 0)
            _roleCapSpin[i]->setValue(window);
        else
            _roleCapSpin[i]->setValue(0);
        updateRoleCapHints();
        break;
    }
}

//----------------------------------------------

void SettingsPage::requestContextCatalogs()
{
    static const WorkerType roles[4] = {
        WorkerTypeManager, WorkerTypeExecutor, WorkerTypeTester, WorkerTypeTool
    };
    AppSettings *s = AppSettings::instance();
    for (int i = 0; i < 4; ++i)
    {
        const LlmProfile profile = s->profileForWorkerType(roles[i]);
        OpenAiCompatibleClient::fetchContextCatalog(profile.baseUrl, profile.apiKey,
                                                    profile.model);
    }
}

//----------------------------------------------

void SettingsPage::sltCatalogWindowsUpdated()
{
    static bool busy = false;
    if (busy)
        return;
    busy = true;
    static const WorkerType roles[4] = {
        WorkerTypeManager, WorkerTypeExecutor, WorkerTypeTester, WorkerTypeTool
    };
    if (_pendingModelWindowRole >= 0 && _pendingModelWindowRole < 4
        && _roleCapSpin[_pendingModelWindowRole])
    {
        const LlmProfile profile = AppSettings::instance()->profileForWorkerType(
                                       roles[_pendingModelWindowRole]);
        const int window = OpenAiCompatibleClient::resolvedContextWindow(
                               profile.baseUrl, profile.model);
        if (window > 0)
            _roleCapSpin[_pendingModelWindowRole]->setValue(window);
        _pendingModelWindowRole = -1;
    }
    updateRoleCapHints();
    busy = false;
}

//----------------------------------------------

QCheckBox *SettingsPage::inspectCheck() const
{
    return _inspectCheck;
}

//----------------------------------------------

void SettingsPage::setProjectPath(QString path)
{
    if (!_projectPathLabel)
        return;
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
    {
        _projectPathLabel->setText(
            tr("Проект не открыт. Откройте или создайте его через меню «Файл»."));
        return;
    }
    _projectPathLabel->setText(QDir::toNativeSeparators(trimmed));
}

//----------------------------------------------

void SettingsPage::updateColorButton(QPushButton *button, const QColor &color)
{
    if (!button)
        return;
    button->setStyleSheet(
        QString::fromUtf8("QPushButton { background:%1; color:%2; min-width:92px; }")
            .arg(color.name(),
                 color.lightness() > 140 ? QString::fromUtf8("#222")
                                         : QString::fromUtf8("#fff")));
    button->setText(color.name());
}

//----------------------------------------------

void SettingsPage::loadAppearance()
{
    AppSettings *s = AppSettings::instance();
    if (_fontSpin)
        _fontSpin->setValue(s->fontPointSize());
    updateColorButton(_textColorButton, s->textColor());
    updateColorButton(_windowColorButton, s->windowColor());
    updateColorButton(_panelColorButton, s->panelColor());
    updateColorButton(_accentColorButton, s->accentColor());
    updateColorButton(_chatUserColorButton, s->chatUserColor());
    updateColorButton(_chatAssistantColorButton, s->chatAssistantColor());
    updateColorButton(_chatOtherColorButton, s->chatOtherColor());
}

//----------------------------------------------

QString SettingsPage::currentProfileId() const
{
    if (!_profileList || !_profileList->currentItem())
        return QString();
    return _profileList->currentItem()->data(Qt::UserRole).toString();
}

//----------------------------------------------

void SettingsPage::fillWorkerCombos()
{
    AppSettings *s = AppSettings::instance();
    const QList<LlmProfile> profiles = s->profiles();
    const WorkerType types[] = { WorkerTypeManager, WorkerTypeExecutor, WorkerTypeTester };
    for (int i = 0; i < 3; ++i)
    {
        if (!_typeProfile[i])
            continue;
        const QString current = s->workerProfileId(types[i]);
        const QSignalBlocker blocker(_typeProfile[i]);
        _typeProfile[i]->clear();
        foreach (const LlmProfile &profile, profiles)
        {
            QString title = profile.name.trimmed();
            if (title.isEmpty())
                title = profile.id;
            _typeProfile[i]->addItem(title, profile.id);
        }
        int index = _typeProfile[i]->findData(current);
        if (index < 0)
            index = 0;
        if (_typeProfile[i]->count() > 0)
            _typeProfile[i]->setCurrentIndex(index);
    }
    updateRoleCapHints();
}

//----------------------------------------------

void SettingsPage::loadCurrentProfileFields()
{
    const LlmProfile profile = AppSettings::instance()->profileById(currentProfileId());
    const QSignalBlocker b1(_profileNameEdit);
    const QSignalBlocker b2(_profileUrlEdit);
    const QSignalBlocker b3(_profileModelEdit);
    const QSignalBlocker b4(_profileTokenEdit);
    const QSignalBlocker b5(_profileBalanceUrlEdit);
    const QSignalBlocker b6(_profileBalancePathEdit);
    if (_profileNameEdit)
        _profileNameEdit->setText(profile.name);
    if (_profileUrlEdit)
        _profileUrlEdit->setText(profile.baseUrl);
    if (_profileModelEdit)
        _profileModelEdit->setText(profile.model);
    if (_profileTokenEdit)
        _profileTokenEdit->setText(profile.apiKey);
    if (_profileBalanceUrlEdit)
        _profileBalanceUrlEdit->setText(profile.balanceUrl);
    if (_profileBalancePathEdit)
        _profileBalancePathEdit->setText(profile.balanceJsonPath);
    if (_removeProfileButton)
        _removeProfileButton->setEnabled(AppSettings::instance()->profiles().size() > 1);
}

//----------------------------------------------

void SettingsPage::loadProfiles()
{
    _updatingProfiles = true;
    const QString keepId = currentProfileId();
    if (_profileList)
    {
        const QSignalBlocker blocker(_profileList);
        _profileList->clear();
        foreach (const LlmProfile &profile, AppSettings::instance()->profiles())
        {
            QString title = profile.name.trimmed();
            if (title.isEmpty())
                title = profile.id;
            QListWidgetItem *item = new QListWidgetItem(title, _profileList);
            item->setData(Qt::UserRole, profile.id);
        }
        int row = 0;
        for (int i = 0; i < _profileList->count(); ++i)
        {
            if (_profileList->item(i)->data(Qt::UserRole).toString() == keepId)
            {
                row = i;
                break;
            }
        }
        if (_profileList->count() > 0)
            _profileList->setCurrentRow(row);
    }
    fillWorkerCombos();
    loadCurrentProfileFields();
    _updatingProfiles = false;
}

//----------------------------------------------

void SettingsPage::commitCurrentProfile()
{
    sltProfileFieldChanged();
}

//----------------------------------------------

QWidget *SettingsPage::buildApiBox()
{
    QGroupBox *box = new QGroupBox(tr("Профили моделей"), this);
    _profileList = new QListWidget(box);
    _profileList->setMinimumWidth(180);
    _profileList->setMinimumHeight(140);

    _profileNameEdit = new QLineEdit(box);
    _profileNameEdit->setPlaceholderText(tr("имя профиля"));
    _profileUrlEdit = new QLineEdit(box);
    _profileUrlEdit->setPlaceholderText(
        QString::fromUtf8("https://api.example.com/v1"));
    _profileModelEdit = new QLineEdit(box);
    _profileModelEdit->setPlaceholderText(tr("идентификатор модели"));
    _profileTokenEdit = new QLineEdit(box);
    _profileTokenEdit->setEchoMode(QLineEdit::Password);
    _profileTokenEdit->setPlaceholderText(tr("токен API"));
    _profileBalanceUrlEdit = new QLineEdit(box);
    _profileBalanceUrlEdit->setPlaceholderText(
        tr("пусто = авто; DeepSeek /user/balance, Qwen /api/v1/quotas"));
    _profileBalancePathEdit = new QLineEdit(box);
    _profileBalancePathEdit->setPlaceholderText(
        tr("опц. путь JSON: data.credits или balance_infos.0.total_balance"));

    _addProfileButton = new QPushButton(tr("Добавить"), box);
    _removeProfileButton = new QPushButton(tr("Удалить"), box);

    connect(_profileList, SIGNAL(currentItemChanged(QListWidgetItem*,QListWidgetItem*)),
            this, SLOT(sltProfileSelected()));
    connect(_profileNameEdit, SIGNAL(editingFinished()), this, SLOT(sltProfileFieldChanged()));
    connect(_profileUrlEdit, SIGNAL(editingFinished()), this, SLOT(sltProfileFieldChanged()));
    connect(_profileModelEdit, SIGNAL(editingFinished()), this, SLOT(sltProfileFieldChanged()));
    connect(_profileTokenEdit, SIGNAL(editingFinished()), this, SLOT(sltProfileFieldChanged()));
    connect(_profileBalanceUrlEdit, SIGNAL(editingFinished()),
            this, SLOT(sltProfileFieldChanged()));
    connect(_profileBalancePathEdit, SIGNAL(editingFinished()),
            this, SLOT(sltProfileFieldChanged()));
    connect(_addProfileButton, SIGNAL(clicked()), this, SLOT(sltAddProfile()));
    connect(_removeProfileButton, SIGNAL(clicked()), this, SLOT(sltRemoveProfile()));

    QFormLayout *form = new QFormLayout();
    form->addRow(tr("Имя профиля:"), _profileNameEdit);
    form->addRow(tr("Адрес API:"), _profileUrlEdit);
    form->addRow(tr("Модель:"), _profileModelEdit);
    form->addRow(tr("Токен:"), _profileTokenEdit);
    form->addRow(tr("URL баланса:"), _profileBalanceUrlEdit);
    form->addRow(tr("JSON путь баланса:"), _profileBalancePathEdit);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addWidget(_addProfileButton);
    buttons->addWidget(_removeProfileButton);
    buttons->addStretch();

    QVBoxLayout *right = new QVBoxLayout();
    right->addLayout(form);
    right->addLayout(buttons);
    right->addStretch();

    QHBoxLayout *row = new QHBoxLayout();
    row->addWidget(_profileList, 1);
    row->addLayout(right, 2);

    QVBoxLayout *layout = new QVBoxLayout(box);
    layout->addLayout(row);
    return box;
}

//----------------------------------------------

QWidget *SettingsPage::buildWorkersBox()
{
    QGroupBox *box = new QGroupBox(tr("Профили воркеров"), this);
    QVBoxLayout *layout = new QVBoxLayout(box);
    layout->addWidget(new QLabel(
        tr("Для каждой роли можно выбрать свой профиль модели."), box));
    for (int i = 0; i < 3; ++i)
    {
        QString typeName;
        if (i == 0)
            typeName = tr("Менеджер");
        else if (i == 1)
            typeName = tr("Исполнитель");
        else
            typeName = tr("Тестировщик");

        _typeProfile[i] = new QComboBox(box);
        _typeProfile[i]->setMinimumWidth(220);
        connect(_typeProfile[i], SIGNAL(currentIndexChanged(int)),
                this, SLOT(sltWorkerProfileChanged()));

        QHBoxLayout *row = new QHBoxLayout();
        row->addWidget(new QLabel(typeName + QString::fromUtf8(":"), box));
        row->addWidget(_typeProfile[i], 1);
        layout->addLayout(row);
    }

    QFrame *line = new QFrame(box);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    QLabel *limitsHint = new QLabel(
        tr("Ограничения дерева воркеров. Модели получают их в первом сообщении, "
           "а не в системном промпте (так префикс кешируется у провайдера): "
           "на последнем уровне менеджер раздаёт работу только исполнителям, "
           "лишние воркеры ждут в очереди."), box);
    limitsHint->setWordWrap(true);
    layout->addWidget(limitsHint);

    _maxDepthSpin = new QSpinBox(box);
    _maxDepthSpin->setRange(1, 8);
    _maxDepthSpin->setToolTip(tr("Главный чат — уровень 0. Менеджер на уровне N-1 "
                                 "может создавать только исполнителей и тестировщиков."));
    connect(_maxDepthSpin, SIGNAL(valueChanged(int)), this, SLOT(sltWorkerLimitsChanged()));

    _maxConcurrentSpin = new QSpinBox(box);
    _maxConcurrentSpin->setRange(1, 64);
    _maxConcurrentSpin->setToolTip(tr("Сколько воркеров одновременно обращаются к модели. "
                                      "Остальные стоят в очереди и стартуют, когда "
                                      "освобождается место."));
    connect(_maxConcurrentSpin, SIGNAL(valueChanged(int)),
            this, SLOT(sltWorkerLimitsChanged()));

    _llmTimeoutSpin = new QSpinBox(box);
    _llmTimeoutSpin->setRange(30, 3600);
    _llmTimeoutSpin->setSingleStep(30);
    _llmTimeoutSpin->setSuffix(tr(" с"));
    _llmTimeoutSpin->setToolTip(tr("Сколько ждать ответ модели на один запрос. "
                                   "Если сервер молчит дольше, запрос прерывается "
                                   "с ошибкой таймаута, а не висит бесконечно."));
    connect(_llmTimeoutSpin, SIGNAL(valueChanged(int)),
            this, SLOT(sltWorkerLimitsChanged()));

    QFormLayout *limits = new QFormLayout();
    limits->setContentsMargins(0, 0, 0, 0);
    limits->addRow(tr("Максимальная глубина вложенности:"), _maxDepthSpin);
    limits->addRow(tr("Одновременно работающих воркеров:"), _maxConcurrentSpin);
    limits->addRow(tr("Таймаут ответа модели:"), _llmTimeoutSpin);
    layout->addLayout(limits);

    QLabel *budgetHint = new QLabel(
        tr("Бюджеты ролей: сколько вызовов функций за один ход и потолок "
           "контекста в токенах. 0 или кнопка «окно модели» берут полный "
           "контекст выбранной для роли модели (в том числе 1M у Qwen). "
           "Сжатие истории у всех ролей одинаковое: сводку пишет сама модель, "
           "формулировка задачи от родителя остаётся дословно."), box);
    budgetHint->setWordWrap(true);
    layout->addWidget(budgetHint);

    static const char *roleTitles[4] = {
        QT_TR_NOOP("Менеджер"),
        QT_TR_NOOP("Исполнитель"),
        QT_TR_NOOP("Тестировщик"),
        QT_TR_NOOP("Инструмент")
    };
    QFormLayout *budgets = new QFormLayout();
    budgets->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < 4; ++i)
    {
        _roleIterSpin[i] = new QSpinBox(box);
        _roleIterSpin[i]->setMinimum(2);
        _roleIterSpin[i]->setMaximum(10000);
        _roleIterSpin[i]->setSingleStep(10);
        _roleIterSpin[i]->setMinimumWidth(80);
        _roleIterSpin[i]->setToolTip(tr("Максимум вызовов функций за ход (до 10000). "
                                        "После лимита воркер обязан завершить задачу."));
        connect(_roleIterSpin[i], SIGNAL(valueChanged(int)),
                this, SLOT(sltWorkerLimitsChanged()));

        _roleCapSpin[i] = new QSpinBox(box);
        _roleCapSpin[i]->setRange(0, 4000000);
        _roleCapSpin[i]->setSingleStep(1024);
        _roleCapSpin[i]->setSpecialValueText(tr("окно модели"));
        _roleCapSpin[i]->setMinimumWidth(110);
        _roleCapSpin[i]->setToolTip(tr("0 — использовать полное окно модели. "
                                       "Можно ввести точное число токенов, включая "
                                       "максимум модели."));
        connect(_roleCapSpin[i], SIGNAL(valueChanged(int)),
                this, SLOT(sltWorkerLimitsChanged()));

        _roleCapModelButton[i] = new QPushButton(tr("окно модели"), box);
        _roleCapModelButton[i]->setToolTip(tr("Подставить максимальный контекст модели этой роли."));
        connect(_roleCapModelButton[i], SIGNAL(clicked()),
                this, SLOT(sltUseModelContext()));

        _roleCapHint[i] = new QLabel(box);
        _roleCapHint[i]->setWordWrap(true);

        QHBoxLayout *row = new QHBoxLayout();
        row->addWidget(new QLabel(tr("функции:"), box));
        row->addWidget(_roleIterSpin[i]);
        row->addWidget(new QLabel(tr("контекст:"), box));
        row->addWidget(_roleCapSpin[i]);
        row->addWidget(_roleCapModelButton[i]);
        QVBoxLayout *cell = new QVBoxLayout();
        cell->setContentsMargins(0, 0, 0, 0);
        cell->setSpacing(2);
        cell->addLayout(row);
        cell->addWidget(_roleCapHint[i]);
        budgets->addRow(tr(roleTitles[i]) + QString::fromUtf8(":"), cell);
    }
    layout->addLayout(budgets);
    return box;
}

//----------------------------------------------

void SettingsPage::sltProfileSelected()
{
    if (_updatingProfiles)
        return;
    loadCurrentProfileFields();
}

//----------------------------------------------

void SettingsPage::sltProfileFieldChanged()
{
    if (_updatingProfiles)
        return;
    const QString id = currentProfileId();
    if (id.isEmpty())
        return;

    LlmProfile profile = AppSettings::instance()->profileById(id);
    if (profile.id.isEmpty())
        return;
    if (_profileNameEdit)
        profile.name = _profileNameEdit->text().trimmed();
    if (_profileUrlEdit)
        profile.baseUrl = _profileUrlEdit->text().trimmed();
    if (_profileModelEdit)
        profile.model = _profileModelEdit->text().trimmed();
    if (_profileTokenEdit)
        profile.apiKey = _profileTokenEdit->text().trimmed();
    if (_profileBalanceUrlEdit)
        profile.balanceUrl = _profileBalanceUrlEdit->text().trimmed();
    if (_profileBalancePathEdit)
        profile.balanceJsonPath = _profileBalancePathEdit->text().trimmed();

    _updatingProfiles = true;
    AppSettings::instance()->updateProfile(profile);
    if (_profileList && _profileList->currentItem())
        _profileList->currentItem()->setText(profile.name);
    fillWorkerCombos();
    _updatingProfiles = false;
    emit profilesChanged();
}

//----------------------------------------------

void SettingsPage::sltAddProfile()
{
    LlmProfile profile;
    profile.name = tr("Новый профиль");
    const QString id = AppSettings::instance()->addProfile(profile);
    loadProfiles();
    if (_profileList)
    {
        for (int i = 0; i < _profileList->count(); ++i)
        {
            if (_profileList->item(i)->data(Qt::UserRole).toString() == id)
            {
                _profileList->setCurrentRow(i);
                break;
            }
        }
    }
    if (_profileNameEdit)
        _profileNameEdit->setFocus();
    emit profilesChanged();
}

//----------------------------------------------

void SettingsPage::sltRemoveProfile()
{
    const QString id = currentProfileId();
    if (id.isEmpty())
        return;
    AppSettings::instance()->removeProfile(id);
    loadProfiles();
    emit profilesChanged();
}

//----------------------------------------------

void SettingsPage::sltWorkerProfileChanged()
{
    if (_updatingProfiles)
        return;
    const WorkerType types[] = { WorkerTypeManager, WorkerTypeExecutor, WorkerTypeTester };
    for (int i = 0; i < 3; ++i)
    {
        if (!_typeProfile[i])
            continue;
        AppSettings::instance()->setWorkerProfileId(types[i],
                                                    _typeProfile[i]->currentData().toString());
    }
    emit profilesChanged();
}

//----------------------------------------------

QWidget *SettingsPage::buildProjectBox()
{
    QGroupBox *box = new QGroupBox(tr("Проект"), this);
    _projectPathLabel = new QLabel(box);
    _projectPathLabel->setWordWrap(true);
    _projectPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    setProjectPath(QString());
    _inspectCheck = new QCheckBox(tr("Просмотр запросов и ответов"), box);
    _inspectCheck->setToolTip(tr("Каждый текстовый запрос и каждый ответ модели "
                                 "показываются в модальном окне. Пока окно открыто, "
                                 "данные в модель не отправляются."));

    QVBoxLayout *layout = new QVBoxLayout(box);
    layout->addWidget(_projectPathLabel);
    layout->addWidget(_inspectCheck);
    return box;
}

//----------------------------------------------

QWidget *SettingsPage::buildSecurityBox()
{
    QGroupBox *box = new QGroupBox(tr("Контроль действий"), this);

    _shellControlCheck = new QCheckBox(tr("Контроль командной строки"), box);
    _shellControlCheck->setToolTip(
        tr("Запрашивать подтверждение перед выполнением команд. "
           "Список «всегда» общий для всех проектов."));
    _outsideFilesControlCheck = new QCheckBox(
        tr("Контроль изменения файлов вне рабочей директории"), box);
    _outsideFilesControlCheck->setToolTip(
        tr("Запрашивать подтверждение при создании, изменении или удалении файлов вне проекта."));
    _systemSettingsControlCheck = new QCheckBox(
        tr("Контроль изменения настроек системы"), box);
    _systemSettingsControlCheck->setToolTip(
        tr("Запрашивать подтверждение для команд, затрагивающих системные настройки "
           "(reg, netsh, sc и т.п.), даже если контроль командной строки выключен."));

    connect(_shellControlCheck, SIGNAL(toggled(bool)), this, SLOT(sltControlToggled()));
    connect(_outsideFilesControlCheck, SIGNAL(toggled(bool)), this, SLOT(sltControlToggled()));
    connect(_systemSettingsControlCheck, SIGNAL(toggled(bool)), this, SLOT(sltControlToggled()));

    _permissionsList = new QListWidget(box);
    _permissionsList->setMinimumHeight(120);
    QPushButton *revoke = new QPushButton(tr("Отозвать разрешение"), box);
    connect(revoke, SIGNAL(clicked()), this, SLOT(sltRevokePermission()));

    QVBoxLayout *layout = new QVBoxLayout(box);
    layout->addWidget(_shellControlCheck);
    layout->addWidget(_outsideFilesControlCheck);
    layout->addWidget(_systemSettingsControlCheck);
    layout->addWidget(new QLabel(tr("Выданные разрешения «всегда» (для всех проектов):"), box));
    layout->addWidget(_permissionsList, 1);
    layout->addWidget(revoke);
    return box;
}

//----------------------------------------------

void SettingsPage::loadControlFlags()
{
    AppSettings *s = AppSettings::instance();
    QSignalBlocker b1(_shellControlCheck);
    QSignalBlocker b2(_outsideFilesControlCheck);
    QSignalBlocker b3(_systemSettingsControlCheck);
    if (_shellControlCheck)
        _shellControlCheck->setChecked(s->shellControlEnabled());
    if (_outsideFilesControlCheck)
        _outsideFilesControlCheck->setChecked(s->outsideFilesControlEnabled());
    if (_systemSettingsControlCheck)
        _systemSettingsControlCheck->setChecked(s->systemSettingsControlEnabled());
}

//----------------------------------------------

void SettingsPage::refreshPermissionsList()
{
    if (!_permissionsList)
        return;
    _permissionsList->clear();
    foreach (const PermissionGrant &grant, AppSettings::instance()->permissionGrants())
    {
        QString line = grant.programKey;
        if (!grant.actionText.isEmpty() && grant.actionText != grant.programKey)
            line += QString::fromUtf8(" — ") + grant.actionText;
        QListWidgetItem *item = new QListWidgetItem(line, _permissionsList);
        item->setData(Qt::UserRole, grant.programKey);
    }
}

//----------------------------------------------

void SettingsPage::sltRevokePermission()
{
    if (!_permissionsList)
        return;
    QListWidgetItem *item = _permissionsList->currentItem();
    if (!item)
        return;
    AppSettings::instance()->removeAlwaysAllowedCommand(item->data(Qt::UserRole).toString());
    refreshPermissionsList();
}

//----------------------------------------------

void SettingsPage::sltControlToggled()
{
    AppSettings *s = AppSettings::instance();
    if (_shellControlCheck)
        s->setShellControlEnabled(_shellControlCheck->isChecked());
    if (_outsideFilesControlCheck)
        s->setOutsideFilesControlEnabled(_outsideFilesControlCheck->isChecked());
    if (_systemSettingsControlCheck)
        s->setSystemSettingsControlEnabled(_systemSettingsControlCheck->isChecked());
}

//----------------------------------------------

QWidget *SettingsPage::buildAppearanceBox()
{
    QGroupBox *box = new QGroupBox(tr("Оформление"), this);
    _fontSpin = new QSpinBox(box);
    _fontSpin->setRange(8, 24);
    _fontSpin->setSuffix(QString::fromUtf8(" pt"));
    _textColorButton = new QPushButton(box);
    _windowColorButton = new QPushButton(box);
    _panelColorButton = new QPushButton(box);
    _accentColorButton = new QPushButton(box);
    _chatUserColorButton = new QPushButton(box);
    _chatAssistantColorButton = new QPushButton(box);
    _chatOtherColorButton = new QPushButton(box);
    QPushButton *apply = new QPushButton(tr("Применить оформление"), box);
    connect(_textColorButton, SIGNAL(clicked()), this, SLOT(sltPickTextColor()));
    connect(_windowColorButton, SIGNAL(clicked()), this, SLOT(sltPickWindowColor()));
    connect(_panelColorButton, SIGNAL(clicked()), this, SLOT(sltPickPanelColor()));
    connect(_accentColorButton, SIGNAL(clicked()), this, SLOT(sltPickAccentColor()));
    connect(_chatUserColorButton, SIGNAL(clicked()), this, SLOT(sltPickChatUserColor()));
    connect(_chatAssistantColorButton, SIGNAL(clicked()),
            this, SLOT(sltPickChatAssistantColor()));
    connect(_chatOtherColorButton, SIGNAL(clicked()), this, SLOT(sltPickChatOtherColor()));
    connect(apply, SIGNAL(clicked()), this, SLOT(sltApplyAppearance()));

    QFormLayout *form = new QFormLayout(box);
    form->addRow(tr("Размер шрифта:"), _fontSpin);
    form->addRow(tr("Цвет текста:"), _textColorButton);
    form->addRow(tr("Цвет фона:"), _windowColorButton);
    form->addRow(tr("Цвет панелей:"), _panelColorButton);
    form->addRow(tr("Цвет акцента:"), _accentColorButton);
    form->addRow(tr("Чат: сообщения пользователя:"), _chatUserColorButton);
    form->addRow(tr("Чат: сообщения ассистента:"), _chatAssistantColorButton);
    form->addRow(tr("Чат: прочие сообщения:"), _chatOtherColorButton);
    form->addRow(QString(), apply);
    return box;
}

//----------------------------------------------

void SettingsPage::sltPickTextColor()
{
    const QColor color = QColorDialog::getColor(AppSettings::instance()->textColor(),
                                                this, tr("Цвет текста"));
    if (color.isValid())
        updateColorButton(_textColorButton, color);
}

//----------------------------------------------

void SettingsPage::sltPickWindowColor()
{
    const QColor color = QColorDialog::getColor(AppSettings::instance()->windowColor(),
                                                this, tr("Цвет фона"));
    if (color.isValid())
        updateColorButton(_windowColorButton, color);
}

//----------------------------------------------

void SettingsPage::sltPickPanelColor()
{
    const QColor color = QColorDialog::getColor(AppSettings::instance()->panelColor(),
                                                this, tr("Цвет панелей"));
    if (color.isValid())
        updateColorButton(_panelColorButton, color);
}

//----------------------------------------------

void SettingsPage::sltPickAccentColor()
{
    const QColor color = QColorDialog::getColor(AppSettings::instance()->accentColor(),
                                                this, tr("Цвет акцента"));
    if (color.isValid())
        updateColorButton(_accentColorButton, color);
}

//----------------------------------------------

void SettingsPage::sltPickChatUserColor()
{
    const QColor color = QColorDialog::getColor(AppSettings::instance()->chatUserColor(),
                                                this, tr("Цвет сообщений пользователя"));
    if (color.isValid())
        updateColorButton(_chatUserColorButton, color);
}

//----------------------------------------------

void SettingsPage::sltPickChatAssistantColor()
{
    const QColor color = QColorDialog::getColor(AppSettings::instance()->chatAssistantColor(),
                                                this, tr("Цвет сообщений ассистента"));
    if (color.isValid())
        updateColorButton(_chatAssistantColorButton, color);
}

//----------------------------------------------

void SettingsPage::sltPickChatOtherColor()
{
    const QColor color = QColorDialog::getColor(AppSettings::instance()->chatOtherColor(),
                                                this, tr("Цвет прочих сообщений"));
    if (color.isValid())
        updateColorButton(_chatOtherColorButton, color);
}

//----------------------------------------------

void SettingsPage::sltApplyAppearance()
{
    AppSettings *s = AppSettings::instance();
    if (_fontSpin)
        s->setFontPointSize(_fontSpin->value());
    if (_textColorButton)
        s->setTextColor(QColor(_textColorButton->text()));
    if (_windowColorButton)
        s->setWindowColor(QColor(_windowColorButton->text()));
    if (_panelColorButton)
        s->setPanelColor(QColor(_panelColorButton->text()));
    if (_accentColorButton)
        s->setAccentColor(QColor(_accentColorButton->text()));
    if (_chatUserColorButton)
        s->setChatUserColor(QColor(_chatUserColorButton->text()));
    if (_chatAssistantColorButton)
        s->setChatAssistantColor(QColor(_chatAssistantColorButton->text()));
    if (_chatOtherColorButton)
        s->setChatOtherColor(QColor(_chatOtherColorButton->text()));
    s->save();
    emit appearanceApplied();
}
