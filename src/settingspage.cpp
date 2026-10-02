#include "settingspage.h"
//----------------------------------------------
#include "appsettings.h"
#include "modelprofilepresets.h"
#include "openaicompatibleclient.h"
#include "workertypes.h"
//----------------------------------------------
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

//----------------------------------------------

SettingsPage::SettingsPage(SettingsKind kind, QWidget *parent)
    : QScrollArea(parent)
    , _kind(kind)
    , _hintLabel(0)
    , _profileList(0)
    , _profileNameEdit(0)
    , _profileUrlEdit(0)
    , _profileModelEdit(0)
    , _profileTokenEdit(0)
    , _profileModalityCombo(0)
    , _profileBalanceUrlEdit(0)
    , _profileBalancePathEdit(0)
    , _addProfileButton(0)
    , _removeProfileButton(0)
    , _presetList(0)
    , _presetTypeCombo(0)
    , _presetTaskKindCombo(0)
    , _presetProfileCombo(0)
    , _rootPresetCombo(0)
    , _addPresetButton(0)
    , _removePresetButton(0)
    , _maxDepthSpin(0)
    , _maxConcurrentSpin(0)
    , _llmTimeoutSpin(0)
    , _pendingModelWindowRole(-1)
    , _projectPathLabel(0)
    , _inspectCheck(0)
    , _showForwardedCheck(0)
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
    , _updatingPresets(false)
{
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
    root->addWidget(buildHintBox());
    if (_kind == SettingsKindDefaults)
        root->addWidget(buildApiBox());
    else
        root->addWidget(buildProfilesRefBox());
    root->addWidget(buildWorkersBox());
    if (_kind == SettingsKindProject)
        root->addWidget(buildProjectBox());
    else
    {
        QGroupBox *viewBox = new QGroupBox(tr("Отображение"), this);
        _showForwardedCheck = new QCheckBox(
            tr("Показывать пересылаемый контекст между воркерами"), viewBox);
        _showForwardedCheck->setToolTip(
            tr("Отчёты дочерних воркеров и прочий пересылаемый контекст "
               "в чатах воркеров и в журнале. На историю модели не влияет."));
        _inspectCheck = new QCheckBox(tr("Просмотр запросов и ответов"), viewBox);
        _inspectCheck->setToolTip(tr("Каждый текстовый запрос и каждый ответ модели "
                                     "показываются в модальном окне. Пока окно открыто, "
                                     "данные в модель не отправляются."));
        QVBoxLayout *viewLay = new QVBoxLayout(viewBox);
        viewLay->addWidget(_showForwardedCheck);
        viewLay->addWidget(_inspectCheck);
        root->addWidget(viewBox);
        connect(_showForwardedCheck, SIGNAL(toggled(bool)),
                this, SLOT(sltShowForwardedToggled(bool)));
    }
    root->addWidget(buildSecurityBox());
    if (_kind == SettingsKindDefaults)
        root->addWidget(buildAppearanceBox());
    root->addStretch(1);
    setWidget(host);
    reloadAll();

    AppSettings *settings = AppSettings::instance();
    connect(settings, &AppSettings::alwaysCommandsChanged,
            this, &SettingsPage::refreshPermissionsList);
    connect(settings, &AppSettings::controlFlagsChanged,
            this, &SettingsPage::loadControlFlags);
    connect(settings, SIGNAL(projectSettingsChanged()),
            this, SLOT(sltProjectSettingsChanged()));
    connect(settings, SIGNAL(workerPresetsChanged()), this, SLOT(loadPresets()));
    connect(settings, SIGNAL(profilesChanged()), this, SLOT(loadPresets()));
    connect(OpenAiCompatibleClient::catalogHub(), SIGNAL(contextWindowChanged()),
            this, SLOT(sltCatalogWindowsUpdated()));
    requestContextCatalogs();
}

//----------------------------------------------

SettingsKind SettingsPage::kind() const
{
    return _kind;
}

//----------------------------------------------

void SettingsPage::reloadAll()
{
    const bool ready = (_kind == SettingsKindDefaults)
                       || AppSettings::instance()->hasProjectSettings();
    foreach (QGroupBox *box, findChildren<QGroupBox *>())
    {
        if (box->objectName() != QString::fromUtf8("projectBox"))
            box->setEnabled(ready);
    }
    if (_hintLabel)
    {
        if (_kind == SettingsKindDefaults)
        {
            _hintLabel->setText(
                tr("Настройки программы: каталог config рядом с exe. Здесь "
                   "хранятся профили моделей (config/ai). Новый проект получает "
                   "ссылки на эти профили; оформление интерфейса задаётся только "
                   "здесь."));
        }
        else if (ready)
        {
            _hintLabel->setText(
                tr("Настройки открытого проекта: каталог ___project___. Пресеты "
                   "и лимиты хранятся здесь и ссылаются на профили из настроек "
                   "программы (без копий тел профилей в папке проекта)."));
        }
        else
        {
            _hintLabel->setText(
                tr("Проект не открыт: настройки проекта недоступны. "
                   "Откройте или создайте проект через меню «Файл»."));
        }
    }
    loadAppearance();
    loadControlFlags();
    refreshPermissionsList();
    loadProfiles();
    loadWorkerLimits();
    if (_showForwardedCheck)
    {
        const QSignalBlocker blocker(_showForwardedCheck);
        _showForwardedCheck->setChecked(AppSettings::instance()->showForwardedContext());
    }
}

//----------------------------------------------

void SettingsPage::sltProjectSettingsChanged()
{
    if (_kind != SettingsKindProject)
        return;
    setProjectPath(AppSettings::instance()->projectRoot());
    reloadAll();
}

//----------------------------------------------

QWidget *SettingsPage::buildHintBox()
{
    QFrame *frame = new QFrame(this);
    frame->setFrameShape(QFrame::StyledPanel);
    _hintLabel = new QLabel(frame);
    _hintLabel->setWordWrap(true);
    QVBoxLayout *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->addWidget(_hintLabel);
    return frame;
}

//----------------------------------------------

void SettingsPage::loadWorkerLimits()
{
    AppSettings *s = AppSettings::instance();
    if (_maxDepthSpin)
    {
        const QSignalBlocker blocker(_maxDepthSpin);
        _maxDepthSpin->setValue(s->maxWorkerDepth(_kind));
    }
    if (_maxConcurrentSpin)
    {
        const QSignalBlocker blocker(_maxConcurrentSpin);
        _maxConcurrentSpin->setValue(s->maxConcurrentWorkers(_kind));
    }
    if (_llmTimeoutSpin)
    {
        const QSignalBlocker blocker(_llmTimeoutSpin);
        _llmTimeoutSpin->setValue(s->llmTimeoutSec(_kind));
    }
    static const WorkerType roles[4] = {
        WorkerTypeManager, WorkerTypeExecutor, WorkerTypeTester, WorkerTypeTool
    };
    for (int i = 0; i < 4; ++i)
    {
        if (_roleIterSpin[i])
        {
            const QSignalBlocker blocker(_roleIterSpin[i]);
            _roleIterSpin[i]->setValue(s->roleToolIterations(_kind, roles[i]));
        }
        if (_roleCapSpin[i])
        {
            const QSignalBlocker blocker(_roleCapSpin[i]);
            _roleCapSpin[i]->setValue(s->roleContextCap(_kind, roles[i]));
        }
    }
    updateRoleCapHints();
}

//----------------------------------------------

void SettingsPage::sltWorkerLimitsChanged()
{
    AppSettings *s = AppSettings::instance();
    if (_maxDepthSpin)
        s->setMaxWorkerDepth(_kind, _maxDepthSpin->value());
    if (_maxConcurrentSpin)
        s->setMaxConcurrentWorkers(_kind, _maxConcurrentSpin->value());
    if (_llmTimeoutSpin)
        s->setLlmTimeoutSec(_kind, _llmTimeoutSpin->value());
    static const WorkerType roles[4] = {
        WorkerTypeManager, WorkerTypeExecutor, WorkerTypeTester, WorkerTypeTool
    };
    for (int i = 0; i < 4; ++i)
    {
        if (_roleIterSpin[i])
            s->setRoleToolIterations(_kind, roles[i], _roleIterSpin[i]->value());
        if (_roleCapSpin[i])
            s->setRoleContextCap(_kind, roles[i], _roleCapSpin[i]->value());
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
        const LlmProfile profile = s->profileForWorkerType(_kind, roles[i]);
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
        const LlmProfile profile = AppSettings::instance()->profileForWorkerType(_kind, roles[i]);
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
        const LlmProfile profile = s->profileForWorkerType(_kind, roles[i]);
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
                                       _kind, roles[_pendingModelWindowRole]);
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

void SettingsPage::loadCurrentProfileFields()
{
    if (!_profileList)
        return;
    const LlmProfile profile = AppSettings::instance()->profileById(currentProfileId());
    const QSignalBlocker b1(_profileNameEdit);
    const QSignalBlocker b2(_profileUrlEdit);
    const QSignalBlocker b3(_profileModelEdit);
    const QSignalBlocker b4(_profileTokenEdit);
    const QSignalBlocker bMod(_profileModalityCombo);
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
    if (_profileModalityCombo)
    {
        int index = _profileModalityCombo->findData(int(profile.modality));
        if (index < 0)
            index = 0;
        _profileModalityCombo->setCurrentIndex(index);
    }
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
    loadCurrentProfileFields();
    fillPresetProfileCombo();
    updateRoleCapHints();
    _updatingProfiles = false;
    loadPresets();
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
    _profileModalityCombo = new QComboBox(box);
    _profileModalityCombo->addItem(tr("LLM (текст / агент)"), int(WorkerTaskKindLlm));
    _profileModalityCombo->addItem(tr("Генерация изображений"),
                                   int(WorkerTaskKindImageGeneration));
    _profileModalityCombo->addItem(tr("Речь (TTS/STT)"), int(WorkerTaskKindSpeech));
    _profileModalityCombo->addItem(tr("Эмбеддинги"), int(WorkerTaskKindEmbedding));
    _profileModalityCombo->addItem(tr("Другое"), int(WorkerTaskKindOther));
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
    connect(_profileModalityCombo, SIGNAL(currentIndexChanged(int)),
            this, SLOT(sltProfileFieldChanged()));
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
    form->addRow(tr("Класс задач API:"), _profileModalityCombo);
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

QWidget *SettingsPage::buildProfilesRefBox()
{
    QGroupBox *box = new QGroupBox(tr("Профили моделей"), this);
    QLabel *hint = new QLabel(
        tr("Полные профили (URL, модель, токен) хранятся только в настройках "
           "программы (config/ai). В проекте пресеты воркеров ссылаются на них "
           "по идентификатору — выберите нужный профиль в комбобоксе пресета."),
        box);
    hint->setWordWrap(true);
    QVBoxLayout *layout = new QVBoxLayout(box);
    layout->addWidget(hint);
    return box;
}

//----------------------------------------------

QWidget *SettingsPage::buildWorkersBox()
{
    QGroupBox *box = new QGroupBox(tr("Воркеры"), this);
    QVBoxLayout *layout = new QVBoxLayout(box);

    QLabel *presetHint = new QLabel(
        tr("Пресеты воркеров: тип роли + класс задач + ссылка на профиль модели "
           "из настроек программы. У профиля свой класс задач API (modality): "
           "LLM, изображения, речь, эмбеддинги. Менеджер выбирает preset_id в "
           "delegate_task и add_group_member под задачу. Уникальна комбинация "
           "тип+класс задач+профиль."), box);
    presetHint->setWordWrap(true);
    layout->addWidget(presetHint);

    _presetList = new QListWidget(box);
    _presetList->setMinimumWidth(180);
    _presetList->setMinimumHeight(120);

    _presetTypeCombo = new QComboBox(box);
    _presetTypeCombo->addItem(tr("Менеджер"), int(WorkerTypeManager));
    _presetTypeCombo->addItem(tr("Исполнитель"), int(WorkerTypeExecutor));
    _presetTypeCombo->addItem(tr("Тестировщик"), int(WorkerTypeTester));
    _presetTypeCombo->addItem(tr("Инструмент"), int(WorkerTypeTool));

    _presetTaskKindCombo = new QComboBox(box);
    _presetTaskKindCombo->addItem(tr("LLM (текст / агент)"), int(WorkerTaskKindLlm));
    _presetTaskKindCombo->addItem(tr("Генерация изображений"),
                                  int(WorkerTaskKindImageGeneration));
    _presetTaskKindCombo->addItem(tr("Речь (TTS/STT)"), int(WorkerTaskKindSpeech));
    _presetTaskKindCombo->addItem(tr("Эмбеддинги"), int(WorkerTaskKindEmbedding));
    _presetTaskKindCombo->addItem(tr("Другое"), int(WorkerTaskKindOther));

    _presetProfileCombo = new QComboBox(box);
    _presetProfileCombo->setMinimumWidth(220);

    _addPresetButton = new QPushButton(tr("Добавить"), box);
    _removePresetButton = new QPushButton(tr("Удалить"), box);

    connect(_presetList, SIGNAL(currentItemChanged(QListWidgetItem*,QListWidgetItem*)),
            this, SLOT(sltPresetSelected()));
    connect(_presetTypeCombo, SIGNAL(currentIndexChanged(int)),
            this, SLOT(sltPresetFieldChanged()));
    connect(_presetTaskKindCombo, SIGNAL(currentIndexChanged(int)),
            this, SLOT(sltPresetFieldChanged()));
    connect(_presetProfileCombo, SIGNAL(currentIndexChanged(int)),
            this, SLOT(sltPresetFieldChanged()));
    connect(_addPresetButton, SIGNAL(clicked()), this, SLOT(sltAddPreset()));
    connect(_removePresetButton, SIGNAL(clicked()), this, SLOT(sltRemovePreset()));

    QFormLayout *presetForm = new QFormLayout();
    presetForm->addRow(tr("Тип воркера:"), _presetTypeCombo);
    presetForm->addRow(tr("Класс задач:"), _presetTaskKindCombo);
    presetForm->addRow(tr("Профиль модели:"), _presetProfileCombo);

    QHBoxLayout *presetButtons = new QHBoxLayout();
    presetButtons->addWidget(_addPresetButton);
    presetButtons->addWidget(_removePresetButton);
    presetButtons->addStretch();

    QVBoxLayout *presetRight = new QVBoxLayout();
    presetRight->addLayout(presetForm);
    presetRight->addLayout(presetButtons);
    presetRight->addStretch();

    QHBoxLayout *presetRow = new QHBoxLayout();
    presetRow->addWidget(_presetList, 1);
    presetRow->addLayout(presetRight, 2);
    layout->addLayout(presetRow);

    if (_kind == SettingsKindDefaults)
    {
        _rootPresetCombo = new QComboBox(box);
        _rootPresetCombo->setMinimumWidth(280);
        _rootPresetCombo->setToolTip(
            tr("Пресет главного менеджера при создании проекта и очистке сеанса. "
               "Можно выбрать только пресет с ролью «Менеджер»."));
        connect(_rootPresetCombo, SIGNAL(currentIndexChanged(int)),
                this, SLOT(sltRootPresetChanged()));
        QLabel *rootHint = new QLabel(
            tr("Главный менеджер нового проекта: только пресет с ролью «Менеджер» "
               "(профиль модели). Копируется в проект при создании."),
            box);
        rootHint->setWordWrap(true);
        layout->addWidget(rootHint);
        QFormLayout *rootForm = new QFormLayout();
        rootForm->addRow(tr("Пресет главного менеджера:"), _rootPresetCombo);
        layout->addLayout(rootForm);
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
           "контекст модели из пресета этой роли (в том числе 1M у Qwen). "
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

QString SettingsPage::currentPresetId() const
{
    if (!_presetList || !_presetList->currentItem())
        return QString();
    return _presetList->currentItem()->data(Qt::UserRole).toString();
}

//----------------------------------------------

QString SettingsPage::presetListLabel(const WorkerPreset &preset) const
{
    QString typeName;
    switch (preset.type)
    {
    case WorkerTypeExecutor:
        typeName = tr("Исполнитель");
        break;
    case WorkerTypeTester:
        typeName = tr("Тестировщик");
        break;
    case WorkerTypeTool:
        typeName = tr("Инструмент");
        break;
    case WorkerTypeManager:
    default:
        typeName = tr("Менеджер");
        break;
    }
    QString taskName;
    switch (preset.taskKind)
    {
    case WorkerTaskKindImageGeneration:
        taskName = tr("Image");
        break;
    case WorkerTaskKindSpeech:
        taskName = tr("Speech");
        break;
    case WorkerTaskKindEmbedding:
        taskName = tr("Embedding");
        break;
    case WorkerTaskKindOther:
        taskName = tr("Other");
        break;
    case WorkerTaskKindLlm:
    default:
        taskName = tr("LLM");
        break;
    }
    const LlmProfile profile = AppSettings::instance()->profileById(preset.profileId);
    QString profileName = profile.name.trimmed();
    if (profileName.isEmpty())
        profileName = preset.profileId;
    if (profileName.isEmpty())
        profileName = tr("профиль не выбран");
    return typeName + QString::fromUtf8(" · ") + taskName
           + QString::fromUtf8(" · ") + profileName;
}

//----------------------------------------------

void SettingsPage::fillPresetProfileCombo()
{
    if (!_presetProfileCombo)
        return;
    const QString current = _presetProfileCombo->currentData().toString();
    const QSignalBlocker blocker(_presetProfileCombo);
    _presetProfileCombo->clear();
    foreach (const LlmProfile &profile, AppSettings::instance()->profiles())
    {
        QString title = profile.name.trimmed();
        if (title.isEmpty())
            title = profile.id;
        _presetProfileCombo->addItem(title, profile.id);
    }
    int index = _presetProfileCombo->findData(current);
    if (index < 0)
        index = 0;
    if (_presetProfileCombo->count() > 0)
        _presetProfileCombo->setCurrentIndex(index);
}

//----------------------------------------------

void SettingsPage::loadPresets()
{
    if (!_presetList || _updatingPresets)
        return;

    _updatingPresets = true;
    const QString keepId = currentPresetId();
    {
        const QSignalBlocker blocker(_presetList);
        _presetList->clear();
        foreach (const WorkerPreset &preset, AppSettings::instance()->workerPresets(_kind))
        {
            QListWidgetItem *item = new QListWidgetItem(presetListLabel(preset), _presetList);
            item->setData(Qt::UserRole, preset.id);
        }
        int row = 0;
        for (int i = 0; i < _presetList->count(); ++i)
        {
            if (_presetList->item(i)->data(Qt::UserRole).toString() == keepId)
            {
                row = i;
                break;
            }
        }
        if (_presetList->count() > 0)
            _presetList->setCurrentRow(row);
    }

    const QString id = currentPresetId();
    const WorkerPreset current = AppSettings::instance()->workerPresetById(_kind, id);
    if (_presetTypeCombo && !id.isEmpty())
    {
        const QSignalBlocker blocker(_presetTypeCombo);
        int index = _presetTypeCombo->findData(int(current.type));
        if (index < 0)
            index = 0;
        _presetTypeCombo->setCurrentIndex(index);
    }
    if (_presetTaskKindCombo && !id.isEmpty())
    {
        const QSignalBlocker blocker(_presetTaskKindCombo);
        int index = _presetTaskKindCombo->findData(int(current.taskKind));
        if (index < 0)
            index = 0;
        _presetTaskKindCombo->setCurrentIndex(index);
    }
    if (_presetProfileCombo && !id.isEmpty())
    {
        const QSignalBlocker blocker(_presetProfileCombo);
        const int index = _presetProfileCombo->findData(current.profileId);
        if (index >= 0)
            _presetProfileCombo->setCurrentIndex(index);
    }
    if (_removePresetButton)
        _removePresetButton->setEnabled(!id.isEmpty());
    fillRootPresetCombo();
    _updatingPresets = false;
    updateRoleCapHints();
}

//----------------------------------------------

void SettingsPage::fillRootPresetCombo()
{
    if (!_rootPresetCombo)
        return;
    AppSettings *s = AppSettings::instance();
    const QString current = s->rootWorkerPresetId(_kind);
    const QSignalBlocker blocker(_rootPresetCombo);
    _rootPresetCombo->clear();
    foreach (const WorkerPreset &preset, s->workerPresets(_kind))
    {
        if (preset.type != WorkerTypeManager)
            continue;
        _rootPresetCombo->addItem(presetListLabel(preset), preset.id);
    }
    int index = _rootPresetCombo->findData(current);
    if (index < 0)
        index = 0;
    if (_rootPresetCombo->count() > 0)
        _rootPresetCombo->setCurrentIndex(index);
}

//----------------------------------------------

void SettingsPage::sltRootPresetChanged()
{
    if (_updatingPresets || !_rootPresetCombo)
        return;
    const QString id = _rootPresetCombo->currentData().toString();
    if (!id.isEmpty())
        AppSettings::instance()->setRootWorkerPresetId(_kind, id);
}

//----------------------------------------------

void SettingsPage::sltPresetSelected()
{
    if (_updatingPresets)
        return;
    const QString id = currentPresetId();
    if (id.isEmpty())
        return;
    const WorkerPreset preset = AppSettings::instance()->workerPresetById(_kind, id);

    _updatingPresets = true;
    if (_presetTypeCombo)
    {
        const QSignalBlocker blocker(_presetTypeCombo);
        int index = _presetTypeCombo->findData(int(preset.type));
        if (index < 0)
            index = 0;
        _presetTypeCombo->setCurrentIndex(index);
    }
    if (_presetTaskKindCombo)
    {
        const QSignalBlocker blocker(_presetTaskKindCombo);
        int index = _presetTaskKindCombo->findData(int(preset.taskKind));
        if (index < 0)
            index = 0;
        _presetTaskKindCombo->setCurrentIndex(index);
    }
    if (_presetProfileCombo)
    {
        const QSignalBlocker blocker(_presetProfileCombo);
        const int index = _presetProfileCombo->findData(preset.profileId);
        if (index >= 0)
            _presetProfileCombo->setCurrentIndex(index);
    }
    if (_removePresetButton)
        _removePresetButton->setEnabled(true);
    _updatingPresets = false;
}

//----------------------------------------------

void SettingsPage::sltPresetFieldChanged()
{
    if (_updatingPresets)
        return;
    const QString id = currentPresetId();
    if (id.isEmpty())
        return;
    WorkerPreset preset = AppSettings::instance()->workerPresetById(_kind, id);
    if (preset.id.isEmpty())
        return;
    if (_presetTypeCombo)
        preset.type = WorkerType(_presetTypeCombo->currentData().toInt());
    if (_presetTaskKindCombo)
        preset.taskKind = WorkerTaskKind(_presetTaskKindCombo->currentData().toInt());
    if (_presetProfileCombo)
        preset.profileId = _presetProfileCombo->currentData().toString();

    QString error;
    AppSettings::instance()->updateWorkerPreset(_kind, preset, &error);
    if (!error.isEmpty())
        QMessageBox::warning(this, tr("Пресеты воркеров"), error);
    loadPresets();
}

//----------------------------------------------

void SettingsPage::sltAddPreset()
{
    AppSettings *s = AppSettings::instance();
    const QList<LlmProfile> profiles = s->profiles();
    if (profiles.isEmpty())
    {
        QMessageBox::warning(
            this, tr("Пресеты воркеров"),
            tr("Сначала добавьте хотя бы один профиль модели в настройках программы."));
        return;
    }

    static const WorkerType kTypes[] = {
        WorkerTypeManager, WorkerTypeExecutor, WorkerTypeTester, WorkerTypeTool
    };
    WorkerType preferType = _presetTypeCombo
                            ? WorkerType(_presetTypeCombo->currentData().toInt())
                            : WorkerTypeManager;
    WorkerTaskKind preferTask = _presetTaskKindCombo
                                ? WorkerTaskKind(_presetTaskKindCombo->currentData().toInt())
                                : WorkerTaskKindLlm;
    QString preferProfile;
    if (_presetProfileCombo)
        preferProfile = _presetProfileCombo->currentData().toString();

    QList<WorkerType> typeOrder;
    typeOrder.append(preferType);
    for (int i = 0; i < 4; ++i)
    {
        if (kTypes[i] != preferType)
            typeOrder.append(kTypes[i]);
    }

    QList<WorkerTaskKind> taskOrder;
    static const WorkerTaskKind kTaskKinds[] = {
        WorkerTaskKindLlm, WorkerTaskKindImageGeneration, WorkerTaskKindSpeech,
        WorkerTaskKindEmbedding, WorkerTaskKindOther
    };
    taskOrder.append(preferTask);
    for (int i = 0; i < 5; ++i)
    {
        if (kTaskKinds[i] != preferTask)
            taskOrder.append(kTaskKinds[i]);
    }

    QStringList profileOrder;
    if (!preferProfile.isEmpty())
        profileOrder.append(preferProfile);
    foreach (const LlmProfile &profile, profiles)
    {
        if (profile.id != preferProfile)
            profileOrder.append(profile.id);
    }

    WorkerPreset preset;
    QString error;
    QString id;
    foreach (WorkerType type, typeOrder)
    {
        foreach (WorkerTaskKind taskKind, taskOrder)
        {
            foreach (const QString &profileId, profileOrder)
            {
                if (profileId.isEmpty())
                    continue;
                preset = WorkerPreset();
                preset.type = type;
                preset.taskKind = taskKind;
                preset.profileId = profileId;
                error.clear();
                id = s->addWorkerPreset(_kind, preset, &error);
                if (!id.isEmpty())
                    break;
            }
            if (!id.isEmpty())
                break;
        }
        if (!id.isEmpty())
            break;
    }

    if (id.isEmpty())
    {
        QMessageBox::warning(
            this, tr("Пресеты воркеров"),
            tr("Все комбинации тип+класс задач+профиль уже заняты.\n"
               "Добавьте новый профиль модели или измените существующий пресет."));
        return;
    }
    loadPresets();
    if (!_presetList)
        return;
    for (int i = 0; i < _presetList->count(); ++i)
    {
        if (_presetList->item(i)->data(Qt::UserRole).toString() == id)
        {
            _presetList->setCurrentRow(i);
            break;
        }
    }
}

//----------------------------------------------

void SettingsPage::sltRemovePreset()
{
    const QString id = currentPresetId();
    if (id.isEmpty())
        return;
    AppSettings::instance()->removeWorkerPreset(_kind, id);
    loadPresets();
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
    if (_updatingProfiles || _kind != SettingsKindDefaults || !_profileList)
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
    if (_profileModalityCombo)
        profile.modality = WorkerTaskKind(_profileModalityCombo->currentData().toInt());
    if (_profileBalanceUrlEdit)
        profile.balanceUrl = _profileBalanceUrlEdit->text().trimmed();
    if (_profileBalancePathEdit)
        profile.balanceJsonPath = _profileBalancePathEdit->text().trimmed();

    _updatingProfiles = true;
    AppSettings::instance()->updateProfile(profile);
    if (_profileList->currentItem())
        _profileList->currentItem()->setText(profile.name);
    fillPresetProfileCombo();
    updateRoleCapHints();
    _updatingProfiles = false;
    loadPresets();
    emit profilesChanged();
}

//----------------------------------------------

void SettingsPage::sltAddProfile()
{
    if (_kind != SettingsKindDefaults)
        return;
    LlmProfile profile;
    if (!pickModelProfilePreset(&profile))
        return;
    if (profile.name.trimmed().isEmpty())
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
    if (_profileTokenEdit)
        _profileTokenEdit->setFocus();
    emit profilesChanged();
}

//----------------------------------------------

bool SettingsPage::pickModelProfilePreset(LlmProfile *outProfile)
{
    if (!outProfile)
        return false;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Новый профиль модели"));
    dialog.resize(640, 480);

    QLabel *hint = new QLabel(
        tr("Выберите пресет провайдера (URL, модель, класс задач). "
           "Токен API нужно ввести отдельно после создания."),
        &dialog);
    hint->setWordWrap(true);

    QComboBox *filter = new QComboBox(&dialog);
    filter->addItem(tr("Все классы"), -1);
    filter->addItem(tr("LLM"), int(WorkerTaskKindLlm));
    filter->addItem(tr("Изображения"), int(WorkerTaskKindImageGeneration));
    filter->addItem(tr("Речь"), int(WorkerTaskKindSpeech));
    filter->addItem(tr("Эмбеддинги"), int(WorkerTaskKindEmbedding));
    filter->addItem(tr("Другое"), int(WorkerTaskKindOther));

    QListWidget *list = new QListWidget(&dialog);
    list->setMinimumHeight(280);
    const QList<ModelProfilePreset> presets = knownModelProfilePresets();

    QLabel *detail = new QLabel(&dialog);
    detail->setWordWrap(true);
    detail->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto refill = [&]() {
        const int filterMod = filter->currentData().toInt();
        const QString keep = list->currentItem()
                             ? list->currentItem()->data(Qt::UserRole + 1).toString()
                             : QString();
        list->clear();
        QListWidgetItem *custom = new QListWidgetItem(tr("Пустой профиль (вручную)"), list);
        custom->setData(Qt::UserRole, -1);
        custom->setData(Qt::UserRole + 1, QString::fromUtf8("custom"));
        for (int i = 0; i < presets.size(); ++i)
        {
            const ModelProfilePreset &p = presets.at(i);
            if (filterMod >= 0 && int(p.modality) != filterMod)
                continue;
            QListWidgetItem *item = new QListWidgetItem(modelProfilePresetLabel(p), list);
            item->setData(Qt::UserRole, i);
            item->setData(Qt::UserRole + 1,
                          p.provider + QChar('|') + p.name + QChar('|') + p.model);
        }
        int row = 0;
        for (int i = 0; i < list->count(); ++i)
        {
            if (list->item(i)->data(Qt::UserRole + 1).toString() == keep)
            {
                row = i;
                break;
            }
        }
        list->setCurrentRow(row);
    };

    auto updateDetail = [&]() {
        QListWidgetItem *item = list->currentItem();
        if (!item || item->data(Qt::UserRole).toInt() < 0)
        {
            detail->setText(tr("Пустой профиль: заполните поля вручную, токен — отдельно."));
            return;
        }
        const int index = item->data(Qt::UserRole).toInt();
        if (index < 0 || index >= presets.size())
        {
            detail->clear();
            return;
        }
        const ModelProfilePreset &p = presets.at(index);
        QString text = tr("Провайдер: %1\nКласс: %2\nURL: %3\nМодель: %4")
                           .arg(p.provider,
                                workerTaskKindToString(p.modality),
                                p.baseUrl,
                                p.model);
        if (!p.balanceUrl.trimmed().isEmpty())
            text += tr("\nБаланс: %1").arg(p.balanceUrl);
        if (!p.hint.trimmed().isEmpty())
            text += QString::fromUtf8("\n") + p.hint;
        detail->setText(text);
    };

    connect(filter, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            &dialog, [&](int) { refill(); });
    connect(list, &QListWidget::currentItemChanged,
            &dialog, [&](QListWidgetItem *, QListWidgetItem *) { updateDetail(); });
    connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QVBoxLayout *root = new QVBoxLayout(&dialog);
    root->addWidget(hint);
    root->addWidget(filter);
    root->addWidget(list, 1);
    root->addWidget(detail);
    root->addWidget(buttons);
    refill();
    updateDetail();

    if (dialog.exec() != QDialog::Accepted)
        return false;

    QListWidgetItem *item = list->currentItem();
    if (!item || item->data(Qt::UserRole).toInt() < 0)
    {
        outProfile->name = tr("Новый профиль");
        outProfile->modality = WorkerTaskKindLlm;
        return true;
    }
    const int index = item->data(Qt::UserRole).toInt();
    if (index < 0 || index >= presets.size())
        return false;
    const ModelProfilePreset &p = presets.at(index);
    outProfile->name = p.name.trimmed().isEmpty() ? p.provider : p.name;
    if (!p.provider.trimmed().isEmpty()
        && !outProfile->name.contains(p.provider, Qt::CaseInsensitive))
        outProfile->name = p.provider + QString::fromUtf8(" · ") + outProfile->name;
    outProfile->baseUrl = p.baseUrl;
    outProfile->model = p.model;
    outProfile->modality = p.modality;
    outProfile->balanceUrl = p.balanceUrl;
    outProfile->balanceJsonPath = p.balanceJsonPath;
    outProfile->apiKey.clear();
    return true;
}

//----------------------------------------------

void SettingsPage::sltRemoveProfile()
{
    if (_kind != SettingsKindDefaults)
        return;
    const QString id = currentProfileId();
    if (id.isEmpty())
        return;
    AppSettings::instance()->removeProfile(id);
    loadProfiles();
    emit profilesChanged();
}

//----------------------------------------------

QWidget *SettingsPage::buildProjectBox()
{
    QGroupBox *box = new QGroupBox(tr("Проект"), this);
    box->setObjectName(QString::fromUtf8("projectBox"));
    _projectPathLabel = new QLabel(box);
    _projectPathLabel->setWordWrap(true);
    _projectPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    setProjectPath(QString());

    QVBoxLayout *layout = new QVBoxLayout(box);
    layout->addWidget(_projectPathLabel);
    return box;
}

//----------------------------------------------

QWidget *SettingsPage::buildSecurityBox()
{
    QGroupBox *box = new QGroupBox(tr("Контроль действий"), this);

    _shellControlCheck = new QCheckBox(tr("Контроль командной строки"), box);
    _shellControlCheck->setToolTip(
        tr("Запрашивать подтверждение перед выполнением команд. "
           "Список «всегда» относится к этому набору настроек."));
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
    layout->addWidget(new QLabel(tr("Выданные разрешения «всегда»:"), box));
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
        _shellControlCheck->setChecked(s->shellControlEnabled(_kind));
    if (_outsideFilesControlCheck)
        _outsideFilesControlCheck->setChecked(s->outsideFilesControlEnabled(_kind));
    if (_systemSettingsControlCheck)
        _systemSettingsControlCheck->setChecked(s->systemSettingsControlEnabled(_kind));
}

//----------------------------------------------

void SettingsPage::refreshPermissionsList()
{
    if (!_permissionsList)
        return;
    _permissionsList->clear();
    foreach (const PermissionGrant &grant, AppSettings::instance()->permissionGrants(_kind))
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
    AppSettings::instance()->removeAlwaysAllowedCommand(
        _kind, item->data(Qt::UserRole).toString());
    refreshPermissionsList();
}

//----------------------------------------------

void SettingsPage::sltControlToggled()
{
    AppSettings *s = AppSettings::instance();
    if (_shellControlCheck)
        s->setShellControlEnabled(_kind, _shellControlCheck->isChecked());
    if (_outsideFilesControlCheck)
        s->setOutsideFilesControlEnabled(_kind, _outsideFilesControlCheck->isChecked());
    if (_systemSettingsControlCheck)
        s->setSystemSettingsControlEnabled(_kind, _systemSettingsControlCheck->isChecked());
}

//----------------------------------------------

void SettingsPage::sltShowForwardedToggled(bool enabled)
{
    AppSettings::instance()->setShowForwardedContext(enabled);
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
