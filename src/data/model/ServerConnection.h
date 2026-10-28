#pragma once

#include <QString>
#include <QMap>
#include <optional>
#include "data/model/ServerConfig.h"
#include "data/model/Channel.h"
#include "network/ConnectionPolicy.h"
#include "util/IrcIdentity.h"

namespace loungecat {

enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING,
    RECONNECTING,
    ERROR
};

QString connectionStateToString(ConnectionState state);

struct ServerConnection {
    qint64 serverId = 0;
    ServerConfig config;
    ConnectionState state = ConnectionState::DISCONNECTED;
    QList<Channel> channels;
    std::optional<QString> currentChannel;
    std::optional<QString> errorMessage;
    std::optional<qint64> latencyMs;
    int reconnectAttempt = 0;
    bool manuallyDisconnected = false;
    bool encrypted = false;
    QString caseMapping = QStringLiteral("rfc1459");
    ConnectionPolicy policy;
    QString canonical(const QString& name) const { return ircIdentity(name, caseMapping); }
    bool same(const QString& first, const QString& second) const { return canonical(first) == canonical(second); }

    bool isConnected() const { return state == ConnectionState::CONNECTED; }
    bool isConnecting() const { return state == ConnectionState::CONNECTING || state == ConnectionState::RECONNECTING; }
    Channel* findChannel(const QString& name);
    const Channel* findChannel(const QString& name) const;
};

} // namespace loungecat
