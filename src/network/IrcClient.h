#pragma once
#include "util/TimestampReplayIdentity.h"

#include "network/ChatClient.h"
#include <QSslSocket>
#include <QTimer>
#include <QMap>
#include <QSet>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QQueue>
#include "network/IrcRegistration.h"

namespace loungecat {

struct ParsedIrcLine {
    QMap<QString, QString> tags;
    QString prefix;
    QString senderNick;
    QString senderUser;
    QString senderHost;
    QString command;
    QStringList params;
};

struct IrcBatchState {
    QString refId;
    QString batchType;
    QString target;
    QMap<QString, QString> openingTags;
    bool historical = false;
    QList<ParsedIrcLine> lines;
    QList<IncomingMessage> messages;
    qint64 bufferedBytes = 0;
    QDeadlineTimer expires{30000};
};

class IrcClient : public ChatClient {
    Q_OBJECT
public:
    static constexpr int MAX_WHOIS_CACHE_SIZE = 200;
    static constexpr int KEEPALIVE_INTERVAL_MS = 30000;
    static constexpr int MAX_LINE_BYTES = 16384;
    static constexpr int MAX_ACTIVE_BATCHES = 16;
    static constexpr int MAX_BATCH_MESSAGES = 500;
    static constexpr qint64 MAX_BATCH_BYTES = 1024 * 1024;

    explicit IrcClient(qint64 serverId, const ServerConfig& config, QObject* parent = nullptr);
    ~IrcClient() override;

    void connectToServer() override;
    void disconnectFromServer(const QString& reason = {}) override;
    bool isEncrypted() const override;
    bool allowsPublicPaste(const QString& channel) const override;
    void joinChannel(const QString& channel, const QString& key = {}) override;
    void partChannel(const QString& channel, const QString& reason = {}) override;
    bool sendMessage(const QString& target, const QString& message, const std::optional<IncomingMessage>& replyTo = std::nullopt) override;
    bool sendMessages(const QString& target, const QStringList& messages,
        const std::optional<IncomingMessage>& replyTo = std::nullopt) override;
    bool sendAction(const QString& target, const QString& action) override;
    bool sendNotice(const QString& target, const QString& notice) override;
    bool sendRawCommand(const QString& command) override;
    void changeNickname(const QString& newNick) override;
    QString getNickname() const override { return m_currentNickname; }
    bool isConnected() const override { return m_isRegistered; }

    // IRCv3 methods
    void sendTypingNotification(const QString& target, const QString& state) override;
    void sendReaction(const QString& target, const QString& messageId, const QString& emoji) override;
    void requestChatHistory(const QString& target, int limit = 50) override;
    QSet<QString> enabledCapabilities() const override { return m_enabledCaps; }
    const QMap<QString, QString>& isupportTokens() const { return m_isupport; }

    void startSilentWhois(const QString& nickname) override;
    std::optional<WhoisInfo> getCachedWhois(const QString& nickname) const override;

    QString serverTabName() const;

    // IRCv3 parsing & escaping helpers
    static ParsedIrcLine parseIrcLine(const QString& rawLine);
    static QString unescapeIrcTagValue(const QString& escaped);
    static QString escapeIrcTagValue(const QString& raw);
    static QString formatIrcTags(const QMap<QString, QString>& tags);
    static std::optional<qint64> parseServerTimeIso8601(const QString& isoStr);

private Q_SLOTS:
    void onSocketConnected();
    void onSocketEncrypted();
    void onSocketReadyRead();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError socketError);
    void onSslErrors(const QList<QSslError>& errors);
    void onKeepAliveTimeout();

private:
    bool sendChat(const QString& target, const QString& text, MessageType type, const std::optional<IncomingMessage>& reply);
    bool sendChatBatch(const QString& target, const QStringList& texts, MessageType type,
        const std::optional<IncomingMessage>& reply);
    bool sendChatPart(const QString& target, const QString& text, MessageType type, const std::optional<IncomingMessage>& reply);
    void failPendingSends();
    void failConnection(const QString& error, RetryDisposition disposition = RetryDisposition::Transient);
    bool writeLine(const QByteArray& payload);
    void drainOutput();
    void clearOutput();
    void finishPostConnect();
    void confirmAccount();
    QString identity(const QString& name) const;
    QString channelKey(const QString& name);
    void changeCaseMapping(const QString& mapping);
    void retireChannelState(const QString& channel);
    bool isRetiredChannel(const QString& channel) const;
    void forgetRetiredChannel(const QString& channel);
    void syncPendingUser(const QString& channel, const QString& nick, const std::optional<ChannelUser>& user);
    void beginRegistration();
    void processLine(const QString& line);
    QString normalizedMessageTarget(const QString& rawTarget) const;
    void handleCap(const ParsedIrcLine& msg);
    void requestSupportedCaps(const QMap<QString, QString>& offeredCaps);
    void finishCapNegotiationIfReady();
    bool isHistoricalMessage(const ParsedIrcLine& message) const;
    QString historyIdentityScope(const ParsedIrcLine& message) const;
    TimestampReplayIdentity m_timestampReplayIdentity;
    bool allowCtcpReply();
    void handleAuthenticate(const ParsedIrcLine& msg);
    void handleBatchCommand(const ParsedIrcLine& msg);
    void handleTagMsg(const ParsedIrcLine& msg);
    void handleNumeric(int numeric, const ParsedIrcLine& msg);
    void parseIsupportTokens(const QStringList& tokens);
    void handleWelcomeSequence();
    void handleNicknameInUse();
    void handleModeChange(const QString& channel, const QString& sender, const QStringList& modeArgs, qint64 timestampMs);
    void applyMessageTags(const ParsedIrcLine& line, IncomingMessage& msg);
    void deliverIncomingMessage(const ParsedIrcLine& line, const IncomingMessage& msg);
    void emitServerMessage(const QString& text, MessageType type = MessageType::SERVER, qint64 timestampMs = 0);
    bool verifyCertificateFingerprint();

    QSslSocket* m_socket = nullptr;
    QByteArray m_readBuffer;
    QTimer m_keepAliveTimer;
    QTimer m_pongDeadlineTimer;
    QTimer m_registrationTimer;
    QTimer m_stateExpiryTimer;
    QTimer m_outputTimer;
    struct Outbound { QByteArray bytes; QDeadlineTimer expires{180000}; };
    QQueue<Outbound> m_outbound;
    qsizetype m_outboundBytes = 0;
    QElapsedTimer m_outputClock;
    int m_outputTokens = 4;
    QTimer m_nickServTimer;
    QElapsedTimer m_nickServClock;
    bool m_waitingNickServ = false;
    IrcRegistration m_registration;
    QString m_caseMapping = QStringLiteral("rfc1459");
    QMap<QString, QString> m_channelNames;
    QMap<QString, QSet<QChar>> m_channelPrivacyModes;
    QElapsedTimer m_pingClock;
    bool m_waitingPong = false;
    quint64 m_generation = 0;
    qint64 m_lastPingSentMs = 0;

    QString m_currentNickname;
    QString m_selfUser;
    QString m_selfHost;
    bool m_isRegistered = false;
    bool m_capNegotiating = false;
    QList<QSet<QString>> m_pendingCapRequests;
    QSet<QString> m_capAckParts;
    bool m_saslInProgress = false;
    QElapsedTimer m_ctcpReplyClock;
    int m_ctcpReplies = 0;

    // IRCv3 capabilities & batches
    QMap<QString, QString> m_availableCaps; // capNameLower -> value
    QSet<QString> m_enabledCaps;
    QMap<QString, QString> m_isupport;
    QMap<QString, IrcBatchState> m_activeBatches;

    // Per-channel user map: channelLower -> (nickLower -> ChannelUser)
    QMap<QString, QMap<QString, ChannelUser>> m_channelUsers;
    // Per-channel accumulating NAMES list before 366 RPL_ENDOFNAMES
    QMap<QString, QMap<QString, ChannelUser>> m_pendingNames;
    QMap<QString, QSet<QString>> m_namesRemoved;
    QMap<QString, QDeadlineTimer> m_namesExpires;
    int m_pongTimeoutMs = KEEPALIVE_INTERVAL_MS;

    // WHOIS state
    QMap<QString, WhoisInfo> m_pendingWhois;
    QMap<QString, WhoisInfo> m_whoisCache;
    QSet<QString> m_silentWhoisTargets;

    // /list accumulator
    QList<ChannelListItem> m_channelListAccumulator;
    bool m_channelListTruncated = false;
    QList<IncomingMessage> m_pendingMessages;
    QQueue<QString> m_recentSelfEchoIds;
    QList<QString> m_retiredChannels;
    QSet<QString> m_joinedChannels; // Display names; compare under the current IRC case mapping.
};

} // namespace loungecat
