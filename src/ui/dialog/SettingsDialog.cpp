#include "ui/dialog/SettingsDialog.h"
#include "ui/dialog/ThemeEditorDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QPushButton>

namespace loungecat {

SettingsDialog::SettingsDialog(
    PreferencesManager* prefsManager,
    ThemeManager* themeManager,
    SoundService* soundService,
    QWidget* parent
)
    : QDialog(parent),
      m_prefsManager(prefsManager),
      m_themeManager(themeManager),
      m_soundService(soundService) {
    setWindowTitle(QStringLiteral("LoungeCat Settings"));
    setMinimumSize(540, 460);

    const UserPreferences p = m_prefsManager ? m_prefsManager->preferences() : UserPreferences{};

    auto* mainLayout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);

    // 1. Appearance Tab
    auto* appPage = new QWidget(tabs);
    auto* appForm = new QFormLayout(appPage);

    auto* themeRow = new QHBoxLayout();
    m_themeCombo = new QComboBox(appPage);
    reloadThemeCombo(m_themeManager ? m_themeManager->currentTheme().name : p.theme);

    if (m_themeManager) {
        connect(m_themeCombo, &QComboBox::currentTextChanged, this, [this](const QString& themeName) {
            if (!themeName.isEmpty() && m_themeManager) {
                m_themeManager->setActiveTheme(themeName);
            }
        });
        connect(m_themeManager, &ThemeManager::themeChanged, this, [this](const ThemeColors& activeTheme) {
            reloadThemeCombo(activeTheme.name);
        });
    }
    themeRow->addWidget(m_themeCombo, 1);

    auto* editThemeBtn = new QPushButton(QStringLiteral("Custom Theme Editor..."), appPage);
    connect(editThemeBtn, &QPushButton::clicked, this, [this]() {
        Q_EMIT openThemeEditorRequested();
        ThemeEditorDialog editor(m_themeManager, m_prefsManager, this);
        if (editor.exec() == QDialog::Accepted) {
            if (m_themeManager) {
                reloadThemeCombo(m_themeManager->currentTheme().name);
            }
        }
    });
    themeRow->addWidget(editThemeBtn);
    auto* themeLabel = new QLabel(QStringLiteral("Theme:"), appPage);
    themeLabel->setBuddy(m_themeCombo);
    appForm->addRow(themeLabel, themeRow);

    m_fontSizeCombo = new QComboBox(appPage);
    m_fontSizeCombo->addItem(QStringLiteral("Small (85%)"), static_cast<int>(FontSizePreference::SMALL));
    m_fontSizeCombo->addItem(QStringLiteral("Medium (100%)"), static_cast<int>(FontSizePreference::MEDIUM));
    m_fontSizeCombo->addItem(QStringLiteral("Large (115%)"), static_cast<int>(FontSizePreference::LARGE));
    m_fontSizeCombo->setCurrentIndex(static_cast<int>(p.fontSize));
    appForm->addRow(QStringLiteral("Font Size:"), m_fontSizeCombo);

    m_timestampCombo = new QComboBox(appPage);
    m_timestampCombo->addItem(QStringLiteral("12-Hour (hh:mm AM/PM)"), static_cast<int>(TimestampFormat::HOURS_12));
    m_timestampCombo->addItem(QStringLiteral("24-Hour (HH:mm)"), static_cast<int>(TimestampFormat::HOURS_24));
    m_timestampCombo->addItem(QStringLiteral("Relative (now, 5m, 2h)"), static_cast<int>(TimestampFormat::RELATIVE));
    m_timestampCombo->setCurrentIndex(static_cast<int>(p.timestampFormat));
    appForm->addRow(QStringLiteral("Timestamps:"), m_timestampCombo);

    m_timezoneEdit = new QLineEdit(p.timezone.value_or(QString()), appPage);
    m_timezoneEdit->setPlaceholderText(QStringLiteral("System Default (or e.g. UTC, Europe/Warsaw)"));
    appForm->addRow(QStringLiteral("Timezone:"), m_timezoneEdit);

    m_coloredNicksCheck = new QCheckBox(QStringLiteral("Deterministic colored nicknames"), appPage);
    m_coloredNicksCheck->setChecked(p.coloredNicknames);
    appForm->addRow(QString(), m_coloredNicksCheck);
    tabs->addTab(appPage, QStringLiteral("Appearance"));

    // 2. Chat & Filtering Tab
    auto* chatPage = new QWidget(tabs);
    auto* chatForm = new QFormLayout(chatPage);

    m_joinPartModeCombo = new QComboBox(chatPage);
    m_joinPartModeCombo->addItem(QStringLiteral("Show All"), static_cast<int>(JoinPartQuitDisplayMode::SHOW_ALL));
    m_joinPartModeCombo->addItem(QStringLiteral("Hide All"), static_cast<int>(JoinPartQuitDisplayMode::HIDE_ALL));
    m_joinPartModeCombo->addItem(QStringLiteral("Grouped (Collapsible Summary)"), static_cast<int>(JoinPartQuitDisplayMode::GROUPED));
    m_joinPartModeCombo->addItem(QStringLiteral("Smart Hide (Active Users Only)"), static_cast<int>(JoinPartQuitDisplayMode::SMART_HIDE));
    m_joinPartModeCombo->setCurrentIndex(static_cast<int>(p.joinPartQuitMode));
    chatForm->addRow(QStringLiteral("Join/Part/Quit Display:"), m_joinPartModeCombo);

    m_smartHideMinsSpin = new QSpinBox(chatPage);
    m_smartHideMinsSpin->setRange(1, 1440);
    m_smartHideMinsSpin->setValue(p.smartHideMinutes);
    chatForm->addRow(QStringLiteral("Smart Hide Threshold (min):"), m_smartHideMinsSpin);

    m_historyReplaySpin = new QSpinBox(chatPage);
    m_historyReplaySpin->setRange(0, 2000);
    m_historyReplaySpin->setValue(p.historyReplayLines);
    chatForm->addRow(QStringLiteral("History Replay Lines:"), m_historyReplaySpin);

    m_inputHistorySpin = new QSpinBox(chatPage);
    m_inputHistorySpin->setRange(0, 500);
    m_inputHistorySpin->setValue(p.inputHistorySize);
    chatForm->addRow(QStringLiteral("Input Command History:"), m_inputHistorySpin);

    m_loggingCheck = new QCheckBox(QStringLiteral("Write message logs to disk"), chatPage);
    m_loggingCheck->setChecked(p.loggingEnabled);
    chatForm->addRow(QString(), m_loggingCheck);

    m_highlightWordsEdit = new QLineEdit(QStringList(p.highlightWords.begin(), p.highlightWords.end()).join(QStringLiteral(", ")), chatPage);
    m_highlightWordsEdit->setPlaceholderText(QStringLiteral("Comma-separated words"));
    chatForm->addRow(QStringLiteral("Highlight Words:"), m_highlightWordsEdit);

    m_ignoredUsersEdit = new QLineEdit(QStringList(p.ignoredUsers.begin(), p.ignoredUsers.end()).join(QStringLiteral(", ")), chatPage);
    m_ignoredUsersEdit->setPlaceholderText(QStringLiteral("Comma-separated nicknames"));
    chatForm->addRow(QStringLiteral("Ignored Users:"), m_ignoredUsersEdit);
    tabs->addTab(chatPage, QStringLiteral("Chat & Logs"));

    // 3. Notifications & Sound Tab
    auto* notifPage = new QWidget(tabs);
    auto* notifForm = new QFormLayout(notifPage);

    m_notifyEnabledCheck = new QCheckBox(QStringLiteral("Enable Desktop Notifications"), notifPage);
    m_notifyEnabledCheck->setChecked(p.notificationsEnabled);
    notifForm->addRow(QString(), m_notifyEnabledCheck);

    m_notifyMentionCheck = new QCheckBox(QStringLiteral("Notify on Nickname Mention"), notifPage);
    m_notifyMentionCheck->setChecked(p.notifyOnMention);
    notifForm->addRow(QString(), m_notifyMentionCheck);

    m_notifyHighlightCheck = new QCheckBox(QStringLiteral("Notify on Highlight Word"), notifPage);
    m_notifyHighlightCheck->setChecked(p.notifyOnHighlight);
    notifForm->addRow(QString(), m_notifyHighlightCheck);

    m_notifyPmCheck = new QCheckBox(QStringLiteral("Notify on Private Message"), notifPage);
    m_notifyPmCheck->setChecked(p.notifyOnPrivateMessage);
    notifForm->addRow(QString(), m_notifyPmCheck);

    m_soundEnabledCheck = new QCheckBox(QStringLiteral("Enable Synthesized Audio Alerts"), notifPage);
    m_soundEnabledCheck->setChecked(p.soundAlertsEnabled);
    notifForm->addRow(QString(), m_soundEnabledCheck);

    m_soundMentionCheck = new QCheckBox(QStringLiteral("Play Sound on Mention (880 Hz)"), notifPage);
    m_soundMentionCheck->setChecked(p.soundOnMention);
    notifForm->addRow(QString(), m_soundMentionCheck);

    m_soundHighlightCheck = new QCheckBox(QStringLiteral("Play Sound on Highlight (1046.5 Hz)"), notifPage);
    m_soundHighlightCheck->setChecked(p.soundOnHighlight);
    notifForm->addRow(QString(), m_soundHighlightCheck);

    m_soundPmCheck = new QCheckBox(QStringLiteral("Play Sound on Private Message (660 Hz)"), notifPage);
    m_soundPmCheck->setChecked(p.soundOnPrivateMessage);
    notifForm->addRow(QString(), m_soundPmCheck);

    auto* volRow = new QHBoxLayout();
    m_soundVolumeSlider = new QSlider(Qt::Horizontal, notifPage);
    m_soundVolumeSlider->setRange(0, 100);
    m_soundVolumeSlider->setValue(static_cast<int>(p.soundVolume * 100.0f));
    volRow->addWidget(m_soundVolumeSlider, 1);

    auto* testSoundBtn = new QPushButton(QStringLiteral("Test Sound"), notifPage);
    connect(testSoundBtn, &QPushButton::clicked, this, [this]() {
        if (m_soundService) {
            m_soundService->play(SoundType::MENTION, static_cast<float>(m_soundVolumeSlider->value()) / 100.0f);
        }
    });
    volRow->addWidget(testSoundBtn);
    notifForm->addRow(QStringLiteral("Sound Volume:"), volRow);
    tabs->addTab(notifPage, QStringLiteral("Notifications & Audio"));

    // 4. Media, Links & Spellcheck Tab
    auto* mediaPage = new QWidget(tabs);
    auto* mediaForm = new QFormLayout(mediaPage);

    m_urlDisplayModeCombo = new QComboBox(mediaPage);
    m_urlDisplayModeCombo->addItem(QStringLiteral("Inline Previews (Images & OpenGraph Cards)"), static_cast<int>(UrlImageDisplayMode::INLINE));
    m_urlDisplayModeCombo->addItem(QStringLiteral("Compact Cards (Text Summary)"), static_cast<int>(UrlImageDisplayMode::COMPACT));
    m_urlDisplayModeCombo->addItem(QStringLiteral("Disabled"), static_cast<int>(UrlImageDisplayMode::DISABLED));
    m_urlDisplayModeCombo->setCurrentIndex(static_cast<int>(p.urlImageDisplayMode));
    mediaForm->addRow(QStringLiteral("Link Preview Mode:"), m_urlDisplayModeCombo);

    m_previewsFromOthersCheck = new QCheckBox(QStringLiteral("Fetch Link Previews from All Users"), mediaPage);
    m_previewsFromOthersCheck->setChecked(p.processLinkPreviewsFromOthers);
    m_previewsFromOthersCheck->setToolTip(QStringLiteral("Automatically fetch thumbnails and OpenGraph previews for URLs posted by any user"));
    mediaForm->addRow(QString(), m_previewsFromOthersCheck);

    m_trustedPreviewUsersEdit = new QLineEdit(QStringList(p.trustedPreviewUsers.begin(), p.trustedPreviewUsers.end()).join(QStringLiteral(", ")), mediaPage);
    m_trustedPreviewUsersEdit->setPlaceholderText(QStringLiteral("Trusted nicknames for link previews"));
    mediaForm->addRow(QStringLiteral("Trusted Users:"), m_trustedPreviewUsersEdit);

    m_useProxyPreviewsCheck = new QCheckBox(QStringLiteral("Route Link Preview Requests through Server Proxy"), mediaPage);
    m_useProxyPreviewsCheck->setChecked(p.useProxyForPreviews);
    mediaForm->addRow(QString(), m_useProxyPreviewsCheck);

    m_pastebinCheck = new QCheckBox(QStringLiteral("Auto-upload long messages to 0x0.st Pastebin"), mediaPage);
    m_pastebinCheck->setChecked(p.pastebinEnabled);
    mediaForm->addRow(QString(), m_pastebinCheck);

    m_pastebinThresholdSpin = new QSpinBox(mediaPage);
    m_pastebinThresholdSpin->setRange(100, 5000);
    m_pastebinThresholdSpin->setValue(p.pastebinThreshold);
    mediaForm->addRow(QStringLiteral("Pastebin Threshold (chars):"), m_pastebinThresholdSpin);

    m_urlShortenCheck = new QCheckBox(QStringLiteral("Auto-shorten long URLs via is.gd"), mediaPage);
    m_urlShortenCheck->setChecked(p.urlShorteningEnabled);
    mediaForm->addRow(QString(), m_urlShortenCheck);

    m_urlShortenThresholdSpin = new QSpinBox(mediaPage);
    m_urlShortenThresholdSpin->setRange(50, 5000);
    m_urlShortenThresholdSpin->setValue(p.urlShorteningThreshold);
    mediaForm->addRow(QStringLiteral("URL Shorten Threshold:"), m_urlShortenThresholdSpin);

    m_imgbbKeyEdit = new QLineEdit(p.imgbbApiKey.value_or(QString()), mediaPage);
    m_imgbbKeyEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_imgbbKeyEdit->setPlaceholderText(QStringLiteral("API Key from api.imgbb.com"));
    mediaForm->addRow(QStringLiteral("ImgBB API Key:"), m_imgbbKeyEdit);

    m_spellCheckEnabled = new QCheckBox(QStringLiteral("Enable Spell Checker (Ctrl+M to auto-fix)"), mediaPage);
    m_spellCheckEnabled->setChecked(p.spellCheckEnabled);
    mediaForm->addRow(QString(), m_spellCheckEnabled);

    m_spellLangCombo = new QComboBox(mediaPage);
    m_spellLangCombo->addItems({QStringLiteral("en_US"), QStringLiteral("en"), QStringLiteral("de"), QStringLiteral("nl"), QStringLiteral("pl")});
    m_spellLangCombo->setCurrentText(p.spellCheckLanguage);
    mediaForm->addRow(QStringLiteral("Spell Check Language:"), m_spellLangCombo);
    tabs->addTab(mediaPage, QStringLiteral("Media & Spellcheck"));

    mainLayout->addWidget(tabs, 1);
    m_saveErrorLabel = new QLabel(this);
    m_saveErrorLabel->setWordWrap(true);
    m_saveErrorLabel->setTextFormat(Qt::PlainText);
    m_saveErrorLabel->hide();
    mainLayout->addWidget(m_saveErrorLabel);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(btnBox, &QDialogButtonBox::accepted, this, &SettingsDialog::saveAndAccept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // Escape and the window close button also reject the dialog.
    connect(this, &QDialog::rejected, this, [this]() {
        if (m_themeManager && m_prefsManager) {
            m_themeManager->setActiveTheme(m_prefsManager->preferences().theme);
        }
    });
    mainLayout->addWidget(btnBox);
}

void SettingsDialog::reloadThemeCombo(const QString& targetThemeName) {
    if (!m_themeManager || !m_themeCombo) return;
    m_themeCombo->blockSignals(true);
    m_themeCombo->clear();
    int matchIdx = -1;
    int idx = 0;
    const QStringList themeNames = m_themeManager->availableThemeNames();
    for (const QString& tName : themeNames) {
        m_themeCombo->addItem(tName);
        if (tName.compare(targetThemeName, Qt::CaseInsensitive) == 0) {
            matchIdx = idx;
        }
        ++idx;
    }
    if (matchIdx >= 0) {
        m_themeCombo->setCurrentIndex(matchIdx);
    } else if (!themeNames.isEmpty()) {
        m_themeCombo->setCurrentIndex(0);
    }
    m_themeCombo->blockSignals(false);
}

void SettingsDialog::saveAndAccept() {
    m_saveErrorLabel->hide();
    auto parseCsvSet = [](const QString& text, bool lowerCase) {
        QSet<QString> s;
        for (const QString& item : text.split(',', Qt::SkipEmptyParts)) {
            const QString trimmed = item.trimmed();
            if (!trimmed.isEmpty()) {
                s.insert(lowerCase ? trimmed.toLower() : trimmed);
            }
        }
        return s;
    };

    const QString selectedTheme = (m_themeCombo && m_themeCombo->currentIndex() >= 0)
        ? m_themeCombo->currentText()
        : (m_themeManager ? m_themeManager->currentTheme().name : QStringLiteral("Dark"));

    if (m_prefsManager) {
        const bool saved = m_prefsManager->update([&](UserPreferences& p) {
            p.theme = selectedTheme;
            p.fontSize = static_cast<FontSizePreference>(m_fontSizeCombo->currentData().toInt());
            p.timestampFormat = static_cast<TimestampFormat>(m_timestampCombo->currentData().toInt());
            const QString tz = m_timezoneEdit->text().trimmed();
            p.timezone = tz.isEmpty() ? std::nullopt : std::make_optional(tz);
            p.coloredNicknames = m_coloredNicksCheck->isChecked();

            p.joinPartQuitMode = static_cast<JoinPartQuitDisplayMode>(m_joinPartModeCombo->currentData().toInt());
            p.smartHideMinutes = m_smartHideMinsSpin->value();
            p.historyReplayLines = m_historyReplaySpin->value();
            p.inputHistorySize = m_inputHistorySpin->value();
            p.loggingEnabled = m_loggingCheck->isChecked();
            p.highlightWords = parseCsvSet(m_highlightWordsEdit->text(), true);
            p.ignoredUsers = parseCsvSet(m_ignoredUsersEdit->text(), false);

            p.notificationsEnabled = m_notifyEnabledCheck->isChecked();
            p.notifyOnMention = m_notifyMentionCheck->isChecked();
            p.notifyOnHighlight = m_notifyHighlightCheck->isChecked();
            p.notifyOnPrivateMessage = m_notifyPmCheck->isChecked();
            p.soundAlertsEnabled = m_soundEnabledCheck->isChecked();
            p.soundOnMention = m_soundMentionCheck->isChecked();
            p.soundOnHighlight = m_soundHighlightCheck->isChecked();
            p.soundOnPrivateMessage = m_soundPmCheck->isChecked();
            p.soundVolume = static_cast<float>(m_soundVolumeSlider->value()) / 100.0f;

            p.urlImageDisplayMode = static_cast<UrlImageDisplayMode>(m_urlDisplayModeCombo->currentData().toInt());
            p.processLinkPreviewsFromOthers = m_previewsFromOthersCheck->isChecked();
            p.trustedPreviewUsers = parseCsvSet(m_trustedPreviewUsersEdit->text(), false);
            p.useProxyForPreviews = m_useProxyPreviewsCheck->isChecked();
            p.pastebinEnabled = m_pastebinCheck->isChecked();
            p.pastebinThreshold = m_pastebinThresholdSpin->value();
            p.urlShorteningEnabled = m_urlShortenCheck->isChecked();
            p.urlShorteningThreshold = m_urlShortenThresholdSpin->value();
            const QString imgKey = m_imgbbKeyEdit->text().trimmed();
            p.imgbbApiKey = imgKey.isEmpty() ? std::nullopt : std::make_optional(imgKey);
            p.spellCheckEnabled = m_spellCheckEnabled->isChecked();
            p.spellCheckLanguage = m_spellLangCombo->currentText();
        });
        if (!saved) {
            m_saveErrorLabel->setText(m_prefsManager->lastError());
            m_saveErrorLabel->show();
            return;
        }
    }

    if (m_themeManager) {
        m_themeManager->setActiveTheme(selectedTheme);
    }

    accept();
}

} // namespace loungecat
