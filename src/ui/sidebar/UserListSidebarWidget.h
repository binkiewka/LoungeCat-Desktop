#pragma once

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QSet>
#include "data/model/ChannelUser.h"
#include "data/model/WhoisInfo.h"
#include "ui/theme/Theme.h"

namespace loungecat {

class UserListSidebarWidget : public QWidget {
    Q_OBJECT
public:
    explicit UserListSidebarWidget(QWidget* parent = nullptr);
    ~UserListSidebarWidget() override = default;

    void setUsers(qint64 serverId, const QString& channelName, const QList<ChannelUser>& users, const QSet<QString>& ignoredUsers, const QString& caseMapping = QStringLiteral("rfc1459"));
    void applyTheme(const ThemeColors& theme, bool coloredNicknames);
    void onWhoisUpdated(qint64 serverId, const QString& nickname, const WhoisInfo& info);

Q_SIGNALS:
    void queryUserRequested(qint64 serverId, const QString& nickname);
    void whoisUserRequested(qint64 serverId, const QString& nickname);
    void silentWhoisRequested(qint64 serverId, const QString& nickname);
    void mentionUserRequested(const QString& nickname);
    void toggleIgnoreUserRequested(const QString& nickname);
    void slapUserRequested(qint64 serverId, const QString& channelName, const QString& nickname);
    void userModeCommandRequested(qint64 serverId, const QString& channelName, const QString& modeCmd);
    void openModerationDialogRequested(qint64 serverId, const QString& channelName, const loungecat::ChannelUser& user);

private Q_SLOTS:
    void rebuildFilteredList();
    void onContextMenuRequested(const QPoint& pos);
    void onItemEntered(QListWidgetItem* item);

private:
    qint64 m_serverId = 0;
    QString m_channelName;
    QString m_caseMapping = QStringLiteral("rfc1459");
    QList<ChannelUser> m_allUsers;
    QSet<QString> m_ignoredUsers;
    ThemeColors m_theme = ThemeColors::darkTheme();
    bool m_coloredNicknames = true;

    QLabel* m_headerIconLabel = nullptr;
    QLabel* m_headerLabel = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QListWidget* m_listWidget = nullptr;
};

} // namespace loungecat
