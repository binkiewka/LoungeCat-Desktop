#include "ui/sidebar/UserListSidebarWidget.h"
#include "util/IrcIdentity.h"
#include "ui/theme/IconFactory.h"
#include "util/NickColorGenerator.h"
#include "util/TextColorContrast.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenu>

namespace loungecat {

namespace {
constexpr int kRoleNickname = Qt::UserRole + 1;
constexpr int kRoleHost = Qt::UserRole + 2;
} // namespace

UserListSidebarWidget::UserListSidebarWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumWidth(190);
    setMaximumWidth(280);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 10, 8, 8);
    layout->setSpacing(8);

    auto* headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(4, 0, 4, 2);
    headerRow->setSpacing(6);

    m_headerIconLabel = new QLabel(this);
    m_headerIconLabel->setFixedSize(20, 20);
    headerRow->addWidget(m_headerIconLabel);

    m_headerLabel = new QLabel(QStringLiteral("USERS (0)"), this);
    headerRow->addWidget(m_headerLabel, 1);
    layout->addLayout(headerRow);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(QStringLiteral("Filter users..."));
    m_filterEdit->setClearButtonEnabled(true);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &UserListSidebarWidget::rebuildFilteredList);
    layout->addWidget(m_filterEdit);

    m_listWidget = new QListWidget(this);
    m_listWidget->setMouseTracking(true);
    m_listWidget->setIconSize(QSize(14, 14));
    m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listWidget, &QListWidget::itemEntered, this, &UserListSidebarWidget::onItemEntered);
    connect(m_listWidget, &QListWidget::customContextMenuRequested, this, &UserListSidebarWidget::onContextMenuRequested);
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (!item) return;
        const QString nick = item->data(kRoleNickname).toString();
        if (!nick.isEmpty()) {
            Q_EMIT queryUserRequested(m_serverId, nick);
        }
    });
    layout->addWidget(m_listWidget, 1);

    applyTheme(m_theme, m_coloredNicknames);
}

void UserListSidebarWidget::setUsers(
    qint64 serverId,
    const QString& channelName,
    const QList<ChannelUser>& users,
    const QSet<QString>& ignoredUsers,
    const QString& caseMapping
) {
    if (m_serverId == serverId &&
        m_channelName == channelName &&
        m_allUsers == users &&
        m_ignoredUsers == ignoredUsers && m_caseMapping == caseMapping) {
        return;
    }

    m_serverId = serverId;
    m_channelName = channelName;
    m_caseMapping = caseMapping;
    m_allUsers = users;
    m_ignoredUsers = ignoredUsers;
    m_headerLabel->setText(QStringLiteral("USERS (%1)").arg(m_allUsers.size()));
    rebuildFilteredList();
}

void UserListSidebarWidget::applyTheme(const ThemeColors& theme, bool coloredNicknames) {
    m_theme = theme;
    m_coloredNicknames = coloredNicknames;

    m_headerIconLabel->setPixmap(IconFactory::create(IconType::Users, theme.comment, 20).pixmap(20, 20));
    m_headerLabel->setStyleSheet(QStringLiteral(
        "color: %1; font-weight: 700; font-size: 8.5pt; letter-spacing: 0.8px;"
    ).arg(theme.comment.name()));

    rebuildFilteredList();
}

void UserListSidebarWidget::rebuildFilteredList() {
    m_listWidget->clear();
    const QString filter = m_filterEdit->text().trimmed();

    const QIcon onlineDot = IconFactory::createStatusDot(QColor(0x43, 0xb5, 0x81), true, 14);
    const QIcon awayDot = IconFactory::createStatusDot(m_theme.comment, false, 14);

    for (const ChannelUser& u : m_allUsers) {
        if (!filter.isEmpty() && !u.nickname.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        auto* item = new QListWidgetItem(m_listWidget);
        const QString prefix = u.displayPrefix();
        const bool ignored = std::any_of(m_ignoredUsers.begin(), m_ignoredUsers.end(), [&](const QString& name) { return ircIdentity(name, m_caseMapping) == ircIdentity(u.nickname, m_caseMapping); });
        QString label = prefix + u.nickname;
        if (ignored) label += QStringLiteral(" (ignored)");
        item->setText(label);
        item->setIcon(u.isAway ? awayDot : onlineDot);
        item->setData(kRoleNickname, u.nickname);
        if (u.host.has_value()) item->setData(kRoleHost, *u.host);

        if (ignored || u.isAway) {
            item->setForeground(m_theme.comment);
        } else if (m_coloredNicknames) {
            item->setForeground(readableTextColor(
                NickColorGenerator::getColorForNickname(u.nickname, m_theme.isDark),
                m_theme.sidebarBackground, m_theme.foreground));
        } else {
            item->setForeground(m_theme.foreground);
        }
    }
}

void UserListSidebarWidget::onItemEntered(QListWidgetItem* item) {
    if (!item) return;
    const QString nick = item->data(kRoleNickname).toString();
    if (!nick.isEmpty() && item->toolTip().isEmpty()) {
        item->setToolTip(QStringLiteral("Nickname: %1\nFetching WHOIS...").arg(nick));
        Q_EMIT silentWhoisRequested(m_serverId, nick);
    }
}

void UserListSidebarWidget::onWhoisUpdated(qint64 serverId, const QString& nickname, const WhoisInfo& info) {
    if (serverId != m_serverId) return;
    for (int i = 0; i < m_listWidget->count(); ++i) {
        QListWidgetItem* item = m_listWidget->item(i);
        if (ircIdentity(item->data(kRoleNickname).toString(), m_caseMapping) == ircIdentity(nickname, m_caseMapping)) {
            item->setToolTip(info.toTooltipString());
            break;
        }
    }
}

void UserListSidebarWidget::onContextMenuRequested(const QPoint& pos) {
    QListWidgetItem* item = m_listWidget->itemAt(pos);
    if (!item) return;
    const QString nick = item->data(kRoleNickname).toString();
    if (nick.isEmpty()) return;

    ChannelUser targetUser;
    targetUser.nickname = nick;
    for (const auto& u : m_allUsers) {
        if (ircIdentity(u.nickname, m_caseMapping) == ircIdentity(nick, m_caseMapping)) {
            targetUser = u;
            break;
        }
    }

    QMenu menu(this);
    menu.addAction(QStringLiteral("Message (Query)"), this, [this, nick]() {
        Q_EMIT queryUserRequested(m_serverId, nick);
    });
    menu.addAction(QStringLiteral("WHOIS"), this, [this, nick]() {
        Q_EMIT whoisUserRequested(m_serverId, nick);
    });
    menu.addAction(QStringLiteral("Mention (@%1)").arg(nick), this, [this, nick]() {
        Q_EMIT mentionUserRequested(nick);
    });
    menu.addAction(QStringLiteral("Slap with Trout!"), this, [this, nick]() {
        Q_EMIT slapUserRequested(m_serverId, m_channelName, nick);
    });

    const bool isIgnored = std::any_of(m_ignoredUsers.begin(), m_ignoredUsers.end(), [&](const QString& name) { return ircIdentity(name, m_caseMapping) == ircIdentity(nick, m_caseMapping); });
    menu.addAction(isIgnored ? QStringLiteral("Unignore User") : QStringLiteral("Ignore User"), this, [this, nick]() {
        Q_EMIT toggleIgnoreUserRequested(nick);
    });

    menu.addSeparator();
    QMenu* modMenu = menu.addMenu(QStringLiteral("Moderation"));
    modMenu->addAction(QStringLiteral("Give Op (+o)"), this, [this, nick]() {
        Q_EMIT userModeCommandRequested(m_serverId, m_channelName, QStringLiteral("/op %1").arg(nick));
    });
    modMenu->addAction(QStringLiteral("Take Op (-o)"), this, [this, nick]() {
        Q_EMIT userModeCommandRequested(m_serverId, m_channelName, QStringLiteral("/deop %1").arg(nick));
    });
    modMenu->addAction(QStringLiteral("Give Voice (+v)"), this, [this, nick]() {
        Q_EMIT userModeCommandRequested(m_serverId, m_channelName, QStringLiteral("/voice %1").arg(nick));
    });
    modMenu->addAction(QStringLiteral("Take Voice (-v)"), this, [this, nick]() {
        Q_EMIT userModeCommandRequested(m_serverId, m_channelName, QStringLiteral("/devoice %1").arg(nick));
    });
    modMenu->addSeparator();
    modMenu->addAction(QStringLiteral("Kick / Ban / Quiet..."), this, [this, targetUser]() {
        Q_EMIT openModerationDialogRequested(m_serverId, m_channelName, targetUser);
    });

    menu.exec(m_listWidget->viewport()->mapToGlobal(pos));
}

} // namespace loungecat
