#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QToolButton>
#include <QLabel>
#include <QVBoxLayout>
#include "data/model/ServerConnection.h"
#include "data/model/UserPreferences.h"
#include "ui/theme/Theme.h"
#include "service/UpdateCheckService.h"

namespace loungecat {

class ServerSidebarWidget : public QWidget {
    Q_OBJECT
public:
    explicit ServerSidebarWidget(QWidget* parent = nullptr);
    ~ServerSidebarWidget() override = default;

    void updateConnections(
        const QList<ServerConnection>& connections,
        qint64 activeServerId,
        const QString& activeChannelName
    );
    void applyTheme(const ThemeColors& theme);
    void showUpdateBanner(const VersionInfo& info);

    bool isCollapsed() const { return m_collapsed; }
    void setCollapsed(bool collapsed);

Q_SIGNALS:
    void channelSelected(qint64 serverId, const QString& channelName);
    void addServerRequested();
    void editServerRequested(qint64 serverId);
    void deleteServerRequested(qint64 serverId);
    void connectServerRequested(qint64 serverId);
    void disconnectServerRequested(qint64 serverId);
    void reconnectServerRequested(qint64 serverId);
    void joinChannelPromptRequested(qint64 serverId);
    void closeChannelRequested(qint64 serverId, const QString& channelName);
    void gridModeCycleRequested();
    void settingsRequested();
    void exportConfigRequested();
    void importConfigRequested();
    void helpRequested();

private Q_SLOTS:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onContextMenuRequested(const QPoint& pos);

private:
    void refreshToolbarIcons();

    bool m_collapsed = false;
    ThemeColors m_theme = ThemeColors::darkTheme();

    QWidget* m_headerWidget = nullptr;
    QLabel* m_logoLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QToolButton* m_collapseBtn = nullptr;
    QWidget* m_toolbarWidget = nullptr;
    QToolButton* m_addServerBtn = nullptr;
    QToolButton* m_gridModeBtn = nullptr;
    QToolButton* m_settingsBtn = nullptr;
    QToolButton* m_backupBtn = nullptr;
    QToolButton* m_helpBtn = nullptr;

    QWidget* m_updateBanner = nullptr;
    QLabel* m_updateLabel = nullptr;
    QString m_updateUrl;

    QTreeWidget* m_tree = nullptr;
};

} // namespace loungecat
