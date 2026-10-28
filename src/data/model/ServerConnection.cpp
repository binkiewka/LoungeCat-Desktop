#include "data/model/ServerConnection.h"

namespace loungecat {

QString connectionStateToString(ConnectionState state) {
    switch (state) {
        case ConnectionState::DISCONNECTED:  return QStringLiteral("Disconnected");
        case ConnectionState::CONNECTING:    return QStringLiteral("Connecting...");
        case ConnectionState::CONNECTED:     return QStringLiteral("Connected");
        case ConnectionState::DISCONNECTING: return QStringLiteral("Disconnecting...");
        case ConnectionState::RECONNECTING:  return QStringLiteral("Reconnecting...");
        case ConnectionState::ERROR:         return QStringLiteral("Error");
    }
    return QStringLiteral("Disconnected");
}

Channel* ServerConnection::findChannel(const QString& name) {
    for (auto& ch : channels) {
        if (same(ch.name, name)) {
            return &ch;
        }
    }
    return nullptr;
}

const Channel* ServerConnection::findChannel(const QString& name) const {
    for (const auto& ch : channels) {
        if (same(ch.name, name)) {
            return &ch;
        }
    }
    return nullptr;
}

} // namespace loungecat
