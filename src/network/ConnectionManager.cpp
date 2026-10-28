#include "util/IrcLimits.h"
#include "util/IrcSecrets.h"
#include "network/ConnectionManager.h"
#include "network/IrcClient.h"
#include "util/IrcCommandParser.h"
#include "util/HighlightMatcher.h"
#include "util/IrcFormatParser.h"
#include "util/SystemInfoCollector.h"
#include <QDateTime>
#include <algorithm>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QThread>
#include <QRandomGenerator>
#include <QScopeGuard>
#include "util/IrcIdentity.h"

namespace loungecat {

ConnectionManager::ConnectionManager(
    DatabaseService* dbService,
    MessageCache* messageCache,
    PreferencesManager* prefsManager,
    TextLogService* textLogService,
    SoundService* soundService,
    QObject* parent
)
    : QObject(parent),
      m_dbService(dbService),
      m_messageCache(messageCache),
      m_prefsManager(prefsManager),
      m_textLogService(textLogService),
      m_soundService(soundService) {
    m_clock.start();
    m_workPool.setMaxThreadCount(1);
    if (m_messageCache) connect(m_messageCache, &MessageCache::storageError, this, &ConnectionManager::operationError);
    if (m_prefsManager) connect(m_prefsManager, &PreferencesManager::storageError, this, &ConnectionManager::operationError);
    if (m_textLogService) connect(m_textLogService, &TextLogService::storageError, this, &ConnectionManager::operationError);
    connect(this, &ConnectionManager::inputFailed, this,
        [this](qint64 serverId, const QString& target, const QString& text, const std::optional<IncomingMessage>&, const QString& error) {
            if (!m_connections.contains(serverId)) { Q_EMIT operationError(error); return; }
            IncomingMessage message;
            message.target = m_connections[serverId].findChannel(target) ? target : m_connections[serverId].channels.first().name;
            message.sender = QStringLiteral("System");
            message.type = MessageType::ERROR;
            message.content = QStringLiteral("Not submitted: %1\nRetained text: %2").arg(error, text);
            appendMessageToChannel(serverId, message.target, message);
        });
    m_typingTimer.setInterval(1000);
    connect(&m_typingTimer, &QTimer::timeout, this, [this]() {
        for (const auto& key : m_typingExpires.keys()) {
            if (!m_typingExpires[key].hasExpired()) continue;
            const int first = key.indexOf(':');
            const int last = key.lastIndexOf(':');
            const auto serverId = key.left(first).toLongLong();
            const auto channelName = key.mid(first + 1, last - first - 1);
            const auto nickname = key.mid(last + 1);
            m_typingExpires.remove(key);
            if (m_connections.contains(serverId)) if (auto* channel = m_connections[serverId].findChannel(channelName)) {
                channel->currentTypingUsers.removeAll(nickname);
                scheduleUiUpdate(serverId, channel->name);
            }
        }
    });
    m_typingTimer.start();
    if (m_textLogService && !m_textLogService->lastError().isEmpty()) {
        const auto error = m_textLogService->lastError();
        QTimer::singleShot(0, this, [this, error]() { Q_EMIT operationError(error); });
    }
    m_uiThrottleTimer.setInterval(UI_UPDATE_THROTTLE_MS);
    m_uiThrottleTimer.setSingleShot(true);
    connect(&m_uiThrottleTimer, &QTimer::timeout, this, [this]() {
        if (m_pendingConnectionsUpdate) {
            m_pendingConnectionsUpdate = false;
            Q_EMIT connectionsChanged();
        }
        const auto updates = m_pendingChannelUpdates;
        m_pendingChannelUpdates.clear();
        for (const auto& [srvId, chName] : updates) {
            Q_EMIT channelMessagesChanged(srvId, chName);
        }
    });
}

ConnectionManager::~ConnectionManager() {
    for (auto* client : m_clients) {
        if (client) {
            client->disconnect(this);
            client->disconnectFromServer();
        }
    }
}

int ConnectionManager::reconnectDelayMsForAttempt(int attempt, double random) {
    return ConnectionPolicy::retryDelay(attempt, random);
}

QString ConnectionManager::canonical(qint64 id, const QString& name) const {
    return m_connections.contains(id) ? m_connections[id].canonical(name) : ircIdentity(name);
}
bool ConnectionManager::same(qint64 id, const QString& a, const QString& b) const {
    return canonical(id, a) == canonical(id, b);
}

bool ConnectionManager::isIgnored(qint64 id, const QString& nickname) const {
    return m_prefsManager && m_prefsManager->isUserIgnored(nickname,
        m_connections.contains(id) ? m_connections[id].caseMapping : QStringLiteral("rfc1459"));
}

void ConnectionManager::clearTypingUser(qint64 serverId, Channel& channel, const QString& nickname) {
    if (channel.currentTypingUsers.removeIf([&](const QString& value) { return same(serverId, value, nickname); }) == 0) return;
    const auto prefix = QStringLiteral("%1:").arg(serverId);
    // Match both components using the negotiated identity rules, including
    // timers retained across a CASEMAPPING update or a display-case change.
    for (const auto& key : m_typingExpires.keys()) {
        if (!key.startsWith(prefix)) continue;
        const int last = key.lastIndexOf(':');
        if (same(serverId, key.mid(prefix.size(), last - prefix.size()), channel.name) &&
            same(serverId, key.mid(last + 1), nickname)) m_typingExpires.remove(key);
    }
}

void ConnectionManager::initializeFromDatabase(bool autoConnectEnabled) {
    if (!m_dbService) return;
    const QList<ServerConfig> configs = m_dbService->getAllServerConfigs();
    for (const ServerConfig& cfg : configs) {
        if (m_connections.size() >= MAX_SERVERS) { Q_EMIT operationError(QStringLiteral("Server limit reached; remaining saved servers were not opened.")); break; }
        ServerConnection conn;
        conn.serverId = cfg.id;
        conn.config = cfg;
        conn.state = ConnectionState::DISCONNECTED;

        const QString srvTab = QStringLiteral("* ") + (cfg.serverName.isEmpty() ? cfg.hostname : cfg.serverName);
        Channel serverChan;
        serverChan.name = srvTab;
        serverChan.type = ChannelType::SERVER;
        conn.channels.append(serverChan);
        conn.currentChannel = srvTab;

        m_connections.insert(cfg.id, conn);
        auto* irc = m_clientFactory ? m_clientFactory(cfg.id, cfg, this) : new IrcClient(cfg.id, cfg, this);
        m_clients.insert(cfg.id, irc);
        wireClientSignals(irc);

        if (m_activeServerId == 0) {
            m_activeServerId = cfg.id;
            m_activeChannelName = srvTab;
        }

        if (autoConnectEnabled && cfg.autoConnect) {
            connectServer(cfg.id);
        }
    }
    Q_EMIT connectionsChanged();
}

QList<ServerConnection> ConnectionManager::connections() const {
    return m_connections.values();
}

std::optional<ServerConnection> ConnectionManager::connection(qint64 serverId) const {
    if (m_connections.contains(serverId)) {
        return m_connections.value(serverId);
    }
    return std::nullopt;
}

void ConnectionManager::setActiveChannel(qint64 serverId, const QString& channelName) {
    if (!m_connections.contains(serverId)) return;
    ServerConnection& conn = m_connections[serverId];
    conn.currentChannel = channelName;
    if (Channel* ch = conn.findChannel(channelName)) {
        ch->unreadCount = 0;
        ch->hasUnreadMention = false;
    }
    m_activeServerId = serverId;
    m_activeChannelName = channelName;
    Q_EMIT activeChannelChanged(serverId, channelName);
    Q_EMIT connectionsChanged();
}

qint64 ConnectionManager::addServer(const ServerConfig& config, bool connectImmediately) {
    if (m_connections.size() >= MAX_SERVERS) { Q_EMIT operationError(QStringLiteral("The server limit was reached.")); return -1; }
    ServerConfig saved = config;
    if (m_dbService) {
        saved.id = m_dbService->insertServerConfig(config);
        if (saved.id <= 0) {
            Q_EMIT operationError(QStringLiteral("Server not added: the database could not save it."));
            return -1;
        }
    } else {
        if (saved.id <= 0) saved.id = QDateTime::currentMSecsSinceEpoch();
        while (m_connections.contains(saved.id)) ++saved.id;
    }

    ServerConnection conn;
    conn.serverId = saved.id;
    conn.config = saved;
    conn.state = ConnectionState::DISCONNECTED;

    const QString srvTab = QStringLiteral("* ") + (saved.serverName.isEmpty() ? saved.hostname : saved.serverName);
    Channel serverChan;
    serverChan.name = srvTab;
    serverChan.type = ChannelType::SERVER;
    conn.channels.append(serverChan);
    conn.currentChannel = srvTab;

    m_connections.insert(saved.id, conn);
    auto* irc = m_clientFactory ? m_clientFactory(saved.id, saved, this) : new IrcClient(saved.id, saved, this);
    m_clients.insert(saved.id, irc);
    wireClientSignals(irc);

    if (m_activeServerId == 0) {
        setActiveChannel(saved.id, srvTab);
    }
    Q_EMIT connectionsChanged();

    if (connectImmediately) {
        connectServer(saved.id);
    }
    return saved.id;
}

void ConnectionManager::updateServer(const ServerConfig& config) {
    const auto serverId = config.id;
    if (!m_connections.contains(config.id)) return;
    ServerConnection& conn = m_connections[config.id];
    const QString oldAutoJoin = conn.config.autoJoinChannels;

    if (m_dbService) {
        if (!m_dbService->updateServerConfig(config)) {
            Q_EMIT operationError(QStringLiteral("Server settings were not saved."));
            return;
        }
    }
    conn.config = config;
    if (m_clients.contains(config.id)) {
        m_clients[config.id]->updateConfig(config);
    }

    if (conn.isConnected() && oldAutoJoin != config.autoJoinChannels && m_clients.contains(config.id)) {
        auto parseChanList = [](const QString& csv) {
            QStringList out;
            for (const QString& s : csv.split(',', Qt::SkipEmptyParts)) {
                QString t = s.trimmed();
                if (!t.isEmpty()) {
                    if (!t.startsWith('#') && !t.startsWith('&') && !t.startsWith('+') && !t.startsWith('!')) {
                        t.prepend('#');
                    }
                    out.append(t);
                }
            }
            return out;
        };
        const QStringList oldChans = parseChanList(oldAutoJoin);
        const QStringList newChans = parseChanList(config.autoJoinChannels);

        for (const QString& oldCh : oldChans) {
            bool stillWanted = false;
            for (const QString& newCh : newChans) {
                if (same(serverId, oldCh, newCh)) {
                    stillWanted = true;
                    break;
                }
            }
            if (!stillWanted && conn.findChannel(oldCh) != nullptr) {
                closeChannelOrQuery(config.id, oldCh);
            }
        }

        for (const QString& newCh : newChans) {
            if (conn.findChannel(newCh) == nullptr) {
                m_clients[config.id]->joinChannel(newCh);
            }
        }
    }

    if (conn.policy.desired && conn.policy.paused) connectServer(config.id);
    Q_EMIT connectionsChanged();
}

void ConnectionManager::updateSavedChannels(qint64 serverId, const QString& channelName, bool isJoining, const std::optional<QString>& key) {
    if (!m_connections.contains(serverId)) return;
    QString target = channelName.trimmed();
    if (target.isEmpty() || target.startsWith(QStringLiteral("* "))) return;
    if (!target.startsWith('#') && !target.startsWith('&') && !target.startsWith('+') && !target.startsWith('!')) {
        if (isJoining) {
            target.prepend('#');
        } else {
            return;
        }
    }

    ServerConnection& conn = m_connections[serverId];
    QStringList currentList;
    for (const QString& item : conn.config.autoJoinChannels.split(',', Qt::SkipEmptyParts)) {
        const QString trimmed = item.trimmed();
        if (!trimmed.isEmpty()) {
            currentList.append(trimmed);
        }
    }

    bool changed = false;
    if (isJoining) {
        bool exists = false;
        for (const QString& ch : currentList) {
            if (same(serverId, ch, target)) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            currentList.append(target);
            changed = true;
        }
    } else {
        const qsizetype before = currentList.size();
        currentList.removeIf([&](const QString& ch) {
            return same(serverId, ch, target);
        });
        changed = (currentList.size() != before);
    }

    auto keys = conn.config.channelKeys;
    if (!isJoining || key) {
        for (const auto& saved : keys.keys()) if (conn.same(saved, target)) keys.remove(saved);
        if (isJoining && key && !key->isEmpty()) keys.insert(target, *key);
    }
    if (changed || keys != conn.config.channelKeys) {
        auto updated = conn.config;
        updated.channelKeys = keys;
        updated.autoJoinChannels = currentList.join(QStringLiteral(","));
        if (m_dbService && !m_dbService->updateServerConfig(updated)) {
            Q_EMIT operationError(QStringLiteral("The channel list could not be saved; the previous configuration was retained."));
            return;
        }
        conn.config = updated;
        if (m_clients.contains(serverId)) {
            m_clients[serverId]->updateConfig(conn.config);
        }
    }
}

void ConnectionManager::removeServer(qint64 serverId) {
    if (m_dbService && !m_dbService->deleteServerConfig(serverId)) {
        Q_EMIT operationError(QStringLiteral("Server not removed: the database write failed."));
        return;
    }
    disconnectServer(serverId);
    if (m_clients.contains(serverId)) {
        m_clients.take(serverId)->deleteLater();
    }
    if (m_reconnectTimers.contains(serverId)) {
        m_reconnectTimers.take(serverId)->deleteLater();
    }
    if (m_stabilityTimers.contains(serverId)) {
        m_stabilityTimers.take(serverId)->deleteLater();
    }
    m_connections.remove(serverId);
    m_generations.remove(serverId);
    const auto removedPrefix = QStringLiteral("%1:").arg(serverId);
    for (const auto& key : m_channelLoads.keys()) if (key.startsWith(removedPrefix)) m_channelLoads.remove(key);
    for (const auto& key : m_typingExpires.keys()) if (key.startsWith(removedPrefix)) m_typingExpires.remove(key);
    if (m_messageCache) {
        m_messageCache->clearServerCache(serverId);
    }
    const auto prefix = QStringLiteral("%1:").arg(serverId);
    for (const auto& key : m_inputHistories.keys()) if (key.startsWith(prefix)) m_inputHistories.remove(key);
    for (const auto& key : m_lastActiveTimes.keys()) if (key.startsWith(prefix)) m_lastActiveTimes.remove(key);
    if (m_activeServerId == serverId) {
        if (!m_connections.isEmpty()) {
            const auto& first = m_connections.first();
            setActiveChannel(first.serverId, first.currentChannel.value_or(QString()));
        } else {
            m_activeServerId = 0;
            m_activeChannelName.clear();
            Q_EMIT activeChannelChanged(0, QString());
        }
    }
    Q_EMIT connectionsChanged();
}

void ConnectionManager::connectServer(qint64 serverId) {
    if (!m_connections.contains(serverId) || !m_clients.contains(serverId)) return;
    if (m_reconnectTimers.contains(serverId)) {
        m_reconnectTimers[serverId]->stop();
    }
    ServerConnection& conn = m_connections[serverId];
    if (!conn.policy.begin()) return;
    m_generations[serverId] = ++m_nextGeneration;
    conn.manuallyDisconnected = false;
    conn.state = ConnectionState::CONNECTING;
    conn.errorMessage = std::nullopt;
    Q_EMIT connectionsChanged();

    m_clients[serverId]->connectToServer();
}

void ConnectionManager::disconnectServer(qint64 serverId, const QString& reason) {
    if (!m_connections.contains(serverId) || !m_clients.contains(serverId)) return;
    if (m_reconnectTimers.contains(serverId)) {
        m_reconnectTimers[serverId]->stop();
    }
    if (m_stabilityTimers.contains(serverId)) {
        m_stabilityTimers[serverId]->stop();
    }
    ServerConnection& conn = m_connections[serverId];
    m_generations[serverId] = ++m_nextGeneration;
    conn.policy.stop();
    conn.manuallyDisconnected = true;
    conn.reconnectAttempt = 0;
    conn.state = ConnectionState::DISCONNECTED;
    Q_EMIT connectionsChanged();

    m_clients[serverId]->disconnectFromServer(reason);
}

void ConnectionManager::reconnectServer(qint64 serverId) {
    disconnectServer(serverId, QStringLiteral("Reconnecting..."));
    const auto generation = m_generations.value(serverId);
    QTimer::singleShot(300, this, [this, serverId, generation]() {
        if (m_generations.value(serverId) == generation) connectServer(serverId);
    });
}

void ConnectionManager::joinChannel(qint64 serverId, const QString& channelName, const QString& key) {
    if (!m_clients.contains(serverId)) return;
    const auto names = channelName.split(',', Qt::KeepEmptyParts);
    const auto keys = key.isEmpty() ? QStringList{} : key.split(',', Qt::KeepEmptyParts);
    auto invalid = [](const QString& value) {
        return value.size() > 200 || std::any_of(value.begin(), value.end(), [](QChar c) {
            return c.isSpace() || c.unicode() < 32 || c == ':';
        });
    };
    if (names.isEmpty() || names.size() > 50 || keys.size() > names.size() ||
        std::any_of(names.begin(), names.end(), [&](const QString& n) { return n.isEmpty() || invalid(n); }) ||
        std::any_of(keys.begin(), keys.end(), invalid) ||
        (!key.isEmpty() && (m_clients[serverId]->isConnected() ?
            !m_clients[serverId]->isEncrypted() : !m_clients[serverId]->config().useSsl))) {
        Q_EMIT operationError(QStringLiteral("Invalid JOIN list, or TLS is required for channel keys.")); return;
    }
    QStringList normalized;
    for (auto name : names) {
        if (!QStringLiteral("#&+!").contains(name.front())) name.prepend('#');
        if (name.size() > 200 || !canCreateChannel(serverId, name)) {
            Q_EMIT operationError(QStringLiteral("Invalid channel or channel limit reached.")); return;
        }
        normalized.append(name);
    }
    for (int i = 0; i < normalized.size(); ++i) {
        const auto channelKey = keys.value(i);
        m_clients[serverId]->joinChannel(normalized[i], channelKey);
        updateSavedChannels(serverId, normalized[i], true, channelKey);
    }
}

void ConnectionManager::partChannel(qint64 serverId, const QString& channelName, const QString& reason) {
    if (!m_clients.contains(serverId)) return;
    m_clients[serverId]->partChannel(channelName, reason);
    updateSavedChannels(serverId, channelName, false);
}

void ConnectionManager::closeChannelOrQuery(qint64 serverId, const QString& channelName) {
    if (!m_connections.contains(serverId)) return;
    ServerConnection& conn = m_connections[serverId];
    Channel* ch = conn.findChannel(channelName);
    if (!ch || ch->type == ChannelType::SERVER) return;

    const bool isChan = (ch->type == ChannelType::CHANNEL);
    if (isChan) {
        if (conn.isConnected() && m_clients.contains(serverId)) {
            m_clients[serverId]->partChannel(channelName);
        }
        updateSavedChannels(serverId, channelName, false);
    }

    for (int i = 0; i < conn.channels.size(); ++i) {
        if (same(serverId, conn.channels[i].name, channelName)) {
            conn.channels.removeAt(i);
            break;
        }
    }

    const QString stateKey = QStringLiteral("%1:%2").arg(serverId).arg(canonical(serverId, channelName));
    m_inputHistories.remove(stateKey);
    m_lastActiveTimes.remove(stateKey);
    m_channelLoads.remove(stateKey);
    if (m_messageCache) m_messageCache->retireChannel(serverId, channelName);
    for (const auto& key : m_typingExpires.keys()) if (key.startsWith(stateKey + ':')) m_typingExpires.remove(key);

    if (m_activeServerId == serverId && same(serverId, m_activeChannelName, channelName)) {
        const QString fallback = conn.channels.isEmpty() ? QString() : conn.channels.first().name;
        setActiveChannel(serverId, fallback);
    }
    Q_EMIT connectionsChanged();
}

bool ConnectionManager::openQuery(qint64 serverId, const QString& nickname, const QString& initialMessage) {
    if (!m_connections.contains(serverId)) return false;
    if (!canCreateChannel(serverId, nickname)) { Q_EMIT operationError(QStringLiteral("Conversation limit reached.")); return false; }
    if (!initialMessage.trimmed().isEmpty()) {
        auto* transport = client(serverId);
        if (!transport || !transport->sendMessage(nickname, initialMessage)) return false;
    }
    ensureChannel(serverId, nickname, ChannelType::QUERY);
    setActiveChannel(serverId, nickname);
    return true;
}

void ConnectionManager::reorderChannel(qint64 serverId, int fromIndex, int toIndex) {
    if (!m_connections.contains(serverId)) return;
    auto& chans = m_connections[serverId].channels;
    // Keep server console at index 0
    if (fromIndex <= 0 || toIndex <= 0 || fromIndex >= chans.size() || toIndex >= chans.size()) {
        return;
    }
    chans.move(fromIndex, toIndex);
    Q_EMIT connectionsChanged();
}

Channel& ConnectionManager::ensureChannel(qint64 serverId, const QString& name, ChannelType defaultType) {
    ServerConnection& conn = m_connections[serverId];
    if (Channel* existing = conn.findChannel(name)) {
        return *existing;
    }
    if (!canCreateChannel(serverId, name)) {
        Q_EMIT operationError(QStringLiteral("Conversation limit reached; existing histories were preserved."));
        return conn.channels.first();
    }

    Channel ch;
    ch.name = name;
    if (name.startsWith(QStringLiteral("* "))) {
        ch.type = ChannelType::SERVER;
    } else if (name.startsWith('#') || name.startsWith('&') || name.startsWith('+') || name.startsWith('!')) {
        ch.type = ChannelType::CHANNEL;
    } else {
        ch.type = defaultType;
    }

    conn.channels.append(ch);
    if (m_messageCache && ch.type != ChannelType::SERVER) {
        const auto key = QStringLiteral("%1:%2").arg(serverId).arg(canonical(serverId, name));
        const auto request = ++m_nextChannelLoad;
        m_channelLoads.insert(key, request);
        const int count = m_prefsManager ? m_prefsManager->preferences().historyReplayLines : 50;
        m_messageCache->loadChannelAsync(serverId, name, count,
            [owner = QPointer<ConnectionManager>(this), serverId, name, key, request](const CachedChannel& cached) {
                if (!owner || owner->m_channelLoads.value(key) != request || !owner->m_connections.contains(serverId)) return;
                auto* channel = owner->m_connections[serverId].findChannel(name);
                if (!channel) return;
                if (!channel->topic) channel->topic = cached.topic;
                for (const auto& message : cached.messages) owner->appendMessageToChannel(serverId, name, message, true);
                owner->scheduleUiUpdate(serverId, name);
            });
    }
    m_pendingConnectionsUpdate = true;
    scheduleUiUpdate(serverId, ch.name);
    return conn.channels.last();
}

void ConnectionManager::wireClientSignals(ChatClient* client) {
    connect(client, &ChatClient::connected, this, [this](qint64 serverId) {
        if (!m_connections.contains(serverId)) return;
        ServerConnection& conn = m_connections[serverId];
        if (!conn.policy.desired || conn.policy.paused) return;
        if (auto* retry = m_reconnectTimers.value(serverId)) retry->stop();
        conn.policy.connected(m_clock.elapsed());
        conn.state = ConnectionState::CONNECTED;
        conn.encrypted = m_clients.value(serverId) && m_clients.value(serverId)->isEncrypted();
        conn.errorMessage = std::nullopt;

        // Start 120s stability timer to reset reconnectAttempt = 0
        QTimer* stab = m_stabilityTimers.value(serverId, nullptr);
        if (!stab) {
            stab = new QTimer(this);
            stab->setSingleShot(true);
            // Coarse timers may fire early, before policy.stable() accepts the reset.
            stab->setTimerType(Qt::PreciseTimer);
            connect(stab, &QTimer::timeout, this, [this, serverId]() {
                if (m_connections.contains(serverId) && m_connections[serverId].isConnected()) {
                    auto& connection = m_connections[serverId];
                    connection.policy.stable(m_clock.elapsed());
                    connection.reconnectAttempt = connection.policy.attempts;
                }
            });
            m_stabilityTimers.insert(serverId, stab);
        }
        stab->start(STABILITY_RESET_MS);

        if (m_prefsManager && m_prefsManager->preferences().soundAlertsEnabled && m_soundService) {
            m_soundService->play(SoundType::CONNECT, m_prefsManager->preferences().soundVolume);
        }
        Q_EMIT connectionsChanged();
    });

    connect(client, &ChatClient::disconnected, this, [this](qint64 serverId, const QString& reason) {
        if (!m_connections.contains(serverId)) return;
        if (m_stabilityTimers.contains(serverId)) {
            m_stabilityTimers[serverId]->stop();
        }
        ServerConnection& conn = m_connections[serverId];
        if (m_prefsManager && m_prefsManager->preferences().soundAlertsEnabled && m_soundService) {
            m_soundService->play(SoundType::DISCONNECT, m_prefsManager->preferences().soundVolume);
        }

        conn.policy.failed(this->client(serverId) ? this->client(serverId)->retryDisposition() : RetryDisposition::Transient);
        if (conn.policy.canRetry()) {
            scheduleReconnect(serverId);
        } else {
            if (!conn.policy.paused) conn.state = ConnectionState::DISCONNECTED;
            Q_UNUSED(reason)
            Q_EMIT connectionsChanged();
        }
    });

    connect(client, &ChatClient::connectionError, this, [this](qint64 serverId, const QString& err) {
        if (!m_connections.contains(serverId)) return;
        ServerConnection& conn = m_connections[serverId];
        if (auto* stable = m_stabilityTimers.value(serverId)) stable->stop();
        conn.policy.failed(this->client(serverId) ? this->client(serverId)->retryDisposition() : RetryDisposition::Transient);
        conn.errorMessage = err;
        conn.state = ConnectionState::ERROR;
        if (conn.policy.paused) if (auto* retry = m_reconnectTimers.value(serverId)) retry->stop();
        Q_EMIT connectionsChanged();
        if (conn.policy.canRetry()) scheduleReconnect(serverId);
    });

    connect(client, &ChatClient::sendError, this, [this](qint64, const QString&, const QString& error) {
        Q_EMIT operationError(error);
    });
    connect(client, &ChatClient::messageUpdated, this, [this](qint64 serverId, const QString& localId, const IncomingMessage& update) {
        if (!m_connections.contains(serverId)) return;
        for (auto& channel : m_connections[serverId].channels) {
            for (auto& message : channel.messages) if (message.id == localId) {
                message = update;
                if (m_messageCache && message.deliveryStatus != QStringLiteral("pending"))
                    m_messageCache->cacheMessage(serverId, channel.name, message);
                // A server echo can replace the local timestamp. Restore the
                // timeline invariant before the next history insertion.
                std::stable_sort(channel.messages.begin(), channel.messages.end(),
                    [](const auto& left, const auto& right) { return left.timestamp < right.timestamp; });
                scheduleUiUpdate(serverId, channel.name);
                return;
            }
        }
    });
    connect(client, &ChatClient::isupportUpdated, this, [this](qint64 serverId, const QMap<QString, QString>& tokens) {
        if (!m_connections.contains(serverId)) return;
        auto& conn = m_connections[serverId];
        const auto requested = tokens.value(QStringLiteral("CASEMAPPING"), QStringLiteral("rfc1459"));
        const auto mapping = requested == "ascii" || requested == "strict-rfc1459" ? requested : QStringLiteral("rfc1459");
        if (mapping != conn.caseMapping) {
            const auto oldMapping = conn.caseMapping;
            conn.caseMapping = mapping;
            QMap<QString, QString> redirects;
            QList<Channel> channels;
            for (const auto& channel : conn.channels) {
                auto found = std::find_if(channels.begin(), channels.end(), [&](const Channel& c) { return conn.same(c.name, channel.name); });
                if (found == channels.end()) channels.append(channel);
                else {
                    for (const auto& message : channel.messages) {
                        if (std::none_of(found->messages.begin(), found->messages.end(), [&](const IncomingMessage& m) { return m.id == message.id; })) found->messages.append(message);
                    }
                    found->unreadCount += channel.unreadCount;
                    found->hasUnreadMention |= channel.hasUnreadMention;
                }
                const auto oldKey = QStringLiteral("%1:%2").arg(serverId).arg(ircIdentity(channel.name, oldMapping));
                const auto newKey = QStringLiteral("%1:%2").arg(serverId).arg(conn.canonical(channel.name));
                redirects.insert(oldKey, newKey);
                if (oldKey != newKey) {
                    m_inputHistories[newKey].append(m_inputHistories.take(oldKey));
                    const auto activity = m_lastActiveTimes.take(oldKey);
                    for (auto it = activity.begin(); it != activity.end(); ++it) m_lastActiveTimes[newKey][conn.canonical(it.key())] = it.value();
                }
            }
            for (auto& channel : channels)
                std::stable_sort(channel.messages.begin(), channel.messages.end(),
                    [](const auto& left, const auto& right) { return left.timestamp < right.timestamp; });
            conn.channels = channels;
            if (conn.currentChannel) if (auto* channel = conn.findChannel(*conn.currentChannel)) conn.currentChannel = channel->name;
            if (m_activeServerId == serverId && conn.currentChannel) m_activeChannelName = *conn.currentChannel;
            ++m_generations[serverId];
            Q_EMIT identityKeysChanged(serverId, redirects);
            trimRetainedHistory(); Q_EMIT connectionsChanged();
        }
        if (m_messageCache) m_messageCache->setCaseMapping(serverId, mapping);
    });

    connect(client, &ChatClient::certificatePinned, this, [this](qint64 serverId, const QString& fp) {
        if (!m_connections.contains(serverId)) return;
        m_connections[serverId].config.pinnedCertFingerprint = fp;
        if (m_dbService) {
            if (!m_dbService->updateServerConfig(m_connections[serverId].config))
                Q_EMIT operationError(QStringLiteral("The certificate pin is active for this session but could not be saved."));
        }
    });

    connect(client, &ChatClient::channelJoined, this, [this](qint64 serverId, const QString& channel) {
        if (!canCreateChannel(serverId, channel)) {
            if (auto* connection = this->client(serverId)) connection->partChannel(channel, QStringLiteral("Client conversation limit"));
            Q_EMIT operationError(QStringLiteral("Channel admission limit reached.")); return;
        }
        ensureChannel(serverId, channel, ChannelType::CHANNEL);
        updateSavedChannels(serverId, channel, true);
        setActiveChannel(serverId, channel);
    });

    connect(client, &ChatClient::channelParted, this, [this](qint64 serverId, const QString& channel) {
        if (!m_connections.contains(serverId)) return;
        updateSavedChannels(serverId, channel, false);
        ServerConnection& conn = m_connections[serverId];
        for (int i = 0; i < conn.channels.size(); ++i) {
            if (same(serverId, conn.channels[i].name, channel)) {
                conn.channels.removeAt(i);
                break;
            }
        }
        if (m_activeServerId == serverId && same(serverId, m_activeChannelName, channel)) {
            const QString fallback = conn.channels.isEmpty() ? QString() : conn.channels.first().name;
            setActiveChannel(serverId, fallback);
        } else {
            Q_EMIT connectionsChanged();
        }
    });

    connect(client, &ChatClient::userListUpdated, this, [this](qint64 serverId, const QString& channel, const QList<ChannelUser>& users) {
        if (!m_connections.contains(serverId)) return;
        if (!canCreateChannel(serverId, channel)) return;
        Channel& ch = ensureChannel(serverId, channel, ChannelType::CHANNEL);
        ch.users = users;
        m_pendingConnectionsUpdate = true;
        scheduleUiUpdate(serverId, ch.name);
    });

    connect(client, &ChatClient::userJoined, this, [this](qint64 serverId, const QString& channel, const ChannelUser& user) {
        if (!m_connections.contains(serverId)) return;
        if (!canCreateChannel(serverId, channel)) return;
        Channel& ch = ensureChannel(serverId, channel, ChannelType::CHANNEL);
        bool found = false;
        for (auto& u : ch.users) {
            if (same(serverId, u.nickname, user.nickname)) {
                u = user;
                found = true;
                break;
            }
        }
        if (!found && ch.users.size() < IrcLimits::UsersPerChannel) ch.users.append(user);
        m_pendingConnectionsUpdate = true;
        scheduleUiUpdate(serverId, ch.name);
    });

    connect(client, &ChatClient::userParted, this, [this](qint64 serverId, const QString& channel, const QString& nick, const QString&) {
        if (!m_connections.contains(serverId)) return;
        if (Channel* ch = m_connections[serverId].findChannel(channel)) {
            ch->users.removeIf([&](const ChannelUser& u) {
                return same(serverId, u.nickname, nick);
            });
            m_pendingConnectionsUpdate = true;
            scheduleUiUpdate(serverId, ch->name);
        }
    });

    connect(client, &ChatClient::userQuit, this, [this](qint64 serverId, const QString& nick, const QString&) {
        if (!m_connections.contains(serverId)) return;
        for (auto& ch : m_connections[serverId].channels) {
            ch.users.removeIf([&](const ChannelUser& u) {
                return same(serverId, u.nickname, nick);
            });
        }
        m_pendingConnectionsUpdate = true;
        scheduleUiUpdate(serverId);
    });

    connect(client, &ChatClient::userKicked, this, [this](qint64 serverId, const QString& channel, const QString& kicked, const QString&, const QString&) {
        if (!m_connections.contains(serverId)) return;
        if (Channel* ch = m_connections[serverId].findChannel(channel)) {
            ch->users.removeIf([&](const ChannelUser& u) {
                return same(serverId, u.nickname, kicked);
            });
            if (this->client(serverId) && same(serverId, kicked, this->client(serverId)->getNickname())) {
                ch->users.clear();
                ch->currentTypingUsers.clear();
                const auto prefix = QStringLiteral("%1:%2:").arg(serverId).arg(canonical(serverId, ch->name));
                for (const auto& key : m_typingExpires.keys()) if (key.startsWith(prefix)) m_typingExpires.remove(key);
            }
            m_pendingConnectionsUpdate = true;
            scheduleUiUpdate(serverId, ch->name);
        }
    });

    connect(client, &ChatClient::nicknameChanged, this, [this](qint64 serverId, const QString& oldNick, const QString& newNick) {
        if (!m_connections.contains(serverId)) return;
        const auto prefix = QStringLiteral("%1:").arg(serverId);
        for (const auto& key : m_typingExpires.keys()) if (key.startsWith(prefix) &&
            canonical(serverId, key.section(':', -1)) == canonical(serverId, oldNick)) {
            const auto nextKey = key.left(key.lastIndexOf(':') + 1) + newNick;
            m_typingExpires.insert(nextKey, m_typingExpires.take(key));
        }
        for (auto& ch : m_connections[serverId].channels) {
            if (ch.isPrivateMessage() && canonical(serverId, ch.name) == canonical(serverId, oldNick)) {
                if (m_connections[serverId].findChannel(newNick) && canonical(serverId, oldNick) != canonical(serverId, newNick)) {
                    Q_EMIT operationError(QStringLiteral("Query rename conflicts with an existing conversation; both were retained."));
                    continue;
                }
                const auto oldKey = QStringLiteral("%1:%2").arg(serverId).arg(canonical(serverId, ch.name));
                const auto newKey = QStringLiteral("%1:%2").arg(serverId).arg(canonical(serverId, newNick));
                m_inputHistories.insert(newKey, m_inputHistories.take(oldKey));
                const bool loading = m_channelLoads.remove(oldKey) > 0;
                m_lastActiveTimes.insert(newKey, m_lastActiveTimes.take(oldKey));
                if (m_messageCache) m_messageCache->renameChannel(serverId, ch.name, newNick);
                ch.name = newNick;
                for (const auto& key : m_typingExpires.keys()) if (key.startsWith(oldKey + ':'))
                    m_typingExpires.insert(newKey + key.mid(oldKey.size()), m_typingExpires.take(key));
                if (loading && m_messageCache) {
                    const auto request = ++m_nextChannelLoad;
                    m_channelLoads.insert(newKey, request);
                    const int count = m_prefsManager ? m_prefsManager->preferences().historyReplayLines : 50;
                    m_messageCache->loadChannelAsync(serverId, oldNick, count,
                        [owner = QPointer<ConnectionManager>(this), serverId, newNick, newKey, request](const CachedChannel& cached) {
                            if (!owner || owner->m_channelLoads.value(newKey) != request || !owner->m_connections.contains(serverId)) return;
                            for (auto message : cached.messages) {
                                message.target = newNick;
                                owner->appendMessageToChannel(serverId, newNick, message, true);
                            }
                        });
                }
                if (m_connections[serverId].currentChannel == oldNick) m_connections[serverId].currentChannel = newNick;
                if (m_activeServerId == serverId && canonical(serverId, m_activeChannelName) == canonical(serverId, oldNick)) m_activeChannelName = newNick;
                Q_EMIT queryRenamed(serverId, oldNick, newNick);
            }
            for (auto& u : ch.users) {
                if (same(serverId, u.nickname, oldNick)) {
                    u.nickname = newNick;
                }
            }
            for (auto& nickname : ch.currentTypingUsers)
                if (canonical(serverId, nickname) == canonical(serverId, oldNick)) nickname = newNick;
        }
        m_pendingConnectionsUpdate = true;
        scheduleUiUpdate(serverId);
    });

    connect(client, &ChatClient::userModeChanged, this, [this](qint64 serverId, const QString& channel, const QString& nick, const QSet<UserMode>& modes) {
        if (!m_connections.contains(serverId)) return;
        if (Channel* ch = m_connections[serverId].findChannel(channel)) {
            for (auto& u : ch->users) {
                if (same(serverId, u.nickname, nick)) {
                    u.modes = modes;
                    break;
                }
            }
            std::sort(ch->users.begin(), ch->users.end(), [](const ChannelUser& a, const ChannelUser& b) {
                const int pa = userModePriority(a.primaryMode());
                const int pb = userModePriority(b.primaryMode());
                if (pa != pb) return pa < pb;
                return a.nickname.compare(b.nickname, Qt::CaseInsensitive) < 0;
            });
            m_pendingConnectionsUpdate = true;
            scheduleUiUpdate(serverId, ch->name);
        }
    });

    connect(client, &ChatClient::topicChanged, this, [this](qint64 serverId, const QString& channel, const QString& topic, const QString& setBy) {
        if (!m_connections.contains(serverId)) return;
        // Topic replies update existing conversations; they cannot admit a new one.
        Channel* existing = m_connections[serverId].findChannel(channel);
        if (!existing || !existing->isChannel()) return;
        Channel& ch = *existing;
        ch.topic = topic;
        ch.topicSetBy = setBy.isEmpty() ? std::nullopt : std::make_optional(setBy);
        ch.topicSetTime.reset();
        if (m_messageCache) {
            m_messageCache->updateTopic(serverId, channel, topic);
        }
        m_pendingConnectionsUpdate = true;
        scheduleUiUpdate(serverId, ch.name);
    });

    connect(client, &ChatClient::latencyUpdated, this, [this](qint64 serverId, qint64 latencyMs) {
        if (!m_connections.contains(serverId)) return;
        m_connections[serverId].latencyMs = latencyMs;
        m_pendingConnectionsUpdate = true;
        scheduleUiUpdate(serverId);
    });

    connect(client, &ChatClient::whoisUpdated, this, &ConnectionManager::whoisInfoUpdated);
    connect(client, &ChatClient::channelListUpdated, this, &ConnectionManager::channelListReceived);

    connect(client, &ChatClient::typingUserUpdated, this, [this](qint64 serverId, const QString& target, const QString& nickname, const QString& state) {
        if (!m_connections.contains(serverId)) return;
        Channel* ch = m_connections[serverId].findChannel(target);
        if (!ch) return;

        if (state != QStringLiteral("active") && state != QStringLiteral("paused") && state != QStringLiteral("done")) return;
        clearTypingUser(serverId, *ch, nickname);
        if (isIgnored(serverId, nickname)) { scheduleUiUpdate(serverId, ch->name); return; }
        if (state != QStringLiteral("done") && ch->currentTypingUsers.size() < 32) {
            ch->currentTypingUsers.append(nickname);
            const auto expiryKey = QStringLiteral("%1:%2:%3").arg(serverId).arg(canonical(serverId, ch->name), nickname);
            m_typingExpires.insert(expiryKey, QDeadlineTimer(6000));
        }
        scheduleUiUpdate(serverId, ch->name);
    });

    connect(client, &ChatClient::reactionReceived, this, [this](qint64 serverId, const QString& target, const QString& messageId, const QString& sender, const QString& emoji) {
        if (!m_connections.contains(serverId)) return;
        if (isIgnored(serverId, sender)) return;
        if (Channel* ch = m_connections[serverId].findChannel(target)) {
            for (auto& msg : ch->messages) {
                if (msg.id == messageId) {
                    if (emoji.size() > 32 || (!msg.reactions.contains(emoji) && msg.reactions.size() >= 32) ||
                        msg.reactions.value(emoji).size() >= 64) return;
                    const auto& reactors = msg.reactions[emoji];
                    if (std::any_of(reactors.begin(), reactors.end(), [&](const QString& value) { return same(serverId, value, sender); })) return;
                    msg.reactions[emoji].insert(sender);
                    if (m_messageCache) m_messageCache->cacheMessage(serverId, ch->name, msg);
                    scheduleUiUpdate(serverId, ch->name);
                    break;
                }
            }
        }
    });

    connect(client, &ChatClient::messageReceived, this, [this](qint64 serverId, const IncomingMessage& message, bool isHistoryReplay) {
        appendMessageToChannel(serverId, message.target, message, isHistoryReplay);
    });
}

void ConnectionManager::appendMessageToChannel(qint64 serverId, const QString& channelName, IncomingMessage message, bool isHistoryReplay) {
    if (!m_connections.contains(serverId)) return;
    if (message.isSelf && ircSecrets::message(channelName, message.content)) return;
    if (!canCreateChannel(serverId, channelName)) { Q_EMIT operationError(QStringLiteral("Conversation admission limit reached.")); return; }

    const UserPreferences prefs = m_prefsManager ? m_prefsManager->preferences() : UserPreferences{};
    if (!message.isSelf && isIgnored(serverId, message.sender)) {
        return;
    }

    const QString myNick = m_clients.contains(serverId) ? m_clients[serverId]->getNickname() : m_connections[serverId].config.nickname;
    if (!message.isSelf && (message.type == MessageType::NORMAL || message.type == MessageType::ACTION || message.type == MessageType::NOTICE)) {
        message.isHighlight = HighlightMatcher::containsHighlight(message.content, myNick, prefs.highlightWords, m_connections[serverId].caseMapping);
    }

    ChannelType defaultType = ChannelType::CHANNEL;
    if (channelName.startsWith(QStringLiteral("* "))) {
        defaultType = ChannelType::SERVER;
    } else if (!channelName.startsWith('#') && !channelName.startsWith('&') && !channelName.startsWith('+') && !channelName.startsWith('!')) {
        defaultType = ChannelType::QUERY;
    }

    Channel& ch = ensureChannel(serverId, channelName, defaultType);

    // Old messages must not dismiss an indicator for text being typed now.
    if (!isHistoryReplay && !message.sender.isEmpty()) {
        clearTypingUser(serverId, ch, message.sender);
    }

    // Deduplicate by message ID (for IRCv3 chathistory / local cache overlap)
    if (!message.id.isEmpty()) {
        for (const auto& existing : ch.messages) {
            if (existing.id == message.id) {
                return;
            }
        }
    }

    // Resolve IRCv3 @+reply=<msgid> against existing messages in the channel if sender/preview not populated
    if (message.replyToMessageId.has_value() && !message.replyToSender.has_value()) {
        const QString targetReplyId = *message.replyToMessageId;
        for (int i = ch.messages.size() - 1; i >= 0; --i) {
            if (ch.messages[i].id == targetReplyId) {
                message.replyToSender = ch.messages[i].sender;
                message.replyToPreview = ch.messages[i].content.left(80);
                break;
            }
        }
    }

    // Track last active speaking time for SMART_HIDE
    bool sidebarBadgeChanged = false;
    if (!isHistoryReplay && (message.type == MessageType::NORMAL || message.type == MessageType::ACTION)) {
        const QString chanKey = QStringLiteral("%1:%2").arg(serverId).arg(canonical(serverId, ch.name));
        m_lastActiveTimes[chanKey].insert(canonical(serverId, message.sender), message.timestamp);
        while (m_lastActiveTimes[chanKey].size() > 2048) m_lastActiveTimes[chanKey].erase(m_lastActiveTimes[chanKey].begin());
        ch.lastActivityTimestamp = message.timestamp;
        if (!isHistoryReplay && !ch.hasRecentActivity) {
            ch.hasRecentActivity = true;
            sidebarBadgeChanged = true;
        }
    }

    const bool isActiveView = (m_activeServerId == serverId && same(serverId, m_activeChannelName, ch.name));
    if (!isHistoryReplay && !isActiveView && !message.isSelf &&
        (message.type == MessageType::NORMAL || message.type == MessageType::ACTION || message.type == MessageType::NOTICE)) {
        ++ch.unreadCount;
        if (message.isHighlight || ch.isPrivateMessage()) {
            ch.hasUnreadMention = true;
        }
        sidebarBadgeChanged = true;
    }

    // Live server-time values need not be monotonic either. Keep all insertion
    // paths sorted; upper_bound preserves arrival order for equal timestamps.
    const auto position = std::upper_bound(ch.messages.begin(), ch.messages.end(), message.timestamp,
        [](qint64 timestamp, const IncomingMessage& existing) { return timestamp < existing.timestamp; });
    ch.messages.insert(position, message);
    if (ch.messages.size() > MessageCache::MAX_MESSAGES_PER_CHANNEL) {
        ch.messages.removeFirst();
    }

    if (m_messageCache && ch.type != ChannelType::SERVER && message.deliveryStatus != QStringLiteral("pending")) {
        m_messageCache->cacheMessage(serverId, ch.name, message);
    }

    if (!isHistoryReplay && prefs.loggingEnabled && m_textLogService && ch.type != ChannelType::SERVER) {
        const QString srvName = m_connections[serverId].config.serverName.isEmpty()
            ? m_connections[serverId].config.hostname
            : m_connections[serverId].config.serverName;
        m_textLogService->logMessage(srvName, canonical(serverId, ch.name), message, serverId);
    }

    // Notifications & Sound Alerts (suppressed during IRCv3 chathistory replay)
    if (!isHistoryReplay && !message.isSelf && (message.type == MessageType::NORMAL || message.type == MessageType::ACTION)) {
        const bool isPm = ch.isPrivateMessage();
        const bool isMention = HighlightMatcher::containsNickMention(message.content, myNick, m_connections[serverId].caseMapping);
        const bool isCustomHighlight = message.isHighlight && !isMention;

        if (prefs.soundAlertsEnabled && m_soundService) {
            if (isPm && prefs.soundOnPrivateMessage) {
                m_soundService->play(SoundType::PRIVATE_MESSAGE, prefs.soundVolume);
            } else if (isMention && prefs.soundOnMention) {
                m_soundService->play(SoundType::MENTION, prefs.soundVolume);
            } else if (isCustomHighlight && prefs.soundOnHighlight) {
                m_soundService->play(SoundType::HIGHLIGHT, prefs.soundVolume);
            }
        }

        if (prefs.notificationsEnabled) {
            if ((isPm && prefs.notifyOnPrivateMessage) ||
                (isMention && prefs.notifyOnMention) ||
                (isCustomHighlight && prefs.notifyOnHighlight)) {
                Q_EMIT desktopNotificationRequested(
                    QStringLiteral("%1 (%2)").arg(message.sender, ch.name),
                    IrcFormatParser::stripFormatting(message.content)
                );
            }
        }
    }

    // Async URL Previews
    if (prefs.urlImageDisplayMode != UrlImageDisplayMode::DISABLED &&
        (message.type == MessageType::NORMAL || message.type == MessageType::ACTION)) {
        const bool trusted = message.isSelf ||
                             prefs.processLinkPreviewsFromOthers ||
                             std::any_of(prefs.trustedPreviewUsers.begin(), prefs.trustedPreviewUsers.end(), [&](const QString& value) { return same(serverId, value, message.sender); });
        if (trusted) {
            const QStringList urls = LinkPreviewService::extractUrls(message.content);
            const ServerConfig srvCfg = m_connections[serverId].config;
            const QString msgId = message.id;
            const QString targetChan = ch.name;
            for (const QString& url : urls.mid(0, 3)) {
                m_linkPreviewService.fetchPreview(
                    url,
                    srvCfg,
                    prefs.useProxyForPreviews,
                    [this, serverId, targetChan, msgId](const UrlPreview& preview) {
                        if (preview.failed || !m_connections.contains(serverId)) return;
                        if (Channel* targetCh = m_connections[serverId].findChannel(targetChan)) {
                            for (auto& m : targetCh->messages) {
                                if (m.id == msgId) {
                                    m.urlPreviews.append(preview);
                                    scheduleUiUpdate(serverId, targetCh->name);
                                    break;
                                }
                            }
                        }
                    }
                );
            }
        }
    }

    if (sidebarBadgeChanged) {
        m_pendingConnectionsUpdate = true;
    }
    scheduleUiUpdate(serverId, ch.name);
    trimRetainedHistory();
}

bool ConnectionManager::canCreateChannel(qint64 serverId, const QString& name) const {
    if (!m_connections.contains(serverId)) return false;
    if (m_connections[serverId].findChannel(name)) return true;
    int channels = 0;
    for (const auto& connection : m_connections) channels += connection.channels.size();
    return channels < MAX_CHANNELS;
}
void ConnectionManager::trimRetainedHistory() {
    int count = 0;
    qint64 bytes = 0;
    for (const auto& connection : m_connections) for (const auto& channel : connection.channels) {
        count += channel.messages.size();
        for (const auto& message : channel.messages) bytes += (message.content.size() + message.sender.size()) * 2;
    }
    while (count > MAX_RETAINED_MESSAGES || bytes > MAX_RETAINED_TEXT_BYTES) {
        Channel* oldest = nullptr;
        qint64 ownerId = 0;
        for (auto& connection : m_connections) for (auto& channel : connection.channels) {
            if (!channel.messages.isEmpty() && channel.messages.first().deliveryStatus != QStringLiteral("pending") &&
                (!oldest || channel.messages.first().timestamp < oldest->messages.first().timestamp)) {
                oldest = &channel; ownerId = connection.serverId;
            }
        }
        if (!oldest) break;
        const auto message = oldest->messages.takeFirst();
        bytes -= (message.content.size() + message.sender.size()) * 2;
        --count;
        scheduleUiUpdate(ownerId, oldest->name);
    }
    qint64 historyBytes = 0;
    const int historyLimit = std::clamp(m_prefsManager ? m_prefsManager->preferences().inputHistorySize : 50, 0, 500);
    for (auto& history : m_inputHistories) {
        while (history.size() > historyLimit) history.removeFirst();
        qint64 channelBytes = 0;
        for (const auto& input : history) channelBytes += input.size() * 2;
        while (channelBytes > 64 * 1024 && !history.isEmpty()) channelBytes -= history.takeFirst().size() * 2;
        historyBytes += channelBytes;
    }
    for (auto& history : m_inputHistories) while (historyBytes > 2 * 1024 * 1024 && !history.isEmpty())
        historyBytes -= history.takeFirst().size() * 2;
}

void ConnectionManager::scheduleReconnect(qint64 serverId) {
    if (!m_connections.contains(serverId)) return;
    if (m_reconnectTimers.contains(serverId) && m_reconnectTimers[serverId]->isActive()) return;
    ServerConnection& conn = m_connections[serverId];
    if (!conn.policy.canRetry()) return;
    const int delayMs = reconnectDelayMsForAttempt(conn.policy.attempts, QRandomGenerator::global()->generateDouble());
    conn.policy.attempts = std::min(30, conn.policy.attempts + 1);
    conn.reconnectAttempt = conn.policy.attempts;
    conn.state = ConnectionState::RECONNECTING;

    const QString srvTab = QStringLiteral("* ") + (conn.config.serverName.isEmpty() ? conn.config.hostname : conn.config.serverName);
    IncomingMessage retryMsg;
    retryMsg.target = srvTab;
    retryMsg.sender = QStringLiteral("System");
    retryMsg.content = QStringLiteral("Connection lost. Reconnecting in %1 seconds (attempt %2)...")
                           .arg(delayMs / 1000)
                           .arg(conn.reconnectAttempt);
    retryMsg.type = MessageType::SERVER;
    appendMessageToChannel(serverId, srvTab, retryMsg);

    QTimer* timer = m_reconnectTimers.value(serverId, nullptr);
    if (!timer) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        m_reconnectTimers.insert(serverId, timer);
    }
    timer->disconnect(this);
    const auto generation = m_generations.value(serverId);
    connect(timer, &QTimer::timeout, this, [this, serverId, generation]() {
        if (m_generations.value(serverId) == generation && m_connections.contains(serverId) && m_connections[serverId].policy.canRetry()) connectServer(serverId);
    });
    timer->start(delayMs);
    Q_EMIT connectionsChanged();
}

void ConnectionManager::scheduleUiUpdate(qint64 serverId, const QString& channelName) {
    if (!channelName.isEmpty()) {
        m_pendingChannelUpdates.insert({serverId, channelName});
    }
    if (!m_uiThrottleTimer.isActive()) {
        m_uiThrottleTimer.start();
    }
}

bool ConnectionManager::sendUserInput(qint64 serverId, const QString& channelName, const QString& rawInput,
    const std::optional<IncomingMessage>& replyContext) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (rawInput.trimmed().isEmpty() || rawInput.toUtf8().size() > 64 * 1024 || !m_connections.contains(serverId)) return false;
    const auto parsed = IrcCommandParser::isCommand(rawInput) ? IrcCommandParser::parse(rawInput) : std::nullopt;
    const bool local = parsed && std::visit([](const auto& command) {
        using Type = std::decay_t<decltype(command)>;
        return std::is_same_v<Type, cmd::Help> || std::is_same_v<Type, cmd::Clear> ||
            std::is_same_v<Type, cmd::ClearAll> || std::is_same_v<Type, cmd::Close> ||
            std::is_same_v<Type, cmd::Ignore> || std::is_same_v<Type, cmd::Unignore> ||
            std::is_same_v<Type, cmd::IgnoreList> || std::is_same_v<Type, cmd::Servlist> ||
            std::is_same_v<Type, cmd::Connect> || std::is_same_v<Type, cmd::Reconnect> ||
            std::is_same_v<Type, cmd::Quit>;
    }, *parsed);
    if (!local && (!client(serverId) || !client(serverId)->isConnected())) {
        Q_EMIT operationError(QStringLiteral("Not connected. Your draft was retained."));
        return false;
    }
    bool accepted = false;
    if (parsed) accepted = executeParsedCommand(serverId, channelName, *parsed);
    else {
        QString text = rawInput;
        if (text.startsWith(QStringLiteral("//"))) text.remove(0, 1);
        accepted = sendChatPipeline(serverId, channelName, text, replyContext, rawInput);
    }
    if (!accepted) return false;
    if (ircSecrets::input(channelName, rawInput)) return true;
    const QString key = QStringLiteral("%1:%2").arg(serverId).arg(canonical(serverId, channelName));
    auto& history = m_inputHistories[key];
    if (history.isEmpty() || history.last() != rawInput) history.append(rawInput);
    const int maximum = std::clamp(m_prefsManager ? m_prefsManager->preferences().inputHistorySize : 50, 0, 500);
    while (history.size() > maximum) history.removeFirst();
    trimRetainedHistory();
    return true;
}
void ConnectionManager::deferInputPublication(quint64 ticket, std::function<void()> publish) {
    auto pending = std::find_if(m_pendingInputPublications.begin(), m_pendingInputPublications.end(),
        [ticket](const auto& input) { return input.ticket == ticket; });
    if (pending == m_pendingInputPublications.end()) return;
    pending->publish = std::move(publish);
    drainInputPublications();
}

void ConnectionManager::drainInputPublications() {
    if (m_drainingInputPublications) return;
    m_drainingInputPublications = true;
    const QPointer<ConnectionManager> owner(this);
    const auto restore = qScopeGuard([owner]() {
        if (owner) owner->m_drainingInputPublications = false;
    });
    while (owner) {
        bool progressed = false;
        for (qsizetype index = 0; index < m_pendingInputPublications.size(); ++index) {
            const auto& candidate = m_pendingInputPublications[index];
            if (!candidate.publish) continue;
            bool blocked = false;
            for (qsizetype earlier = 0; earlier < index; ++earlier) {
                const auto& previous = m_pendingInputPublications[earlier];
                if (previous.serverId == candidate.serverId && previous.generation == candidate.generation &&
                    same(candidate.serverId, previous.target, candidate.target)) {
                    blocked = true;
                    break;
                }
            }
            if (blocked) continue;
            // No iterators or references survive a publication: it may synchronously
            // enqueue another input, close a conversation, or destroy this owner.
            auto publish = std::move(m_pendingInputPublications[index].publish);
            m_pendingInputPublications.removeAt(index);
            publish();
            progressed = true;
            break;
        }
        if (!progressed) break;
    }
}

bool ConnectionManager::sendChatPipeline(qint64 serverId, const QString& target, const QString& text,
    const std::optional<IncomingMessage>& replyContext, const QString& draft) {
    if (!client(serverId) || !client(serverId)->isConnected() || m_inFlightInputs >= 32) return false;
    // Never shorten or auto-upload credential-bearing service messages.
    if (ircSecrets::message(target, text)) return client(serverId)->sendMessage(target, text, replyContext);
    const UserPreferences prefs = m_prefsManager ? m_prefsManager->preferences() : UserPreferences{};
    const QPointer<ConnectionManager> owner(this);
    const QPointer<ChatClient> connection(client(serverId));
    const auto generation = m_generations.value(serverId);
    auto accepted = std::make_shared<bool>(true);
    auto finished = std::make_shared<bool>(false);
    ++m_inFlightInputs;
    const auto ticket = ++m_nextInputTicket;
    m_pendingInputPublications.append({ticket, serverId, generation, target, {}});
    auto finish = [owner, accepted, finished, ticket, serverId, target, draft, replyContext](bool success, const QString& error) {
        if (*finished) return;
        *finished = true;
        *accepted = success;
        if (!owner) return;
        --owner->m_inFlightInputs;
        owner->m_pendingInputPublications.removeIf([ticket](const auto& input) { return input.ticket == ticket; });
        if (!success) Q_EMIT owner->inputFailed(serverId, target, draft, replyContext, error);
        if (owner) owner->drainInputPublications();
    };
    auto current = [owner, connection, serverId, generation, target]() {
        return owner && connection && owner->client(serverId) == connection &&
            owner->m_generations.value(serverId) == generation && connection->isConnected() &&
            owner->m_connections.contains(serverId) && owner->m_connections[serverId].findChannel(target);
    };
    auto afterShorten = [owner, connection, current, target, prefs, replyContext, finish, ticket](const QString& processedText) {
        if (!current()) { finish(false, QStringLiteral("The connection changed; the message was not submitted.")); return; }
        const int lineCount = processedText.count('\n') + 1;
        if (prefs.pastebinEnabled && (processedText.length() > prefs.pastebinThreshold || lineCount > 5)) {
            if (!connection->allowsPublicPaste(target)) {
                finish(false, QStringLiteral("Public paste upload is disabled for private conversations or unknown channel privacy; your draft was kept.")); return;
            }
            owner->m_pastebinService.upload(processedText,
                [owner, connection, current, target, finish, ticket, lineCount, length = processedText.length()](bool success, const QString& result) {
                    if (!current()) { finish(false, QStringLiteral("The connection changed during upload.")); return; }
                    if (!success) { finish(false, result); return; }
                    owner->deferInputPublication(ticket, [connection, current, target, finish, result, lineCount, length]() {
                        if (!current()) { finish(false, QStringLiteral("The connection changed before submission.")); return; }
                        const bool sent = connection->sendMessage(target, QStringLiteral("[Pastebin] %1 (%2 lines, %3 chars)").arg(result).arg(lineCount).arg(length));
                        finish(sent, sent ? QString() : QStringLiteral("The uploaded message could not be submitted."));
                    });
                });
            return;
        }
        const auto lines = processedText.split('\n', Qt::SkipEmptyParts);
        QStringList outbound;
        for (int index = 0; index < lines.size(); ++index) {
            auto line = lines[index];
            if (index == 0 && replyContext && !connection->enabledCapabilities().contains(QStringLiteral("message-tags")))
                line = QStringLiteral("@%1: %2").arg(replyContext->sender, line);
            // The transport splits UTF-8 using the visible server prefix and a bounded
            // queue. Keep the whole input preflight here, not the old single-line cap.
            if (line.size() > 8192 || line.contains('\r') || line.contains(QChar(0))) {
                finish(false, QStringLiteral("A message line exceeds the local limit or contains invalid controls."));
                return;
            }
            outbound.append(line);
        }
        owner->deferInputPublication(ticket, [connection, current, target, outbound, replyContext, finish]() {
            if (!current()) { finish(false, QStringLiteral("The connection changed before submission.")); return; }
            if (!connection->sendMessages(target, outbound, replyContext)) {
                finish(false, QStringLiteral("The connection rejected the message; your draft was kept."));
                return;
            }
            finish(true, {});
        });
    };
    if (prefs.urlShorteningEnabled) m_urlShortenerService.processMessageUrls(text, prefs.urlShorteningThreshold, afterShorten);
    else afterShorten(text);
    return *accepted;
}

void ConnectionManager::sendTypingNotification(qint64 serverId, const QString& target, const QString& state) {
    if (!m_clients.contains(serverId) || target.startsWith(QStringLiteral("* "))) return;
    m_clients[serverId]->sendTypingNotification(target, state);
}

void ConnectionManager::sendReaction(qint64 serverId, const QString& target, const QString& messageId, const QString& emoji) {
    if (!m_clients.contains(serverId) || target.startsWith(QStringLiteral("* "))) return;
    m_clients[serverId]->sendReaction(target, messageId, emoji);
}

bool ConnectionManager::executeParsedCommand(qint64 serverId, const QString& currentChannel, const IrcCommand& command) {
    if (!m_clients.contains(serverId)) return false;
    ChatClient* client = m_clients[serverId];
    bool success = true;
    const QPointer<ChatClient> connection(client);
    const QPointer<ConnectionManager> owner(this);
    const auto generation = m_generations.value(serverId);

    auto postSystemMsg = [owner, serverId, currentChannel](const QString& content, MessageType type = MessageType::SERVER) {
        if (!owner) return;
        IncomingMessage m;
        m.target = currentChannel;
        m.sender = QStringLiteral("System");
        m.content = content;
        m.type = type;
        owner->appendMessageToChannel(serverId, currentChannel, m);
    };

    std::visit([&](const auto& cmd) {
        using T = std::decay_t<decltype(cmd)>;
        if constexpr (std::is_same_v<T, cmd::Join>) {
            joinChannel(serverId, cmd.channel, cmd.key.value_or(QString()));
        } else if constexpr (std::is_same_v<T, cmd::Part>) {
            const QString ch = cmd.channel.value_or(currentChannel);
            partChannel(serverId, ch, cmd.reason.value_or(QString()));
        } else if constexpr (std::is_same_v<T, cmd::Close>) {
            closeChannelOrQuery(serverId, cmd.target.value_or(currentChannel));
        } else if constexpr (std::is_same_v<T, cmd::Quit>) {
            disconnectServer(serverId, cmd.reason.value_or(QStringLiteral("Client Quit")));
        } else if constexpr (std::is_same_v<T, cmd::Message>) {
            if (!cmd.target.startsWith('#') && !cmd.target.startsWith('&')) {
                ensureChannel(serverId, cmd.target, ChannelType::QUERY);
            }
            success = client->sendMessage(cmd.target, cmd.message);
        } else if constexpr (std::is_same_v<T, cmd::Query>) {
            success = openQuery(serverId, cmd.nickname, cmd.message.value_or(QString()));
        } else if constexpr (std::is_same_v<T, cmd::Action>) {
            success = client->sendAction(currentChannel, cmd.action);
        } else if constexpr (std::is_same_v<T, cmd::Notice>) {
            success = client->sendNotice(cmd.target, cmd.message);
        } else if constexpr (std::is_same_v<T, cmd::Nick>) {
            client->changeNickname(cmd.newNickname);
        } else if constexpr (std::is_same_v<T, cmd::Whois>) {
            if (cmd.detailed) {
                success = client->sendRawCommand(QStringLiteral("WHOIS %1 %1").arg(cmd.nickname));
            } else {
                success = client->sendRawCommand(QStringLiteral("WHOIS %1").arg(cmd.nickname));
            }
        } else if constexpr (std::is_same_v<T, cmd::Whowas>) {
            success = client->sendRawCommand(QStringLiteral("WHOWAS %1").arg(cmd.nickname));
        } else if constexpr (std::is_same_v<T, cmd::Who>) {
            success = client->sendRawCommand(QStringLiteral("WHO %1").arg(cmd.mask));
        } else if constexpr (std::is_same_v<T, cmd::Topic>) {
            const QString ch = cmd.channel.value_or(currentChannel);
            if (cmd.newTopic.has_value()) {
                success = client->sendRawCommand(QStringLiteral("TOPIC %1 :%2").arg(ch, *cmd.newTopic));
            } else {
                success = client->sendRawCommand(QStringLiteral("TOPIC %1").arg(ch));
            }
        } else if constexpr (std::is_same_v<T, cmd::Kick>) {
            if (cmd.reason.has_value()) {
                success = client->sendRawCommand(QStringLiteral("KICK %1 %2 :%3").arg(currentChannel, cmd.nickname, *cmd.reason));
            } else {
                success = client->sendRawCommand(QStringLiteral("KICK %1 %2").arg(currentChannel, cmd.nickname));
            }
        } else if constexpr (std::is_same_v<T, cmd::Ban>) {
            QString mask = cmd.maskOrNick.contains('!') ? cmd.maskOrNick : QStringLiteral("%1!*@*").arg(cmd.maskOrNick);
            success = client->sendRawCommand(QStringLiteral("MODE %1 +b %2").arg(currentChannel, mask));
        } else if constexpr (std::is_same_v<T, cmd::Unban>) {
            QString mask = cmd.maskOrNick.contains('!') ? cmd.maskOrNick : QStringLiteral("%1!*@*").arg(cmd.maskOrNick);
            success = client->sendRawCommand(QStringLiteral("MODE %1 -b %2").arg(currentChannel, mask));
        } else if constexpr (std::is_same_v<T, cmd::Kickban>) {
            QString mask = cmd.nickname.contains('!') ? cmd.nickname : QStringLiteral("%1!*@*").arg(cmd.nickname);
            success = client->sendRawCommand(QStringLiteral("MODE %1 +b %2").arg(currentChannel, mask));
            if (cmd.reason.has_value()) {
                success = client->sendRawCommand(QStringLiteral("KICK %1 %2 :%3").arg(currentChannel, cmd.nickname, *cmd.reason));
            } else {
                success = client->sendRawCommand(QStringLiteral("KICK %1 %2").arg(currentChannel, cmd.nickname));
            }
        } else if constexpr (std::is_same_v<T, cmd::Voice>) {
            success = client->sendRawCommand(QStringLiteral("MODE %1 +v %2").arg(currentChannel, cmd.nickname));
        } else if constexpr (std::is_same_v<T, cmd::Devoice>) {
            success = client->sendRawCommand(QStringLiteral("MODE %1 -v %2").arg(currentChannel, cmd.nickname));
        } else if constexpr (std::is_same_v<T, cmd::Op>) {
            success = client->sendRawCommand(QStringLiteral("MODE %1 +o %2").arg(currentChannel, cmd.nickname));
        } else if constexpr (std::is_same_v<T, cmd::Deop>) {
            success = client->sendRawCommand(QStringLiteral("MODE %1 -o %2").arg(currentChannel, cmd.nickname));
        } else if constexpr (std::is_same_v<T, cmd::Halfop>) {
            success = client->sendRawCommand(QStringLiteral("MODE %1 +h %2").arg(currentChannel, cmd.nickname));
        } else if constexpr (std::is_same_v<T, cmd::Dehalfop>) {
            success = client->sendRawCommand(QStringLiteral("MODE %1 -h %2").arg(currentChannel, cmd.nickname));
        } else if constexpr (std::is_same_v<T, cmd::Quiet>) {
            success = client->sendRawCommand(QStringLiteral("MODE %1 +q %2").arg(currentChannel, cmd.maskOrNick));
        } else if constexpr (std::is_same_v<T, cmd::Unquiet>) {
            success = client->sendRawCommand(QStringLiteral("MODE %1 -q %2").arg(currentChannel, cmd.maskOrNick));
        } else if constexpr (std::is_same_v<T, cmd::Mode>) {
            const QString target = cmd.target.value_or(currentChannel);
            success = client->sendRawCommand(QStringLiteral("MODE %1 %2").arg(target, cmd.modes));
        } else if constexpr (std::is_same_v<T, cmd::Invite>) {
            const QString ch = cmd.channel.value_or(currentChannel);
            success = client->sendRawCommand(QStringLiteral("INVITE %1 %2").arg(cmd.nickname, ch));
        } else if constexpr (std::is_same_v<T, cmd::Away>) {
            if (cmd.message.has_value()) {
                success = client->sendRawCommand(QStringLiteral("AWAY :%1").arg(*cmd.message));
            } else {
                success = client->sendRawCommand(QStringLiteral("AWAY"));
            }
        } else if constexpr (std::is_same_v<T, cmd::Back>) {
            success = client->sendRawCommand(QStringLiteral("AWAY"));
        } else if constexpr (std::is_same_v<T, cmd::Raw>) {
            success = client->sendRawCommand(cmd.command);
        } else if constexpr (std::is_same_v<T, cmd::List>) {
            if (cmd.filter.has_value()) {
                success = client->sendRawCommand(QStringLiteral("LIST %1").arg(*cmd.filter));
            } else {
                success = client->sendRawCommand(QStringLiteral("LIST"));
            }
            if (success) Q_EMIT openChannelListDialogRequested(serverId, cmd.filter.value_or(QString()));
        } else if constexpr (std::is_same_v<T, cmd::Names>) {
            success = client->sendRawCommand(QStringLiteral("NAMES %1").arg(cmd.channel.value_or(currentChannel)));
        } else if constexpr (std::is_same_v<T, cmd::Ignore>) {
            if (m_prefsManager) {
                success = m_prefsManager->addIgnoredUser(cmd.nickname, m_connections[serverId].caseMapping);
                if (success) postSystemMsg(QStringLiteral("Added %1 to ignore list.").arg(cmd.nickname));
            }
        } else if constexpr (std::is_same_v<T, cmd::Unignore>) {
            if (m_prefsManager) {
                success = m_prefsManager->removeIgnoredUser(cmd.nickname, m_connections[serverId].caseMapping);
                if (success) postSystemMsg(QStringLiteral("Removed %1 from ignore list.").arg(cmd.nickname));
            }
        } else if constexpr (std::is_same_v<T, cmd::IgnoreList>) {
            if (m_prefsManager) {
                const QStringList list = m_prefsManager->preferences().ignoredUsers.values();
                postSystemMsg(list.isEmpty()
                    ? QStringLiteral("Ignore list is empty.")
                    : QStringLiteral("Ignored users: %1").arg(list.join(QStringLiteral(", "))));
            }
        } else if constexpr (std::is_same_v<T, cmd::Clear>) {
            clearChannelMessages(serverId, currentChannel);
        } else if constexpr (std::is_same_v<T, cmd::ClearAll>) {
            clearAllServerMessages(serverId);
        } else if constexpr (std::is_same_v<T, cmd::Cycle>) {
            client->partChannel(currentChannel, QStringLiteral("Cycling"));
            QTimer::singleShot(300, client, [owner, connection, serverId, generation, currentChannel]() {
                if (owner && connection && owner->m_generations.value(serverId) == generation && connection->isConnected())
                    connection->joinChannel(currentChannel);
            });
        } else if constexpr (std::is_same_v<T, cmd::Ping>) {
            success = client->sendRawCommand(QStringLiteral("PING :%1").arg(QDateTime::currentMSecsSinceEpoch()));
        } else if constexpr (std::is_same_v<T, cmd::Time>) {
            success = client->sendRawCommand(QStringLiteral("TIME"));
        } else if constexpr (std::is_same_v<T, cmd::Version>) {
            if (cmd.target.has_value()) {
                success = client->sendRawCommand(QStringLiteral("PRIVMSG %1 :\x01VERSION\x01").arg(*cmd.target));
            } else {
                success = client->sendRawCommand(QStringLiteral("VERSION"));
            }
        } else if constexpr (std::is_same_v<T, cmd::Motd>) {
            success = client->sendRawCommand(QStringLiteral("MOTD"));
        } else if constexpr (std::is_same_v<T, cmd::Lusers>) {
            success = client->sendRawCommand(QStringLiteral("LUSERS"));
        } else if constexpr (std::is_same_v<T, cmd::Links>) {
            success = client->sendRawCommand(QStringLiteral("LINKS"));
        } else if constexpr (std::is_same_v<T, cmd::Stats>) {
            success = client->sendRawCommand(cmd.query ? QStringLiteral("STATS %1").arg(*cmd.query) : QStringLiteral("STATS"));
        } else if constexpr (std::is_same_v<T, cmd::Trace>) {
            success = client->sendRawCommand(cmd.target ? QStringLiteral("TRACE %1").arg(*cmd.target) : QStringLiteral("TRACE"));
        } else if constexpr (std::is_same_v<T, cmd::Admin>) {
            success = client->sendRawCommand(QStringLiteral("ADMIN"));
        } else if constexpr (std::is_same_v<T, cmd::Info>) {
            success = client->sendRawCommand(QStringLiteral("INFO"));
        } else if constexpr (std::is_same_v<T, cmd::Servlist>) {
            QStringList srvNames;
            for (const auto& c : m_connections) {
                srvNames << QStringLiteral("%1 (%2)").arg(c.config.serverName, connectionStateToString(c.state));
            }
            postSystemMsg(QStringLiteral("Servers: %1").arg(srvNames.join(QStringLiteral(", "))));
        } else if constexpr (std::is_same_v<T, cmd::Connect>) {
            ServerConfig cfg;
            cfg.serverName = cmd.hostname;
            cfg.hostname = cmd.hostname;
            cfg.port = cmd.port;
            cfg.useSsl = cmd.useSsl;
            cfg.nickname = client->getNickname();
            success = addServer(cfg, true) > 0;
        } else if constexpr (std::is_same_v<T, cmd::Reconnect>) {
            reconnectServer(serverId);
        } else if constexpr (std::is_same_v<T, cmd::ServiceMessage>) {
            success = client->sendMessage(cmd.service, cmd.command);
        } else if constexpr (std::is_same_v<T, cmd::Slap>) {
            success = client->sendAction(currentChannel, QStringLiteral("slaps %1 around a bit with a large trout").arg(cmd.nickname));
        } else if constexpr (std::is_same_v<T, cmd::EmoteText>) {
            success = client->sendMessage(currentChannel, cmd.text);
        } else if constexpr (std::is_same_v<T, cmd::Sysinfo>) {
            if (m_workPool.activeThreadCount() > 0) { postSystemMsg(QStringLiteral("System information is already being collected.")); return; }
            auto* watcher = new QFutureWatcher<QString>(this);
            connect(watcher, &QFutureWatcher<QString>::finished, this, [owner, connection, serverId, generation, currentChannel, watcher]() {
                const auto summary = watcher->result();
                watcher->deleteLater();
                if (owner && connection && owner->m_generations.value(serverId) == generation && connection->isConnected())
                    connection->sendMessage(currentChannel, summary);
            });
            watcher->setFuture(QtConcurrent::run(&m_workPool, []() { return SystemInfoCollector::getSystemInfoSummary(); }));
        } else if constexpr (std::is_same_v<T, cmd::UploadImage>) {
            const QString apiKey = (m_prefsManager && m_prefsManager->preferences().imgbbApiKey)
                ? *m_prefsManager->preferences().imgbbApiKey
                : QString();
            const QString retryCommand = cmd.filePath ? QStringLiteral("/uploadimage %1").arg(*cmd.filePath) : QStringLiteral("/uploadimage");
            auto cb = [owner, connection, serverId, generation, currentChannel, retryCommand, postSystemMsg](bool ok, const QString& urlOrErr) {
                if (!owner) return;
                if (!connection || owner->m_generations.value(serverId) != generation || !connection->isConnected()) {
                    Q_EMIT owner->inputFailed(serverId, currentChannel, retryCommand, std::nullopt, QStringLiteral("The connection changed during upload."));
                } else if (ok) {
                    if (!connection->sendMessage(currentChannel, urlOrErr))
                        Q_EMIT owner->inputFailed(serverId, currentChannel, retryCommand, std::nullopt, QStringLiteral("Upload complete, but the link was not submitted."));
                } else {
                    postSystemMsg(urlOrErr, MessageType::ERROR);
                    Q_EMIT owner->inputFailed(serverId, currentChannel, retryCommand, std::nullopt, urlOrErr);
                }
            };
            if (cmd.filePath && !cmd.filePath->isEmpty()) {
                m_imgBbService.uploadFromFile(*cmd.filePath, apiKey, cb);
            } else {
                m_imgBbService.uploadFromClipboard(apiKey, cb);
            }
        } else if constexpr (std::is_same_v<T, cmd::Chathistory>) {
            client->requestChatHistory(currentChannel, cmd.limit);
        } else if constexpr (std::is_same_v<T, cmd::React>) {
            QString targetMsgId = cmd.targetRef;
            if (m_connections.contains(serverId)) {
                if (Channel* ch = m_connections[serverId].findChannel(currentChannel)) {
                    bool exactIdMatch = false;
                    for (const auto& m : ch->messages) {
                        if (m.id == cmd.targetRef) {
                            exactIdMatch = true;
                            break;
                        }
                    }
                    if (!exactIdMatch) {
                        for (int i = ch->messages.size() - 1; i >= 0; --i) {
                            if (same(serverId, ch->messages[i].sender, cmd.targetRef)) {
                                targetMsgId = ch->messages[i].id;
                                break;
                            }
                        }
                    }
                }
            }
            client->sendReaction(currentChannel, targetMsgId, cmd.emoji);
        } else if constexpr (std::is_same_v<T, cmd::Setname>) {
            success = client->sendRawCommand(QStringLiteral("SETNAME :%1").arg(cmd.realname));
        } else if constexpr (std::is_same_v<T, cmd::Help>) {
            postSystemMsg(IrcCommandParser::helpText(cmd.command));
            Q_EMIT openHelpDialogRequested(cmd.command);
        } else if constexpr (std::is_same_v<T, cmd::Unknown>) {
            success = false;
            postSystemMsg(IrcCommandParser::commandError(cmd.command), MessageType::ERROR);
        }
    }, command);
    return success;
}

void ConnectionManager::clearChannelMessages(qint64 serverId, const QString& channelName) {
    if (!m_connections.contains(serverId)) return;
    if (Channel* ch = m_connections[serverId].findChannel(channelName)) {
        ch->messages.clear();
        ch->unreadCount = 0;
        ch->hasUnreadMention = false;
        if (m_messageCache) {
            m_messageCache->clearChannelCache(serverId, channelName);
        }
        Q_EMIT channelMessagesChanged(serverId, ch->name);
    }
}

void ConnectionManager::clearAllServerMessages(qint64 serverId) {
    if (!m_connections.contains(serverId)) return;
    for (auto& ch : m_connections[serverId].channels) {
        ch.messages.clear();
        ch.unreadCount = 0;
        ch.hasUnreadMention = false;
        Q_EMIT channelMessagesChanged(serverId, ch.name);
    }
    if (m_messageCache) {
        m_messageCache->clearServerCache(serverId);
    }
}

QStringList ConnectionManager::inputHistory(qint64 serverId, const QString& channelName) const {
    const QString key = QStringLiteral("%1:%2").arg(serverId).arg(canonical(serverId, channelName));
    return m_inputHistories.value(key);
}

QMap<QString, qint64> ConnectionManager::lastActiveTimes(qint64 serverId, const QString& channelName) const {
    const QString key = QStringLiteral("%1:%2").arg(serverId).arg(canonical(serverId, channelName));
    return m_lastActiveTimes.value(key);
}

} // namespace loungecat
