#include "ui/window/MainWindow.h"
#include <algorithm>
#include "ui/dialog/ConnectServerDialog.h"
#include "ui/dialog/SettingsDialog.h"
#include "ui/dialog/ModerationDialog.h"
#include "ui/dialog/ChannelListDialog.h"
#include "ui/dialog/ThemeEditorDialog.h"
#include "ui/dialog/HelpDialog.h"
#include "util/ChatExporter.h"
#include "util/ConfigExporter.h"
#include <QIcon>
#include <QShortcut>
#include <QKeySequence>
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QStatusBar>
#include "service/ImageCache.h"

namespace loungecat {

MainWindow::MainWindow(
    DatabaseService* dbService,
    PreferencesManager* prefsManager,
    ThemeManager* themeManager,
    SoundService* soundService,
    UpdateCheckService* updateCheckService,
    ConnectionManager* connectionManager,
    bool headlessMode,
    QWidget* parent
)
    : QMainWindow(parent),
      m_dbService(dbService),
      m_prefsManager(prefsManager),
      m_themeManager(themeManager),
      m_soundService(soundService),
      m_updateCheckService(updateCheckService),
      m_connectionManager(connectionManager) {
    setWindowTitle(QStringLiteral("LoungeCat IRC Client"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/logo_transparent.png")));
    setMinimumSize(800, 600);
    resize(1200, 800);

    if (m_prefsManager) {
        m_spellChecker.setEnabled(m_prefsManager->preferences().spellCheckEnabled);
        m_spellChecker.setLanguage(m_prefsManager->preferences().spellCheckLanguage);
    }

    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->setChildrenCollapsible(false);

    m_serverSidebar = new ServerSidebarWidget(m_mainSplitter);
    m_chatGrid = new ChatGridContainer(m_connectionManager, m_prefsManager, &m_spellChecker, m_mainSplitter);
    m_userSidebar = new UserListSidebarWidget(m_mainSplitter);

    m_mainSplitter->addWidget(m_serverSidebar);
    m_mainSplitter->addWidget(m_chatGrid);
    m_mainSplitter->addWidget(m_userSidebar);
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setStretchFactor(2, 0);
    setCentralWidget(m_mainSplitter);

    m_trayController = new SystemTrayController(this, headlessMode, this);
    connect(m_trayController, &SystemTrayController::quitRequested, qApp, &QApplication::quit);

    // Wire Left Sidebar signals
    connect(m_serverSidebar, &ServerSidebarWidget::channelSelected, m_connectionManager, &ConnectionManager::setActiveChannel);
    connect(m_serverSidebar, &ServerSidebarWidget::addServerRequested, this, &MainWindow::showAddServerDialog);
    connect(m_serverSidebar, &ServerSidebarWidget::editServerRequested, this, &MainWindow::showEditServerDialog);
    connect(m_serverSidebar, &ServerSidebarWidget::deleteServerRequested, this, &MainWindow::confirmDeleteServer);
    connect(m_serverSidebar, &ServerSidebarWidget::connectServerRequested, m_connectionManager, &ConnectionManager::connectServer);
    connect(m_serverSidebar, &ServerSidebarWidget::disconnectServerRequested, this, [this](qint64 sId) {
        m_connectionManager->disconnectServer(sId);
    });
    connect(m_serverSidebar, &ServerSidebarWidget::reconnectServerRequested, m_connectionManager, &ConnectionManager::reconnectServer);
    connect(m_serverSidebar, &ServerSidebarWidget::joinChannelPromptRequested, this, &MainWindow::promptJoinChannel);
    connect(m_serverSidebar, &ServerSidebarWidget::closeChannelRequested, m_connectionManager, &ConnectionManager::closeChannelOrQuery);
    connect(m_serverSidebar, &ServerSidebarWidget::gridModeCycleRequested, m_chatGrid, &ChatGridContainer::cycleGridMode);
    connect(m_serverSidebar, &ServerSidebarWidget::settingsRequested, this, &MainWindow::showSettingsDialog);
    connect(m_serverSidebar, &ServerSidebarWidget::exportConfigRequested, this, &MainWindow::exportConfigBackup);
    connect(m_serverSidebar, &ServerSidebarWidget::importConfigRequested, this, &MainWindow::importConfigBackup);
    connect(m_serverSidebar, &ServerSidebarWidget::helpRequested, this, [this]() {
        showHelpDialog(std::nullopt);
    });

    // Wire Pane signals
    // The primary pane owns the application-wide conversation selection.
    // Auxiliary selectors keep their independent targets.
    connect(m_chatGrid->primaryPane(), &ChannelPaneWidget::paneActivated,
            m_connectionManager, &ConnectionManager::setActiveChannel);
    for (ChannelPaneWidget* pane : m_chatGrid->panes()) {
        connect(pane, &ChannelPaneWidget::toggleUserListRequested, this, [this]() {
            m_userSidebar->setVisible(!m_userSidebar->isVisible());
        });
        connect(pane, &ChannelPaneWidget::exportChatRequested, this, &MainWindow::exportChannelHistory);
        connect(pane, &ChannelPaneWidget::uploadImageRequested, this, &MainWindow::uploadImageForChannel);
    }

    // Wire Right User List Sidebar signals
    connect(m_userSidebar, &UserListSidebarWidget::queryUserRequested, this, [this](qint64 sId, const QString& nick) {
        m_connectionManager->openQuery(sId, nick);
    });
    connect(m_userSidebar, &UserListSidebarWidget::whoisUserRequested, this, [this](qint64 sId, const QString& nick) {
        m_connectionManager->sendUserInput(sId, m_connectionManager->activeChannelName(), QStringLiteral("/whois %1").arg(nick));
    });
    connect(m_userSidebar, &UserListSidebarWidget::silentWhoisRequested, this, [this](qint64 sId, const QString& nick) {
        if (ChatClient* cl = m_connectionManager->client(sId)) {
            cl->startSilentWhois(nick);
        }
    });
    connect(m_userSidebar, &UserListSidebarWidget::mentionUserRequested, this, [this](const QString& nick) {
        m_chatGrid->primaryPane()->appendMentionToInput(nick);
    });
    connect(m_userSidebar, &UserListSidebarWidget::toggleIgnoreUserRequested, this, [this](const QString& nick) {
        if (!m_prefsManager) return;
        const auto connection = m_connectionManager->connection(m_connectionManager->activeServerId());
        const auto mapping = connection ? connection->caseMapping : QStringLiteral("rfc1459");
        if (m_prefsManager->isUserIgnored(nick, mapping)) m_prefsManager->removeIgnoredUser(nick, mapping);
        else m_prefsManager->addIgnoredUser(nick, mapping);
        updateRightSidebarUsers();
    });
    connect(m_userSidebar, &UserListSidebarWidget::slapUserRequested, this, [this](qint64 sId, const QString& ch, const QString& nick) {
        m_connectionManager->sendUserInput(sId, ch, QStringLiteral("/slap %1").arg(nick));
    });
    connect(m_userSidebar, &UserListSidebarWidget::userModeCommandRequested, this, [this](qint64 sId, const QString& ch, const QString& cmd) {
        m_connectionManager->sendUserInput(sId, ch, cmd);
    });
    connect(m_userSidebar, &UserListSidebarWidget::openModerationDialogRequested, this, &MainWindow::showModerationDialog);

    // Wire ConnectionManager signals
    connect(m_connectionManager, &ConnectionManager::connectionsChanged, this, &MainWindow::onConnectionsChanged);
    connect(m_connectionManager, &ConnectionManager::operationError, this, [this](const QString& error) {
        statusBar()->showMessage(error, 15000);
    });
    connect(&ImageCache::instance(), &ImageCache::storageError, this, [this](const QString& error) {
        statusBar()->showMessage(error, 15000);
    });
    connect(m_connectionManager, &ConnectionManager::activeChannelChanged, this, &MainWindow::onActiveChannelChanged);
    connect(m_connectionManager, &ConnectionManager::channelMessagesChanged, this, &MainWindow::onChannelMessagesChanged);
    connect(m_connectionManager, &ConnectionManager::desktopNotificationRequested, m_trayController, &SystemTrayController::showNotification);
    connect(m_connectionManager, &ConnectionManager::openChannelListDialogRequested, this, &MainWindow::showChannelListDialog);
    connect(m_connectionManager, &ConnectionManager::openHelpDialogRequested, this, &MainWindow::showHelpDialog);
    connect(m_connectionManager, &ConnectionManager::whoisInfoUpdated, m_userSidebar, &UserListSidebarWidget::onWhoisUpdated);

    if (m_themeManager) {
        connect(m_themeManager, &ThemeManager::themeChanged, this, &MainWindow::onThemeChanged);
    }
    if (m_prefsManager) {
        connect(m_prefsManager, &PreferencesManager::preferencesChanged, this, &MainWindow::onPreferencesChanged);
    }
    if (m_updateCheckService) {
        connect(m_updateCheckService, &UpdateCheckService::updateAvailable, m_serverSidebar, &ServerSidebarWidget::showUpdateBanner);
    }

    // Search the pane that owns keyboard focus, not always the primary pane.
    auto* searchShortcut = new QShortcut(QKeySequence::Find, this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        auto* focused = QApplication::focusWidget();
        for (auto* pane : m_chatGrid->panes()) {
            if (pane->isVisible() && (pane == focused || pane->isAncestorOf(focused))) {
                pane->openSearchBar();
                return;
            }
        }
        m_chatGrid->primaryPane()->openSearchBar();
    });

    if (m_themeManager) {
        if (m_prefsManager) {
            m_themeManager->setActiveTheme(m_prefsManager->preferences().theme);
        }
        onThemeChanged(m_themeManager->currentTheme());
    }
    onConnectionsChanged();
}

void MainWindow::onConnectionsChanged() {
    if (!m_connectionManager) return;
    m_serverSidebar->updateConnections(
        m_connectionManager->connections(),
        m_connectionManager->activeServerId(),
        m_connectionManager->activeChannelName()
    );
    if (m_chatGrid->primaryPane()->serverId() != m_connectionManager->activeServerId() ||
        m_chatGrid->primaryPane()->channelName() != m_connectionManager->activeChannelName()) {
        m_chatGrid->primaryPane()->setTargetChannel(
            m_connectionManager->activeServerId(),
            m_connectionManager->activeChannelName()
        );
    } else {
        m_chatGrid->refreshAllVisiblePanes();
    }
    updateRightSidebarUsers();
}

void MainWindow::onActiveChannelChanged(qint64 serverId, const QString& channelName) {
    m_chatGrid->primaryPane()->setTargetChannel(serverId, channelName);
    updateRightSidebarUsers();
}

void MainWindow::onChannelMessagesChanged(qint64 serverId, const QString& channelName) {
    for (auto* pane : m_chatGrid->panes()) {
        if (pane->serverId() == serverId && pane->channelName().compare(channelName, Qt::CaseInsensitive) == 0)
            pane->refreshPane();
    }
}

void MainWindow::updateRightSidebarUsers() {
    if (!m_connectionManager) return;
    const qint64 sId = m_connectionManager->activeServerId();
    const QString chName = m_connectionManager->activeChannelName();
    const auto connOpt = m_connectionManager->connection(sId);
    if (!connOpt.has_value()) {
        m_userSidebar->setUsers(0, QString(), {}, {});
        return;
    }
    const Channel* ch = connOpt->findChannel(chName);
    const QSet<QString> ignored = m_prefsManager ? m_prefsManager->preferences().ignoredUsers : QSet<QString>{};
    if (!ch || !ch->isChannel()) {
        m_userSidebar->setUsers(sId, chName, {}, ignored, connOpt->caseMapping);
    } else {
        m_userSidebar->setUsers(sId, chName, ch->users, ignored, connOpt->caseMapping);
    }
}

void MainWindow::onThemeChanged(const ThemeColors& theme) {
    const UserPreferences prefs = m_prefsManager ? m_prefsManager->preferences() : UserPreferences{};
    setStyleSheet(theme.generateStyleSheet(fontSizeMultiplier(prefs.fontSize)));
    m_serverSidebar->applyTheme(theme);
    m_userSidebar->applyTheme(theme, prefs.coloredNicknames);
    m_chatGrid->applyTheme(theme);
    onConnectionsChanged();
}

void MainWindow::onPreferencesChanged(const UserPreferences& prefs) {
    m_spellChecker.setEnabled(prefs.spellCheckEnabled);
    m_spellChecker.setLanguage(prefs.spellCheckLanguage);
    if (m_themeManager) {
        if (m_themeManager->currentTheme().name.compare(prefs.theme, Qt::CaseInsensitive) != 0) {
            m_themeManager->setActiveTheme(prefs.theme);
        } else {
            onThemeChanged(m_themeManager->currentTheme());
        }
    }
}

void MainWindow::showAddServerDialog() {
    ConnectServerDialog dlg(ServerConfig{}, false, this);
    if (dlg.exec() == QDialog::Accepted && m_connectionManager) {
        m_connectionManager->addServer(dlg.serverConfig(), true);
    }
}

void MainWindow::showEditServerDialog(qint64 serverId) {
    if (!m_connectionManager) return;
    const auto connOpt = m_connectionManager->connection(serverId);
    if (!connOpt.has_value()) return;

    ConnectServerDialog dlg(connOpt->config, true, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_connectionManager->updateServer(dlg.serverConfig());
    }
}

void MainWindow::confirmDeleteServer(qint64 serverId) {
    if (!m_connectionManager) return;
    const auto connOpt = m_connectionManager->connection(serverId);
    if (!connOpt.has_value()) return;

    if (QMessageBox::question(
            this,
            QStringLiteral("Remove Server"),
            QStringLiteral("Are you sure you want to remove server \"%1\"?").arg(connOpt->config.serverName)
        ) == QMessageBox::Yes) {
        m_connectionManager->removeServer(serverId);
    }
}

void MainWindow::promptJoinChannel(qint64 serverId) {
    bool ok = false;
    const QString ch = QInputDialog::getText(
        this,
        QStringLiteral("Join Channel"),
        QStringLiteral("Channel name (e.g. #linux):"),
        QLineEdit::Normal,
        QStringLiteral("#"),
        &ok
    );
    if (ok && !ch.trimmed().isEmpty() && m_connectionManager) {
        m_connectionManager->joinChannel(serverId, ch.trimmed());
    }
}

void MainWindow::showSettingsDialog() {
    SettingsDialog dlg(m_prefsManager, m_themeManager, m_soundService, this);
    connect(&dlg, &SettingsDialog::openThemeEditorRequested, this, &MainWindow::showThemeEditorDialog);
    dlg.exec();
}

void MainWindow::showThemeEditorDialog() {
    ThemeEditorDialog dlg(m_themeManager, m_prefsManager, this);
    dlg.exec();
}

void MainWindow::showHelpDialog(const std::optional<QString>& cmd) {
    HelpDialog dlg(cmd, this);
    dlg.exec();
}

void MainWindow::showChannelListDialog(qint64 serverId, const QString& filter) {
    auto* dlg = new ChannelListDialog(serverId, filter, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_connectionManager, &ConnectionManager::channelListReceived, dlg,
            [dlg, serverId](qint64 sId, const QList<ChannelListItem>& items, bool, bool truncated) {
                if (sId == serverId) dlg->setChannels(items, truncated);
            });
    connect(dlg, &ChannelListDialog::joinChannelRequested, this, [this](qint64 sId, const QString& ch) {
        if (m_connectionManager) m_connectionManager->joinChannel(sId, ch);
    });
    dlg->show();
}

void MainWindow::showModerationDialog(qint64 serverId, const QString& channelName, const ChannelUser& user) {
    ModerationDialog dlg(channelName, user, this);
    if (dlg.exec() == QDialog::Accepted && m_connectionManager) {
        for (const QString& cmd : dlg.selectedCommands()) {
            m_connectionManager->sendUserInput(serverId, channelName, cmd);
        }
    }
}

void MainWindow::exportChannelHistory(qint64 serverId, const QString& channelName) {
    if (!m_connectionManager) return;
    const auto connOpt = m_connectionManager->connection(serverId);
    if (!connOpt.has_value()) return;
    const Channel* ch = connOpt->findChannel(channelName);
    if (!ch) return;

    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export Chat History"),
        channelName + QStringLiteral(".txt"),
        QStringLiteral("Text Files (*.txt);;JSON Files (*.json)")
    );
    if (path.isEmpty()) return;

    const ExportFormat fmt = path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)
        ? ExportFormat::JSON
        : ExportFormat::TXT;
    if (!ChatExporter::exportToFile(path, channelName, ch->messages, fmt,
            m_prefsManager ? m_prefsManager->preferences().timezone : std::nullopt))
        QMessageBox::warning(this, QStringLiteral("Export Failed"), QStringLiteral("The chat export could not be saved."));
}

void MainWindow::exportConfigBackup() {
    if (!m_prefsManager || !m_dbService) return;
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Export Backup (Passwords, Keys and Commands Omitted)"),
        QStringLiteral("loungecat-backup.json"),
        QStringLiteral("JSON Files (*.json)")
    );
    if (path.isEmpty()) return;
    if (!ConfigExporter::exportToFile(path, m_prefsManager->preferences(), m_dbService->getAllServerConfigs()))
        QMessageBox::warning(this, QStringLiteral("Backup Failed"), QStringLiteral("The backup could not be saved."));
}

void MainWindow::importConfigBackup() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Import LoungeCat Backup"),
        QString(),
        QStringLiteral("JSON Files (*.json)")
    );
    if (path.isEmpty()) return;

    const auto backupOpt = ConfigExporter::importFromFile(path);
    if (!backupOpt.has_value()) {
        QMessageBox::warning(this, QStringLiteral("Import Failed"), QStringLiteral("Invalid backup JSON file."));
        return;
    }

    QList<ServerConfig> pending;
    auto known = m_dbService ? m_dbService->getAllServerConfigs() : QList<ServerConfig>{};
    for (auto server : backupOpt->servers) {
        const bool duplicate = std::any_of(known.cbegin(), known.cend(), [&](const ServerConfig& existing) {
            return existing.hostname.compare(server.hostname, Qt::CaseInsensitive) == 0 &&
                existing.nickname.compare(server.nickname, Qt::CaseInsensitive) == 0;
        });
        if (!duplicate) { server.id = 0; pending.append(server); known.append(server); }
    }
    if (!m_connectionManager || m_connectionManager->connections().size() + pending.size() > ConnectionManager::MAX_SERVERS) {
        QMessageBox::warning(this, QStringLiteral("Import Failed"), QStringLiteral("The backup would exceed the 16-server limit. Nothing was changed."));
        return;
    }
    auto importedPreferences = backupOpt->preferences;
    // An omitted portable key must not erase a key already saved on this machine.
    if (backupOpt->secretsOmitted && m_prefsManager)
        importedPreferences.imgbbApiKey = m_prefsManager->preferences().imgbbApiKey;
    if (m_prefsManager && !m_prefsManager->setPreferences(importedPreferences)) {
        QMessageBox::warning(this, QStringLiteral("Import Failed"), m_prefsManager->lastError());
        return;
    }
    if (m_themeManager) m_themeManager->setActiveTheme(importedPreferences.theme);
    QList<qint64> imported;
    for (const auto& server : pending) {
        const auto id = m_connectionManager->addServer(server, false);
        if (id <= 0) {
            QMessageBox::warning(this, QStringLiteral("Import Incomplete"),
                QStringLiteral("Settings and %1 server records were imported, but a database write failed. No imported servers were connected. Existing records were preserved.").arg(imported.size()));
            return;
        }
        imported.append(id);
    }
    for (int index = 0; index < imported.size(); ++index)
        if (pending[index].autoConnect) m_connectionManager->connectServer(imported[index]);
    if (backupOpt->secretsOmitted)
        QMessageBox::information(this, QStringLiteral("Backup Imported"),
            QStringLiteral("Passwords, API keys, channel keys and saved commands were not included. Imported profiles have auto-connect off. Re-enter the required details before connecting. Existing local API keys were kept."));
}

void MainWindow::uploadImageForChannel(qint64 serverId, const QString& channelName) {
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Select Image to Upload (or Cancel to upload from Clipboard)"),
        QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp)")
    );
    if (!m_connectionManager) return;
    if (path.isEmpty()) {
        m_connectionManager->sendUserInput(serverId, channelName, QStringLiteral("/uploadimage"));
    } else {
        m_connectionManager->sendUserInput(serverId, channelName, QStringLiteral("/uploadimage %1").arg(path));
    }
}

} // namespace loungecat
