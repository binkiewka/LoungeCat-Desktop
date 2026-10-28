#pragma once

#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QSlider>
#include <QLabel>
#include "data/preferences/PreferencesManager.h"
#include "ui/theme/ThemeManager.h"
#include "service/SoundService.h"

namespace loungecat {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(
        PreferencesManager* prefsManager,
        ThemeManager* themeManager,
        SoundService* soundService,
        QWidget* parent = nullptr
    );
    ~SettingsDialog() override = default;

Q_SIGNALS:
    void openThemeEditorRequested();

private Q_SLOTS:
    void saveAndAccept();

private:
    void reloadThemeCombo(const QString& targetThemeName);

    PreferencesManager* m_prefsManager;
    ThemeManager* m_themeManager;
    SoundService* m_soundService;

    QLabel* m_saveErrorLabel = nullptr;

    // Appearance
    QComboBox* m_themeCombo = nullptr;
    QComboBox* m_fontSizeCombo = nullptr;
    QComboBox* m_timestampCombo = nullptr;
    QLineEdit* m_timezoneEdit = nullptr;
    QCheckBox* m_coloredNicksCheck = nullptr;

    // Chat & Filtering
    QComboBox* m_joinPartModeCombo = nullptr;
    QSpinBox* m_smartHideMinsSpin = nullptr;
    QSpinBox* m_historyReplaySpin = nullptr;
    QSpinBox* m_inputHistorySpin = nullptr;
    QCheckBox* m_loggingCheck = nullptr;
    QLineEdit* m_highlightWordsEdit = nullptr;
    QLineEdit* m_ignoredUsersEdit = nullptr;

    // Notifications & Sound
    QCheckBox* m_notifyEnabledCheck = nullptr;
    QCheckBox* m_notifyMentionCheck = nullptr;
    QCheckBox* m_notifyHighlightCheck = nullptr;
    QCheckBox* m_notifyPmCheck = nullptr;
    QCheckBox* m_soundEnabledCheck = nullptr;
    QCheckBox* m_soundMentionCheck = nullptr;
    QCheckBox* m_soundHighlightCheck = nullptr;
    QCheckBox* m_soundPmCheck = nullptr;
    QSlider* m_soundVolumeSlider = nullptr;

    // Media, Links & Spellcheck
    QComboBox* m_urlDisplayModeCombo = nullptr;
    QCheckBox* m_previewsFromOthersCheck = nullptr;
    QLineEdit* m_trustedPreviewUsersEdit = nullptr;
    QCheckBox* m_useProxyPreviewsCheck = nullptr;
    QCheckBox* m_pastebinCheck = nullptr;
    QSpinBox* m_pastebinThresholdSpin = nullptr;
    QCheckBox* m_urlShortenCheck = nullptr;
    QSpinBox* m_urlShortenThresholdSpin = nullptr;
    QLineEdit* m_imgbbKeyEdit = nullptr;
    QCheckBox* m_spellCheckEnabled = nullptr;
    QComboBox* m_spellLangCombo = nullptr;
};

} // namespace loungecat
