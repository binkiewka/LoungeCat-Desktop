#pragma once

#include <QString>
#include <QList>
#include <optional>
#include "data/model/IncomingMessage.h"
#include "data/model/ChannelUser.h"

namespace loungecat {

enum class ChannelType {
    SERVER,
    CHANNEL,
    QUERY,
    PM
};

struct Channel {
    QString name;
    ChannelType type = ChannelType::CHANNEL;
    QList<IncomingMessage> messages;
    QList<ChannelUser> users;
    std::optional<QString> topic;
    std::optional<QString> topicSetBy;
    std::optional<qint64> topicSetTime;
    int unreadCount = 0;
    bool hasUnreadMention = false;
    bool hasRecentActivity = false;
    qint64 lastActivityTimestamp = 0;
    QList<QString> currentTypingUsers;

    bool isChannel() const { return type == ChannelType::CHANNEL; }
    bool isServer() const { return type == ChannelType::SERVER; }
    bool isPrivateMessage() const { return type == ChannelType::QUERY || type == ChannelType::PM; }
};

} // namespace loungecat
