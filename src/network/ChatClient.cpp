#include "network/ChatClient.h"

namespace loungecat {

ChatClient::ChatClient(qint64 serverId, const ServerConfig& config, QObject* parent)
    : QObject(parent), m_serverId(serverId), m_config(config) {}

} // namespace loungecat
