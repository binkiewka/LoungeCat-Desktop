#include "ui/sidebar/ServerSidebarWidget.h"
#include "ui/theme/IconFactory.h"
#include "util/PlatformUtils.h"
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QIcon>

namespace loungecat {

namespace {

constexpr int kRoleServerId = Qt::UserRole + 1;
constexpr int kRoleChannelName = Qt::UserRole + 2;
constexpr int kRoleIsServerNode = Qt::UserRole + 3;
constexpr int kRoleStateKey = Qt::UserRole + 4;

} // namespace

ServerSidebarWidget::ServerSidebarWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumWidth(230);
    setMaximumWidth(320);
    setObjectName(QStringLiteral("serverSidebar"));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // Header row with Logo + Title + Collapse button
    m_headerWidget = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(4, 2, 2, 2);
    headerLayout->setSpacing(8);

    m_logoLabel = new QLabel(m_headerWidget);
    QPixmap logoPx(QStringLiteral(":/icons/icon.png"));
    if (!logoPx.isNull()) {
        m_logoLabel->setPixmap(logoPx.scaled(28, 28, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    headerLayout->addWidget(m_logoLabel);

    m_titleLabel = new QLabel(QStringLiteral("<span style=\"font-size:12pt; font-weight:700;\">LoungeCat</span>"), m_headerWidget);
    headerLayout->addWidget(m_titleLabel, 1);

    m_collapseBtn = new QToolButton(m_headerWidget);
    m_collapseBtn->setFixedSize(34, 34);
    m_collapseBtn->setIconSize(QSize(20, 20));
    m_collapseBtn->setToolTip(QStringLiteral("Collapse Sidebar"));
    m_collapseBtn->setAccessibleName(QStringLiteral("Collapse Sidebar"));
    connect(m_collapseBtn, &QToolButton::clicked, this, [this]() {
        setCollapsed(!m_collapsed);
    });
    headerLayout->addWidget(m_collapseBtn);
    mainLayout->addWidget(m_headerWidget);

    // Action Toolbar
    m_toolbarWidget = new QWidget(this);
    auto* toolsLayout = new QHBoxLayout(m_toolbarWidget);
    toolsLayout->setContentsMargins(0, 0, 0, 0);
    toolsLayout->setSpacing(5);

    m_addServerBtn = new QToolButton(m_toolbarWidget);
    m_addServerBtn->setText(QStringLiteral(" Server"));
    m_addServerBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_addServerBtn->setIconSize(QSize(20, 20));
    m_addServerBtn->setMinimumHeight(34);
    m_addServerBtn->setToolTip(QStringLiteral("Add IRC Server"));
    m_addServerBtn->setAccessibleName(QStringLiteral("Add IRC Server"));
    connect(m_addServerBtn, &QToolButton::clicked, this, &ServerSidebarWidget::addServerRequested);
    toolsLayout->addWidget(m_addServerBtn, 1);

    auto configureIconBtn = [](QToolButton* btn, const QString& tooltip) {
        btn->setFixedSize(34, 34);
        btn->setIconSize(QSize(20, 20));
        btn->setToolTip(tooltip);
        btn->setAccessibleName(tooltip);
    };

    m_gridModeBtn = new QToolButton(m_toolbarWidget);
    configureIconBtn(m_gridModeBtn, QStringLiteral("Cycle Split / 2x2 Grid View"));
    connect(m_gridModeBtn, &QToolButton::clicked, this, &ServerSidebarWidget::gridModeCycleRequested);
    toolsLayout->addWidget(m_gridModeBtn);

    m_settingsBtn = new QToolButton(m_toolbarWidget);
    configureIconBtn(m_settingsBtn, QStringLiteral("Settings"));
    connect(m_settingsBtn, &QToolButton::clicked, this, &ServerSidebarWidget::settingsRequested);
    toolsLayout->addWidget(m_settingsBtn);

    m_backupBtn = new QToolButton(m_toolbarWidget);
    configureIconBtn(m_backupBtn, QStringLiteral("Export / Import Configuration Backup"));
    connect(m_backupBtn, &QToolButton::clicked, this, [this]() {
        QMenu menu(this);
        menu.addAction(QStringLiteral("Export Config Backup..."), this, &ServerSidebarWidget::exportConfigRequested);
        menu.addAction(QStringLiteral("Import Config Backup..."), this, &ServerSidebarWidget::importConfigRequested);
        menu.exec(m_backupBtn->mapToGlobal(QPoint(0, m_backupBtn->height())));
    });
    toolsLayout->addWidget(m_backupBtn);

    m_helpBtn = new QToolButton(m_toolbarWidget);
    configureIconBtn(m_helpBtn, QStringLiteral("IRC Commands Help"));
    connect(m_helpBtn, &QToolButton::clicked, this, &ServerSidebarWidget::helpRequested);
    toolsLayout->addWidget(m_helpBtn);

    mainLayout->addWidget(m_toolbarWidget);

    // Update Banner (hidden by default)
    m_updateBanner = new QWidget(this);
    auto* bannerLayout = new QHBoxLayout(m_updateBanner);
    bannerLayout->setContentsMargins(6, 4, 6, 4);
    m_updateLabel = new QLabel(m_updateBanner);
    m_updateLabel->setCursor(Qt::PointingHandCursor);
    auto* openBtn = new QToolButton(m_updateBanner);
    openBtn->setText(QStringLiteral("View"));
    connect(openBtn, &QToolButton::clicked, this, [this]() {
        if (!m_updateUrl.isEmpty()) PlatformUtils::openUrl(m_updateUrl);
    });
    bannerLayout->addWidget(m_updateLabel, 1);
    bannerLayout->addWidget(openBtn);
    m_updateBanner->setVisible(false);
    mainLayout->addWidget(m_updateBanner);

    // Server & Channel Tree (animation disabled to prevent expand/collapse flicker)
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(18);
    m_tree->setAnimated(false);
    m_tree->setUniformRowHeights(false);
    m_tree->setIconSize(QSize(18, 18));
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::itemClicked, this, &ServerSidebarWidget::onItemClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &ServerSidebarWidget::onContextMenuRequested);
    mainLayout->addWidget(m_tree, 1);

    refreshToolbarIcons();
}

void ServerSidebarWidget::refreshToolbarIcons() {
    const QColor fg = m_theme.foreground;
    const QColor accent = m_theme.accentColor;

    m_collapseBtn->setIcon(IconFactory::create(
        m_collapsed ? IconType::ChevronRight : IconType::ChevronLeft, fg, 20
    ));
    m_addServerBtn->setIcon(IconFactory::create(IconType::Plus, accent, 20));
    m_gridModeBtn->setIcon(IconFactory::create(IconType::Grid, fg, 20));
    m_settingsBtn->setIcon(IconFactory::create(IconType::Settings, fg, 20));
    m_backupBtn->setIcon(IconFactory::create(IconType::Backup, fg, 20));
    m_helpBtn->setIcon(IconFactory::create(IconType::Help, fg, 20));
}

void ServerSidebarWidget::setCollapsed(bool collapsed) {
    m_collapsed = collapsed;
    m_logoLabel->setVisible(!collapsed);
    m_titleLabel->setVisible(!collapsed);
    m_toolbarWidget->setVisible(!collapsed);
    m_tree->setVisible(!collapsed);
    if (collapsed) {
        m_collapseBtn->setToolTip(QStringLiteral("Expand Sidebar"));
        m_collapseBtn->setAccessibleName(QStringLiteral("Expand Sidebar"));
        setMinimumWidth(56);
        setMaximumWidth(56);
    } else {
        m_collapseBtn->setToolTip(QStringLiteral("Collapse Sidebar"));
        m_collapseBtn->setAccessibleName(QStringLiteral("Collapse Sidebar"));
        setMinimumWidth(230);
        setMaximumWidth(320);
    }
    refreshToolbarIcons();
}

void ServerSidebarWidget::showUpdateBanner(const VersionInfo& info) {
    m_updateUrl = info.releaseUrl;
    m_updateLabel->setText(QStringLiteral("Update v%1 available!").arg(info.version));
    m_updateBanner->setVisible(!m_collapsed);
}

void ServerSidebarWidget::applyTheme(const ThemeColors& theme) {
    m_theme = theme;
    refreshToolbarIcons();
    // Force icon re-tint on next updateConnections
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        if (auto* srvItem = m_tree->topLevelItem(i)) {
            srvItem->setData(0, kRoleStateKey, QString());
            for (int j = 0; j < srvItem->childCount(); ++j) {
                srvItem->child(j)->setData(0, kRoleStateKey, QString());
            }
        }
    }
}

void ServerSidebarWidget::updateConnections(
    const QList<ServerConnection>& connections,
    qint64 activeServerId,
    const QString& activeChannelName
) {
    m_tree->blockSignals(true);
    m_tree->setUpdatesEnabled(false);

    // 1. Remove top-level server nodes that no longer exist
    QSet<qint64> validServerIds;
    for (const ServerConnection& conn : connections) {
        validServerIds.insert(conn.serverId);
    }
    for (int i = m_tree->topLevelItemCount() - 1; i >= 0; --i) {
        QTreeWidgetItem* item = m_tree->topLevelItem(i);
        const qint64 sId = item->data(0, kRoleServerId).toLongLong();
        if (!validServerIds.contains(sId)) {
            delete m_tree->takeTopLevelItem(i);
        }
    }

    // 2. Update or create each server node in-place (preserving user expand/collapse state!)
    for (int sIdx = 0; sIdx < connections.size(); ++sIdx) {
        const ServerConnection& conn = connections[sIdx];

        QTreeWidgetItem* srvItem = nullptr;
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            if (m_tree->topLevelItem(i)->data(0, kRoleServerId).toLongLong() == conn.serverId) {
                srvItem = m_tree->topLevelItem(i);
                break;
            }
        }

        const bool isNewServerItem = (srvItem == nullptr);
        if (isNewServerItem) {
            srvItem = new QTreeWidgetItem();
            srvItem->setData(0, kRoleServerId, conn.serverId);
            srvItem->setData(0, kRoleIsServerNode, true);
            QFont srvFont = srvItem->font(0);
            srvFont.setBold(true);
            srvItem->setFont(0, srvFont);
            m_tree->insertTopLevelItem(sIdx, srvItem);
            srvItem->setExpanded(true);
        }

        QString srvTitle = conn.config.serverName.isEmpty() ? conn.config.hostname : conn.config.serverName;
        if (conn.isConnected() && conn.encrypted) srvTitle += QStringLiteral("  [TLS]");
        if (conn.latencyMs.has_value() && conn.isConnected()) {
            srvTitle += QStringLiteral("  %1ms").arg(*conn.latencyMs);
        }
        if (srvItem->text(0) != srvTitle) {
            srvItem->setText(0, srvTitle);
        }

        const QString srvStateKey = QStringLiteral("%1:%2")
                                        .arg(static_cast<int>(conn.state))
                                        .arg(m_theme.name);
        if (srvItem->data(0, kRoleStateKey).toString() != srvStateKey) {
            QColor statusColor = m_theme.comment;
            bool hollow = false;
            if (conn.state == ConnectionState::CONNECTED) {
                statusColor = QColor(0x43, 0xb5, 0x81); // vibrant online emerald
            } else if (conn.isConnecting()) {
                statusColor = m_theme.yellow;
            } else if (conn.state == ConnectionState::ERROR) {
                statusColor = m_theme.red;
            } else {
                hollow = true;
            }
            srvItem->setIcon(0, IconFactory::createStatusDot(statusColor, hollow, 18));
            srvItem->setData(0, kRoleStateKey, srvStateKey);
        }

        // 3. Synchronize child channel items in-place without clearing
        // Remove channels that were parted/closed
        QSet<QString> validChanNamesLower;
        for (const Channel& ch : conn.channels) {
            validChanNamesLower.insert(conn.canonical(ch.name));
        }
        QSet<QString> seenNames;
        for (int cIdx = srvItem->childCount() - 1; cIdx >= 0; --cIdx) {
            const QString existingName = srvItem->child(cIdx)->data(0, kRoleChannelName).toString();
            if (!validChanNamesLower.contains(conn.canonical(existingName)) || seenNames.contains(conn.canonical(existingName))) {
                delete srvItem->takeChild(cIdx);
            } else seenNames.insert(conn.canonical(existingName));
        }

        for (int cIdx = 0; cIdx < conn.channels.size(); ++cIdx) {
            const Channel& ch = conn.channels[cIdx];

            QTreeWidgetItem* chItem = nullptr;
            if (cIdx < srvItem->childCount() &&
                conn.same(srvItem->child(cIdx)->data(0, kRoleChannelName).toString(), ch.name)) {
                chItem = srvItem->child(cIdx);
            } else {
                for (int k = 0; k < srvItem->childCount(); ++k) {
                    if (conn.same(srvItem->child(k)->data(0, kRoleChannelName).toString(), ch.name)) {
                        chItem = srvItem->takeChild(k);
                        srvItem->insertChild(cIdx, chItem);
                        break;
                    }
                }
                if (!chItem) {
                    chItem = new QTreeWidgetItem();
                    chItem->setData(0, kRoleServerId, conn.serverId);
                    chItem->setData(0, kRoleChannelName, ch.name);
                    chItem->setData(0, kRoleIsServerNode, false);
                    srvItem->insertChild(cIdx, chItem);
                }
            }

            chItem->setData(0, kRoleChannelName, ch.name);
            // Clean channel display name (strip leading "* " or "#" since icon already shows type)
            QString cleanName = ch.name;
            if (ch.isServer() && cleanName.startsWith(QStringLiteral("* "))) {
                cleanName = cleanName.mid(2);
            } else if (ch.isChannel() && cleanName.startsWith('#')) {
                cleanName = cleanName.mid(1);
            }

            QString label = cleanName;
            if (ch.unreadCount > 0) {
                label += QStringLiteral("  (%1)").arg(ch.unreadCount);
            } else if (ch.hasRecentActivity && !ch.isServer()) {
                label += QStringLiteral("  •");
            }
            if (chItem->text(0) != label) {
                chItem->setText(0, label);
            }

            const bool isCurrentActive =
                (conn.serverId == activeServerId && conn.same(ch.name, activeChannelName));

            const QString chanStyleKey = QStringLiteral("%1:%2:%3:%4:%5")
                                             .arg(static_cast<int>(ch.type))
                                             .arg(ch.hasUnreadMention ? 1 : 0)
                                             .arg(ch.unreadCount > 0 ? 1 : 0)
                                             .arg(isCurrentActive ? 1 : 0)
                                             .arg(m_theme.name);

            if (chItem->data(0, kRoleStateKey).toString() != chanStyleKey) {
                QColor textColor = m_theme.foreground;
                QColor iconColor = m_theme.comment;
                bool bold = false;

                if (ch.hasUnreadMention) {
                    textColor = m_theme.unreadBadge;
                    iconColor = m_theme.unreadBadge;
                    bold = true;
                } else if (ch.unreadCount > 0) {
                    textColor = m_theme.accentColor;
                    iconColor = m_theme.accentColor;
                    bold = true;
                } else if (isCurrentActive) {
                    textColor = m_theme.foreground;
                    iconColor = m_theme.accentColor;
                    bold = true;
                } else if (ch.isServer()) {
                    textColor = m_theme.comment;
                }

                chItem->setForeground(0, textColor);
                QFont f = chItem->font(0);
                f.setBold(bold);
                chItem->setFont(0, f);

                IconType iconType = IconType::ChannelHash;
                if (ch.isServer()) iconType = IconType::ServerPulse;
                else if (ch.isPrivateMessage()) iconType = IconType::DirectMessage;
                chItem->setIcon(0, IconFactory::create(iconType, iconColor, 18));
                chItem->setData(0, kRoleStateKey, chanStyleKey);
            }

            if (isCurrentActive && m_tree->currentItem() != chItem) {
                m_tree->setCurrentItem(chItem);
            }
        }
    }

    m_tree->setUpdatesEnabled(true);
    m_tree->blockSignals(false);
}

void ServerSidebarWidget::onItemClicked(QTreeWidgetItem* item, int) {
    if (!item) return;
    const qint64 srvId = item->data(0, kRoleServerId).toLongLong();
    const bool isServerNode = item->data(0, kRoleIsServerNode).toBool();
    if (isServerNode) {
        if (item->childCount() > 0) {
            const QString chName = item->child(0)->data(0, kRoleChannelName).toString();
            Q_EMIT channelSelected(srvId, chName);
        }
    } else {
        const QString chName = item->data(0, kRoleChannelName).toString();
        Q_EMIT channelSelected(srvId, chName);
    }
}

void ServerSidebarWidget::onContextMenuRequested(const QPoint& pos) {
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (!item) return;

    const qint64 srvId = item->data(0, kRoleServerId).toLongLong();
    const bool isServerNode = item->data(0, kRoleIsServerNode).toBool();

    QMenu menu(this);
    if (isServerNode) {
        menu.addAction(QStringLiteral("Connect"), this, [this, srvId]() {
            Q_EMIT connectServerRequested(srvId);
        });
        menu.addAction(QStringLiteral("Disconnect"), this, [this, srvId]() {
            Q_EMIT disconnectServerRequested(srvId);
        });
        menu.addAction(QStringLiteral("Reconnect"), this, [this, srvId]() {
            Q_EMIT reconnectServerRequested(srvId);
        });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Join Channel..."), this, [this, srvId]() {
            Q_EMIT joinChannelPromptRequested(srvId);
        });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Edit Server..."), this, [this, srvId]() {
            Q_EMIT editServerRequested(srvId);
        });
        menu.addAction(QStringLiteral("Remove Server"), this, [this, srvId]() {
            Q_EMIT deleteServerRequested(srvId);
        });
    } else {
        const QString chName = item->data(0, kRoleChannelName).toString();
        if (!chName.startsWith(QStringLiteral("* "))) {
            menu.addAction(
                chName.startsWith('#') ? QStringLiteral("Leave Channel (%1)").arg(chName)
                                       : QStringLiteral("Close Query (%1)").arg(chName),
                this,
                [this, srvId, chName]() {
                    Q_EMIT closeChannelRequested(srvId, chName);
                }
            );
        }
    }

    if (!menu.isEmpty()) {
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
    }
}

} // namespace loungecat
