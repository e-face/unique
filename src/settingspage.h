#ifndef SETTINGSPAGE_H
#define SETTINGSPAGE_H

#include <QColor>
#include <QScrollArea>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

//----------------------------------------------

/**
 * @brief Страница настроек: профили API, воркеры, проект, безопасность и оформление.
 */
class SettingsPage : public QScrollArea
{
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = 0);

    QCheckBox  *inspectCheck() const;
    void setProjectPath(QString path);

    void loadAppearance();
    void refreshPermissionsList();
    void loadControlFlags();
    void loadProfiles();
    void commitCurrentProfile();

signals:
    void appearanceApplied();
    void profilesChanged();

private slots:
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
    void sltProfileSelected();
    void sltProfileFieldChanged();
    void sltAddProfile();
    void sltRemoveProfile();
    void sltWorkerProfileChanged();
    void sltWorkerLimitsChanged();
    void sltUseModelContext();
    void sltCatalogWindowsUpdated();

private:
    void updateColorButton(QPushButton *button, const QColor &color);
    QWidget *buildApiBox();
    QWidget *buildWorkersBox();
    QWidget *buildProjectBox();
    QWidget *buildSecurityBox();
    QWidget *buildAppearanceBox();
    void fillWorkerCombos();
    void loadWorkerLimits();
    void updateRoleCapHints();
    void requestContextCatalogs();
    void loadCurrentProfileFields();
    QString currentProfileId() const;

    QListWidget *_profileList;
    QLineEdit   *_profileNameEdit;
    QLineEdit   *_profileUrlEdit;
    QLineEdit   *_profileModelEdit;
    QLineEdit   *_profileTokenEdit;
    QLineEdit   *_profileBalanceUrlEdit;
    QLineEdit   *_profileBalancePathEdit;
    QPushButton *_addProfileButton;
    QPushButton *_removeProfileButton;
    QComboBox   *_typeProfile[3];
    QSpinBox    *_maxDepthSpin;
    QSpinBox    *_maxConcurrentSpin;
    QSpinBox    *_llmTimeoutSpin;
    QSpinBox    *_roleIterSpin[4];
    QSpinBox    *_roleCapSpin[4];
    QLabel      *_roleCapHint[4];
    QPushButton *_roleCapModelButton[4];
    int          _pendingModelWindowRole;
    QLabel      *_projectPathLabel;
    QCheckBox   *_inspectCheck;
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
};

#endif // SETTINGSPAGE_H
