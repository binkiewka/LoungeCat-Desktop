#pragma once

#include <QObject>
#include <QMap>
#include <QList>
#include <QTimer>
#include <QStringList>
#include <QPointer>
#include <QThreadPool>
#include <optional>
#include <functional>
#include <QElapsedTimer>
#include "data/model/ServerConfig.h"
#include "data/model/ServerConnection.h"
#include "data/model/UserPreferences.h"
#include "data/database/DatabaseService.h"
#include "data/cache/MessageCache.h"
#include "data/preferences/PreferencesManager.h"
#include "network/ChatClient.h"
#include "service/LinkPreviewService.h"
#include "service/PastebinService.h"
#include "service/ImgBbService.h"
#include "service/UrlShortenerService.h"
#include "service/TextLogService.h"
#include "service/SoundService.h"
#include "util/IrcCommandParser.h"

namespace loungecat {

class ConnectionManager : public QObject {
    Q_OBJECT
public:
    static constexpr int UI_UPDATE_THROTTLE_MS = 100;
    static constexpr int STABILITY_RESET_MS = 120000;
    static constexpr int MAX_SERVERS = 16;
    static constexpr int MAX_CHANNELS = 128;
    static constexpr int MAX_RETAINED_MESSAGES = 10000;
    static constexpr qint64 MAX_RETAINED_TEXT_BYTES = 16 * 1024 * 1024;

    ConnectionManager(
        DatabaseService* dbService,
        MessageCache* messageCache,
        PreferencesManager* prefsManager,
        TextLogService* textLogService,
        SoundService* soundService,
        QObject* parent = nullptr
    );
    ~ConnectionManager() override;

    void initializeFromDatabase(bool autoConnectEnabled = true);

    QList<ServerConnection> connections() const;
    std::optional<ServerConnection> connection(qint64 serverId) const;
    ChatClient* client(qint64 serverId) const { return m_clients.value(serverId, nullptr); }

    qint64 activeServerId() const { return m_activeServerId; }
    QString activeChannelName() const { return m_activeChannelName; }
    void setActiveChannel(qint64 serverId, const QString& channelName);

    qint64 addServer(const ServerConfig& config, bool connectImmediately = true);
    void updateServer(const ServerConfig& config);
    void removeServer(qint64 serverId);

    void connectServer(qint64 serverId);
    void disconnectServer(qint64 serverId, const QString& reason = {});
    void reconnectServer(qint64 serverId);

    void joinChannel(qint64 serverId, const QString& channelName, const QString& key = {});
    void partChannel(qint64 serverId, const QString& channelName, const QString& reason = {});
    void closeChannelOrQuery(qint64 serverId, const QString& channelName);
    bool openQuery(qint64 serverId, const QString& nickname, const QString& initialMessage = {});
    void reorderChannel(qint64 serverId, int fromIndex, int toIndex);

    bool sendUserInput(
        qint64 serverId,
        const QString& channelName,
        const QString& rawInput,
        const std::optional<IncomingMessage>& replyContext = std::nullopt
    );
    void sendTypingNotification(qint64 serverId, const QString& target, const QString& state);
    void sendReaction(qint64 serverId, const QString& target, const QString& messageId, const QString& emoji);

    void clearChannelMessages(qint64 serverId, const QString& channelName);
    void clearAllServerMessages(qint64 serverId);

    QStringList inputHistory(qint64 serverId, const QString& channelName) const;
    QMap<QString, qint64> lastActiveTimes(qint64 serverId, const QString& channelName) const;

    static int reconnectDelayMsForAttempt(int attempt, double random = 0.5);
    using ClientFactory = std::function<ChatClient*(qint64, const ServerConfig&, QObject*)>;
    void setClientFactory(ClientFactory factory) { Q_ASSERT(m_clients.isEmpty()); m_clientFactory = std::move(factory); }
    QString conversationKey(qint64 serverId, const QString& target) const {
        return QStringLiteral("%1:%2").arg(serverId).arg(canonical(serverId, target));
    }

    LinkPreviewService* linkPreviewService() { return &m_linkPreviewService; }
    PastebinService* pastebinService() { return &m_pastebinService; }
    ImgBbService* imgBbService() { return &m_imgBbService; }
    UrlShortenerService* urlShortenerService() { return &m_urlShortenerService; }

Q_SIGNALS:
    void operationError(const QString& error);
    void inputFailed(qint64 serverId, const QString& channel, const QString& text, const std::optional<loungecat::IncomingMessage>& reply, const QString& error);
    void queryRenamed(qint64 serverId, const QString& oldName, const QString& newName);
    void identityKeysChanged(qint64 serverId, const QMap<QString, QString>& redirects);
    void connectionsChanged();
    void channelMessagesChanged(qint64 serverId, const QString& channelName);
    void activeChannelChanged(qint64 serverId, const QString& channelName);
    void desktopNotificationRequested(const QString& title, const QString& message);
    void openChannelListDialogRequested(qint64 serverId, const QString& filter);
    void openHelpDialogRequested(const std::optional<QString>& command);
    void channelListReceived(qint64 serverId, const QList<loungecat::ChannelListItem>& items, bool finished, bool truncated = false);
    void whoisInfoUpdated(qint64 serverId, const QString& nickname, const loungecat::WhoisInfo& info);

private:
    void wireClientSignals(ChatClient* client);
    void updateSavedChannels(qint64 serverId, const QString& channelName, bool isJoining, const std::optional<QString>& key = std::nullopt);
    QString canonical(qint64 id, const QString& name) const;
    bool same(qint64 id, const QString& first, const QString& second) const;
    bool isIgnored(qint64 id, const QString& nickname) const;
    Channel& ensureChannel(qint64 serverId, const QString& name, ChannelType defaultType = ChannelType::CHANNEL);
    void appendMessageToChannel(qint64 serverId, const QString& channelName, IncomingMessage message, bool isHistoryReplay = false);
    void scheduleReconnect(qint64 serverId);
    void scheduleUiUpdate(qint64 serverId, const QString& channelName = {});
    bool canCreateChannel(qint64 serverId, const QString& channelName) const;
    void trimRetainedHistory();
    void deferInputPublication(quint64 ticket, std::function<void()> publish);
    void drainInputPublications();
    void clearTypingUser(qint64 serverId, Channel& channel, const QString& nickname);
    bool executeParsedCommand(qint64 serverId, const QString& currentChannel, const IrcCommand& command);
    bool sendChatPipeline(
        qint64 serverId,
        const QString& target,
        const QString& text,
        const std::optional<IncomingMessage>& replyContext,
        const QString& draft
    );

    DatabaseService* m_dbService;
    MessageCache* m_messageCache;
    PreferencesManager* m_prefsManager;
    TextLogService* m_textLogService;
    SoundService* m_soundService;

    LinkPreviewService m_linkPreviewService;
    PastebinService m_pastebinService;
    ImgBbService m_imgBbService;
    UrlShortenerService m_urlShortenerService;

    QMap<qint64, ServerConnection> m_connections;
    ClientFactory m_clientFactory;
    QElapsedTimer m_clock;
    QMap<qint64, ChatClient*> m_clients;
    QMap<qint64, QTimer*> m_reconnectTimers;
    QMap<qint64, QTimer*> m_stabilityTimers;
    QMap<qint64, quint64> m_generations;
    quint64 m_nextGeneration = 0;
    QMap<QString, quint64> m_channelLoads;
    quint64 m_nextChannelLoad = 0;
    QThreadPool m_workPool;
    int m_inFlightInputs = 0;
    struct PendingInputPublication {
        quint64 ticket;
        qint64 serverId;
        quint64 generation;
        QString target;
        std::function<void()> publish;
    };
    quint64 m_nextInputTicket = 0;
    QList<PendingInputPublication> m_pendingInputPublications;
    bool m_drainingInputPublications = false;
    QMap<QString, QStringList> m_inputHistories;
    QMap<QString, QMap<QString, qint64>> m_lastActiveTimes;

    qint64 m_activeServerId = 0;
    QString m_activeChannelName;

    QTimer m_uiThrottleTimer;
    QTimer m_typingTimer;
    QMap<QString, QDeadlineTimer> m_typingExpires;
    QSet<QPair<qint64, QString>> m_pendingChannelUpdates;
    bool m_pendingConnectionsUpdate = false;
};

} // namespace loungecat
