#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include "appsettings.h"
#include "workertypes.h"
//----------------------------------------------
#include <QColor>
#include <QScrollArea>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QFormLayout;
class QPushButton;
class QSpinBox;
class QTimer;
class WorkerManager;

//----------------------------------------------

/**
 * @brief Страница настроек одного набора: defaults программы или проекта.
 *
 * SettingsKindDefaults — config/ рядом с exe; полные профили моделей только здесь.
 * SettingsKindProject — ___project___/; пресеты и лимиты ссылаются на профили
 * программы по id. Оформление UI только на странице defaults.
 */
class SettingsPage : public QScrollArea
{
    Q_OBJECT
public:
    explicit SettingsPage(SettingsKind kind, QWidget *parent = 0);

    SettingsKind kind() const;
    QCheckBox  *inspectCheck() const;
    void setProjectPath(QString path);
    void setWorkerManager(WorkerManager *manager);

    void loadAppearance();
    void refreshPermissionsList();
    void loadControlFlags();
    void loadProfiles();
    void commitCurrentProfile();
    void reloadAll();

public slots:
    void retranslateUi();

signals:
    void appearanceApplied();
    void profilesChanged();
    /** Удалить живых воркеров текущего проекта, использовавших профиль. */
    void removeWorkersForProfile(const QString &profileId);

private slots:
    void sltProjectSettingsChanged();
    void sltPickTextColor();
    void sltPickWindowColor();
    void sltPickPanelColor();
    void sltPickAccentColor();
    void sltPickChatUserColor();
    void sltPickChatAssistantColor();
    void sltPickChatOtherColor();
    void sltApplyAppearance();
    void sltRevokePermission();
    void sltControlToggled();
    void sltShowForwardedToggled(bool enabled);
    void sltProfileSelected();
    void sltProfileFieldChanged();
    void sltAddProfile();
    void sltRemoveProfile();
    void sltEditProfile();
    void sltEditProfileItem(QListWidgetItem *item);
    void sltWorkerLimitsChanged();
    void sltUseModelContext();
    void sltCatalogWindowsUpdated();
    void sltPresetSelected();
    void sltPresetFieldChanged();
    void sltAddPreset();
    void sltRemovePreset();
    void sltEditPreset();
    void sltEditPresetItem(QListWidgetItem *item);
    void sltRootPresetChanged();
    void loadPresets();
    void scheduleLoadPresets();
    void fillRootPresetCombo();
    void sltLanguageComboChanged(int index);

private:
    void updateColorButton(QPushButton *button, const QColor &color);
    QWidget *buildHintBox();
    QWidget *buildLanguageBox();
    QWidget *buildApiBox();
    QWidget *buildProfilesRefBox();
    QWidget *buildWorkersBox();
    QWidget *buildProjectBox();
    QWidget *buildSecurityBox();
    QWidget *buildAppearanceBox();
    void loadWorkerLimits();
    void updateProfileCapHint();
    void requestContextCatalogs();
    void loadCurrentProfileFields();
    QString currentProfileId() const;
    QStringList selectedProfileIds() const;
    QStringList selectedPresetIds() const;
    void refreshProfilePreview();
    void refreshPresetPreview();
    void updateProfileActionButtons();
    void updatePresetActionButtons();
    QString profilePreviewText(const LlmProfile &profile) const;
    QString presetPreviewText(const WorkerPreset &preset) const;
    QString modalityTitle(WorkerTaskKind kind) const;
    QString workerTypeTitle(WorkerType type) const;
    bool runProfileDialog(LlmProfile *profile, bool isNew);
    bool runPresetDialog(WorkerPreset *preset, bool isNew);
    void editProfileById(const QString &id);
    void editPresetById(const QString &id);
    bool pickModelProfilePreset(LlmProfile *outProfile);
    void fillPresetProfileCombo();
    QString currentPresetId() const;
    QString presetListLabel(const WorkerPreset &preset) const;

    SettingsKind _kind;
    WorkerManager *_workers;
    QGroupBox   *_viewBox;
    QGroupBox   *_apiBox;
    QGroupBox   *_profilesRefBox;
    QGroupBox   *_workersBox;
    QGroupBox   *_projectBox;
    QGroupBox   *_securityBox;
    QGroupBox   *_appearanceBox;
    QGroupBox   *_languageGroup;
    QComboBox   *_languageCombo;
    QLabel      *_hintLabel;
    QLabel      *_presetHint;
    QLabel      *_limitsHint;
    QLabel      *_rootHint;
    QLabel      *_profilesRefHint;
    QLabel      *_permissionsHeadingLabel;
    QFormLayout *_profileForm;
    QFormLayout *_presetForm;
    QFormLayout *_rootForm;
    QFormLayout *_limitsForm;
    QFormLayout *_appearanceForm;
    QFormLayout *_languageForm;
    QPushButton *_revokeButton;
    QPushButton *_applyAppearanceButton;
    QListWidget *_profileList;
    QLineEdit   *_profileNameEdit;
    QLineEdit   *_profileUrlEdit;
    QLineEdit   *_profileModelEdit;
    QLineEdit   *_profileTokenEdit;
    QComboBox   *_profileModalityCombo;
    QLineEdit   *_profilePurposeEdit;
    QLineEdit   *_profileBalanceUrlEdit;
    QLineEdit   *_profileBalancePathEdit;
    QSpinBox    *_profileToolIterSpin;
    QSpinBox    *_profileCapSpin;
    QLabel      *_profileCapHint;
    QPushButton *_profileCapModelButton;
    QPushButton *_addProfileButton;
    QPushButton *_editProfileButton;
    QPushButton *_removeProfileButton;
    QPlainTextEdit *_profilePreview;
    QListWidget *_presetList;
    QComboBox   *_presetTypeCombo;
    QComboBox   *_presetProfileCombo;
    QComboBox   *_rootPresetCombo;
    QPushButton *_addPresetButton;
    QPushButton *_editPresetButton;
    QPushButton *_removePresetButton;
    QPlainTextEdit *_presetPreview;
    QSpinBox    *_maxDepthSpin;
    QSpinBox    *_maxConcurrentSpin;
    QSpinBox    *_llmTimeoutSpin;
    bool         _pendingProfileCapWindow;
    QLabel      *_projectPathLabel;
    QCheckBox   *_inspectCheck;
    QCheckBox   *_showForwardedCheck;
    QCheckBox   *_shellControlCheck;
    QCheckBox   *_outsideFilesControlCheck;
    QCheckBox   *_systemSettingsControlCheck;
    QListWidget *_permissionsList;
    QSpinBox    *_fontSpin;
    QPushButton *_textColorButton;
    QPushButton *_windowColorButton;
    QPushButton *_panelColorButton;
    QPushButton *_accentColorButton;
    QPushButton *_chatUserColorButton;
    QPushButton *_chatAssistantColorButton;
    QPushButton *_chatOtherColorButton;
    bool         _updatingProfiles;
    bool         _updatingPresets;
    QTimer      *_presetsReloadTimer;
};

#endif // SETTINGSPAGE_H
