#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QSet>
#include <optional>
#include "data/model/ServerConfig.h"
#include "data/model/IncomingMessage.h"
#include "data/model/ChannelUser.h"
#include "data/model/WhoisInfo.h"
#include "network/ConnectionPolicy.h"

namespace loungecat {

struct ChannelListItem {
    QString channel;
    int userCount = 0;
    QString topic;
};

class ChatClient : public QObject {
    Q_OBJECT
public:
    explicit ChatClient(qint64 serverId, const ServerConfig& config, QObject* parent = nullptr);
    ~ChatClient() override = default;

    qint64 serverId() const { return m_serverId; }
    const ServerConfig& config() const { return m_config; }
    void updateConfig(const ServerConfig& config) { m_config = config; }

    virtual void connectToServer() = 0;
    virtual void disconnectFromServer(const QString& reason = {}) = 0;
    virtual void joinChannel(const QString& channel, const QString& key = {}) = 0;
    virtual void partChannel(const QString& channel, const QString& reason = {}) = 0;
    virtual bool sendMessage(const QString& target, const QString& message, const std::optional<IncomingMessage>& replyTo = std::nullopt) = 0;
    // Adapters must explicitly provide atomic multi-line admission. A single-line
    // adapter must not partially submit an unsupported batch.
    virtual bool sendMessages(const QString& target, const QStringList& messages,
        const std::optional<IncomingMessage>& replyTo = std::nullopt) {
        return messages.size() == 1 && sendMessage(target, messages.first(), replyTo);
    }
    virtual bool sendAction(const QString& target, const QString& action) = 0;
    virtual bool sendNotice(const QString& target, const QString& notice) = 0;
    virtual bool sendRawCommand(const QString& command) = 0;
    virtual void changeNickname(const QString& newNick) = 0;
    virtual QString getNickname() const = 0;
    virtual bool isConnected() const = 0;
    virtual bool isEncrypted() const { return false; }
    virtual bool allowsPublicPaste(const QString&) const { return false; }
    RetryDisposition retryDisposition() const { return m_retryDisposition; }

    // IRCv3 extensions
    virtual void sendTypingNotification(const QString& target, const QString& state) { Q_UNUSED(target) Q_UNUSED(state) }
    virtual void sendReaction(const QString& target, const QString& messageId, const QString& emoji) { Q_UNUSED(target) Q_UNUSED(messageId) Q_UNUSED(emoji) }
    virtual void requestChatHistory(const QString& target, int limit = 50) { Q_UNUSED(target) Q_UNUSED(limit) }
    virtual QSet<QString> enabledCapabilities() const { return {}; }

    virtual void startSilentWhois(const QString&) {}
    virtual std::optional<WhoisInfo> getCachedWhois(const QString&) const { return std::nullopt; }

Q_SIGNALS:
    void connected(qint64 serverId);
    void disconnected(qint64 serverId, const QString& reason);
    void connectionError(qint64 serverId, const QString& message);
    void sendError(qint64 serverId, const QString& target, const QString& error);
    void messageReceived(qint64 serverId, const loungecat::IncomingMessage& message, bool isHistoryReplay);
    void messageUpdated(qint64 serverId, const QString& localId, const loungecat::IncomingMessage& message);
    void isupportUpdated(qint64 serverId, const QMap<QString, QString>& tokens);
    void channelJoined(qint64 serverId, const QString& channel);
    void channelParted(qint64 serverId, const QString& channel);
    void userJoined(qint64 serverId, const QString& channel, const loungecat::ChannelUser& user);
    void userParted(qint64 serverId, const QString& channel, const QString& nickname, const QString& reason);
    void userQuit(qint64 serverId, const QString& nickname, const QString& reason);
    void userKicked(qint64 serverId, const QString& channel, const QString& kickedUser, const QString& kicker, const QString& reason);
    void nicknameChanged(qint64 serverId, const QString& oldNick, const QString& newNick);
    void topicChanged(qint64 serverId, const QString& channel, const QString& topic, const QString& setBy);
    void userListUpdated(qint64 serverId, const QString& channel, const QList<loungecat::ChannelUser>& users);
    void userModeChanged(qint64 serverId, const QString& channel, const QString& nickname, const QSet<loungecat::UserMode>& modes);
    void userDetailsUpdated(qint64 serverId, const QString& nickname);
    void serverInfoReceived(qint64 serverId, const QString& info);
    void latencyUpdated(qint64 serverId, qint64 latencyMs);
    void whoisUpdated(qint64 serverId, const QString& nickname, const loungecat::WhoisInfo& info);
    void channelListUpdated(qint64 serverId, const QList<loungecat::ChannelListItem>& items, bool finished, bool truncated = false);
    void certificatePinned(qint64 serverId, const QString& sha256Fingerprint);

    // IRCv3 signals
    void capabilitiesUpdated(qint64 serverId, const QSet<QString>& enabledCaps);
    void typingUserUpdated(qint64 serverId, const QString& target, const QString& nickname, const QString& state);
    void reactionReceived(qint64 serverId, const QString& target, const QString& messageId, const QString& sender, const QString& emoji);

protected:
    qint64 m_serverId;
    ServerConfig m_config;
    RetryDisposition m_retryDisposition = RetryDisposition::Transient;
};

} // namespace loungecat
