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
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMessageBox>
#include <QTextEdit>
#include <QVariant>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
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

static void fillProfileCombo(QComboBox *combo, const QString &selectId)
{
    if (!combo)
        return;
    const QSignalBlocker blocker(combo);
    combo->clear();
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
        combo->addItem(title, profile.id);
    }
    int index = combo->findData(selectId);
    if (index < 0)
        index = 0;
    if (combo->count() > 0)
        combo->setCurrentIndex(index);
}

//----------------------------------------------

static void restoreListSelection(QListWidget *list, const QStringList &ids)
{
    if (!list)
        return;
    QListWidgetItem *first = 0;
    for (int i = 0; i < list->count(); ++i)
    {
        QListWidgetItem *item = list->item(i);
        const bool on = ids.contains(item->data(Qt::UserRole).toString());
        item->setSelected(on);
        if (on && !first)
            first = item;
    }
    if (first)
        list->setCurrentItem(first, QItemSelectionModel::NoUpdate);
    else if (list->count() > 0)
    {
        list->item(0)->setSelected(true);
        list->setCurrentItem(list->item(0), QItemSelectionModel::NoUpdate);
    }
}

//----------------------------------------------

static void selectOnlyId(QListWidget *list, const QString &id)
{
    if (!list)
        return;
    const QSignalBlocker blocker(list);
    list->clearSelection();
    for (int i = 0; i < list->count(); ++i)
    {
        QListWidgetItem *item = list->item(i);
        if (item->data(Qt::UserRole).toString() != id)
            continue;
        item->setSelected(true);
        list->setCurrentItem(item, QItemSelectionModel::NoUpdate);
        list->scrollToItem(item);
        break;
    }
}

//----------------------------------------------

SettingsPage::SettingsPage(SettingsKind kind, QWidget *parent)
    : QScrollArea(parent)
    , _kind(kind)
    , _workers(0)
    , _viewBox(0)
    , _contextBox(0)
    , _apiBox(0)
    , _workersBox(0)
    , _projectBox(0)
    , _securityBox(0)
    , _appearanceBox(0)
    , _languageGroup(0)
    , _languageCombo(0)
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
    , _copyProfileButton(0)
    , _editProfileButton(0)
    , _removeProfileButton(0)
    , _profilePreview(0)
    , _presetList(0)
    , _presetTypeCombo(0)
    , _presetProfileCombo(0)
    , _rootPresetCombo(0)
    , _addPresetButton(0)
    , _copyPresetButton(0)
    , _editPresetButton(0)
    , _removePresetButton(0)
    , _presetPreview(0)
    , _maxDepthSpin(0)
    , _maxConcurrentSpin(0)
    , _llmTimeoutSpin(0)
    , _pendingProfileCapWindow(false)
    , _projectPathLabel(0)
    , _inspectCheck(0)
    , _showForwardedCheck(0)
    , _dropLargeFilesCheck(0)
    , _largeFileKbSpin(0)
    , _contextForm(0)
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
    , _presetsReloadTimer(0)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setObjectName(QString::fromUtf8("settingsPage"));

    QWidget *host = new QWidget(this);
    host->setObjectName(QString::fromUtf8("settingsHost"));
    host->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout *root = new QVBoxLayout(host);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);
    if (_kind == SettingsKindDefaults)
        root->addWidget(buildLanguageBox());
    if (_kind == SettingsKindDefaults)
        root->addWidget(buildApiBox());
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

        _contextBox = new QGroupBox(tr("Контекст"), this);
        _dropLargeFilesCheck = new QCheckBox(
            tr("Удалять большие файлы из контекста"), _contextBox);
        _dropLargeFilesCheck->setToolTip(
            tr("Тело файла убирается только если модель просит FILE_DROP "
               "и оно не меньше порога. Мелкие файлы остаются."));
        _largeFileKbSpin = new QSpinBox(_contextBox);
        _largeFileKbSpin->setRange(1, 4096);
        _largeFileKbSpin->setSuffix(tr(" КБ"));
        _largeFileKbSpin->setToolTip(
            tr("Размер тела файла в контексте. Меньшие фрагменты не удаляются."));
        _contextForm = new QFormLayout();
        _contextForm->setContentsMargins(0, 0, 0, 0);
        _contextForm->addRow(tr("Порог размера:"), _largeFileKbSpin);
        QVBoxLayout *contextLay = new QVBoxLayout(_contextBox);
        contextLay->addWidget(_dropLargeFilesCheck);
        contextLay->addLayout(_contextForm);
        root->addWidget(_contextBox);
        connect(_dropLargeFilesCheck, SIGNAL(toggled(bool)),
                this, SLOT(sltDropLargeFilesToggled(bool)));
        connect(_largeFileKbSpin, SIGNAL(valueChanged(int)),
                this, SLOT(sltLargeFileKbChanged(int)));
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
    // profilesChanged не трогаем: правка URL/токена не должна пересобирать списки.
    connect(settings, SIGNAL(workerPresetsChanged()), this, SLOT(scheduleLoadPresets()));
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
    if (_dropLargeFilesCheck)
    {
        const QSignalBlocker blocker(_dropLargeFilesCheck);
        _dropLargeFilesCheck->setChecked(AppSettings::instance()->dropLargeContextFiles());
    }
    if (_largeFileKbSpin)
    {
        const QSignalBlocker blocker(_largeFileKbSpin);
        _largeFileKbSpin->setValue(AppSettings::instance()->largeContextFileKb());
        _largeFileKbSpin->setEnabled(AppSettings::instance()->dropLargeContextFiles());
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
    if (_contextBox)
        _contextBox->setTitle(tr("Контекст"));
    if (_dropLargeFilesCheck)
    {
        _dropLargeFilesCheck->setText(tr("Удалять большие файлы из контекста"));
        _dropLargeFilesCheck->setToolTip(
            tr("Тело файла убирается только если модель просит FILE_DROP "
               "и оно не меньше порога. Мелкие файлы остаются."));
    }
    if (_largeFileKbSpin)
        _largeFileKbSpin->setSuffix(tr(" КБ"));
    if (_contextForm && _largeFileKbSpin)
        setFormLabelText(_contextForm, _largeFileKbSpin, tr("Порог размера:"));
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
    if (_copyProfileButton)
        _copyProfileButton->setText(tr("Копировать"));
    if (_editProfileButton)
        _editProfileButton->setText(tr("Редактировать"));
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

    if (_workersBox)
        _workersBox->setTitle(tr("Воркеры"));
    if (_addPresetButton)
        _addPresetButton->setText(tr("Добавить"));
    if (_copyPresetButton)
        _copyPresetButton->setText(tr("Копировать"));
    if (_editPresetButton)
        _editPresetButton->setText(tr("Редактировать"));
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
            case WorkerTypeTool:
                text = tr("Инструмент");
                break;
            case WorkerTypeExecutor:
            case WorkerTypeManager:
            case WorkerTypeTester:
            default:
                text = tr("Исполнитель");
                break;
            }
            _presetTypeCombo->setItemText(i, text);
        }
    }
    if (_rootPresetCombo)
    {
        _rootPresetCombo->setToolTip(
            tr("Пресет корневого исполнителя при создании проекта и очистке сеанса. "
               "Можно выбрать только пресет исполнителя."));
    }
    if (_rootForm && _rootPresetCombo)
        setFormLabelText(_rootForm, _rootPresetCombo, tr("Пресет корневого исполнителя:"));

    if (_maxDepthSpin)
        _maxDepthSpin->setToolTip(tr("Главный чат — уровень 0. "
                                       "На максимальной глубине исполнитель уже не создаёт детей."));
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

void SettingsPage::reloadWorkerLimits()
{
    loadWorkerLimits();
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
    refreshProfilePreview();
    requestContextCatalogs();
}

//----------------------------------------------

void SettingsPage::sltUseModelContext()
{
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
    _pendingProfileCapWindow = false;
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
    updateProfileCapHint();
    updateProfileActionButtons();
}

//----------------------------------------------

void SettingsPage::loadProfiles()
{
    _updatingProfiles = true;
    const QStringList keepIds = selectedProfileIds();
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
        restoreListSelection(_profileList, keepIds);
    }
    loadCurrentProfileFields();
    fillPresetProfileCombo();
    _updatingProfiles = false;
    loadPresets();
}

//----------------------------------------------

void SettingsPage::commitCurrentProfile()
{
}

//----------------------------------------------

QWidget *SettingsPage::buildApiBox()
{
    _apiBox = new QGroupBox(tr("Профили моделей"), this);
    QGroupBox *box = _apiBox;
    _profileList = new QListWidget(box);
    _profileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    _profileList->setMinimumWidth(180);
    _profileList->setMinimumHeight(180);

    _profilePreview = new QTextEdit(box);
    _profilePreview->setReadOnly(true);
    _profilePreview->setAcceptRichText(true);
    _profilePreview->setMinimumWidth(280);
    _profilePreview->setMinimumHeight(180);
    _profilePreview->setToolTip(
        tr("Только просмотр. Изменение — двойной щелчок или «Редактировать»."));

    _addProfileButton = new QPushButton(tr("Добавить"), box);
    _copyProfileButton = new QPushButton(tr("Копировать"), box);
    _editProfileButton = new QPushButton(tr("Редактировать"), box);
    _removeProfileButton = new QPushButton(tr("Удалить"), box);
    _copyProfileButton->setEnabled(false);
    _copyProfileButton->setToolTip(
        tr("Новый профиль с адресом, моделью, токеном и остальными параметрами выбранного."));
    _editProfileButton->setEnabled(false);
    _removeProfileButton->setEnabled(false);

    connect(_profileList, SIGNAL(itemSelectionChanged()),
            this, SLOT(sltProfileSelected()));
    connect(_profileList, &QListWidget::itemDoubleClicked,
            this, &SettingsPage::sltEditProfileItem);
    connect(_addProfileButton, SIGNAL(clicked()), this, SLOT(sltAddProfile()));
    connect(_copyProfileButton, SIGNAL(clicked()), this, SLOT(sltCopyProfile()));
    connect(_editProfileButton, SIGNAL(clicked()), this, SLOT(sltEditProfile()));
    connect(_removeProfileButton, SIGNAL(clicked()), this, SLOT(sltRemoveProfile()));

    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addWidget(_addProfileButton);
    buttons->addWidget(_copyProfileButton);
    buttons->addWidget(_editProfileButton);
    buttons->addWidget(_removeProfileButton);
    buttons->addStretch();

    QVBoxLayout *left = new QVBoxLayout();
    left->addWidget(_profileList, 1);
    left->addLayout(buttons);

    QHBoxLayout *row = new QHBoxLayout();
    row->addLayout(left, 1);
    row->addWidget(_profilePreview, 2);

    QVBoxLayout *layout = new QVBoxLayout(box);
    layout->addLayout(row);
    return box;
}

//----------------------------------------------

QWidget *SettingsPage::buildWorkersBox()
{
    _workersBox = new QGroupBox(tr("Воркеры"), this);
    QGroupBox *box = _workersBox;
    QVBoxLayout *layout = new QVBoxLayout(box);

    _presetList = new QListWidget(box);
    _presetList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    _presetList->setMinimumWidth(180);
    _presetList->setMinimumHeight(140);

    _presetPreview = new QTextEdit(box);
    _presetPreview->setReadOnly(true);
    _presetPreview->setAcceptRichText(true);
    _presetPreview->setMinimumWidth(280);
    _presetPreview->setMinimumHeight(140);
    _presetPreview->setToolTip(
        tr("Только просмотр. Изменение — двойной щелчок или «Редактировать»."));

    _addPresetButton = new QPushButton(tr("Добавить"), box);
    _copyPresetButton = new QPushButton(tr("Копировать"), box);
    _editPresetButton = new QPushButton(tr("Редактировать"), box);
    _removePresetButton = new QPushButton(tr("Удалить"), box);
    _copyPresetButton->setEnabled(false);
    _copyPresetButton->setToolTip(
        tr("Новый воркер с типом и профилем модели выбранного."));
    _editPresetButton->setEnabled(false);
    _removePresetButton->setEnabled(false);

    connect(_presetList, SIGNAL(itemSelectionChanged()),
            this, SLOT(sltPresetSelected()));
    connect(_presetList, &QListWidget::itemDoubleClicked,
            this, &SettingsPage::sltEditPresetItem);
    connect(_addPresetButton, SIGNAL(clicked()), this, SLOT(sltAddPreset()));
    connect(_copyPresetButton, SIGNAL(clicked()), this, SLOT(sltCopyPreset()));
    connect(_editPresetButton, SIGNAL(clicked()), this, SLOT(sltEditPreset()));
    connect(_removePresetButton, SIGNAL(clicked()), this, SLOT(sltRemovePreset()));

    QHBoxLayout *presetButtons = new QHBoxLayout();
    presetButtons->addWidget(_addPresetButton);
    presetButtons->addWidget(_copyPresetButton);
    presetButtons->addWidget(_editPresetButton);
    presetButtons->addWidget(_removePresetButton);
    presetButtons->addStretch();

    QVBoxLayout *presetLeft = new QVBoxLayout();
    presetLeft->addWidget(_presetList, 1);
    presetLeft->addLayout(presetButtons);

    QHBoxLayout *presetRow = new QHBoxLayout();
    presetRow->addLayout(presetLeft, 1);
    presetRow->addWidget(_presetPreview, 2);
    layout->addLayout(presetRow);

    if (_kind == SettingsKindDefaults)
    {
        _rootPresetCombo = new QComboBox(box);
        _rootPresetCombo->setMinimumWidth(280);
        _rootPresetCombo->setToolTip(
            tr("Пресет корневого исполнителя при создании проекта и очистке сеанса. "
               "Можно выбрать только пресет исполнителя."));
        connect(_rootPresetCombo, SIGNAL(currentIndexChanged(int)),
                this, SLOT(sltRootPresetChanged()));
        _rootForm = new QFormLayout();
        _rootForm->addRow(tr("Пресет корневого исполнителя:"), _rootPresetCombo);
        layout->addLayout(_rootForm);
    }

    QFrame *line = new QFrame(box);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    _maxDepthSpin = new QSpinBox(box);
    _maxDepthSpin->setRange(1, 8);
    _maxDepthSpin->setToolTip(tr("Главный чат — уровень 0. "
                                 "На максимальной глубине исполнитель уже не создаёт детей."));
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
    case WorkerTypeTool:
        typeName = tr("Инструмент");
        break;
    case WorkerTypeExecutor:
    case WorkerTypeManager:
    case WorkerTypeTester:
    default:
        typeName = tr("Исполнитель");
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
    fillProfileCombo(_presetProfileCombo, _presetProfileCombo->currentData().toString());
}

//----------------------------------------------

void SettingsPage::scheduleLoadPresets()
{
    if (_updatingPresets || _updatingProfiles)
        return;
    if (!_presetsReloadTimer)
    {
        _presetsReloadTimer = new QTimer(this);
        _presetsReloadTimer->setSingleShot(true);
        connect(_presetsReloadTimer, SIGNAL(timeout()), this, SLOT(loadPresets()));
    }
    _presetsReloadTimer->start(200);
}

//----------------------------------------------

void SettingsPage::loadPresets()
{
    if (!_presetList || _updatingPresets)
        return;

    _updatingPresets = true;
    const QStringList keepIds = selectedPresetIds();
    {
        const QSignalBlocker blocker(_presetList);
        _presetList->clear();
        foreach (const WorkerPreset &preset, AppSettings::instance()->workerPresets(_kind))
        {
            QListWidgetItem *item = new QListWidgetItem(presetListLabel(preset), _presetList);
            item->setData(Qt::UserRole, preset.id);
        }
        restoreListSelection(_presetList, keepIds);
    }

    refreshPresetPreview();
    updatePresetActionButtons();
    fillRootPresetCombo();
    _updatingPresets = false;
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
        if (preset.type != WorkerTypeExecutor)
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
    refreshPresetPreview();
    updatePresetActionButtons();
}

//----------------------------------------------

void SettingsPage::sltPresetFieldChanged()
{
}

//----------------------------------------------

void SettingsPage::sltAddPreset()
{
    const QList<LlmProfile> profiles = AppSettings::instance()->profiles();
    if (profiles.isEmpty())
    {
        QMessageBox box(QMessageBox::Warning, tr("Воркеры"),
                        tr("Сначала добавьте хотя бы один профиль модели в настройках программы."),
                        QMessageBox::Ok, this);
        box.setButtonText(QMessageBox::Ok, tr("ОК"));
        box.exec();
        return;
    }

    WorkerPreset preset;
    preset.type = WorkerTypeExecutor;
    preset.profileId = profiles.first().id;
    if (!runPresetDialog(&preset, true))
        return;
    loadPresets();
    selectOnlyId(_presetList, preset.id);
    refreshPresetPreview();
    updatePresetActionButtons();
}

//----------------------------------------------

void SettingsPage::sltCopyPreset()
{
    const QStringList ids = selectedPresetIds();
    if (ids.size() != 1)
        return;
    const WorkerPreset source = AppSettings::instance()->workerPresetById(_kind, ids.first());
    if (source.id.isEmpty())
        return;
    WorkerPreset copy;
    copy.type = source.type;
    copy.profileId = source.profileId;
    if (!runPresetDialog(&copy, true))
        return;
    loadPresets();
    selectOnlyId(_presetList, copy.id);
    refreshPresetPreview();
    updatePresetActionButtons();
}

//----------------------------------------------

void SettingsPage::sltRemovePreset()
{
    const QStringList ids = selectedPresetIds();
    if (ids.isEmpty())
        return;

    QStringList labels;
    foreach (const QString &id, ids)
    {
        const WorkerPreset preset = AppSettings::instance()->workerPresetById(_kind, id);
        if (preset.id.isEmpty())
            continue;
        labels << presetListLabel(preset);
    }
    QString text;
    if (ids.size() == 1)
        text = tr("Удалить воркер «%1»?").arg(labels.isEmpty() ? ids.first() : labels.first());
    else
        text = tr("Удалить выбранные воркеры (%1)?\n\n%2")
                   .arg(ids.size())
                   .arg(labels.join(QString::fromUtf8("\n")));
    QMessageBox box(QMessageBox::Question, tr("Удаление воркеров"), text,
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setDefaultButton(QMessageBox::No);
    box.setButtonText(QMessageBox::Yes, tr("Да"));
    box.setButtonText(QMessageBox::No, tr("Нет"));
    if (box.exec() != QMessageBox::Yes)
        return;

    foreach (const QString &id, ids)
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
}

//----------------------------------------------

void SettingsPage::sltAddProfile()
{
    if (_kind != SettingsKindDefaults)
        return;
    LlmProfile profile;
    profile.name = tr("Новый профиль");
    if (!runProfileDialog(&profile, true))
        return;
    if (profile.name.trimmed().isEmpty())
        profile.name = tr("Новый профиль");
    const QString id = AppSettings::instance()->addProfile(profile);
    loadProfiles();
    selectOnlyId(_profileList, id);
    refreshProfilePreview();
    updateProfileActionButtons();
    emit profilesChanged();
}

//----------------------------------------------

void SettingsPage::sltCopyProfile()
{
    if (_kind != SettingsKindDefaults)
        return;
    const QStringList ids = selectedProfileIds();
    if (ids.size() != 1)
        return;
    LlmProfile copy = AppSettings::instance()->profileById(ids.first());
    if (copy.id.isEmpty())
        return;
    copy.id.clear();
    const QString sourceName = copy.name.trimmed().isEmpty()
                               ? tr("Профиль")
                               : copy.name.trimmed();
    const QString suffix = tr(" (копия)");
    if (!sourceName.endsWith(suffix))
        copy.name = sourceName + suffix;
    if (!runProfileDialog(&copy, true))
        return;
    if (copy.name.trimmed().isEmpty())
        copy.name = sourceName + suffix;
    const QString id = AppSettings::instance()->addProfile(copy);
    loadProfiles();
    selectOnlyId(_profileList, id);
    refreshProfilePreview();
    updateProfileActionButtons();
    emit profilesChanged();
}

//----------------------------------------------

bool SettingsPage::pickModelProfilePreset(LlmProfile *outProfile)
{
    if (!outProfile)
        return false;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Пресеты моделей"));
    dialog.resize(640, 480);

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
            detail->clear();
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
    if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok))
        ok->setText(tr("ОК"));
    if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel))
        cancel->setText(tr("Отмена"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QVBoxLayout *root = new QVBoxLayout(&dialog);
    root->addWidget(filter);
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
    const QStringList ids = selectedProfileIds();
    if (ids.isEmpty())
        return;

    AppSettings *s = AppSettings::instance();
    QStringList names;
    QStringList depLines;
    int liveTotal = 0;
    foreach (const QString &id, ids)
    {
        const LlmProfile profile = s->profileById(id);
        QString profileName = profile.name.trimmed();
        if (profileName.isEmpty())
            profileName = id;
        names << profileName;
        foreach (const WorkerPreset &preset, s->presetsUsingProfile(id))
            depLines << presetListLabel(preset);
        if (_workers)
            liveTotal += _workers->countWorkersUsingProfile(id);
    }

    QString text;
    if (ids.size() == 1)
        text = tr("Удалить профиль «%1»?").arg(names.first());
    else
        text = tr("Удалить выбранные профили (%1)?\n\n%2")
                   .arg(ids.size())
                   .arg(names.join(QString::fromUtf8("\n")));
    if (!depLines.isEmpty() || liveTotal > 0)
    {
        text += tr("\n\nВместе с ними будут удалены пресеты воркеров, "
                   "которые ссылаются на эти профили, и живые воркеры "
                   "текущего проекта с этими моделями.");
        if (!depLines.isEmpty())
            text += QString::fromUtf8("\n\n") + depLines.join(QString::fromUtf8("\n"));
        if (liveTotal > 0)
            text += tr("\n\nЖивых воркеров: %1.").arg(liveTotal);
    }
    if (ids.size() >= s->profiles().size())
        text += tr("\n\nПоследний профиль удалить нельзя.");

    QMessageBox box(QMessageBox::Question, tr("Удаление профиля модели"), text,
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setDefaultButton(QMessageBox::No);
    box.setButtonText(QMessageBox::Yes, tr("Да"));
    box.setButtonText(QMessageBox::No, tr("Нет"));
    if (box.exec() != QMessageBox::Yes)
        return;

    foreach (const QString &id, ids)
    {
        if (s->profiles().size() <= 1)
            break;
        const QList<WorkerPreset> used = s->presetsUsingProfile(id);
        const int liveCount = _workers ? _workers->countWorkersUsingProfile(id) : 0;
        if (!used.isEmpty())
            s->removePresetsUsingProfile(id);
        if (!used.isEmpty() || liveCount > 0)
            emit removeWorkersForProfile(id);
        s->removeProfile(id);
    }
    loadProfiles();
    emit profilesChanged();
}

//----------------------------------------------

QStringList SettingsPage::selectedProfileIds() const
{
    QStringList ids;
    if (!_profileList)
        return ids;
    for (int i = 0; i < _profileList->count(); ++i)
    {
        QListWidgetItem *item = _profileList->item(i);
        if (item && item->isSelected())
            ids.append(item->data(Qt::UserRole).toString());
    }
    return ids;
}

//----------------------------------------------

QStringList SettingsPage::selectedPresetIds() const
{
    QStringList ids;
    if (!_presetList)
        return ids;
    for (int i = 0; i < _presetList->count(); ++i)
    {
        QListWidgetItem *item = _presetList->item(i);
        if (item && item->isSelected())
            ids.append(item->data(Qt::UserRole).toString());
    }
    return ids;
}

//----------------------------------------------

QString SettingsPage::workerTypeTitle(WorkerType type) const
{
    switch (type)
    {
    case WorkerTypeTool:
        return tr("Инструмент");
    case WorkerTypeExecutor:
    case WorkerTypeManager:
    case WorkerTypeTester:
    default:
        return tr("Исполнитель");
    }
}

//----------------------------------------------

QString SettingsPage::modalityTitle(WorkerTaskKind kind) const
{
    switch (kind)
    {
    case WorkerTaskKindImageGeneration:
        return tr("Генерация изображений");
    case WorkerTaskKindSpeech:
        return tr("Речь (TTS/STT)");
    case WorkerTaskKindAudioGeneration:
        return tr("Генерация аудио");
    case WorkerTaskKindVideoGeneration:
        return tr("Генерация видео");
    case WorkerTaskKindEmbedding:
        return tr("Эмбеддинги");
    case WorkerTaskKindDecisions:
        return tr("Decisions (решения)");
    case WorkerTaskKindOther:
        return tr("Другое");
    case WorkerTaskKindLlm:
    default:
        return tr("LLM (текст / агент)");
    }
}

//----------------------------------------------

static QString previewField(const QString &label, const QString &value, const QString &muted)
{
    return QString::fromUtf8(
               "<p style=\"margin-top:2px; margin-bottom:8px;\">"
               "<span style=\"color:%1;\">%2</span><br><b>%3</b></p>")
        .arg(muted, label.toHtmlEscaped(), value.toHtmlEscaped());
}

//----------------------------------------------

QString SettingsPage::profilePreviewHtml(const LlmProfile &profile) const
{
    const QString dash = QString::fromUtf8("—");
    const QString name = profile.name.trimmed().isEmpty() ? dash : profile.name.trimmed();
    const QString url = profile.baseUrl.trimmed().isEmpty() ? dash : profile.baseUrl.trimmed();
    const QString modelName = profile.model.trimmed().isEmpty() ? dash : profile.model.trimmed();
    const QString purpose = profile.purpose.trimmed().isEmpty() ? dash : profile.purpose.trimmed();
    const QString balanceUrl = profile.balanceUrl.trimmed().isEmpty()
                               ? dash : profile.balanceUrl.trimmed();
    const QString balancePath = profile.balanceJsonPath.trimmed().isEmpty()
                                ? dash : profile.balanceJsonPath.trimmed();
    const QString token = profile.apiKey.trimmed().isEmpty() ? tr("пусто") : tr("задан");
    const QString cap = profile.contextCap > 0
                        ? QString::number(profile.contextCap)
                        : tr("окно модели");

    QString shownModel = profile.model.trimmed();
    if (shownModel.isEmpty())
        shownModel = tr("модель не задана");
    const int window = OpenAiCompatibleClient::resolvedContextWindow(
                           profile.baseUrl, profile.model);
    const bool fromApi = OpenAiCompatibleClient::hasApiContextWindow(
                             profile.baseUrl, profile.model);
    const QString windowLine = fromApi
        ? tr("%1 ток. (из API /models)").arg(window)
        : tr("%1 ток. (оценка: API не отдаёт размер окна)").arg(window);

    const QString muted = AppSettings::instance()->mutedTextColor().name();
    QString html;
    html += previewField(tr("Имя профиля"), name, muted);
    html += previewField(tr("Адрес API"), url, muted);
    html += previewField(tr("Модель"), modelName, muted);
    html += previewField(tr("Токен"), token, muted);
    html += previewField(tr("Класс задач API"), modalityTitle(profile.modality), muted);
    html += previewField(tr("Назначение модели"), purpose, muted);
    html += previewField(tr("URL баланса"), balanceUrl, muted);
    html += previewField(tr("JSON путь баланса"), balancePath, muted);
    if (!profile.requestPath.trimmed().isEmpty())
        html += previewField(tr("Путь запроса"), profile.requestPath.trimmed(), muted);
    if (!profile.taskPath.trimmed().isEmpty())
        html += previewField(tr("Путь статуса задачи"), profile.taskPath.trimmed(), muted);
    if (!profile.catalogUrl.trimmed().isEmpty())
        html += previewField(tr("Каталог моделей"), profile.catalogUrl.trimmed(), muted);
    if (!profile.extraBody.trimmed().isEmpty())
        html += previewField(tr("Доп. поля запроса"), profile.extraBody.trimmed(), muted);
    html += previewField(tr("Лимит вызовов функций"),
                         QString::number(profile.maxToolIterations), muted);
    html += previewField(tr("Потолок контекста"), cap, muted);
    html += previewField(tr("Окно %1").arg(shownModel), windowLine, muted);
    return html;
}

//----------------------------------------------

QString SettingsPage::presetPreviewHtml(const WorkerPreset &preset) const
{
    const LlmProfile profile = AppSettings::instance()->profileById(preset.profileId);
    QString profileTitle = profile.name.trimmed();
    if (profileTitle.isEmpty())
        profileTitle = preset.profileId.trimmed();
    if (profileTitle.isEmpty())
        profileTitle = tr("профиль не выбран");
    const QString purpose = profile.purpose.trimmed().simplified();
    if (!purpose.isEmpty())
    {
        QString shortPurpose = purpose;
        if (shortPurpose.size() > 80)
            shortPurpose = shortPurpose.left(77) + QString::fromUtf8("...");
        profileTitle += QString::fromUtf8(" — ") + shortPurpose;
    }
    const WorkerTaskKind modality = profile.id.isEmpty() ? preset.taskKind : profile.modality;

    const QString muted = AppSettings::instance()->mutedTextColor().name();
    QString html;
    html += previewField(tr("Тип воркера"), workerTypeTitle(preset.type), muted);
    html += previewField(tr("Профиль модели"), profileTitle, muted);
    html += previewField(tr("Класс задач API"), modalityTitle(modality), muted);
    return html;
}

//----------------------------------------------

void SettingsPage::setPreviewHtml(QTextEdit *edit, const QString &html)
{
    if (!edit)
        return;
    const QString wrapped = QString::fromUtf8(
                                "<div style=\"color:%1;\">%2</div>")
                                .arg(AppSettings::instance()->textColor().name(), html);
    if (edit->property("previewHtml").toString() == wrapped)
        return;
    edit->setHtml(wrapped);
    edit->setProperty("previewHtml", wrapped);
}

//----------------------------------------------

void SettingsPage::applyTheme()
{
    if (_profilePreview)
        _profilePreview->setProperty("previewHtml", QVariant());
    if (_presetPreview)
        _presetPreview->setProperty("previewHtml", QVariant());
    refreshProfilePreview();
    refreshPresetPreview();
}

//----------------------------------------------

void SettingsPage::refreshProfilePreview()
{
    if (!_profilePreview)
        return;
    const QStringList ids = selectedProfileIds();
    const QString muted = AppSettings::instance()->mutedTextColor().name();
    QString html;
    if (ids.isEmpty())
    {
        html = QString::fromUtf8("<p style=\"color:%1;\"><i>%2</i></p>")
                   .arg(muted, tr("Профили не выделены.").toHtmlEscaped());
    }
    else
    {
        int shown = 0;
        foreach (const QString &id, ids)
        {
            const LlmProfile profile = AppSettings::instance()->profileById(id);
            if (profile.id.isEmpty())
                continue;
            if (shown > 0)
                html += QString::fromUtf8("<hr>");
            html += profilePreviewHtml(profile);
            ++shown;
        }
        if (shown == 0)
        {
            html = QString::fromUtf8("<p style=\"color:%1;\"><i>%2</i></p>")
                       .arg(muted, tr("Профили не выделены.").toHtmlEscaped());
        }
    }
    setPreviewHtml(_profilePreview, html);
}

//----------------------------------------------

void SettingsPage::refreshPresetPreview()
{
    if (!_presetPreview)
        return;
    const QStringList ids = selectedPresetIds();
    const QString muted = AppSettings::instance()->mutedTextColor().name();
    QString html;
    if (ids.isEmpty())
    {
        html = QString::fromUtf8("<p style=\"color:%1;\"><i>%2</i></p>")
                   .arg(muted, tr("Воркеры не выделены.").toHtmlEscaped());
    }
    else
    {
        int shown = 0;
        foreach (const QString &id, ids)
        {
            const WorkerPreset preset = AppSettings::instance()->workerPresetById(_kind, id);
            if (preset.id.isEmpty())
                continue;
            if (shown > 0)
                html += QString::fromUtf8("<hr>");
            html += presetPreviewHtml(preset);
            ++shown;
        }
        if (shown == 0)
        {
            html = QString::fromUtf8("<p style=\"color:%1;\"><i>%2</i></p>")
                       .arg(muted, tr("Воркеры не выделены.").toHtmlEscaped());
        }
    }
    setPreviewHtml(_presetPreview, html);
}

//----------------------------------------------

void SettingsPage::updateProfileActionButtons()
{
    const int count = selectedProfileIds().size();
    if (_addProfileButton)
        _addProfileButton->setEnabled(true);
    if (_copyProfileButton)
        _copyProfileButton->setEnabled(count == 1);
    if (_editProfileButton)
        _editProfileButton->setEnabled(count == 1);
    if (_removeProfileButton)
        _removeProfileButton->setEnabled(count >= 1);
}

//----------------------------------------------

void SettingsPage::updatePresetActionButtons()
{
    const int count = selectedPresetIds().size();
    if (_addPresetButton)
        _addPresetButton->setEnabled(true);
    if (_copyPresetButton)
        _copyPresetButton->setEnabled(count == 1);
    if (_editPresetButton)
        _editPresetButton->setEnabled(count == 1);
    if (_removePresetButton)
        _removePresetButton->setEnabled(count >= 1);
}

//----------------------------------------------

void SettingsPage::sltEditProfile()
{
    const QStringList ids = selectedProfileIds();
    if (ids.size() != 1)
        return;
    editProfileById(ids.first());
}

//----------------------------------------------

void SettingsPage::sltEditProfileItem(QListWidgetItem *item)
{
    if (!item)
        return;
    editProfileById(item->data(Qt::UserRole).toString());
}

//----------------------------------------------

void SettingsPage::editProfileById(const QString &id)
{
    if (_kind != SettingsKindDefaults || id.isEmpty())
        return;
    LlmProfile profile = AppSettings::instance()->profileById(id);
    if (profile.id.isEmpty())
        return;
    if (!runProfileDialog(&profile, false))
        return;
    AppSettings::instance()->updateProfile(profile);
    loadProfiles();
    selectOnlyId(_profileList, profile.id);
    refreshProfilePreview();
    updateProfileActionButtons();
}

//----------------------------------------------

void SettingsPage::sltEditPreset()
{
    const QStringList ids = selectedPresetIds();
    if (ids.size() != 1)
        return;
    editPresetById(ids.first());
}

//----------------------------------------------

void SettingsPage::sltEditPresetItem(QListWidgetItem *item)
{
    if (!item)
        return;
    editPresetById(item->data(Qt::UserRole).toString());
}

//----------------------------------------------

void SettingsPage::editPresetById(const QString &id)
{
    if (id.isEmpty())
        return;
    WorkerPreset preset = AppSettings::instance()->workerPresetById(_kind, id);
    if (preset.id.isEmpty())
        return;
    if (!runPresetDialog(&preset, false))
        return;
    loadPresets();
    selectOnlyId(_presetList, preset.id);
    refreshPresetPreview();
    updatePresetActionButtons();
}

//----------------------------------------------

bool SettingsPage::runProfileDialog(LlmProfile *profile, bool isNew)
{
    if (!profile)
        return false;

    QDialog dialog(this);
    dialog.setWindowTitle(isNew ? tr("Новый профиль") : tr("Профиль модели"));
    dialog.resize(560, 520);

    QLineEdit *nameEdit = new QLineEdit(&dialog);
    nameEdit->setPlaceholderText(tr("имя профиля"));
    QLineEdit *urlEdit = new QLineEdit(&dialog);
    urlEdit->setPlaceholderText(QString::fromUtf8("https://api.example.com/v1"));
    QLineEdit *modelEdit = new QLineEdit(&dialog);
    modelEdit->setPlaceholderText(tr("идентификатор модели"));
    QLineEdit *tokenEdit = new QLineEdit(&dialog);
    tokenEdit->setEchoMode(QLineEdit::Password);
    tokenEdit->setPlaceholderText(tr("токен API"));

    QComboBox *modalityCombo = new QComboBox(&dialog);
    modalityCombo->addItem(tr("LLM (текст / агент)"), int(WorkerTaskKindLlm));
    modalityCombo->addItem(tr("Генерация изображений"), int(WorkerTaskKindImageGeneration));
    modalityCombo->addItem(tr("Речь (TTS/STT)"), int(WorkerTaskKindSpeech));
    modalityCombo->addItem(tr("Генерация аудио"), int(WorkerTaskKindAudioGeneration));
    modalityCombo->addItem(tr("Генерация видео"), int(WorkerTaskKindVideoGeneration));
    modalityCombo->addItem(tr("Эмбеддинги"), int(WorkerTaskKindEmbedding));
    modalityCombo->addItem(tr("Decisions (решения)"), int(WorkerTaskKindDecisions));
    modalityCombo->addItem(tr("Другое"), int(WorkerTaskKindOther));

    QLineEdit *purposeEdit = new QLineEdit(&dialog);
    purposeEdit->setPlaceholderText(
        tr("кратко: для какой задачи эта модель, напр. генерация картинок / сегментация"));
    QLineEdit *balanceUrlEdit = new QLineEdit(&dialog);
    balanceUrlEdit->setPlaceholderText(
        tr("пусто = авто; DeepSeek /user/balance, Qwen /api/v1/quotas"));
    QLineEdit *balancePathEdit = new QLineEdit(&dialog);
    balancePathEdit->setPlaceholderText(
        tr("опц. путь JSON: data.credits или balance_infos.0.total_balance"));

    QLineEdit *requestPathEdit = new QLineEdit(&dialog);
    requestPathEdit->setPlaceholderText(
        tr("пусто = стандартный путь класса задач; иначе путь от адреса API или полный URL"));
    requestPathEdit->setToolTip(
        tr("Куда слать запрос генерации. Нужно, если у провайдера нестандартный путь."));
    QLineEdit *taskPathEdit = new QLineEdit(&dialog);
    taskPathEdit->setPlaceholderText(
        tr("пусто = ссылка из ответа или /tasks/{id}; шаблон с {id}"));
    taskPathEdit->setToolTip(
        tr("Опрос асинхронной задачи. {id} заменяется на идентификатор из ответа."));
    QLineEdit *catalogUrlEdit = new QLineEdit(&dialog);
    catalogUrlEdit->setPlaceholderText(
        tr("пусто = /models; каталог со схемой параметров моделей"));
    catalogUrlEdit->setToolTip(
        tr("Откуда брать допустимые параметры модели и их значения для воркеров."));
    QLineEdit *extraBodyEdit = new QLineEdit(&dialog);
    extraBodyEdit->setPlaceholderText(
        tr("JSON-объект, добавляется в каждый запрос генерации: {\"ключ\": значение}"));
    extraBodyEdit->setToolTip(
        tr("Постоянные поля запроса для этой модели. Поля из задачи воркера имеют приоритет."));

    QSpinBox *toolSpin = new QSpinBox(&dialog);
    toolSpin->setMinimum(2);
    toolSpin->setMaximum(10000);
    toolSpin->setSingleStep(10);
    toolSpin->setMinimumWidth(80);

    QSpinBox *capSpin = new QSpinBox(&dialog);
    capSpin->setRange(0, 4000000);
    capSpin->setSingleStep(1024);
    capSpin->setSpecialValueText(tr("окно модели"));
    capSpin->setMinimumWidth(110);
    capSpin->setToolTip(
        tr("0 — полное окно модели. Можно указать точное число, в том числе равное окну. "
           "Иначе история сжимается раньше, по этому потолку."));

    QPushButton *capButton = new QPushButton(tr("окно модели"), &dialog);
    capButton->setToolTip(tr("Использовать размер окна, полученный из API /models."));

    auto updateCapMode = [&]() {
        const QString baseUrl = urlEdit->text().trimmed();
        const QString modelName = modelEdit->text().trimmed();
        const bool fromApi = OpenAiCompatibleClient::hasApiContextWindow(baseUrl, modelName);
        const int keep = capSpin->value();
        capButton->setEnabled(fromApi);
        if (fromApi)
        {
            capSpin->setRange(0, 4000000);
            capSpin->setSpecialValueText(tr("окно модели"));
            capSpin->setToolTip(
                tr("0 — полное окно модели. Можно указать точное число, в том числе равное окну. "
                   "Иначе история сжимается раньше, по этому потолку."));
            if (keep > 0)
                capSpin->setValue(keep);
        }
        else
        {
            capSpin->setSpecialValueText(QString());
            capSpin->setRange(1024, 4000000);
            capSpin->setToolTip(
                tr("API не сообщил размер окна. Укажите потолок контекста числом."));
            if (keep >= 1024)
                capSpin->setValue(keep);
        }
    };

    auto requestCatalog = [&]() {
        const QString baseUrl = urlEdit->text().trimmed();
        const QString modelName = modelEdit->text().trimmed();
        if (!baseUrl.isEmpty() && !modelName.isEmpty())
        {
            OpenAiCompatibleClient::fetchContextCatalog(
                baseUrl, tokenEdit->text().trimmed(), modelName);
        }
        updateCapMode();
    };

    auto applyProfile = [&](const LlmProfile &source) {
        nameEdit->setText(source.name);
        urlEdit->setText(source.baseUrl);
        modelEdit->setText(source.model);
        tokenEdit->setText(source.apiKey);
        int index = modalityCombo->findData(int(source.modality));
        if (index < 0)
            index = 0;
        modalityCombo->setCurrentIndex(index);
        purposeEdit->setText(source.purpose);
        balanceUrlEdit->setText(source.balanceUrl);
        balancePathEdit->setText(source.balanceJsonPath);
        requestPathEdit->setText(source.requestPath);
        taskPathEdit->setText(source.taskPath);
        catalogUrlEdit->setText(source.catalogUrl);
        extraBodyEdit->setText(source.extraBody);
        toolSpin->setValue(qMax(2, source.maxToolIterations));
        capSpin->setValue(source.contextCap);
        requestCatalog();
    };

    connect(capButton, &QPushButton::clicked, &dialog, [&]() {
        if (OpenAiCompatibleClient::hasApiContextWindow(
                urlEdit->text().trimmed(), modelEdit->text().trimmed()))
            capSpin->setValue(0);
    });
    connect(OpenAiCompatibleClient::catalogHub(), &LlmClient::contextWindowChanged,
            &dialog, [&]() { updateCapMode(); });
    connect(urlEdit, &QLineEdit::editingFinished, &dialog, [&]() { requestCatalog(); });
    connect(modelEdit, &QLineEdit::editingFinished, &dialog, [&]() { requestCatalog(); });
    connect(tokenEdit, &QLineEdit::editingFinished, &dialog, [&]() { requestCatalog(); });

    QPushButton *fromPreset = new QPushButton(tr("Из пресета…"), &dialog);
    connect(fromPreset, &QPushButton::clicked, &dialog, [&]() {
        LlmProfile picked;
        picked.id = profile->id;
        picked.apiKey = tokenEdit->text();
        picked.maxToolIterations = toolSpin->value();
        picked.contextCap = capSpin->value();
        if (!pickModelProfilePreset(&picked))
            return;
        picked.id = profile->id;
        if (picked.apiKey.trimmed().isEmpty())
            picked.apiKey = tokenEdit->text();
        picked.maxToolIterations = toolSpin->value();
        picked.contextCap = capSpin->value();
        applyProfile(picked);
    });

    QFormLayout *form = new QFormLayout();
    if (isNew)
    {
        QComboBox *sourceCombo = new QComboBox(&dialog);
        sourceCombo->addItem(tr("Не копировать"), QString());
        foreach (const LlmProfile &existing, AppSettings::instance()->profiles())
        {
            QString title = existing.name.trimmed();
            if (title.isEmpty())
                title = existing.model.trimmed();
            if (title.isEmpty())
                title = existing.id;
            sourceCombo->addItem(title, existing.id);
        }
        connect(sourceCombo,
                static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                &dialog, [&](int) {
            const QString sourceId = sourceCombo->currentData().toString().trimmed();
            if (sourceId.isEmpty())
                return;
            LlmProfile source = AppSettings::instance()->profileById(sourceId);
            if (source.id.isEmpty())
                return;
            source.id = profile->id;
            const QString sourceName = source.name.trimmed().isEmpty()
                                       ? tr("Профиль")
                                       : source.name.trimmed();
            const QString suffix = tr(" (копия)");
            source.name = sourceName.endsWith(suffix) ? sourceName : sourceName + suffix;
            applyProfile(source);
        });
        form->addRow(tr("На основе:"), sourceCombo);
    }
    form->addRow(tr("Имя профиля:"), nameEdit);
    form->addRow(tr("Адрес API:"), urlEdit);
    form->addRow(tr("Модель:"), modelEdit);
    form->addRow(tr("Токен:"), tokenEdit);
    form->addRow(tr("Класс задач API:"), modalityCombo);
    form->addRow(tr("Назначение модели:"), purposeEdit);
    form->addRow(tr("URL баланса:"), balanceUrlEdit);
    form->addRow(tr("JSON путь баланса:"), balancePathEdit);
    form->addRow(tr("Путь запроса:"), requestPathEdit);
    form->addRow(tr("Путь статуса задачи:"), taskPathEdit);
    form->addRow(tr("Каталог моделей:"), catalogUrlEdit);
    form->addRow(tr("Доп. поля запроса:"), extraBodyEdit);
    form->addRow(tr("Лимит вызовов функций:"), toolSpin);

    QHBoxLayout *capRow = new QHBoxLayout();
    capRow->addWidget(capSpin);
    capRow->addWidget(capButton);
    capRow->addStretch();
    form->addRow(tr("Потолок контекста:"), capRow);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok))
        ok->setText(tr("ОК"));
    if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel))
        cancel->setText(tr("Отмена"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString extra = extraBodyEdit->text().trimmed();
        if (!extra.isEmpty())
        {
            QJsonParseError pe;
            const QJsonDocument doc = QJsonDocument::fromJson(extra.toUtf8(), &pe);
            if (!doc.isObject())
            {
                QMessageBox box(QMessageBox::Warning, tr("Доп. поля запроса"),
                                tr("Нужен JSON-объект вида {\"ключ\": значение}.\n%1")
                                    .arg(pe.errorString()),
                                QMessageBox::Ok, &dialog);
                box.setButtonText(QMessageBox::Ok, tr("ОК"));
                box.exec();
                return;
            }
        }
        profile->name = nameEdit->text().trimmed();
        profile->baseUrl = urlEdit->text().trimmed();
        profile->model = modelEdit->text().trimmed();
        profile->apiKey = tokenEdit->text().trimmed();
        profile->modality = WorkerTaskKind(modalityCombo->currentData().toInt());
        profile->purpose = purposeEdit->text().trimmed();
        profile->balanceUrl = balanceUrlEdit->text().trimmed();
        profile->balanceJsonPath = balancePathEdit->text().trimmed();
        profile->requestPath = requestPathEdit->text().trimmed();
        profile->taskPath = taskPathEdit->text().trimmed();
        profile->catalogUrl = catalogUrlEdit->text().trimmed();
        profile->extraBody = extra;
        profile->maxToolIterations = toolSpin->value();
        profile->contextCap = AppSettings::normalizeProfileContextCap(capSpin->value());
        dialog.accept();
    });

    QHBoxLayout *bottom = new QHBoxLayout();
    bottom->addWidget(fromPreset);
    bottom->addStretch();
    bottom->addWidget(buttons);

    QVBoxLayout *root = new QVBoxLayout(&dialog);
    root->addLayout(form);
    root->addLayout(bottom);
    applyProfile(*profile);
    nameEdit->setFocus();
    return dialog.exec() == QDialog::Accepted;
}

//----------------------------------------------

bool SettingsPage::runPresetDialog(WorkerPreset *preset, bool isNew)
{
    if (!preset)
        return false;

    QDialog dialog(this);
    dialog.setWindowTitle(isNew ? tr("Новый воркер") : tr("Воркер"));
    dialog.resize(480, 220);

    QComboBox *typeCombo = new QComboBox(&dialog);
    typeCombo->addItem(tr("Исполнитель"), int(WorkerTypeExecutor));
    typeCombo->addItem(tr("Инструмент"), int(WorkerTypeTool));
    int typeIndex = typeCombo->findData(int(preset->type));
    if (typeIndex < 0)
        typeIndex = 0;
    typeCombo->setCurrentIndex(typeIndex);

    QComboBox *profileCombo = new QComboBox(&dialog);
    profileCombo->setMinimumWidth(280);
    fillProfileCombo(profileCombo, preset->profileId);
    if (!preset->profileId.isEmpty()
        && profileCombo->findData(preset->profileId) < 0)
    {
        profileCombo->insertItem(0, tr("профиль не найден"), preset->profileId);
        profileCombo->setCurrentIndex(0);
    }

    QFormLayout *form = new QFormLayout();
    if (isNew)
    {
        QComboBox *sourceCombo = new QComboBox(&dialog);
        sourceCombo->addItem(tr("Не копировать"), QString());
        foreach (const WorkerPreset &existing,
                 AppSettings::instance()->workerPresets(_kind))
            sourceCombo->addItem(presetListLabel(existing), existing.id);
        connect(sourceCombo,
                static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                &dialog, [&](int) {
            const QString sourceId = sourceCombo->currentData().toString().trimmed();
            if (sourceId.isEmpty())
                return;
            const WorkerPreset source =
                AppSettings::instance()->workerPresetById(_kind, sourceId);
            if (source.id.isEmpty())
                return;
            int index = typeCombo->findData(int(source.type));
            if (index < 0)
                index = 0;
            typeCombo->setCurrentIndex(index);
            int profileIndex = profileCombo->findData(source.profileId);
            if (profileIndex < 0)
            {
                profileCombo->insertItem(0, tr("профиль не найден"), source.profileId);
                profileIndex = 0;
            }
            profileCombo->setCurrentIndex(profileIndex);
        });
        form->addRow(tr("На основе:"), sourceCombo);
    }
    form->addRow(tr("Тип воркера:"), typeCombo);
    form->addRow(tr("Профиль модели:"), profileCombo);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok))
        ok->setText(tr("ОК"));
    if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel))
        cancel->setText(tr("Отмена"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        WorkerPreset trial = *preset;
        trial.type = WorkerType(typeCombo->currentData().toInt());
        trial.profileId = profileCombo->currentData().toString();
        QString error;
        if (isNew)
        {
            const QString id = AppSettings::instance()->addWorkerPreset(_kind, trial, &error);
            if (id.isEmpty())
            {
                QMessageBox box(QMessageBox::Warning, tr("Воркеры"), error,
                                QMessageBox::Ok, &dialog);
                box.setButtonText(QMessageBox::Ok, tr("ОК"));
                box.exec();
                return;
            }
            trial.id = id;
        }
        else
        {
            AppSettings::instance()->updateWorkerPreset(_kind, trial, &error);
            if (!error.isEmpty())
            {
                QMessageBox box(QMessageBox::Warning, tr("Воркеры"), error,
                                QMessageBox::Ok, &dialog);
                box.setButtonText(QMessageBox::Ok, tr("ОК"));
                box.exec();
                return;
            }
        }
        *preset = trial;
        dialog.accept();
    });

    QVBoxLayout *root = new QVBoxLayout(&dialog);
    root->addLayout(form);
    root->addWidget(buttons);
    return dialog.exec() == QDialog::Accepted;
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

void SettingsPage::sltDropLargeFilesToggled(bool enabled)
{
    AppSettings::instance()->setDropLargeContextFiles(enabled);
    if (_largeFileKbSpin)
        _largeFileKbSpin->setEnabled(enabled);
}

//----------------------------------------------

void SettingsPage::sltLargeFileKbChanged(int kb)
{
    AppSettings::instance()->setLargeContextFileKb(kb);
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
