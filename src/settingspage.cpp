#include "settingspage.h"
//----------------------------------------------
#include "applocale.h"
#include "appsettings.h"
#include "modelprofilepresets.h"
#include "openaicompatibleclient.h"
#include "workermanager.h"
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

static void setFormLabelText(QFormLayout *form, QWidget *field, const QString &text)
{
    if (!form || !field)
        return;
    QWidget *w = form->labelForField(field);
    if (QLabel *lab = qobject_cast<QLabel *>(w))
        lab->setText(text);
}

//----------------------------------------------

SettingsPage::SettingsPage(SettingsKind kind, QWidget *parent)
    : QScrollArea(parent)
    , _kind(kind)
    , _workers(0)
    , _viewBox(0)
    , _apiBox(0)
    , _profilesRefBox(0)
    , _workersBox(0)
    , _projectBox(0)
    , _securityBox(0)
    , _appearanceBox(0)
    , _languageGroup(0)
    , _languageCombo(0)
    , _hintLabel(0)
    , _presetHint(0)
    , _limitsHint(0)
    , _rootHint(0)
    , _profilesRefHint(0)
    , _permissionsHeadingLabel(0)
    , _profileForm(0)
    , _presetForm(0)
    , _rootForm(0)
    , _limitsForm(0)
    , _appearanceForm(0)
    , _languageForm(0)
    , _revokeButton(0)
    , _applyAppearanceButton(0)
    , _profileList(0)
    , _profileNameEdit(0)
    , _profileUrlEdit(0)
    , _profileModelEdit(0)
    , _profileTokenEdit(0)
    , _profileModalityCombo(0)
    , _profilePurposeEdit(0)
    , _profileBalanceUrlEdit(0)
    , _profileBalancePathEdit(0)
    , _profileToolIterSpin(0)
    , _profileCapSpin(0)
    , _profileCapHint(0)
    , _profileCapModelButton(0)
    , _addProfileButton(0)
    , _removeProfileButton(0)
    , _presetList(0)
    , _presetTypeCombo(0)
    , _presetProfileCombo(0)
    , _rootPresetCombo(0)
    , _addPresetButton(0)
    , _removePresetButton(0)
    , _maxDepthSpin(0)
    , _maxConcurrentSpin(0)
    , _llmTimeoutSpin(0)
    , _pendingProfileCapWindow(false)
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
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);

    QWidget *host = new QWidget(this);
    QVBoxLayout *root = new QVBoxLayout(host);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);
    root->addWidget(buildHintBox());
    if (_kind == SettingsKindDefaults)
        root->addWidget(buildLanguageBox());
    if (_kind == SettingsKindDefaults)
        root->addWidget(buildApiBox());
    else
        root->addWidget(buildProfilesRefBox());
    root->addWidget(buildWorkersBox());
    if (_kind == SettingsKindProject)
        root->addWidget(buildProjectBox());
    else
    {
        _viewBox = new QGroupBox(tr("Отображение"), this);
        _showForwardedCheck = new QCheckBox(
            tr("Показывать пересылаемый контекст между воркерами"), _viewBox);
        _showForwardedCheck->setToolTip(
            tr("Отчёты дочерних воркеров и прочий пересылаемый контекст "
               "в чатах воркеров и в журнале. На историю модели не влияет."));
        _inspectCheck = new QCheckBox(tr("Просмотр запросов и ответов"), _viewBox);
        _inspectCheck->setToolTip(tr("Каждый текстовый запрос и каждый ответ модели "
                                     "показываются в модальном окне. Пока окно открыто, "
                                     "данные в модель не отправляются."));
        QVBoxLayout *viewLay = new QVBoxLayout(_viewBox);
        viewLay->addWidget(_showForwardedCheck);
        viewLay->addWidget(_inspectCheck);
        root->addWidget(_viewBox);
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
    connect(AppLocale::instance(), SIGNAL(languageChanged(QString)),
            this, SLOT(retranslateUi()));
    requestContextCatalogs();
}

//----------------------------------------------

SettingsKind SettingsPage::kind() const
{
    return _kind;
}

//----------------------------------------------

void SettingsPage::setWorkerManager(WorkerManager *manager)
{
    _workers = manager;
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
            _hintLabel->setText(L("settings.hint.defaults"));
        else if (ready)
            _hintLabel->setText(L("settings.hint.project"));
        else
            _hintLabel->setText(L("settings.hint.noProject"));
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

QWidget *SettingsPage::buildLanguageBox()
{
    _languageGroup = new QGroupBox(L("settings.language.title"), this);
    _languageForm = new QFormLayout(_languageGroup);
    _languageCombo = new QComboBox(_languageGroup);
    AppLocale *locale = AppLocale::instance();
    const QStringList codes = locale->availableLanguages();
    foreach (const QString &code, codes)
    {
        if (code != QString::fromUtf8("ru")
            && code != QString::fromUtf8("en")
            && code != QString::fromUtf8("zh"))
            continue;
        _languageCombo->addItem(locale->languageDisplayName(code), code);
    }
    const QString current = AppSettings::instance()->uiLanguage();
    const int idx = _languageCombo->findData(current);
    if (idx >= 0)
        _languageCombo->setCurrentIndex(idx);
    _languageForm->addRow(L("settings.language.label"), _languageCombo);
    connect(_languageCombo, SIGNAL(currentIndexChanged(int)),
            this, SLOT(sltLanguageComboChanged(int)));
    return _languageGroup;
}

//----------------------------------------------

void SettingsPage::sltLanguageComboChanged(int index)
{
    if (!_languageCombo || index < 0)
        return;
    const QString code = _languageCombo->itemData(index).toString();
    if (code.isEmpty())
        return;
    if (code == AppLocale::instance()->language())
        return;
    AppSettings::instance()->setUiLanguage(code);
}

//----------------------------------------------

void SettingsPage::retranslateUi()
{
    if (_languageGroup)
        _languageGroup->setTitle(L("settings.language.title"));
    if (_languageForm && _languageCombo)
        setFormLabelText(_languageForm, _languageCombo, L("settings.language.label"));
    if (_languageCombo && _languageGroup)
    {
        const QSignalBlocker blocker(_languageCombo);
        const QString current = AppLocale::instance()->language();
        for (int i = 0; i < _languageCombo->count(); ++i)
        {
            const QString code = _languageCombo->itemData(i).toString();
            Q_UNUSED(code);
            _languageCombo->setItemText(i, AppLocale::instance()->languageDisplayName(
                                            _languageCombo->itemData(i).toString()));
        }
        const int idx = _languageCombo->findData(current);
        if (idx >= 0)
            _languageCombo->setCurrentIndex(idx);
    }

    if (_viewBox)
        _viewBox->setTitle(tr("Отображение"));
    if (_showForwardedCheck)
    {
        _showForwardedCheck->setText(
            tr("Показывать пересылаемый контекст между воркерами"));
        _showForwardedCheck->setToolTip(
            tr("Отчёты дочерних воркеров и прочий пересылаемый контекст "
               "в чатах воркеров и в журнале. На историю модели не влияет."));
    }
    if (_inspectCheck)
    {
        _inspectCheck->setText(tr("Просмотр запросов и ответов"));
        _inspectCheck->setToolTip(
            tr("Каждый текстовый запрос и каждый ответ модели "
               "показываются в модальном окне. Пока окно открыто, "
               "данные в модель не отправляются."));
    }

    if (_apiBox)
        _apiBox->setTitle(tr("Профили моделей"));
    if (_profileNameEdit)
        _profileNameEdit->setPlaceholderText(tr("имя профиля"));
    if (_profileModelEdit)
        _profileModelEdit->setPlaceholderText(tr("идентификатор модели"));
    if (_profileTokenEdit)
        _profileTokenEdit->setPlaceholderText(tr("токен API"));
    if (_profileBalanceUrlEdit)
        _profileBalanceUrlEdit->setPlaceholderText(
            tr("пусто = авто; DeepSeek /user/balance, Qwen /api/v1/quotas"));
    if (_profileBalancePathEdit)
        _profileBalancePathEdit->setPlaceholderText(
            tr("опц. путь JSON: data.credits или balance_infos.0.total_balance"));
    if (_addProfileButton)
        _addProfileButton->setText(tr("Добавить"));
    if (_removeProfileButton)
        _removeProfileButton->setText(tr("Удалить"));
    if (_profileForm)
    {
        setFormLabelText(_profileForm, _profileNameEdit, tr("Имя профиля:"));
        setFormLabelText(_profileForm, _profileUrlEdit, tr("Адрес API:"));
        setFormLabelText(_profileForm, _profileModelEdit, tr("Модель:"));
        setFormLabelText(_profileForm, _profileTokenEdit, tr("Токен:"));
        setFormLabelText(_profileForm, _profileModalityCombo, tr("Класс задач API:"));
        setFormLabelText(_profileForm, _profilePurposeEdit, tr("Назначение модели:"));
        setFormLabelText(_profileForm, _profileBalanceUrlEdit, tr("URL баланса:"));
        setFormLabelText(_profileForm, _profileBalancePathEdit, tr("JSON путь баланса:"));
    }
    if (_profilePurposeEdit)
        _profilePurposeEdit->setPlaceholderText(
            tr("кратко: для какой задачи эта модель, напр. генерация картинок / сегментация"));
    if (_profileModalityCombo)
    {
        const QSignalBlocker blocker(_profileModalityCombo);
        for (int i = 0; i < _profileModalityCombo->count(); ++i)
        {
            const int kind = _profileModalityCombo->itemData(i).toInt();
            QString text;
            switch (WorkerTaskKind(kind))
            {
            case WorkerTaskKindImageGeneration:
                text = tr("Генерация изображений");
                break;
            case WorkerTaskKindSpeech:
                text = tr("Речь (TTS/STT)");
                break;
            case WorkerTaskKindAudioGeneration:
                text = tr("Генерация аудио");
                break;
            case WorkerTaskKindVideoGeneration:
                text = tr("Генерация видео");
                break;
            case WorkerTaskKindEmbedding:
                text = tr("Эмбеддинги");
                break;
            case WorkerTaskKindDecisions:
                text = tr("Decisions (решения)");
                break;
            case WorkerTaskKindOther:
                text = tr("Другое");
                break;
            case WorkerTaskKindLlm:
            default:
                text = tr("LLM (текст / агент)");
                break;
            }
            _profileModalityCombo->setItemText(i, text);
        }
    }

    if (_profilesRefBox)
        _profilesRefBox->setTitle(tr("Профили моделей"));
    if (_profilesRefHint)
        _profilesRefHint->setText(L("settings.hint.profilesRef"));

    if (_workersBox)
        _workersBox->setTitle(tr("Воркеры"));
    if (_presetHint)
        _presetHint->setText(L("settings.hint.presets"));
    if (_addPresetButton)
        _addPresetButton->setText(tr("Добавить"));
    if (_removePresetButton)
        _removePresetButton->setText(tr("Удалить"));
    if (_presetForm)
    {
        setFormLabelText(_presetForm, _presetTypeCombo, tr("Тип воркера:"));
        setFormLabelText(_presetForm, _presetProfileCombo, tr("Профиль модели:"));
    }
    if (_presetTypeCombo)
    {
        const QSignalBlocker blocker(_presetTypeCombo);
        for (int i = 0; i < _presetTypeCombo->count(); ++i)
        {
            const WorkerType type = WorkerType(_presetTypeCombo->itemData(i).toInt());
            QString text;
            switch (type)
            {
            case WorkerTypeExecutor:
                text = tr("Исполнитель");
                break;
            case WorkerTypeTester:
                text = tr("Тестировщик");
                break;
            case WorkerTypeTool:
                text = tr("Инструмент");
                break;
            case WorkerTypeManager:
            default:
                text = tr("Менеджер");
                break;
            }
            _presetTypeCombo->setItemText(i, text);
        }
    }
    if (_rootPresetCombo)
    {
        _rootPresetCombo->setToolTip(
            tr("Пресет главного менеджера при создании проекта и очистке сеанса. "
               "Можно выбрать только пресет с ролью «Менеджер»."));
    }
    if (_rootHint)
        _rootHint->setText(L("settings.hint.root"));
    if (_rootForm && _rootPresetCombo)
        setFormLabelText(_rootForm, _rootPresetCombo, tr("Пресет главного менеджера:"));

    if (_limitsHint)
        _limitsHint->setText(L("settings.hint.limits"));
    if (_maxDepthSpin)
        _maxDepthSpin->setToolTip(tr("Главный чат — уровень 0. Менеджер на уровне N-1 "
                                       "может создавать только исполнителей и тестировщиков."));
    if (_maxConcurrentSpin)
        _maxConcurrentSpin->setToolTip(tr("Сколько воркеров одновременно обращаются к модели. "
                                          "Остальные стоят в очереди и стартуют, когда "
                                          "освобождается место."));
    if (_llmTimeoutSpin)
    {
        _llmTimeoutSpin->setSuffix(tr(" с"));
        _llmTimeoutSpin->setToolTip(tr("Сколько ждать ответ модели на один запрос. "
                                       "Если сервер молчит дольше, запрос прерывается "
                                       "с ошибкой таймаута, а не висит бесконечно."));
    }
    if (_limitsForm)
    {
        setFormLabelText(_limitsForm, _maxDepthSpin, tr("Максимальная глубина вложенности:"));
        setFormLabelText(_limitsForm, _maxConcurrentSpin,
                         tr("Одновременно работающих воркеров:"));
        setFormLabelText(_limitsForm, _llmTimeoutSpin, tr("Таймаут ответа модели:"));
    }

    if (_profileForm)
    {
        setFormLabelText(_profileForm, _profileToolIterSpin,
                         tr("Лимит вызовов функций:"));
        setFormLabelText(_profileForm, _profileCapSpin, tr("Потолок контекста:"));
    }
    if (_profileToolIterSpin)
    {
        _profileToolIterSpin->setToolTip(tr("Максимум вызовов функций за ход (до 10000). "
                                            "После лимита воркер обязан завершить задачу."));
    }
    if (_profileCapSpin)
        _profileCapSpin->setSpecialValueText(tr("окно модели"));
    if (_profileCapModelButton)
        _profileCapModelButton->setText(tr("окно модели"));

    if (_projectBox)
        _projectBox->setTitle(tr("Проект"));

    if (_securityBox)
        _securityBox->setTitle(tr("Контроль действий"));
    if (_shellControlCheck)
    {
        _shellControlCheck->setText(tr("Контроль командной строки"));
        _shellControlCheck->setToolTip(
            tr("Запрашивать подтверждение перед выполнением команд. "
               "Список «всегда» относится к этому набору настроек."));
    }
    if (_outsideFilesControlCheck)
    {
        _outsideFilesControlCheck->setText(
            tr("Контроль изменения файлов вне рабочей директории"));
        _outsideFilesControlCheck->setToolTip(
            tr("Запрашивать подтверждение при создании, изменении или удалении "
               "файлов вне проекта."));
    }
    if (_systemSettingsControlCheck)
    {
        _systemSettingsControlCheck->setText(tr("Контроль изменения настроек системы"));
        _systemSettingsControlCheck->setToolTip(
            tr("Запрашивать подтверждение для команд, затрагивающих системные настройки "
               "(reg, netsh, sc и т.п.), даже если контроль командной строки выключен."));
    }
    if (_permissionsHeadingLabel)
        _permissionsHeadingLabel->setText(tr("Выданные разрешения «всегда»:"));
    if (_revokeButton)
        _revokeButton->setText(tr("Отозвать разрешение"));

    if (_appearanceBox)
        _appearanceBox->setTitle(tr("Оформление"));
    if (_applyAppearanceButton)
        _applyAppearanceButton->setText(tr("Применить оформление"));
    if (_appearanceForm)
    {
        setFormLabelText(_appearanceForm, _fontSpin, tr("Размер шрифта:"));
        setFormLabelText(_appearanceForm, _textColorButton, tr("Цвет текста:"));
        setFormLabelText(_appearanceForm, _windowColorButton, tr("Цвет фона:"));
        setFormLabelText(_appearanceForm, _panelColorButton, tr("Цвет панелей:"));
        setFormLabelText(_appearanceForm, _accentColorButton, tr("Цвет акцента:"));
        setFormLabelText(_appearanceForm, _chatUserColorButton,
                         tr("Чат: сообщения пользователя:"));
        setFormLabelText(_appearanceForm, _chatAssistantColorButton,
                         tr("Чат: сообщения ассистента:"));
        setFormLabelText(_appearanceForm, _chatOtherColorButton,
                         tr("Чат: прочие сообщения:"));
    }

    reloadAll();
    updateProfileCapHint();
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
}

//----------------------------------------------

void SettingsPage::updateProfileCapHint()
{
    if (_kind != SettingsKindDefaults || !_profileCapHint)
        return;
    const LlmProfile profile = AppSettings::instance()->profileById(currentProfileId());
    if (profile.id.isEmpty())
    {
        _profileCapHint->clear();
        return;
    }
    const int window = OpenAiCompatibleClient::resolvedContextWindow(
                           profile.baseUrl, profile.model);
    QString model = profile.model.trimmed();
    if (model.isEmpty())
        model = tr("модель не задана");
    const bool fromApi = OpenAiCompatibleClient::hasApiContextWindow(
                             profile.baseUrl, profile.model);
    if (fromApi)
    {
        _profileCapHint->setText(tr("окно %1: %2 ток. (из API /models)")
                                 .arg(model)
                                 .arg(window));
    }
    else
    {
        _profileCapHint->setText(tr("окно %1: %2 ток. (оценка: API не отдаёт размер окна)")
                                 .arg(model)
                                 .arg(window));
    }
    if (_profileCapSpin)
    {
        _profileCapSpin->setToolTip(
            tr("0 — полное окно модели (%1 ток.). "
               "Можно указать точное число, в том числе равное окну. "
               "Иначе история сжимается раньше, по этому потолку.")
                .arg(window));
    }
    if (_profileCapModelButton)
    {
        _profileCapModelButton->setToolTip(
            tr("Запросить GET /models и подставить окно %1 (%2 ток.).")
                .arg(model)
                .arg(window));
    }
    requestContextCatalogs();
}

//----------------------------------------------

void SettingsPage::sltUseModelContext()
{
    if (_kind != SettingsKindDefaults || sender() != _profileCapModelButton || !_profileCapSpin)
        return;
    commitCurrentProfile();
    const LlmProfile profile = AppSettings::instance()->profileById(currentProfileId());
    if (profile.id.isEmpty())
        return;
    _pendingProfileCapWindow = true;
    OpenAiCompatibleClient::fetchContextCatalog(profile.baseUrl, profile.apiKey,
                                                profile.model);
    const int window = OpenAiCompatibleClient::resolvedContextWindow(
                           profile.baseUrl, profile.model);
    if (window > 0)
        _profileCapSpin->setValue(window);
    else
        _profileCapSpin->setValue(0);
    sltProfileFieldChanged();
    updateProfileCapHint();
}

//----------------------------------------------

void SettingsPage::requestContextCatalogs()
{
    if (_kind != SettingsKindDefaults)
        return;
    foreach (const LlmProfile &profile, AppSettings::instance()->profiles())
    {
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
    if (_pendingProfileCapWindow && _profileCapSpin)
    {
        const LlmProfile profile = AppSettings::instance()->profileById(currentProfileId());
        const int window = OpenAiCompatibleClient::resolvedContextWindow(
                               profile.baseUrl, profile.model);
        if (window > 0)
            _profileCapSpin->setValue(window);
        _pendingProfileCapWindow = false;
        sltProfileFieldChanged();
    }
    updateProfileCapHint();
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
    const QSignalBlocker bPurpose(_profilePurposeEdit);
    const QSignalBlocker b5(_profileBalanceUrlEdit);
    const QSignalBlocker b6(_profileBalancePathEdit);
    const QSignalBlocker b7(_profileToolIterSpin);
    const QSignalBlocker b8(_profileCapSpin);
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
    if (_profilePurposeEdit)
        _profilePurposeEdit->setText(profile.purpose);
    if (_profileBalanceUrlEdit)
        _profileBalanceUrlEdit->setText(profile.balanceUrl);
    if (_profileBalancePathEdit)
        _profileBalancePathEdit->setText(profile.balanceJsonPath);
    if (_profileToolIterSpin)
        _profileToolIterSpin->setValue(profile.maxToolIterations);
    if (_profileCapSpin)
        _profileCapSpin->setValue(profile.contextCap);
    if (_removeProfileButton)
        _removeProfileButton->setEnabled(AppSettings::instance()->profiles().size() > 1);
    updateProfileCapHint();
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
    updateProfileCapHint();
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
    _apiBox = new QGroupBox(tr("Профили моделей"), this);
    QGroupBox *box = _apiBox;
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
    _profileModalityCombo->addItem(tr("Генерация аудио"), int(WorkerTaskKindAudioGeneration));
    _profileModalityCombo->addItem(tr("Генерация видео"), int(WorkerTaskKindVideoGeneration));
    _profileModalityCombo->addItem(tr("Эмбеддинги"), int(WorkerTaskKindEmbedding));
    _profileModalityCombo->addItem(tr("Decisions (решения)"), int(WorkerTaskKindDecisions));
    _profileModalityCombo->addItem(tr("Другое"), int(WorkerTaskKindOther));
    _profilePurposeEdit = new QLineEdit(box);
    _profilePurposeEdit->setPlaceholderText(
        tr("кратко: для какой задачи эта модель, напр. генерация картинок / сегментация"));
    _profileBalanceUrlEdit = new QLineEdit(box);
    _profileBalanceUrlEdit->setPlaceholderText(
        tr("пусто = авто; DeepSeek /user/balance, Qwen /api/v1/quotas"));
    _profileBalancePathEdit = new QLineEdit(box);
    _profileBalancePathEdit->setPlaceholderText(
        tr("опц. путь JSON: data.credits или balance_infos.0.total_balance"));

    _profileToolIterSpin = new QSpinBox(box);
    _profileToolIterSpin->setMinimum(2);
    _profileToolIterSpin->setMaximum(10000);
    _profileToolIterSpin->setSingleStep(10);
    _profileToolIterSpin->setMinimumWidth(80);

    _profileCapSpin = new QSpinBox(box);
    _profileCapSpin->setRange(0, 4000000);
    _profileCapSpin->setSingleStep(1024);
    _profileCapSpin->setSpecialValueText(tr("окно модели"));
    _profileCapSpin->setMinimumWidth(110);

    _profileCapModelButton = new QPushButton(tr("окно модели"), box);
    _profileCapHint = new QLabel(box);
    _profileCapHint->setWordWrap(true);

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
    connect(_profilePurposeEdit, SIGNAL(editingFinished()),
            this, SLOT(sltProfileFieldChanged()));
    connect(_profileBalanceUrlEdit, SIGNAL(editingFinished()),
            this, SLOT(sltProfileFieldChanged()));
    connect(_profileBalancePathEdit, SIGNAL(editingFinished()),
            this, SLOT(sltProfileFieldChanged()));
    connect(_profileToolIterSpin, SIGNAL(valueChanged(int)),
            this, SLOT(sltProfileFieldChanged()));
    connect(_profileCapSpin, SIGNAL(valueChanged(int)),
            this, SLOT(sltProfileFieldChanged()));
    connect(_profileCapModelButton, SIGNAL(clicked()),
            this, SLOT(sltUseModelContext()));
    connect(_addProfileButton, SIGNAL(clicked()), this, SLOT(sltAddProfile()));
    connect(_removeProfileButton, SIGNAL(clicked()), this, SLOT(sltRemoveProfile()));

    _profileForm = new QFormLayout();
    _profileForm->addRow(tr("Имя профиля:"), _profileNameEdit);
    _profileForm->addRow(tr("Адрес API:"), _profileUrlEdit);
    _profileForm->addRow(tr("Модель:"), _profileModelEdit);
    _profileForm->addRow(tr("Токен:"), _profileTokenEdit);
    _profileForm->addRow(tr("Класс задач API:"), _profileModalityCombo);
    _profileForm->addRow(tr("Назначение модели:"), _profilePurposeEdit);
    _profileForm->addRow(tr("URL баланса:"), _profileBalanceUrlEdit);
    _profileForm->addRow(tr("JSON путь баланса:"), _profileBalancePathEdit);
    _profileForm->addRow(tr("Лимит вызовов функций:"), _profileToolIterSpin);

    QHBoxLayout *capRow = new QHBoxLayout();
    capRow->addWidget(_profileCapSpin);
    capRow->addWidget(_profileCapModelButton);
    QVBoxLayout *capCell = new QVBoxLayout();
    capCell->setContentsMargins(0, 0, 0, 0);
    capCell->setSpacing(2);
    capCell->addLayout(capRow);
    capCell->addWidget(_profileCapHint);
    _profileForm->addRow(tr("Потолок контекста:"), capCell);

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addWidget(_addProfileButton);
    buttons->addWidget(_removeProfileButton);
    buttons->addStretch();

    QVBoxLayout *right = new QVBoxLayout();
    right->addLayout(_profileForm);
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
    _profilesRefBox = new QGroupBox(tr("Профили моделей"), this);
    QGroupBox *box = _profilesRefBox;
    _profilesRefHint = new QLabel(L("settings.hint.profilesRef"), box);
    _profilesRefHint->setWordWrap(true);
    QVBoxLayout *layout = new QVBoxLayout(box);
    layout->addWidget(_profilesRefHint);
    return box;
}

//----------------------------------------------

QWidget *SettingsPage::buildWorkersBox()
{
    _workersBox = new QGroupBox(tr("Воркеры"), this);
    QGroupBox *box = _workersBox;
    QVBoxLayout *layout = new QVBoxLayout(box);

    _presetHint = new QLabel(L("settings.hint.presets"), box);
    _presetHint->setWordWrap(true);
    layout->addWidget(_presetHint);

    _presetList = new QListWidget(box);
    _presetList->setMinimumWidth(180);
    _presetList->setMinimumHeight(120);

    _presetTypeCombo = new QComboBox(box);
    _presetTypeCombo->addItem(tr("Менеджер"), int(WorkerTypeManager));
    _presetTypeCombo->addItem(tr("Исполнитель"), int(WorkerTypeExecutor));
    _presetTypeCombo->addItem(tr("Тестировщик"), int(WorkerTypeTester));
    _presetTypeCombo->addItem(tr("Инструмент"), int(WorkerTypeTool));

    _presetProfileCombo = new QComboBox(box);
    _presetProfileCombo->setMinimumWidth(220);

    _addPresetButton = new QPushButton(tr("Добавить"), box);
    _removePresetButton = new QPushButton(tr("Удалить"), box);

    connect(_presetList, SIGNAL(currentItemChanged(QListWidgetItem*,QListWidgetItem*)),
            this, SLOT(sltPresetSelected()));
    connect(_presetTypeCombo, SIGNAL(currentIndexChanged(int)),
            this, SLOT(sltPresetFieldChanged()));
    connect(_presetProfileCombo, SIGNAL(currentIndexChanged(int)),
            this, SLOT(sltPresetFieldChanged()));
    connect(_addPresetButton, SIGNAL(clicked()), this, SLOT(sltAddPreset()));
    connect(_removePresetButton, SIGNAL(clicked()), this, SLOT(sltRemovePreset()));

    _presetForm = new QFormLayout();
    _presetForm->addRow(tr("Тип воркера:"), _presetTypeCombo);
    _presetForm->addRow(tr("Профиль модели:"), _presetProfileCombo);

    QHBoxLayout *presetButtons = new QHBoxLayout();
    presetButtons->addWidget(_addPresetButton);
    presetButtons->addWidget(_removePresetButton);
    presetButtons->addStretch();

    QVBoxLayout *presetRight = new QVBoxLayout();
    presetRight->addLayout(_presetForm);
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
        _rootHint = new QLabel(L("settings.hint.root"), box);
        _rootHint->setWordWrap(true);
        layout->addWidget(_rootHint);
        _rootForm = new QFormLayout();
        _rootForm->addRow(tr("Пресет главного менеджера:"), _rootPresetCombo);
        layout->addLayout(_rootForm);
    }

    QFrame *line = new QFrame(box);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    _limitsHint = new QLabel(L("settings.hint.limits"), box);
    _limitsHint->setWordWrap(true);
    layout->addWidget(_limitsHint);

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

    _limitsForm = new QFormLayout();
    _limitsForm->setContentsMargins(0, 0, 0, 0);
    _limitsForm->addRow(tr("Максимальная глубина вложенности:"), _maxDepthSpin);
    _limitsForm->addRow(tr("Одновременно работающих воркеров:"), _maxConcurrentSpin);
    _limitsForm->addRow(tr("Таймаут ответа модели:"), _llmTimeoutSpin);
    layout->addLayout(_limitsForm);
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
    const LlmProfile profile = AppSettings::instance()->profileById(preset.profileId);
    QString taskName;
    const WorkerTaskKind modality = profile.id.isEmpty()
                                    ? preset.taskKind
                                    : profile.modality;
    switch (modality)
    {
    case WorkerTaskKindImageGeneration:
        taskName = tr("Image");
        break;
    case WorkerTaskKindSpeech:
        taskName = tr("Speech");
        break;
    case WorkerTaskKindAudioGeneration:
        taskName = tr("Audio");
        break;
    case WorkerTaskKindVideoGeneration:
        taskName = tr("Video");
        break;
    case WorkerTaskKindEmbedding:
        taskName = tr("Embedding");
        break;
    case WorkerTaskKindDecisions:
        taskName = tr("Decisions");
        break;
    case WorkerTaskKindOther:
        taskName = tr("Other");
        break;
    case WorkerTaskKindLlm:
    default:
        taskName = tr("LLM");
        break;
    }
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
        const QString purpose = profile.purpose.trimmed().simplified();
        if (!purpose.isEmpty())
        {
            QString shortPurpose = purpose;
            if (shortPurpose.size() > 48)
                shortPurpose = shortPurpose.left(45) + QString::fromUtf8("...");
            title += QString::fromUtf8(" — ") + shortPurpose;
        }
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
    updateProfileCapHint();
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
        foreach (const QString &profileId, profileOrder)
        {
            if (profileId.isEmpty())
                continue;
            preset = WorkerPreset();
            preset.type = type;
            preset.profileId = profileId;
            error.clear();
            id = s->addWorkerPreset(_kind, preset, &error);
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
            tr("Все комбинации тип+профиль уже заняты.\n"
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
    if (_profilePurposeEdit)
        profile.purpose = _profilePurposeEdit->text().trimmed();
    if (_profileBalanceUrlEdit)
        profile.balanceUrl = _profileBalanceUrlEdit->text().trimmed();
    if (_profileBalancePathEdit)
        profile.balanceJsonPath = _profileBalancePathEdit->text().trimmed();
    if (_profileToolIterSpin)
        profile.maxToolIterations = _profileToolIterSpin->value();
    if (_profileCapSpin)
        profile.contextCap = AppSettings::normalizeProfileContextCap(_profileCapSpin->value());

    _updatingProfiles = true;
    AppSettings::instance()->updateProfile(profile);
    if (_profileList->currentItem())
        _profileList->currentItem()->setText(profile.name);
    fillPresetProfileCombo();
    updateProfileCapHint();
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
    filter->addItem(tr("Аудио"), int(WorkerTaskKindAudioGeneration));
    filter->addItem(tr("Видео"), int(WorkerTaskKindVideoGeneration));
    filter->addItem(tr("Эмбеддинги"), int(WorkerTaskKindEmbedding));
    filter->addItem(tr("Decisions"), int(WorkerTaskKindDecisions));
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
        if (!p.purpose.trimmed().isEmpty())
            text += tr("\nНазначение: %1").arg(p.purpose);
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
    outProfile->purpose = p.purpose;
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

    AppSettings *s = AppSettings::instance();
    const LlmProfile profile = s->profileById(id);
    QString profileName = profile.name.trimmed();
    if (profileName.isEmpty())
        profileName = id;

    const QList<WorkerPreset> used = s->presetsUsingProfile(id);
    const int liveCount = _workers ? _workers->countWorkersUsingProfile(id) : 0;
    if (!used.isEmpty() || liveCount > 0)
    {
        QStringList lines;
        foreach (const WorkerPreset &preset, used)
            lines << presetListLabel(preset);
        QString text;
        if (!lines.isEmpty() && liveCount > 0)
        {
            text = tr(
                "Профиль «%1» используется пресетами воркеров в настройках "
                "программы и/или открытого проекта:\n\n%2\n\n"
                "Также в текущем проекте есть воркеры с этой моделью: %3.\n\n"
                "Удалить эти пресеты из настроек проекта и программы "
                "и воркеров, использовавших эту модель?")
                       .arg(profileName, lines.join(QChar('\n')))
                       .arg(liveCount);
        }
        else if (!lines.isEmpty())
        {
            text = tr(
                "Профиль «%1» используется пресетами воркеров в настройках "
                "программы и/или открытого проекта:\n\n%2\n\n"
                "Удалить эти пресеты из настроек проекта и программы?")
                       .arg(profileName, lines.join(QChar('\n')));
        }
        else
        {
            text = tr(
                "В текущем проекте есть воркеры, использующие модель «%1»: %2.\n\n"
                "Удалить этих воркеров (корням сбросить профиль) вместе с профилем?")
                       .arg(profileName)
                       .arg(liveCount);
        }
        const int answer = QMessageBox::question(
            this, tr("Удаление профиля модели"), text,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        if (!used.isEmpty())
            s->removePresetsUsingProfile(id);
        emit removeWorkersForProfile(id);
    }

    s->removeProfile(id);
    loadProfiles();
    loadPresets();
    emit profilesChanged();
}

//----------------------------------------------

QWidget *SettingsPage::buildProjectBox()
{
    _projectBox = new QGroupBox(tr("Проект"), this);
    QGroupBox *box = _projectBox;
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
    _securityBox = new QGroupBox(tr("Контроль действий"), this);
    QGroupBox *box = _securityBox;

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
    _revokeButton = new QPushButton(tr("Отозвать разрешение"), box);
    connect(_revokeButton, SIGNAL(clicked()), this, SLOT(sltRevokePermission()));

    _permissionsHeadingLabel = new QLabel(tr("Выданные разрешения «всегда»:"), box);

    QVBoxLayout *layout = new QVBoxLayout(box);
    layout->addWidget(_shellControlCheck);
    layout->addWidget(_outsideFilesControlCheck);
    layout->addWidget(_systemSettingsControlCheck);
    layout->addWidget(_permissionsHeadingLabel);
    layout->addWidget(_permissionsList, 1);
    layout->addWidget(_revokeButton);
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
    _appearanceBox = new QGroupBox(tr("Оформление"), this);
    QGroupBox *box = _appearanceBox;
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
    _applyAppearanceButton = new QPushButton(tr("Применить оформление"), box);
    connect(_textColorButton, SIGNAL(clicked()), this, SLOT(sltPickTextColor()));
    connect(_windowColorButton, SIGNAL(clicked()), this, SLOT(sltPickWindowColor()));
    connect(_panelColorButton, SIGNAL(clicked()), this, SLOT(sltPickPanelColor()));
    connect(_accentColorButton, SIGNAL(clicked()), this, SLOT(sltPickAccentColor()));
    connect(_chatUserColorButton, SIGNAL(clicked()), this, SLOT(sltPickChatUserColor()));
    connect(_chatAssistantColorButton, SIGNAL(clicked()),
            this, SLOT(sltPickChatAssistantColor()));
    connect(_chatOtherColorButton, SIGNAL(clicked()), this, SLOT(sltPickChatOtherColor()));
    connect(_applyAppearanceButton, SIGNAL(clicked()), this, SLOT(sltApplyAppearance()));

    _appearanceForm = new QFormLayout(box);
    _appearanceForm->addRow(tr("Размер шрифта:"), _fontSpin);
    _appearanceForm->addRow(tr("Цвет текста:"), _textColorButton);
    _appearanceForm->addRow(tr("Цвет фона:"), _windowColorButton);
    _appearanceForm->addRow(tr("Цвет панелей:"), _panelColorButton);
    _appearanceForm->addRow(tr("Цвет акцента:"), _accentColorButton);
    _appearanceForm->addRow(tr("Чат: сообщения пользователя:"), _chatUserColorButton);
    _appearanceForm->addRow(tr("Чат: сообщения ассистента:"), _chatAssistantColorButton);
    _appearanceForm->addRow(tr("Чат: прочие сообщения:"), _chatOtherColorButton);
    _appearanceForm->addRow(QString(), _applyAppearanceButton);
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
