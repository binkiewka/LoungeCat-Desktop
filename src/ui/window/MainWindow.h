#pragma once

#include <QMainWindow>
#include <QSplitter>
#include "network/ConnectionManager.h"
#include "data/database/DatabaseService.h"
#include "data/preferences/PreferencesManager.h"
#include "ui/theme/ThemeManager.h"
#include "service/SoundService.h"
#include "service/UpdateCheckService.h"
#include "util/SpellChecker.h"
#include "ui/sidebar/ServerSidebarWidget.h"
#include "ui/sidebar/UserListSidebarWidget.h"
#include "ui/chat/ChatGridContainer.h"
#include "ui/tray/SystemTrayController.h"

namespace loungecat {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(
        DatabaseService* dbService,
        PreferencesManager* prefsManager,
        ThemeManager* themeManager,
        SoundService* soundService,
        UpdateCheckService* updateCheckService,
        ConnectionManager* connectionManager,
        bool headlessMode = false,
        QWidget* parent = nullptr
    );
    ~MainWindow() override = default;

private Q_SLOTS:
    void onConnectionsChanged();
    void onActiveChannelChanged(qint64 serverId, const QString& channelName);
    void onChannelMessagesChanged(qint64 serverId, const QString& channelName);
    void onThemeChanged(const loungecat::ThemeColors& theme);
    void onPreferencesChanged(const loungecat::UserPreferences& prefs);

    void showAddServerDialog();
    void showEditServerDialog(qint64 serverId);
    void confirmDeleteServer(qint64 serverId);
    void promptJoinChannel(qint64 serverId);
    void showSettingsDialog();
    void showThemeEditorDialog();
    void showHelpDialog(const std::optional<QString>& cmd = std::nullopt);
    void showChannelListDialog(qint64 serverId, const QString& filter);
    void showModerationDialog(qint64 serverId, const QString& channelName, const loungecat::ChannelUser& user);
    void exportChannelHistory(qint64 serverId, const QString& channelName);
    void exportConfigBackup();
    void importConfigBackup();
    void uploadImageForChannel(qint64 serverId, const QString& channelName);

private:
    void updateRightSidebarUsers();

    DatabaseService* m_dbService;
    PreferencesManager* m_prefsManager;
    ThemeManager* m_themeManager;
    SoundService* m_soundService;
    UpdateCheckService* m_updateCheckService;
    ConnectionManager* m_connectionManager;

    SpellChecker m_spellChecker;

    QSplitter* m_mainSplitter = nullptr;
    ServerSidebarWidget* m_serverSidebar = nullptr;
    ChatGridContainer* m_chatGrid = nullptr;
    UserListSidebarWidget* m_userSidebar = nullptr;
    SystemTrayController* m_trayController = nullptr;
};

} // namespace loungecat
