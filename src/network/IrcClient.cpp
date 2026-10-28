#include "util/IrcSecrets.h"
#include "util/ServerAddress.h"
#include "util/IrcLimits.h"
#include "network/IrcClient.h"
#include "util/IrcTextChunks.h"
#include <QNetworkProxy>
#include <QCryptographicHash>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QDateTime>
#include <QTimeZone>
#include <QUuid>
#include <algorithm>
#include <utility>
#include <QThread>
#include "util/IrcIdentity.h"

namespace loungecat {

namespace {
const QStringList& desiredCapabilities() {
    static const QStringList caps = {
        QStringLiteral("message-tags"),
        QStringLiteral("server-time"),
        QStringLiteral("echo-message"),
        QStringLiteral("batch"),
        QStringLiteral("labeled-response"),
        QStringLiteral("msgid"),
        QStringLiteral("multi-prefix"),
        QStringLiteral("away-notify"),
        QStringLiteral("account-notify"),
        QStringLiteral("account-tag"),
        QStringLiteral("extended-join"),
        QStringLiteral("chghost"),
        QStringLiteral("setname"),
        QStringLiteral("invite-notify"),
        QStringLiteral("userhost-in-names"),
        QStringLiteral("cap-notify"),
        QStringLiteral("standard-replies"),
        QStringLiteral("chathistory"),
        QStringLiteral("draft/chathistory"),
        QStringLiteral("draft/multiline")
    };
    return caps;
}
}

IrcClient::IrcClient(qint64 serverId, const ServerConfig& config, QObject* parent)
    : ChatClient(serverId, config, parent),
      m_currentNickname(config.nickname.isEmpty() ? QStringLiteral("LoungeCatUser") : config.nickname) {
    m_registration.required = config.useSasl;
    m_outputTimer.setInterval(500);
    connect(&m_outputTimer, &QTimer::timeout, this, &IrcClient::drainOutput);
    m_nickServTimer.setInterval(2000);
    connect(&m_nickServTimer, &QTimer::timeout, this, [this]() {
        if (!m_isRegistered || !m_waitingNickServ) { m_nickServTimer.stop(); return; }
        if (m_nickServClock.hasExpired(15000)) {
            m_waitingNickServ = false; m_nickServTimer.stop();
            emitServerMessage(QStringLiteral("NickServ account was not confirmed. Automatic joins and commands were withheld. Use SASL, or join manually after identifying."), MessageType::ERROR);
            return;
        }
        startSilentWhois(m_currentNickname);
    });
    m_keepAliveTimer.setInterval(KEEPALIVE_INTERVAL_MS);
    connect(&m_keepAliveTimer, &QTimer::timeout, this, &IrcClient::onKeepAliveTimeout);
    // The periodic coarse timer can wake just before a PONG deadline and
    // otherwise defer failure for a whole extra keepalive interval.
    m_pongDeadlineTimer.setSingleShot(true);
    m_pongDeadlineTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_pongDeadlineTimer, &QTimer::timeout, this, [this]() {
        if (m_isRegistered && m_waitingPong)
            failConnection(QStringLiteral("The server did not answer the keepalive."));
    });
    m_registrationTimer.setSingleShot(true);
    m_registrationTimer.setInterval(30000);
    connect(&m_registrationTimer, &QTimer::timeout, this, [this]() { failConnection(QStringLiteral("Connection or registration timed out.")); });
    m_stateExpiryTimer.setInterval(5000);
    connect(&m_stateExpiryTimer, &QTimer::timeout, this, [this]() {
        for (const auto& batch : m_activeBatches) if (batch.expires.hasExpired()) {
            failConnection(QStringLiteral("An incomplete IRC batch expired.")); return;
        }
        const auto now = QDateTime::currentMSecsSinceEpoch();
        for (const auto& key : m_namesExpires.keys()) if (m_namesExpires[key].hasExpired()) {
            m_pendingNames.remove(key); m_namesRemoved.remove(key); m_namesExpires.remove(key);
            emitServerMessage(QStringLiteral("An incomplete user-list snapshot expired."), MessageType::ERROR);
        }
        for (const auto& key : m_pendingWhois.keys()) if (now - m_pendingWhois[key].cachedAtMs > 30000) {
            m_pendingWhois.remove(key); m_silentWhoisTargets.remove(key);
        }
    });
}

IrcClient::~IrcClient() {
    m_keepAliveTimer.stop();
    m_pongDeadlineTimer.stop();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
    }
}

QString IrcClient::identity(const QString& name) const { return ircIdentity(name, m_caseMapping); }
QString IrcClient::normalizedMessageTarget(const QString& rawTarget) const {
    const auto status = m_isupport.value(QStringLiteral("STATUSMSG"));
    const auto types = m_isupport.value(QStringLiteral("CHANTYPES"), QStringLiteral("#&+!"));
    if (rawTarget.size() > 1 && status.contains(rawTarget.front()) && types.contains(rawTarget.at(1))) {
        return rawTarget.mid(1);
    }
    return rawTarget;
}

QString IrcClient::channelKey(const QString& name) {
    const auto key = identity(name);
    if (m_channelNames.size() < 256 || m_channelNames.contains(key)) m_channelNames[key] = name;
    return key;
}
bool IrcClient::allowsPublicPaste(const QString& channel) const {
    const auto key = identity(channel);
    return m_isRegistered && m_channelPrivacyModes.contains(key) && m_channelPrivacyModes.value(key).isEmpty();
}

bool IrcClient::isRetiredChannel(const QString& channel) const {
    return std::any_of(m_retiredChannels.cbegin(), m_retiredChannels.cend(),
        [&](const QString& retired) { return identity(retired) == identity(channel); });
}
void IrcClient::forgetRetiredChannel(const QString& channel) {
    m_retiredChannels.removeIf([&](const QString& retired) { return identity(retired) == identity(channel); });
}
void IrcClient::retireChannelState(const QString& channel) {
    const auto key = identity(channel);
    m_channelPrivacyModes.remove(key);
    m_joinedChannels.removeIf([&](const QString& joined) { return identity(joined) == key; });
    m_channelUsers.remove(key);
    m_channelNames.remove(key);
    m_pendingNames.remove(key); m_namesRemoved.remove(key); m_namesExpires.remove(key);
    forgetRetiredChannel(channel);
    m_retiredChannels.append(channel);
    // Keep late multipart NAMES from recreating a departed channel. A confirmed
    // rejoin or an explicit NAMES command admits a new roster; memory stays bounded.
    if (m_retiredChannels.size() > 256) m_retiredChannels.removeFirst();
}

void IrcClient::changeCaseMapping(const QString& requested) {
    const auto mapping = requested == "ascii" || requested == "strict-rfc1459" ? requested : QStringLiteral("rfc1459");
    if (mapping == m_caseMapping) return;
    const auto oldNames = m_channelNames;
    m_channelPrivacyModes.clear(); // A changed equivalence relation requires fresh mode snapshots.
    m_caseMapping = mapping;
    QMap<QString, QMap<QString, ChannelUser>> next;
    m_channelNames.clear();
    for (auto channel = m_channelUsers.begin(); channel != m_channelUsers.end(); ++channel) {
        const auto key = channelKey(oldNames.value(channel.key(), channel.key()));
        for (const auto& user : channel.value()) next[key].insert(identity(user.nickname), user);
    }
    m_channelUsers = next;
    // An in-progress snapshot has old canonical tombstones; refresh it under the new rules.
    const auto pending = m_pendingNames.keys();
    m_pendingNames.clear(); m_namesRemoved.clear(); m_namesExpires.clear();
    for (const auto& channel : pending) if (m_isRegistered) sendRawCommand(QStringLiteral("NAMES %1").arg(oldNames.value(channel, channel)));
    QMap<QString, WhoisInfo> whois;
    for (const auto& value : m_whoisCache) whois.insert(identity(value.nickname), value);
    m_whoisCache = whois; m_pendingWhois.clear(); m_silentWhoisTargets.clear();
}

QString IrcClient::serverTabName() const {
    return QStringLiteral("* ") + (m_config.serverName.isEmpty() ? m_config.hostname : m_config.serverName);
}

QString IrcClient::unescapeIrcTagValue(const QString& escaped) {
    if (escaped.isEmpty()) return QString();
    QString out;
    out.reserve(escaped.length());
    int i = 0;
    const int n = escaped.length();
    while (i < n) {
        if (escaped[i] == '\\') {
            if (i + 1 >= n) {
                // Lone trailing backslash is ignored per IRCv3 message-tags spec
                break;
            }
            const QChar next = escaped[i + 1];
            switch (next.unicode()) {
                case ':':  out.append(';');  break;
                case 's':  out.append(' ');  break;
                case '\\': out.append('\\'); break;
                case 'r':  out.append('\r'); break;
                case 'n':  out.append('\n'); break;
                default:   out.append(next); break;
            }
            i += 2;
        } else {
            out.append(escaped[i]);
            ++i;
        }
    }
    return out;
}

QString IrcClient::escapeIrcTagValue(const QString& raw) {
    QString out;
    out.reserve(raw.length());
    for (QChar ch : raw) {
        switch (ch.unicode()) {
            case ';':  out.append(QStringLiteral("\\:")); break;
            case ' ':  out.append(QStringLiteral("\\s")); break;
            case '\\': out.append(QStringLiteral("\\\\")); break;
            case '\r': out.append(QStringLiteral("\\r")); break;
            case '\n': out.append(QStringLiteral("\\n")); break;
            default:   out.append(ch); break;
        }
    }
    return out;
}

QString IrcClient::formatIrcTags(const QMap<QString, QString>& tags) {
    if (tags.isEmpty()) return QString();
    QStringList parts;
    for (auto it = tags.begin(); it != tags.end(); ++it) {
        if (it.value().isEmpty()) {
            parts.append(it.key());
        } else {
            parts.append(QStringLiteral("%1=%2").arg(it.key(), escapeIrcTagValue(it.value())));
        }
    }
    return QStringLiteral("@") + parts.join(';') + QStringLiteral(" ");
}

std::optional<qint64> IrcClient::parseServerTimeIso8601(const QString& isoStr) {
    if (isoStr.trimmed().isEmpty()) return std::nullopt;
    QDateTime dt = QDateTime::fromString(isoStr.trimmed(), Qt::ISODateWithMs);
    if (!dt.isValid()) {
        dt = QDateTime::fromString(isoStr.trimmed(), Qt::ISODate);
    }
    if (dt.isValid()) {
        return dt.toMSecsSinceEpoch();
    }
    return std::nullopt;
}

ParsedIrcLine IrcClient::parseIrcLine(const QString& rawLine) {
    ParsedIrcLine parsed;
    QString line = rawLine;

    // 1. Parse optional IRCv3 message tags (@key1=val1;key2=val2 )
    if (line.startsWith('@')) {
        const int sp = line.indexOf(' ');
        if (sp != -1) {
            const QString tagSection = line.mid(1, sp - 1);
            const QStringList rawTags = tagSection.split(';', Qt::SkipEmptyParts);
            for (const QString& tagItem : rawTags) {
                const int eq = tagItem.indexOf('=');
                if (eq == -1) {
                    parsed.tags.insert(tagItem, QString());
                } else {
                    const QString key = tagItem.left(eq);
                    const QString val = unescapeIrcTagValue(tagItem.mid(eq + 1));
                    parsed.tags.insert(key, val);
                }
            }
            line = line.mid(sp + 1);
            while (line.startsWith(' ')) line.remove(0, 1);
        }
    }

    // 2. Parse optional prefix (:nick!user@host )
    if (line.startsWith(':')) {
        const int sp = line.indexOf(' ');
        if (sp != -1) {
            parsed.prefix = line.mid(1, sp - 1);
            line = line.mid(sp + 1);

            const int bang = parsed.prefix.indexOf('!');
            const int at = parsed.prefix.indexOf('@');
            if (bang != -1 && at != -1 && bang < at) {
                parsed.senderNick = parsed.prefix.left(bang);
                parsed.senderUser = parsed.prefix.mid(bang + 1, at - bang - 1);
                parsed.senderHost = parsed.prefix.mid(at + 1);
            } else {
                parsed.senderNick = parsed.prefix;
            }
        }
    }

    // 3. Extract command and parameters
    int pos = 0;
    const int n = line.length();
    while (pos < n && line[pos] == ' ') ++pos;

    const int cmdEnd = line.indexOf(' ', pos);
    if (cmdEnd == -1) {
        parsed.command = line.mid(pos).toUpper();
        return parsed;
    }

    parsed.command = line.mid(pos, cmdEnd - pos).toUpper();
    pos = cmdEnd + 1;

    while (pos < n) {
        while (pos < n && line[pos] == ' ') ++pos;
        if (pos >= n) break;

        if (line[pos] == ':') {
            parsed.params.append(line.mid(pos + 1));
            break;
        }

        const int nextSpace = line.indexOf(' ', pos);
        if (nextSpace == -1) {
            parsed.params.append(line.mid(pos));
            break;
        }
        parsed.params.append(line.mid(pos, nextSpace - pos));
        pos = nextSpace + 1;
    }

    return parsed;
}

void IrcClient::connectToServer() {
    if (m_isRegistered || m_registrationTimer.isActive()) return;
    ++m_generation;
    m_retryDisposition = RetryDisposition::Transient;
    m_registration = {};
    m_registration.required = m_config.useSasl;
    m_caseMapping = QStringLiteral("rfc1459");
    m_channelNames.clear();
    clearOutput();
    const auto host = normalizedServerHost(m_config.hostname);
    if (!host) {
        failConnection(QStringLiteral("Invalid hostname. Enter a hostname or IP address, not a URL or hostname:port."), RetryDisposition::UserActionRequired);
        return;
    }
    const bool savedSecrets = !m_config.saslPassword.isEmpty() || !m_config.nickServPassword.isEmpty() || !m_config.onConnectCommands.trimmed().isEmpty() || !m_config.channelKeys.isEmpty();
    if ((!m_config.useSsl && savedSecrets) || m_config.hostname.trimmed().isEmpty() ||
        m_config.port < 1 || m_config.port > 65535 ||
        (m_config.useSasl && m_config.saslPassword.isEmpty())) {
        failConnection(QStringLiteral("Invalid connection configuration. Enable TLS for saved credentials and supply the required SASL PLAIN password."), RetryDisposition::UserActionRequired);
        return;
    }
    m_waitingPong = false;
    m_pongDeadlineTimer.stop();
    m_registrationTimer.start();
    m_stateExpiryTimer.start();
    failPendingSends();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    m_readBuffer.clear();
    m_isRegistered = false;
    m_capNegotiating = false;
    m_pendingCapRequests.clear();
    m_recentSelfEchoIds.clear();
    m_retiredChannels.clear();
    m_joinedChannels.clear();
    m_capAckParts.clear();
    m_saslInProgress = false;
    m_ctcpReplyClock.invalidate();
    m_ctcpReplies = 0;
    m_currentNickname = m_config.nickname.isEmpty() ? QStringLiteral("LoungeCatUser") : m_config.nickname;
    m_selfUser.clear(); m_selfHost.clear();
    m_availableCaps.clear();
    m_enabledCaps.clear();
    m_isupport.clear();
    m_activeBatches.clear();
    m_timestampReplayIdentity.clear();
    m_channelUsers.clear();
    m_channelPrivacyModes.clear();
    m_pendingNames.clear();
    m_namesRemoved.clear();
    m_namesExpires.clear();
    m_pendingWhois.clear();
    m_whoisCache.clear();
    m_silentWhoisTargets.clear();
    m_channelListAccumulator.clear();
    m_channelListTruncated = false;

    m_socket = new QSslSocket(this);
    m_socket->setReadBufferSize(64 * 1024);

    // Configure Proxy
    if (m_config.proxyType == ProxyType::SOCKS5 && !m_config.proxyHost.isEmpty()) {
        QNetworkProxy proxy(
            QNetworkProxy::Socks5Proxy,
            m_config.proxyHost,
            static_cast<quint16>(m_config.proxyPort > 0 ? m_config.proxyPort : 1080),
            m_config.proxyUsername,
            m_config.proxyPassword
        );
        proxy.setCapabilities(proxy.capabilities() | QNetworkProxy::HostNameLookupCapability);
        m_socket->setProxy(proxy);
    } else if (m_config.proxyType == ProxyType::HTTP && !m_config.proxyHost.isEmpty()) {
        QNetworkProxy proxy(
            QNetworkProxy::HttpProxy,
            m_config.proxyHost,
            static_cast<quint16>(m_config.proxyPort > 0 ? m_config.proxyPort : 8080),
            m_config.proxyUsername,
            m_config.proxyPassword
        );
        m_socket->setProxy(proxy);
    } else {
        m_socket->setProxy(QNetworkProxy::NoProxy);
    }

    connect(m_socket, &QSslSocket::connected, this, &IrcClient::onSocketConnected);
    connect(m_socket, &QSslSocket::encrypted, this, &IrcClient::onSocketEncrypted);
    connect(m_socket, &QSslSocket::readyRead, this, &IrcClient::onSocketReadyRead);
    connect(m_socket, &QSslSocket::disconnected, this, &IrcClient::onSocketDisconnected);
    connect(m_socket, &QSslSocket::errorOccurred, this, &IrcClient::onSocketError);
    connect(m_socket, &QSslSocket::sslErrors, this, &IrcClient::onSslErrors);

    if (m_config.useSsl) {
        if ((m_config.pinnedCertFingerprint && !m_config.pinnedCertFingerprint->isEmpty()) || m_config.trustOnFirstUse) {
            QSslConfiguration sslCfg = m_socket->sslConfiguration();
            sslCfg.setPeerVerifyMode(QSslSocket::QueryPeer);
            m_socket->setSslConfiguration(sslCfg);
        }
        m_socket->connectToHostEncrypted(*host, static_cast<quint16>(m_config.port));
    } else {
        m_socket->connectToHost(*host, static_cast<quint16>(m_config.port));
    }
}

void IrcClient::disconnectFromServer(const QString& reason) {
    ++m_generation;
    m_registrationTimer.stop();
    m_stateExpiryTimer.stop();
    clearOutput();
    m_waitingNickServ = false; m_nickServTimer.stop();
    failPendingSends();
    m_keepAliveTimer.stop();
    m_pongDeadlineTimer.stop();
    if (!m_socket) {
        m_isRegistered = false;
        return;
    }
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        const QString quitMsg = reason.isEmpty() ? QStringLiteral("LoungeCat IRC Client") : reason;
        sendRawCommand(QStringLiteral("QUIT :%1").arg(quitMsg));
        m_socket->flush();
    }
    m_isRegistered = false;
    auto* closingSocket = m_socket;
    m_socket->disconnectFromHost();
    QTimer::singleShot(1000, closingSocket, [closingSocket]() {
        if (closingSocket->state() != QAbstractSocket::UnconnectedState) closingSocket->abort();
    });
}

void IrcClient::onSocketConnected() {
    if (!m_config.useSsl) {
        beginRegistration();
    }
}

void IrcClient::onSocketEncrypted() {
    if (!verifyCertificateFingerprint()) {
        return;
    }
    beginRegistration();
}

bool IrcClient::verifyCertificateFingerprint() {
    if (!m_socket) return false;
    const QSslCertificate cert = m_socket->peerCertificate();
    if (cert.isNull()) {
        if (m_config.pinnedCertFingerprint && !m_config.pinnedCertFingerprint->isEmpty()) {
            failConnection(QStringLiteral("Server did not present a TLS certificate."), RetryDisposition::UserActionRequired);
            return false;
        }
        return true;
    }

    const QString actualFp = QString::fromLatin1(
        QCryptographicHash::hash(cert.toDer(), QCryptographicHash::Sha256).toHex()
    ).toLower();

    if (m_config.pinnedCertFingerprint && !m_config.pinnedCertFingerprint->trimmed().isEmpty()) {
        QString expectedFp = m_config.pinnedCertFingerprint->trimmed().toLower();
        expectedFp.remove(':').remove(' ');
        if (actualFp != expectedFp) {
            const QString errMsg = QStringLiteral("TLS Certificate fingerprint mismatch! Expected %1, got %2")
                                       .arg(expectedFp, actualFp);
            emitServerMessage(errMsg, MessageType::ERROR);
            failConnection(errMsg, RetryDisposition::UserActionRequired);
            return false;
        }
    } else if (m_config.trustOnFirstUse) {
        m_config.pinnedCertFingerprint = actualFp;
        emitServerMessage(QStringLiteral("Pinned TLS certificate SHA-256 fingerprint (TOFU): %1").arg(actualFp));
        Q_EMIT certificatePinned(m_serverId, actualFp);
    }
    return true;
}

void IrcClient::onSslErrors(const QList<QSslError>& errors) {
    if (!m_socket) return;
    if ((m_config.pinnedCertFingerprint && !m_config.pinnedCertFingerprint->isEmpty()) || m_config.trustOnFirstUse) {
        m_socket->ignoreSslErrors();
        return;
    }
    QStringList errStrings;
    for (const auto& e : errors) {
        errStrings.append(e.errorString());
    }
    failConnection(QStringLiteral("TLS certificate verification failed: %1").arg(errStrings.join(QStringLiteral("; "))), RetryDisposition::UserActionRequired);
}

void IrcClient::beginRegistration() {
    // Always negotiate IRCv3.2 capabilities
    m_capNegotiating = true;
    sendRawCommand(QStringLiteral("CAP LS 302"));

    const QString user = m_config.username.trimmed().isEmpty() ? m_currentNickname : m_config.username.trimmed();
    const QString real = m_config.realName.trimmed().isEmpty() ? m_currentNickname : m_config.realName.trimmed();
    sendRawCommand(QStringLiteral("NICK %1").arg(m_currentNickname));
    sendRawCommand(QStringLiteral("USER %1 0 * :%2").arg(user, real));
}

bool IrcClient::writeLine(const QByteArray& payload) {
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState &&
        (!m_config.useSsl || m_socket->isEncrypted()) &&
        m_socket->bytesToWrite() + payload.size() <= 256 * 1024 &&
        m_socket->write(payload) == payload.size();
}

void IrcClient::clearOutput() {
    m_outputTimer.stop(); m_outbound.clear(); m_outboundBytes = 0;
    m_outputTokens = 4; m_outputClock.start();
}

void IrcClient::drainOutput() {
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) { clearOutput(); return; }
    if (!m_outputClock.isValid()) m_outputClock.start();
    const auto elapsed = m_outputClock.elapsed();
    if (elapsed >= 500) {
        m_outputTokens = std::min(4, m_outputTokens + int(elapsed / 500));
        m_outputClock.restart();
    }
    while (!m_outbound.isEmpty() && m_outputTokens > 0) {
        if (m_outbound.head().expires.hasExpired()) { failConnection(QStringLiteral("Outgoing IRC queue expired; queued work was retired.")); return; }
        const auto next = m_outbound.dequeue(); m_outboundBytes -= next.bytes.size();
        --m_outputTokens;
        if (!writeLine(next.bytes)) { failConnection(QStringLiteral("An outgoing IRC command could not be written.")); return; }
    }
    if (m_outbound.isEmpty()) m_outputTimer.stop();
    else if (!m_outputTimer.isActive()) m_outputTimer.start();
}

bool IrcClient::sendRawCommand(const QString& command) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState ||
        (m_config.useSsl && !m_socket->isEncrypted())) return false;
    QString sanitized = command;
    sanitized.remove('\r').remove('\n');
    const QByteArray payload = sanitized.toUtf8() + "\r\n";
    if (payload.size() > 8192) return false;
    QString bare = sanitized;
    if (bare.startsWith('@')) bare = bare.mid(bare.indexOf(' ') + 1);
    const auto verb = bare.section(' ', 0, 0).toUpper();
    const bool control = verb == "PONG" || verb == "PING" || verb == "QUIT" || verb == "CAP" ||
        (!m_isRegistered && (verb == "NICK" || verb == "USER" || verb == "PASS" || verb == "AUTHENTICATE"));
    if (control) return writeLine(payload);
    if (!m_isRegistered || m_outbound.size() >= 256 || m_outboundBytes + payload.size() > 128 * 1024) return false;
    m_outbound.enqueue(Outbound{payload}); m_outboundBytes += payload.size();
    // 321 is obsolete/optional: a newly accepted LIST owns a fresh snapshot.
    // Keep prior results intact when connection/admission checks reject it.
    if (verb == QStringLiteral("LIST")) {
        m_channelListAccumulator.clear();
        m_channelListTruncated = false;
    }
    if (verb == QStringLiteral("NAMES")) {
        const auto targets = bare.section(' ', 1, 1);
        if (targets.isEmpty()) m_retiredChannels.clear();
        else for (const auto& target : targets.split(',', Qt::SkipEmptyParts)) forgetRetiredChannel(target);
    }
    drainOutput();
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

bool IrcClient::isEncrypted() const {
    return m_socket && m_socket->isEncrypted();
}

void IrcClient::joinChannel(const QString& channel, const QString& key) {
    QString ch = channel.trimmed();
    if (ch.isEmpty()) return;
    if (ch.size() > 200 || key.size() > 200 ||
        std::any_of(ch.begin(), ch.end(), [](QChar c) { return c.isSpace() || c == ',' || c.unicode() == 0; }) ||
        std::any_of(key.begin(), key.end(), [](QChar c) { return c.isSpace() || c == ',' || c.unicode() == 0; }) ||
        (!key.isEmpty() && !m_config.useSsl)) {
        Q_EMIT sendError(m_serverId, ch, QStringLiteral("Invalid channel/key, or TLS is required for a channel key.")); return;
    }
    if (!ch.startsWith('#') && !ch.startsWith('&') && !ch.startsWith('+') && !ch.startsWith('!')) {
        ch.prepend('#');
    }
    if (key.isEmpty()) {
        sendRawCommand(QStringLiteral("JOIN %1").arg(ch));
    } else {
        sendRawCommand(QStringLiteral("JOIN %1 %2").arg(ch, key));
    }
}

void IrcClient::partChannel(const QString& channel, const QString& reason) {
    if (reason.isEmpty()) {
        sendRawCommand(QStringLiteral("PART %1").arg(channel));
    } else {
        sendRawCommand(QStringLiteral("PART %1 :%2").arg(channel, reason));
    }
}

bool IrcClient::sendMessage(const QString& target, const QString& message, const std::optional<IncomingMessage>& replyTo) {
    return sendChat(target, message, MessageType::NORMAL, replyTo);
}
bool IrcClient::sendAction(const QString& target, const QString& action) {
    return sendChat(target, action, MessageType::ACTION, std::nullopt);
}
bool IrcClient::sendNotice(const QString& target, const QString& notice) {
    return sendChat(target, notice, MessageType::NOTICE, std::nullopt);
}
bool IrcClient::sendMessages(const QString& target, const QStringList& texts,
    const std::optional<IncomingMessage>& reply) {
    return sendChatBatch(target, texts, MessageType::NORMAL, reply);
}

bool IrcClient::sendChat(const QString& target, const QString& text, MessageType type,
    const std::optional<IncomingMessage>& reply) {
    return sendChatBatch(target, {text}, type, reply);
}

bool IrcClient::sendChatBatch(const QString& target, const QStringList& texts, MessageType type,
    const std::optional<IncomingMessage>& reply) {
    const QString verb = type == MessageType::NOTICE ? QStringLiteral("NOTICE") : QStringLiteral("PRIVMSG");
    const qsizetype prefixBytes = m_currentNickname.toUtf8().size() + 4 +
        (m_selfUser.isEmpty() ? 12 : m_selfUser.toUtf8().size()) +
        (m_selfHost.isEmpty() ? 255 : m_selfHost.toUtf8().size());
    const qsizetype budget = 510 - prefixBytes - verb.size() - target.toUtf8().size() - 3 -
        (type == MessageType::ACTION ? 9 : 0);
    auto reject = [&]() {
        Q_EMIT sendError(m_serverId, target, QStringLiteral("Message not submitted: invalid input, disconnected, or outgoing limit reached."));
        return false;
    };
    const bool invalidTarget = target.isEmpty() || std::any_of(target.begin(), target.end(), [](QChar c) {
        return c.isSpace() || c == ',' || c == ':' || c.unicode() == 0;
    });
    if (!m_isRegistered || invalidTarget || texts.isEmpty() || texts.size() > 256) return reject();

    struct PreparedPart { QString text; std::optional<IncomingMessage> reply; };
    QList<PreparedPart> prepared;
    qsizetype queuedBytes = 0;
    for (qsizetype index = 0; index < texts.size(); ++index) {
        const auto& text = texts[index];
        const auto parts = ircTextChunks(text, budget);
        if (!parts || (parts->size() > 1 && (ircSecrets::message(target, text) || text.contains(QChar(1)))))
            return reject();
        const auto partReply = index == 0 ? reply : std::nullopt;
        QMap<QString, QString> tags;
        if (partReply && m_enabledCaps.contains(QStringLiteral("message-tags"))) {
            tags.insert(QStringLiteral("+reply"), partReply->id);
            tags.insert(QStringLiteral("+draft/reply"), partReply->id);
        }
        const auto tagBytes = formatIrcTags(tags).toUtf8().size();
        for (const auto& part : *parts) {
            const auto bytes = tagBytes + verb.size() + target.toUtf8().size() + 5 + part.toUtf8().size() +
                (type == MessageType::ACTION ? 9 : 0);
            // sendRawCommand has an independent whole-frame limit, including tags.
            if (bytes > 8192 || prepared.size() >= 64) return reject();
            queuedBytes += bytes;
            prepared.append(PreparedPart{part, partReply});
        }
    }
    if (m_pendingMessages.size() + prepared.size() > 64 || m_outbound.size() + prepared.size() > 256 ||
        m_outboundBytes + queuedBytes > 128 * 1024) return reject();

    // No queue/local-echo side effects occur until every logical row is admitted.
    // A real socket failure during commit cannot be rolled back.
    for (const auto& part : prepared) {
        if (!sendChatPart(target, part.text, type, part.reply)) {
            Q_EMIT sendError(m_serverId, target, QStringLiteral("Sending stopped; earlier parts may already have been submitted."));
            return false;
        }
    }
    return true;
}

bool IrcClient::sendChatPart(const QString& target, const QString& text, MessageType type,
    const std::optional<IncomingMessage>& reply) {
    const bool echoed = m_enabledCaps.contains(QStringLiteral("echo-message"));
    const QString verb = type == MessageType::NOTICE ? QStringLiteral("NOTICE") : QStringLiteral("PRIVMSG");
    const QString body = type == MessageType::ACTION ? QString(QChar(1)) + QStringLiteral("ACTION ") + text + QChar(1) : text;
    const QString command = QStringLiteral("%1 %2 :%3").arg(verb, target, body);
    if (!m_isRegistered || target.isEmpty() || command.toUtf8().size() > 510 || m_pendingMessages.size() >= 64) {
        Q_EMIT sendError(m_serverId, target, QStringLiteral("Message not submitted: disconnected, too long, or too many pending sends."));
        return false;
    }
    QMap<QString, QString> tags;
    if (reply && m_enabledCaps.contains(QStringLiteral("message-tags"))) {
        tags.insert(QStringLiteral("+reply"), reply->id);
        tags.insert(QStringLiteral("+draft/reply"), reply->id);
    }
    if (!sendRawCommand(formatIrcTags(tags) + command)) {
        Q_EMIT sendError(m_serverId, target, QStringLiteral("The connection could not accept the message."));
        return false;
    }
    // The original was sent over the wire; never retain it in local echo/pending queues.
    if (ircSecrets::message(target, text)) return true;
    IncomingMessage message;
    message.target = target;
    message.sender = m_currentNickname;
    message.content = text;
    message.type = type;
    message.isSelf = true;
    message.deliveryStatus = echoed ? QStringLiteral("pending") : QStringLiteral("submitted");
    if (reply) {
        message.replyToMessageId = reply->id;
        message.replyToSender = reply->sender;
        message.replyToPreview = reply->content.left(80);
    }
    if (echoed) m_pendingMessages.append(message);
    Q_EMIT messageReceived(m_serverId, message, false);
    return true;
}
void IrcClient::failPendingSends() {
    const auto pending = std::exchange(m_pendingMessages, {});
    for (auto message : pending) {
        message.deliveryStatus = QStringLiteral("failed");
        Q_EMIT messageUpdated(m_serverId, message.id, message);
    }
}

void IrcClient::sendTypingNotification(const QString& target, const QString& state) {
    if (!m_isRegistered || !m_enabledCaps.contains(QStringLiteral("message-tags")) || target.startsWith(QStringLiteral("* "))) {
        return;
    }
    QMap<QString, QString> tags;
    tags.insert(QStringLiteral("+typing"), state);
    sendRawCommand(QStringLiteral("%1TAGMSG %2").arg(formatIrcTags(tags), target));
}

void IrcClient::sendReaction(const QString& target, const QString& messageId, const QString& emoji) {
    if (!m_isRegistered || !m_enabledCaps.contains(QStringLiteral("message-tags")) || messageId.isEmpty() || emoji.isEmpty()) {
        return;
    }
    QMap<QString, QString> tags;
    tags.insert(QStringLiteral("+draft/reply"), messageId);
    tags.insert(QStringLiteral("+reply"), messageId);
    tags.insert(QStringLiteral("+draft/react"), emoji);
    sendRawCommand(QStringLiteral("%1TAGMSG %2").arg(formatIrcTags(tags), target));

    if (!m_enabledCaps.contains(QStringLiteral("echo-message"))) {
        Q_EMIT reactionReceived(m_serverId, target, messageId, m_currentNickname, emoji);
    }
}

void IrcClient::requestChatHistory(const QString& target, int limit) {
    if (!m_isRegistered) return;
    if (m_enabledCaps.contains(QStringLiteral("chathistory")) ||
        m_enabledCaps.contains(QStringLiteral("draft/chathistory"))) {
        sendRawCommand(QStringLiteral("CHATHISTORY LATEST %1 * %2").arg(target).arg(std::clamp(limit, 1, 500)));
    }
}

void IrcClient::changeNickname(const QString& newNick) {
    if (!newNick.trimmed().isEmpty()) {
        sendRawCommand(QStringLiteral("NICK %1").arg(newNick.trimmed()));
    }
}

void IrcClient::startSilentWhois(const QString& nickname) {
    const QString clean = nickname.trimmed();
    if (clean.isEmpty() || !m_isRegistered) return;
    if (m_pendingWhois.size() >= MAX_WHOIS_CACHE_SIZE && !m_pendingWhois.contains(identity(clean))) return;
    auto& pending = m_pendingWhois[identity(clean)];
    pending.nickname = clean;
    pending.cachedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_silentWhoisTargets.insert(identity(clean));
    sendRawCommand(QStringLiteral("WHOIS %1").arg(clean));
}

std::optional<WhoisInfo> IrcClient::getCachedWhois(const QString& nickname) const {
    const QString key = identity(nickname.trimmed());
    auto it = m_whoisCache.find(key);
    if (it != m_whoisCache.end()) {
        return it.value();
    }
    return std::nullopt;
}

void IrcClient::onKeepAliveTimeout() {
    for (int index = m_pendingMessages.size() - 1; index >= 0; --index) {
        if (QDateTime::currentMSecsSinceEpoch() - m_pendingMessages[index].timestamp < 180000) continue;
        auto message = m_pendingMessages.takeAt(index);
        message.deliveryStatus = QStringLiteral("failed");
        Q_EMIT messageUpdated(m_serverId, message.id, message);
    }
    if (m_isRegistered) {
        if (m_waitingPong && m_pingClock.hasExpired(m_pongTimeoutMs)) {
            failConnection(QStringLiteral("The server did not answer the keepalive.")); return;
        }
        if (m_waitingPong) return;
        m_lastPingSentMs = QDateTime::currentMSecsSinceEpoch();
        m_waitingPong = true;
        m_pingClock.start();
        m_pongDeadlineTimer.start(m_pongTimeoutMs);
        if (!sendRawCommand(QStringLiteral("PING :keepalive"))) failConnection(QStringLiteral("The keepalive could not be submitted."));
    }
}

void IrcClient::failConnection(const QString& error, RetryDisposition disposition) {
    m_retryDisposition = disposition;
    m_registration.failed = true;
    clearOutput(); m_waitingNickServ = false; m_nickServTimer.stop();
    ++m_generation;
    m_registrationTimer.stop();
    m_keepAliveTimer.stop();
    m_pongDeadlineTimer.stop();
    m_stateExpiryTimer.stop();
    m_isRegistered = false;
    m_readBuffer.clear();
    m_activeBatches.clear();
    m_timestampReplayIdentity.clear();
    failPendingSends();
    Q_EMIT connectionError(m_serverId, error);
    if (m_socket) m_socket->abort();
}
void IrcClient::onSocketReadyRead() {
    if (!m_socket) return;
    const auto generation = m_generation;
    int processed = 0;
    while (m_socket && m_generation == generation) {
        int newline = m_readBuffer.indexOf('\n');
        if (newline < 0) {
            if (m_readBuffer.size() >= MAX_LINE_BYTES) {
                failConnection(QStringLiteral("IRC line limit exceeded.")); return;
            }
            if (m_socket->bytesAvailable() == 0) break;
            m_readBuffer.append(m_socket->read(MAX_LINE_BYTES - m_readBuffer.size()));
            newline = m_readBuffer.indexOf('\n');
            if (newline < 0) continue;
        }
        auto bytes = m_readBuffer.left(newline);
        m_readBuffer.remove(0, newline + 1);
        if (bytes.endsWith('\r')) bytes.chop(1);
        if (bytes.startsWith('@')) {
            // The server tag budget includes the leading @ and separating space,
            // and is measured in wire bytes, not decoded QString characters.
            const auto tagEnd = bytes.indexOf(' ');
            if (tagEnd >= 8191 || (tagEnd < 0 && bytes.size() >= 8191)) {
                failConnection(QStringLiteral("IRC message tag limit exceeded.")); return;
            }
        }
        const auto line = QString::fromUtf8(bytes);
        if (!line.trimmed().isEmpty()) processLine(line);
        if (++processed >= 128) {
            QTimer::singleShot(0, this, [this, generation]() {
                if (m_generation == generation) onSocketReadyRead();
            });
            break;
        }
    }
}
void IrcClient::syncPendingUser(const QString& channel, const QString& nick, const std::optional<ChannelUser>& user) {
    if (!m_pendingNames.contains(channel)) return;
    if (user) {
        if (!m_pendingNames[channel].contains(nick)) {
            int count = 0;
            for (const auto& users : m_pendingNames) count += users.size();
            if (count >= IrcLimits::UsersPerNetwork || m_pendingNames[channel].size() >= IrcLimits::UsersPerChannel) {
                failConnection(QStringLiteral("Pending roster resource limit reached.")); return;
            }
        }
        m_pendingNames[channel].insert(nick, *user);
        m_namesRemoved[channel].remove(nick);
    } else {
        if (m_namesRemoved[channel].size() >= IrcLimits::UsersPerChannel) { failConnection(QStringLiteral("Incomplete roster state limit reached.")); return; }
        m_pendingNames[channel].remove(nick);
        m_namesRemoved[channel].insert(nick);
    }
}

void IrcClient::onSocketDisconnected() {
    clearOutput(); m_waitingNickServ = false; m_nickServTimer.stop();
    m_registrationTimer.stop();
    m_stateExpiryTimer.stop();
    m_activeBatches.clear();
    m_timestampReplayIdentity.clear();
    m_pendingNames.clear();
    m_namesRemoved.clear();
    m_namesExpires.clear();
    failPendingSends();
    m_keepAliveTimer.stop();
    m_pongDeadlineTimer.stop();
    m_isRegistered = false;
    Q_EMIT disconnected(m_serverId, QStringLiteral("Connection closed"));
}

void IrcClient::onSocketError(QAbstractSocket::SocketError error) {
    if (error == QAbstractSocket::TemporaryError) return;
    if (!m_socket) return;
    const QString err = m_socket->errorString();
    emitServerMessage(QStringLiteral("Socket error: %1").arg(err), MessageType::ERROR);
    // Retry never runs alongside a still-live failed transport. Terminal TLS disposition stays sticky.
    failConnection(err, m_retryDisposition);
}

void IrcClient::emitServerMessage(const QString& text, MessageType type, qint64 timestampMs) {
    IncomingMessage msg;
    msg.target = serverTabName();
    msg.sender = m_config.serverName.isEmpty() ? m_config.hostname : m_config.serverName;
    msg.content = text;
    msg.type = type;
    if (timestampMs > 0) msg.timestamp = timestampMs;
    Q_EMIT messageReceived(m_serverId, msg, false);
}

void IrcClient::applyMessageTags(const ParsedIrcLine& line, IncomingMessage& msg) {
    if (line.tags.isEmpty()) return;

    // 1. server-time (@time=2026-10-18T06:27:00.123Z)
    if (line.tags.contains(QStringLiteral("time"))) {
        auto parsedTime = parseServerTimeIso8601(line.tags.value(QStringLiteral("time")));
        if (parsedTime.has_value()) {
            msg.timestamp = *parsedTime;
        }
    }

    // 2. msgid (@msgid=...)
    if (line.tags.contains(QStringLiteral("msgid"))) {
        const QString id = line.tags.value(QStringLiteral("msgid")).trimmed();
        if (!id.isEmpty()) {
            msg.id = id;
        }
    }

    // A bouncer can replay timestamped chat without upstream message IDs.
    // Keep exact-payload occurrence identities across replay batches/transports.
    if (line.tags.value(QStringLiteral("msgid")).trimmed().isEmpty() &&
        (line.command == QStringLiteral("PRIVMSG") || line.command == QStringLiteral("NOTICE"))) {
        const auto rawTime = line.tags.value(QStringLiteral("time"));
        const auto parsed = parseServerTimeIso8601(rawTime);
        if (parsed && *parsed <= QDateTime::currentMSecsSinceEpoch() + 1000)
            msg.id = m_timestampReplayIdentity.next(rawTime,
                {QString::number(m_serverId), identity(msg.target), msg.sender,
                 line.command, QString::number(static_cast<int>(msg.type)), msg.content},
                historyIdentityScope(line));
    }

    // 3. account-tag (@account=...)
    if (line.tags.contains(QStringLiteral("account"))) {
        const QString acc = line.tags.value(QStringLiteral("account")).trimmed();
        if (!acc.isEmpty() && acc != QStringLiteral("*")) {
            msg.senderAccount = acc;
            // A replay describes the sender then, not their current account.
            if (!isHistoricalMessage(line)) {
                const QString nickKey = identity(line.senderNick);
                for (auto& roster : m_pendingNames) {
                    if (roster.contains(nickKey)) roster[nickKey].account = acc;
                }
                for (auto it = m_channelUsers.begin(); it != m_channelUsers.end(); ++it) {
                    if (it.value().contains(nickKey) && it.value()[nickKey].account != acc) {
                        it.value()[nickKey].account = acc;
                        syncPendingUser(it.key(), nickKey, it.value()[nickKey]);
                        auto users = it.value().values();
                        std::sort(users.begin(), users.end(), [](const ChannelUser& a, const ChannelUser& b) {
                            const int pa = userModePriority(a.primaryMode());
                            const int pb = userModePriority(b.primaryMode());
                            if (pa != pb) return pa < pb;
                            return a.nickname.compare(b.nickname, Qt::CaseInsensitive) < 0;
                        });
                        Q_EMIT userListUpdated(m_serverId, it.key(), users);
                    }
                }
            }
        }
    }

    // 4. +reply / +draft/reply
    if (line.tags.contains(QStringLiteral("+reply"))) {
        msg.replyToMessageId = line.tags.value(QStringLiteral("+reply"));
    } else if (line.tags.contains(QStringLiteral("+draft/reply"))) {
        msg.replyToMessageId = line.tags.value(QStringLiteral("+draft/reply"));
    }
}

void IrcClient::deliverIncomingMessage(const ParsedIrcLine& line, const IncomingMessage& msg) {
    if (msg.isSelf && ircSecrets::message(msg.target, msg.content)) return;
    if (msg.isSelf && !line.tags.contains(QStringLiteral("batch"))) {
        const auto serverMessageId = line.tags.value(QStringLiteral("msgid"));
        if (!serverMessageId.isEmpty()) {
            // A repeated echo must not consume the next pending identical send.
            // Deduplicate before content-based matching, not only in the UI model.
            if (m_recentSelfEchoIds.contains(serverMessageId)) return;
            m_recentSelfEchoIds.enqueue(serverMessageId);
            if (m_recentSelfEchoIds.size() > 512) m_recentSelfEchoIds.dequeue();
        }
        for (int index = 0; index < m_pendingMessages.size(); ++index) {
            const auto& pending = m_pendingMessages[index];
            if (ircIdentity(pending.target) != ircIdentity(msg.target) || pending.type != msg.type || pending.content != msg.content) continue;
            const QString localId = pending.id;
            m_pendingMessages.removeAt(index);
            auto delivered = msg;
            delivered.deliveryStatus = QStringLiteral("delivered");
            Q_EMIT messageUpdated(m_serverId, localId, delivered);
            return;
        }
    }
    // Check if this message belongs to an active IRCv3 BATCH
    if (line.tags.contains(QStringLiteral("batch"))) {
        const QString batchRef = line.tags.value(QStringLiteral("batch"));
        if (m_activeBatches.contains(batchRef)) {
            IrcBatchState& batch = m_activeBatches[batchRef];
            qint64 totalBytes = 0;
            for (const auto& state : m_activeBatches) totalBytes += state.bufferedBytes;
            const qint64 bytes = msg.content.toUtf8().size() + 512;
            if (batch.messages.size() >= MAX_BATCH_MESSAGES || totalBytes + bytes > MAX_BATCH_BYTES) {
                failConnection(QStringLiteral("IRC batch resource limit reached.")); return;
            }
            batch.bufferedBytes += bytes;
            batch.lines.append(line);
            batch.messages.append(msg);
            return;
        }
    }
    Q_EMIT messageReceived(m_serverId, msg, false);
}

void IrcClient::processLine(const QString& line) {
    const ParsedIrcLine msg = parseIrcLine(line);
    if (msg.command.isEmpty()) return;
    if (identity(msg.senderNick) == identity(m_currentNickname) &&
        !msg.senderUser.isEmpty() && !msg.senderHost.isEmpty() && !isHistoricalMessage(msg)) {
        m_selfUser = msg.senderUser;
        m_selfHost = msg.senderHost;
        if (msg.command == QStringLiteral("CHGHOST") && msg.params.size() >= 2) {
            m_selfUser = msg.params[0]; m_selfHost = msg.params[1];
        }
    }

    qint64 lineTimeMs = QDateTime::currentMSecsSinceEpoch();
    if (msg.tags.contains(QStringLiteral("time"))) {
        lineTimeMs = parseServerTimeIso8601(msg.tags.value(QStringLiteral("time"))).value_or(lineTimeMs);
    }

    bool isNumeric = false;
    const int numeric = msg.command.toInt(&isNumeric);
    if (isNumeric) {
        handleNumeric(numeric, msg);
        return;
    }

    if (msg.command == QStringLiteral("PING")) {
        const QString token = msg.params.isEmpty() ? QStringLiteral("keepalive") : msg.params.last();
        sendRawCommand(QStringLiteral("PONG :%1").arg(token));
        return;
    }
    if (msg.command == QStringLiteral("PONG")) {
        if (msg.params.contains(QStringLiteral("keepalive"))) {
            m_waitingPong = false;
            m_pongDeadlineTimer.stop();
        }
        if (m_lastPingSentMs > 0) {
            const qint64 rtt = std::max<qint64>(1, QDateTime::currentMSecsSinceEpoch() - m_lastPingSentMs);
            Q_EMIT latencyUpdated(m_serverId, rtt);
        }
        return;
    }
    if (msg.command == QStringLiteral("CAP")) {
        handleCap(msg);
        return;
    }
    if (msg.command == QStringLiteral("AUTHENTICATE")) {
        handleAuthenticate(msg);
        return;
    }
    if (msg.command == QStringLiteral("BATCH")) {
        handleBatchCommand(msg);
        return;
    }
    if (msg.command == QStringLiteral("TAGMSG")) {
        handleTagMsg(msg);
        return;
    }
    if (msg.command == QStringLiteral("PRIVMSG") && msg.params.size() >= 2) {
        const QString rawTarget = normalizedMessageTarget(msg.params[0]);
        const QString text = msg.params.last();
        const bool isSelfSender = (identity(msg.senderNick) == identity(m_currentNickname));
        const QString effectiveTarget = (identity(rawTarget) == identity(m_currentNickname))
            ? msg.senderNick
            : rawTarget;

        // Check CTCP (\x01...\x01)
        if (text.startsWith(QChar(0x01)) && text.endsWith(QChar(0x01)) && text.length() >= 2) {
            const QString ctcpBody = text.mid(1, text.length() - 2);
            if (ctcpBody.startsWith(QStringLiteral("ACTION "), Qt::CaseInsensitive) ||
                ctcpBody.compare(QStringLiteral("ACTION"), Qt::CaseInsensitive) == 0) {
                IncomingMessage actMsg;
                actMsg.target = effectiveTarget;
                actMsg.sender = msg.senderNick;
                if (!msg.senderHost.isEmpty()) actMsg.senderHost = msg.senderHost;
                actMsg.content = ctcpBody.length() > 7 ? ctcpBody.mid(7) : QString();
                actMsg.type = MessageType::ACTION;
                actMsg.isSelf = isSelfSender;
                applyMessageTags(msg, actMsg);
                deliverIncomingMessage(msg, actMsg);
            } else if (!isSelfSender) {
                const QString ctcpCmd = ctcpBody.section(' ', 0, 0).toUpper();
                const bool automaticReply = (ctcpCmd == QStringLiteral("VERSION") || ctcpCmd == QStringLiteral("PING") || ctcpCmd == QStringLiteral("TIME")) &&
                    identity(rawTarget) == identity(m_currentNickname) && !isHistoricalMessage(msg) && allowCtcpReply();
                if (automaticReply && ctcpCmd == QStringLiteral("VERSION")) {
                    sendRawCommand(QStringLiteral("NOTICE %1 :\x01" "VERSION LoungeCat C++ IRC Client\x01").arg(msg.senderNick));
                } else if (automaticReply && ctcpCmd == QStringLiteral("PING")) {
                    sendRawCommand(QStringLiteral("NOTICE %1 :\x01%2\x01").arg(msg.senderNick, ctcpBody));
                } else if (automaticReply && ctcpCmd == QStringLiteral("TIME")) {
                    sendRawCommand(QStringLiteral("NOTICE %1 :\x01" "TIME %2\x01")
                                       .arg(msg.senderNick, QDateTime::currentDateTime().toString(Qt::TextDate)));
                }
                IncomingMessage ctcpMsg;
                ctcpMsg.target = serverTabName();
                ctcpMsg.sender = msg.senderNick;
                ctcpMsg.content = QStringLiteral("Received CTCP %1 from %2").arg(ctcpBody, msg.senderNick);
                ctcpMsg.type = MessageType::CTCP;
                applyMessageTags(msg, ctcpMsg);
                deliverIncomingMessage(msg, ctcpMsg);
            }
            return;
        }

        IncomingMessage chatMsg;
        chatMsg.target = effectiveTarget;
        chatMsg.sender = msg.senderNick;
        if (!msg.senderHost.isEmpty()) chatMsg.senderHost = msg.senderHost;
        chatMsg.content = text;
        chatMsg.type = MessageType::NORMAL;
        chatMsg.isSelf = isSelfSender;
        applyMessageTags(msg, chatMsg);
        deliverIncomingMessage(msg, chatMsg);
        return;
    }
    if (msg.command == QStringLiteral("NOTICE") && msg.params.size() >= 2) {
        const QString rawTarget = normalizedMessageTarget(msg.params[0]);
        const QString text = msg.params.last();
        const auto channelTypes = m_isupport.value(QStringLiteral("CHANTYPES"), QStringLiteral("#&+!"));
        const bool channelTarget = !rawTarget.isEmpty() && channelTypes.contains(rawTarget.front());
        const bool isServerNotice = !channelTarget && (msg.senderNick.isEmpty() || msg.senderNick.contains('.') ||
                                    rawTarget == QStringLiteral("*") || rawTarget == QStringLiteral("AUTH") ||
                                    identity(rawTarget) == identity(m_currentNickname));
        IncomingMessage noticeMsg;
        noticeMsg.target = isServerNotice ? serverTabName() : rawTarget;
        noticeMsg.sender = msg.senderNick.isEmpty() ? m_config.hostname : msg.senderNick;
        noticeMsg.content = text;
        noticeMsg.type = MessageType::NOTICE;
        noticeMsg.isSelf = (identity(msg.senderNick) == identity(m_currentNickname));
        applyMessageTags(msg, noticeMsg);
        deliverIncomingMessage(msg, noticeMsg);
        return;
    }
    if (msg.command == QStringLiteral("JOIN") && !msg.params.isEmpty()) {
        const QString ch = msg.params[0];
        const QString chKey = channelKey(ch);
        const bool selfJoin = identity(msg.senderNick) == identity(m_currentNickname);
        // Repeated membership confirmations are idempotent. In particular, do not
        // replace a member's existing modes or reselect an already joined channel.
        if (selfJoin) {
            if (std::any_of(m_joinedChannels.cbegin(), m_joinedChannels.cend(),
                [&](const QString& joined) { return identity(joined) == identity(ch); })) return;
        } else if (m_channelUsers.value(chKey).contains(identity(msg.senderNick))) {
            return;
        }
        int users = 0;
        for (const auto& roster : m_channelUsers) users += roster.size();
        if ((!m_channelUsers.contains(chKey) && m_channelUsers.size() >= IrcLimits::ChannelsPerNetwork) ||
            (!m_channelUsers.value(chKey).contains(identity(msg.senderNick)) &&
             (users >= IrcLimits::UsersPerNetwork || m_channelUsers.value(chKey).size() >= IrcLimits::UsersPerChannel))) {
            failConnection(QStringLiteral("IRC roster resource limit reached.")); return;
        }
        if (selfJoin) {
            m_joinedChannels.insert(ch);
            forgetRetiredChannel(ch);
            m_channelPrivacyModes.remove(chKey);
            sendRawCommand(QStringLiteral("MODE %1").arg(ch));
            Q_EMIT channelJoined(m_serverId, ch);
            // If IRCv3 chathistory is enabled, request recent messages automatically
            if (m_enabledCaps.contains(QStringLiteral("chathistory")) ||
                m_enabledCaps.contains(QStringLiteral("draft/chathistory"))) {
                requestChatHistory(ch, 50);
            }
        } else {
            ChannelUser user;
            user.nickname = msg.senderNick;
            if (!msg.senderHost.isEmpty()) user.host = msg.senderHost;
            // IRCv3 extended-join: JOIN #channel accountname :Real Name
            if (msg.params.size() >= 2) {
                const QString acc = msg.params[1];
                if (!acc.isEmpty() && acc != QStringLiteral("*")) {
                    user.account = acc;
                }
            }
            if (msg.params.size() >= 3) {
                user.realname = msg.params[2];
            }
            if (msg.tags.contains(QStringLiteral("account"))) {
                const QString tagAcc = msg.tags.value(QStringLiteral("account"));
                if (!tagAcc.isEmpty() && tagAcc != QStringLiteral("*")) user.account = tagAcc;
            }
            m_channelUsers[chKey].insert(identity(msg.senderNick), user);
            syncPendingUser(chKey, identity(msg.senderNick), user);
            Q_EMIT userJoined(m_serverId, ch, user);

            IncomingMessage joinMsg;
            joinMsg.target = ch;
            joinMsg.sender = msg.senderNick;
            if (!msg.senderHost.isEmpty()) joinMsg.senderHost = msg.senderHost;
            joinMsg.content = user.account.has_value()
                ? QStringLiteral("%1 (%2) has joined %3").arg(msg.senderNick, *user.account, ch)
                : QStringLiteral("%1 has joined %2").arg(msg.senderNick, ch);
            joinMsg.type = MessageType::JOIN;
            applyMessageTags(msg, joinMsg);
            deliverIncomingMessage(msg, joinMsg);
        }
        return;
    }
    if (msg.command == QStringLiteral("PART") && !msg.params.isEmpty()) {
        const QString ch = msg.params[0];
        const QString reason = msg.params.size() >= 2 ? msg.params[1] : QString();
        const QString chKey = channelKey(ch);
        if (identity(msg.senderNick) == identity(m_currentNickname)) {
            retireChannelState(ch);
            Q_EMIT channelParted(m_serverId, ch);
        } else {
            if (m_channelUsers.contains(chKey)) m_channelUsers[chKey].remove(identity(msg.senderNick));
            syncPendingUser(chKey, identity(msg.senderNick), std::nullopt);
            Q_EMIT userParted(m_serverId, ch, msg.senderNick, reason);

            IncomingMessage partMsg;
            partMsg.target = ch;
            partMsg.sender = msg.senderNick;
            partMsg.content = reason.isEmpty()
                ? QStringLiteral("%1 has left %2").arg(msg.senderNick, ch)
                : QStringLiteral("%1 has left %2 (%3)").arg(msg.senderNick, ch, reason);
            partMsg.type = MessageType::PART;
            applyMessageTags(msg, partMsg);
            deliverIncomingMessage(msg, partMsg);
        }
        return;
    }
    if (msg.command == QStringLiteral("QUIT")) {
        const QString reason = msg.params.isEmpty() ? QString() : msg.params.first();
        const QString nickKey = identity(msg.senderNick);
        for (const auto& channel : m_pendingNames.keys()) syncPendingUser(channel, nickKey, std::nullopt);
        for (auto it = m_channelUsers.begin(); it != m_channelUsers.end(); ++it) {
            if (it.value().contains(nickKey)) {
                it.value().remove(nickKey);
                IncomingMessage quitMsg;
                quitMsg.target = it.key();
                quitMsg.sender = msg.senderNick;
                quitMsg.content = reason.isEmpty()
                    ? QStringLiteral("%1 has quit").arg(msg.senderNick)
                    : QStringLiteral("%1 has quit (%2)").arg(msg.senderNick, reason);
                quitMsg.type = MessageType::QUIT;
                applyMessageTags(msg, quitMsg);
                deliverIncomingMessage(msg, quitMsg);
            }
        }
        Q_EMIT userQuit(m_serverId, msg.senderNick, reason);
        return;
    }
    if (msg.command == QStringLiteral("KICK") && msg.params.size() >= 2) {
        const QString ch = msg.params[0];
        const QString victim = msg.params[1];
        const QString reason = msg.params.size() >= 3 ? msg.params[2] : QString();
        if (m_channelUsers.contains(identity(ch))) m_channelUsers[identity(ch)].remove(identity(victim));
        syncPendingUser(identity(ch), identity(victim), std::nullopt);
        if (identity(victim) == identity(m_currentNickname)) {
            retireChannelState(ch);
            Q_EMIT userListUpdated(m_serverId, ch, {});
        }
        Q_EMIT userKicked(m_serverId, ch, victim, msg.senderNick, reason);

        IncomingMessage kickMsg;
        kickMsg.target = ch;
        kickMsg.sender = msg.senderNick;
        kickMsg.content = reason.isEmpty()
            ? QStringLiteral("%1 was kicked from %2 by %3").arg(victim, ch, msg.senderNick)
            : QStringLiteral("%1 was kicked from %2 by %3 (%4)").arg(victim, ch, msg.senderNick, reason);
        kickMsg.type = MessageType::KICK;
        applyMessageTags(msg, kickMsg);
        deliverIncomingMessage(msg, kickMsg);
        return;
    }
    if (msg.command == QStringLiteral("NICK") && !msg.params.isEmpty()) {
        const QString oldNick = msg.senderNick;
        const QString newNick = msg.params[0];
        if (identity(oldNick) == identity(m_currentNickname)) {
            m_currentNickname = newNick;
        }
        const QString oldKey = identity(oldNick);
        const QString newKey = identity(newNick);
        for (const auto& channel : m_pendingNames.keys()) if (m_pendingNames[channel].contains(oldKey)) {
            auto user = m_pendingNames[channel].value(oldKey);
            user.nickname = newNick;
            syncPendingUser(channel, oldKey, std::nullopt);
            syncPendingUser(channel, newKey, user);
        }
        for (auto it = m_channelUsers.begin(); it != m_channelUsers.end(); ++it) {
            if (it.value().contains(oldKey)) {
                ChannelUser u = it.value().take(oldKey);
                u.nickname = newNick;
                it.value().insert(newKey, u);
                syncPendingUser(it.key(), oldKey, std::nullopt);
                syncPendingUser(it.key(), newKey, u);

                IncomingMessage nickMsg;
                nickMsg.target = it.key();
                nickMsg.sender = newNick;
                nickMsg.content = QStringLiteral("%1 is now known as %2").arg(oldNick, newNick);
                nickMsg.type = MessageType::NICK;
                applyMessageTags(msg, nickMsg);
                deliverIncomingMessage(msg, nickMsg);
            }
        }
        Q_EMIT nicknameChanged(m_serverId, oldNick, newNick);
        return;
    }
    if (msg.command == QStringLiteral("TOPIC") && msg.params.size() >= 2) {
        const QString ch = msg.params[0];
        const QString newTopic = msg.params[1];
        Q_EMIT topicChanged(m_serverId, ch, newTopic, msg.senderNick);

        IncomingMessage topicMsg;
        topicMsg.target = ch;
        topicMsg.sender = msg.senderNick;
        topicMsg.content = QStringLiteral("%1 changed topic to: %2").arg(msg.senderNick, newTopic);
        topicMsg.type = MessageType::TOPIC;
        applyMessageTags(msg, topicMsg);
        deliverIncomingMessage(msg, topicMsg);
        return;
    }
    if (msg.command == QStringLiteral("MODE") && msg.params.size() >= 2) {
        const QString target = msg.params[0];
        QStringList modeArgs = msg.params.mid(1);
        if (target.startsWith('#') || target.startsWith('&')) {
            handleModeChange(target, msg.senderNick, modeArgs, lineTimeMs);
        } else {
            emitServerMessage(QStringLiteral("Mode %1 on %2").arg(modeArgs.join(' '), target), MessageType::MODE, lineTimeMs);
        }
        return;
    }
    auto emitUpdatedChannelsForUser = [this](const QString& nickKey, auto updater) {
        for (auto& roster : m_pendingNames) if (roster.contains(nickKey)) updater(roster[nickKey]);
        for (auto it = m_channelUsers.begin(); it != m_channelUsers.end(); ++it) {
            if (it.value().contains(nickKey)) {
                updater(it.value()[nickKey]);
                syncPendingUser(it.key(), nickKey, it.value()[nickKey]);
                QList<ChannelUser> users = it.value().values();
                std::sort(users.begin(), users.end(), [](const ChannelUser& a, const ChannelUser& b) {
                    const int pa = userModePriority(a.primaryMode());
                    const int pb = userModePriority(b.primaryMode());
                    if (pa != pb) return pa < pb;
                    return a.nickname.compare(b.nickname, Qt::CaseInsensitive) < 0;
                });
                Q_EMIT userListUpdated(m_serverId, it.key(), users);
            }
        }
    };

    // IRCv3 away-notify: :nick!user@host AWAY [:message]
    if (msg.command == QStringLiteral("AWAY")) {
        const bool isAway = !msg.params.isEmpty() && !msg.params[0].isEmpty();
        emitUpdatedChannelsForUser(identity(msg.senderNick), [isAway](ChannelUser& u) {
            u.isAway = isAway;
        });
        Q_EMIT userDetailsUpdated(m_serverId, msg.senderNick);
        return;
    }
    // IRCv3 account-notify: :nick!user@host ACCOUNT <accountname|*>
    if (msg.command == QStringLiteral("ACCOUNT") && !msg.params.isEmpty()) {
        const QString acc = msg.params[0];
        const std::optional<QString> newAcc = (acc.isEmpty() || acc == QStringLiteral("*"))
            ? std::nullopt
            : std::make_optional(acc);
        emitUpdatedChannelsForUser(identity(msg.senderNick), [newAcc](ChannelUser& u) {
            u.account = newAcc;
        });
        Q_EMIT userDetailsUpdated(m_serverId, msg.senderNick);
        return;
    }
    // IRCv3 chghost: :nick!user@host CHGHOST <newuser> <newhost>
    if (msg.command == QStringLiteral("CHGHOST") && msg.params.size() >= 2) {
        const QString newHost = msg.params[1];
        emitUpdatedChannelsForUser(identity(msg.senderNick), [newHost](ChannelUser& u) {
            u.host = newHost;
        });
        Q_EMIT userDetailsUpdated(m_serverId, msg.senderNick);
        return;
    }
    // IRCv3 setname: :nick!user@host SETNAME :<newrealname>
    if (msg.command == QStringLiteral("SETNAME") && !msg.params.isEmpty()) {
        const QString newReal = msg.params[0];
        emitUpdatedChannelsForUser(identity(msg.senderNick), [newReal](ChannelUser& u) {
            u.realname = newReal;
        });
        Q_EMIT userDetailsUpdated(m_serverId, msg.senderNick);
        return;
    }
    // IRCv3 invite-notify: :inviter!u@h INVITE <target> <#channel>
    if (msg.command == QStringLiteral("INVITE") && msg.params.size() >= 2) {
        const QString invited = msg.params[0];
        const QString chan = msg.params[1];
        const QString text = QStringLiteral("%1 invited %2 to %3").arg(msg.senderNick, invited, chan);
        if (identity(invited) == identity(m_currentNickname)) {
            emitServerMessage(text, MessageType::NOTICE, lineTimeMs);
        } else {
            IncomingMessage invMsg;
            invMsg.target = chan;
            invMsg.sender = msg.senderNick;
            invMsg.content = text;
            invMsg.type = MessageType::SERVER;
            applyMessageTags(msg, invMsg);
            deliverIncomingMessage(msg, invMsg);
        }
        return;
    }
    // IRCv3 standard-replies: FAIL / WARN / NOTE <command> <code> [context...] :<description>
    if (msg.command == QStringLiteral("FAIL") || msg.command == QStringLiteral("WARN") || msg.command == QStringLiteral("NOTE")) {
        const QString summary = QStringLiteral("[%1] %2").arg(msg.command, msg.params.join(' '));
        const MessageType mType = (msg.command == QStringLiteral("FAIL")) ? MessageType::ERROR : MessageType::SERVER;
        emitServerMessage(summary, mType, lineTimeMs);
        return;
    }
    if (msg.command == QStringLiteral("ERROR")) {
        const QString err = msg.params.isEmpty() ? QStringLiteral("IRC Error") : msg.params.join(' ');
        emitServerMessage(err, MessageType::ERROR, lineTimeMs);
        Q_EMIT connectionError(m_serverId, err);
        return;
    }
}

QString IrcClient::historyIdentityScope(const ParsedIrcLine& message) const {
    auto reference = message.tags.value(QStringLiteral("batch"));
    QString scope;
    for (int depth = 0; depth < MAX_ACTIVE_BATCHES && !reference.isEmpty(); ++depth) {
        const auto batch = m_activeBatches.constFind(reference);
        if (batch == m_activeBatches.cend()) break;
        if (batch->historical) scope = reference;
        reference = batch->openingTags.value(QStringLiteral("batch"));
    }
    return scope;
}

bool IrcClient::isHistoricalMessage(const ParsedIrcLine& message) const {
    auto reference = message.tags.value(QStringLiteral("batch"));
    for (int depth = 0; depth < MAX_ACTIVE_BATCHES && !reference.isEmpty(); ++depth) {
        const auto batch = m_activeBatches.constFind(reference);
        if (batch == m_activeBatches.cend()) return false;
        if (batch->batchType == QStringLiteral("chathistory") ||
            batch->batchType == QStringLiteral("draft/chathistory") ||
            batch->batchType == QStringLiteral("znc.in/playback")) return true;
        reference = batch->openingTags.value(QStringLiteral("batch"));
    }
    return false;
}

bool IrcClient::allowCtcpReply() {
    // A global budget cannot be bypassed by cycling sender nicks, and leaves the send queue for chat.
    if (!m_ctcpReplyClock.isValid() || m_ctcpReplyClock.hasExpired(10000)) {
        m_ctcpReplyClock.start(); m_ctcpReplies = 0;
    }
    if (m_ctcpReplies >= 3) return false;
    ++m_ctcpReplies;
    return true;
}

void IrcClient::handleCap(const ParsedIrcLine& msg) {
    if (msg.params.size() < 2) return;
    const QString subCmd = msg.params[1].toUpper();
    const bool hasMorePages = (msg.params.size() >= 4 && msg.params[2] == QStringLiteral("*"));
    const QString capsStr = msg.params.last();

    auto parseCapsMap = [](const QString& rawCaps) {
        QMap<QString, QString> map;
        for (const QString& token : rawCaps.split(' ', Qt::SkipEmptyParts)) {
            const int eq = token.indexOf('=');
            const auto name = (eq == -1 ? token : token.left(eq)).toLower();
            // Unknown extensions need no retained state. Keeping only capabilities
            // we can negotiate bounds memory without letting a long vendor list
            // crowd out a late SASL or other supported advertisement.
            if (name != QStringLiteral("sasl") && !desiredCapabilities().contains(name)) continue;
            map.insert(name, eq == -1 ? QString() : token.mid(eq + 1));
        }
        return map;
    };

    if (subCmd == QStringLiteral("LS")) {
        const auto pageCaps = parseCapsMap(capsStr);
        for (auto it = pageCaps.begin(); it != pageCaps.end(); ++it) {
            if (m_availableCaps.size() < 128 || m_availableCaps.contains(it.key()))
                m_availableCaps.insert(it.key().left(128), it.value().left(512));
        }
        if (!hasMorePages) {
            requestSupportedCaps(m_availableCaps);
        }
    } else if (subCmd == QStringLiteral("NEW")) {
        // IRCv3 cap-notify dynamic capability advertisement
        const auto newCaps = parseCapsMap(capsStr);
        for (auto it = newCaps.begin(); it != newCaps.end(); ++it) {
            if (m_availableCaps.size() < 128 || m_availableCaps.contains(it.key()))
                m_availableCaps.insert(it.key().left(128), it.value().left(512));
        }
        requestSupportedCaps(newCaps);
    } else if (subCmd == QStringLiteral("DEL")) {
        const auto delCaps = parseCapsMap(capsStr);
        for (auto it = delCaps.begin(); it != delCaps.end(); ++it) {
            m_availableCaps.remove(it.key());
            m_enabledCaps.remove(it.key());
        }
        Q_EMIT capabilitiesUpdated(m_serverId, m_enabledCaps);
    } else if (subCmd == QStringLiteral("ACK") || subCmd == QStringLiteral("NAK")) {
        QSet<QString> replyCaps;
        for (const auto& token : capsStr.split(' ', Qt::SkipEmptyParts)) {
            const auto name = token.section('=', 0, 0).toLower();
            if (name.size() <= 128 && replyCaps.size() < 128) replyCaps.insert(name);
        }
        if (subCmd == QStringLiteral("ACK")) {
            for (const auto& cap : replyCaps) if (m_capAckParts.size() < 128) m_capAckParts.insert(cap);
            if (hasMorePages) return;
            replyCaps = m_capAckParts;
            m_capAckParts.clear();
        }
        // ACK/NAK are replies to an outstanding REQ, not unsolicited state changes.
        // Match the complete request and apply multi-line ACKs atomically.
        const auto pending = std::find(m_pendingCapRequests.begin(), m_pendingCapRequests.end(), replyCaps);
        if (pending == m_pendingCapRequests.end()) return;
        m_pendingCapRequests.erase(pending);
        if (subCmd == QStringLiteral("ACK")) {
            for (const auto& cap : replyCaps) {
                if (cap.startsWith('-')) m_enabledCaps.remove(cap.mid(1));
                else {
                    if (m_enabledCaps.size() < 128) m_enabledCaps.insert(cap);
                    if (cap == QStringLiteral("sasl") && m_config.useSasl && !m_registration.authenticated) {
                        m_saslInProgress = true;
                        sendRawCommand(QStringLiteral("AUTHENTICATE PLAIN"));
                    }
                }
            }
            Q_EMIT capabilitiesUpdated(m_serverId, m_enabledCaps);
        } else if (m_config.useSasl && replyCaps.contains(QStringLiteral("sasl"))) {
            failConnection(QStringLiteral("Required SASL capability was rejected."), RetryDisposition::UserActionRequired);
            return;
        }
        finishCapNegotiationIfReady();
    }
}

void IrcClient::requestSupportedCaps(const QMap<QString, QString>& offeredCaps) {


    QStringList toRequest;
    auto pending = [this](const QString& cap) {
        return std::any_of(m_pendingCapRequests.cbegin(), m_pendingCapRequests.cend(),
                           [&cap](const auto& request) { return request.contains(cap); });
    };
    if (m_config.useSasl && !m_registration.authenticated &&
        (!offeredCaps.contains(QStringLiteral("sasl")) || !IrcRegistration::offersPlain(offeredCaps.value(QStringLiteral("sasl")))) && m_capNegotiating) {
        failConnection(QStringLiteral("Server does not offer the required SASL PLAIN authentication."), RetryDisposition::UserActionRequired);
        return;
    }
    if (m_config.useSasl && offeredCaps.contains(QStringLiteral("sasl")) && !m_enabledCaps.contains(QStringLiteral("sasl")) && !pending(QStringLiteral("sasl"))) {
        toRequest.append(QStringLiteral("sasl"));
    }
    for (const QString& cap : desiredCapabilities()) {
        if (offeredCaps.contains(cap) && !m_enabledCaps.contains(cap) && !pending(cap)) {
            toRequest.append(cap);
        }
    }

    if (toRequest.isEmpty()) {
        finishCapNegotiationIfReady();
        return;
    }

    m_pendingCapRequests.append(QSet<QString>(toRequest.cbegin(), toRequest.cend()));
    sendRawCommand(QStringLiteral("CAP REQ :%1").arg(toRequest.join(' ')));
}

void IrcClient::finishCapNegotiationIfReady() {
    if (m_capNegotiating && m_pendingCapRequests.isEmpty() && !m_saslInProgress) {
        m_capNegotiating = false;
        sendRawCommand(QStringLiteral("CAP END"));
    }
}

void IrcClient::handleAuthenticate(const ParsedIrcLine& msg) {
    if (m_saslInProgress && !msg.params.isEmpty() && msg.params[0] == QStringLiteral("+")) {
        QByteArray authBytes;
        const QByteArray userUtf8 = (m_config.saslUsername.isEmpty() ? m_currentNickname : m_config.saslUsername).toUtf8();
        authBytes.append(userUtf8);
        authBytes.append('\0');
        authBytes.append(userUtf8);
        authBytes.append('\0');
        authBytes.append(m_config.saslPassword.toUtf8());

        const QString b64 = QString::fromLatin1(authBytes.toBase64());
        // Per IRCv3 SASL specification, split base64 into 400-byte chunks
        if (b64.isEmpty()) {
            sendRawCommand(QStringLiteral("AUTHENTICATE +"));
            return;
        }
        int offset = 0;
        while (offset < b64.length()) {
            const QString chunk = b64.mid(offset, 400);
            sendRawCommand(QStringLiteral("AUTHENTICATE ") + chunk);
            offset += chunk.length();
            if (chunk.length() == 400 && offset == b64.length()) {
                sendRawCommand(QStringLiteral("AUTHENTICATE +"));
            }
        }
    }
}

void IrcClient::handleBatchCommand(const ParsedIrcLine& msg) {
    if (msg.params.isEmpty()) return;
    const QString refParam = msg.params[0];
    if (refParam.length() < 2) return;

    const bool opening = refParam.startsWith('+');
    const bool closing = refParam.startsWith('-');
    const QString refId = refParam.mid(1);

    if (opening && msg.params.size() >= 2) {
        if (m_activeBatches.contains(refId) || m_activeBatches.size() >= MAX_ACTIVE_BATCHES) {
            failConnection(QStringLiteral("Too many incomplete IRC batches.")); return;
        }
        IrcBatchState state;
        state.refId = refId;
        state.batchType = msg.params[1];
        state.target = msg.params.size() >= 3 ? msg.params[2] : serverTabName();
        state.openingTags = msg.tags;
        // Capture inherited playback context while the parent is still active.
        // Batch types are opaque, case-sensitive identifiers, not IRC commands.
        state.historical = state.batchType == QStringLiteral("chathistory") ||
                           state.batchType == QStringLiteral("draft/chathistory") ||
                           state.batchType == QStringLiteral("znc.in/playback") ||
                           isHistoricalMessage(msg);
        m_activeBatches.insert(refId, state);
        return;
    }

    if (closing && m_activeBatches.contains(refId)) {
        const IrcBatchState finished = m_activeBatches.take(refId);
        m_timestampReplayIdentity.end(refId);
        if ((finished.batchType == QStringLiteral("draft/multiline") ||
             finished.batchType == QStringLiteral("multiline")) &&
            !finished.messages.isEmpty()) {
            // Combine multiline batch into a single message
            IncomingMessage combined = finished.messages.first();
            if (finished.openingTags.contains(QStringLiteral("msgid"))) {
                combined.id = finished.openingTags.value(QStringLiteral("msgid"));
            }
            if (finished.openingTags.contains(QStringLiteral("time"))) {
                combined.timestamp = parseServerTimeIso8601(finished.openingTags.value(QStringLiteral("time")))
                                         .value_or(combined.timestamp);
            }
            QString fullText;
            for (int i = 0; i < finished.messages.size(); ++i) {
                const bool concat = (i > 0) &&
                    (finished.lines[i].tags.contains(QStringLiteral("draft/multiline-concat")) ||
                     finished.lines[i].tags.contains(QStringLiteral("multiline-concat")));
                if (i > 0 && !concat) {
                    fullText.append('\n');
                }
                fullText.append(finished.messages[i].content);
            }
            combined.content = fullText;
            Q_EMIT messageReceived(m_serverId, combined, finished.historical);
        } else {
            for (const IncomingMessage& m : finished.messages) {
                Q_EMIT messageReceived(m_serverId, m, finished.historical);
            }
        }
    }
}

void IrcClient::handleTagMsg(const ParsedIrcLine& msg) {
    if (msg.params.isEmpty()) return;
    const QString rawTarget = normalizedMessageTarget(msg.params[0]);
    const QString effectiveTarget = (identity(rawTarget) == identity(m_currentNickname))
        ? msg.senderNick
        : rawTarget;

    // 1. Typing indicator (+typing / +draft/typing)
    QString typingState;
    if (msg.tags.contains(QStringLiteral("+typing"))) {
        typingState = msg.tags.value(QStringLiteral("+typing")).toLower();
    } else if (msg.tags.contains(QStringLiteral("+draft/typing"))) {
        typingState = msg.tags.value(QStringLiteral("+draft/typing")).toLower();
    }
    if (!isHistoricalMessage(msg) && !typingState.isEmpty() && identity(msg.senderNick) != identity(m_currentNickname)) {
        Q_EMIT typingUserUpdated(m_serverId, effectiveTarget, msg.senderNick, typingState);
    }

    // 2. Message reaction (+draft/react / +react)
    QString emoji;
    if (msg.tags.contains(QStringLiteral("+draft/react"))) {
        emoji = msg.tags.value(QStringLiteral("+draft/react"));
    } else if (msg.tags.contains(QStringLiteral("+react"))) {
        emoji = msg.tags.value(QStringLiteral("+react"));
    }
    QString replyMsgId;
    if (msg.tags.contains(QStringLiteral("+reply"))) {
        replyMsgId = msg.tags.value(QStringLiteral("+reply"));
    } else if (msg.tags.contains(QStringLiteral("+draft/reply"))) {
        replyMsgId = msg.tags.value(QStringLiteral("+draft/reply"));
    }
    if (!emoji.isEmpty() && !replyMsgId.isEmpty()) {
        Q_EMIT reactionReceived(m_serverId, effectiveTarget, replyMsgId, msg.senderNick, emoji);
    }
}

void IrcClient::handleModeChange(
    const QString& channel,
    const QString& sender,
    const QStringList& modeArgs,
    qint64 timestampMs
) {
    if (modeArgs.isEmpty()) return;
    const QString modeStr = modeArgs[0];
    int argIdx = 1;
    bool adding = true;
    const QString chKey = channelKey(channel);

    for (QChar c : modeStr) {
        if (c == '+') {
            adding = true;
            continue;
        }
        if (c == '-') {
            adding = false;
            continue;
        }
        if (QStringLiteral("kisp").contains(c) && m_channelPrivacyModes.contains(chKey)) {
            if (adding) m_channelPrivacyModes[chKey].insert(c);
            else m_channelPrivacyModes[chKey].remove(c);
        }
        auto uMode = userModeFromModeChar(c);
        if (uMode.has_value() && argIdx < modeArgs.size()) {
            const QString targetNick = modeArgs[argIdx++];
            const QString nickKey = identity(targetNick);
            if (m_channelUsers.contains(chKey) && m_channelUsers[chKey].contains(nickKey)) {
                ChannelUser& u = m_channelUsers[chKey][nickKey];
                if (adding) u.modes.insert(*uMode);
                else u.modes.remove(*uMode);
                syncPendingUser(chKey, nickKey, u);
                Q_EMIT userModeChanged(m_serverId, channel, u.nickname, u.modes);
            }
        } else if (QStringLiteral("beIkovl").contains(c) && argIdx < modeArgs.size()) {
            if (adding || c != 'l') {
                ++argIdx;
            }
        }
    }

    IncomingMessage modeMsg;
    modeMsg.target = channel;
    modeMsg.sender = sender;
    modeMsg.content = QStringLiteral("%1 sets mode %2").arg(sender, modeStr.contains('k') ? modeStr : modeArgs.join(' '));
    modeMsg.type = MessageType::MODE;
    modeMsg.timestamp = timestampMs;
    Q_EMIT messageReceived(m_serverId, modeMsg, false);
}

void IrcClient::parseIsupportTokens(const QStringList& tokens) {
    for (int i = 0; i + 1 < tokens.size(); ++i) {
        const QString tok = tokens[i];
        if (tok.startsWith('-')) {
            m_isupport.remove(tok.mid(1).toUpper());
        } else {
            const int eq = tok.indexOf('=');
            const QString key = (eq == -1 ? tok : tok.left(eq)).toUpper();
            // The admission budget must not freeze updates of existing keys.
            if (m_isupport.size() >= 128 && !m_isupport.contains(key)) continue;
            m_isupport.insert(key, eq == -1 ? QString() : tok.mid(eq + 1));
        }
    }
    changeCaseMapping(m_isupport.value(QStringLiteral("CASEMAPPING"), QStringLiteral("rfc1459")));
    Q_EMIT isupportUpdated(m_serverId, m_isupport);
}

void IrcClient::handleNumeric(int numeric, const ParsedIrcLine& msg) {
    const QStringList args = msg.params.size() > 1 ? msg.params.mid(1) : QStringList{};
    static const QSet<int> whoisNumerics{301, 311, 314, 312, 313, 317, 319, 330, 307, 671};
    if (whoisNumerics.contains(numeric) && !args.isEmpty()) {
        const auto key = identity(args.first());
        if (!m_pendingWhois.contains(key) && m_pendingWhois.size() >= MAX_WHOIS_CACHE_SIZE) return;
        if (!m_pendingWhois.contains(key)) { m_pendingWhois[key].cachedAtMs = QDateTime::currentMSecsSinceEpoch(); m_pendingWhois[key].nickname = args.first(); }
    }
    qint64 tsMs = QDateTime::currentMSecsSinceEpoch();
    if (msg.tags.contains(QStringLiteral("time"))) {
        tsMs = parseServerTimeIso8601(msg.tags.value(QStringLiteral("time"))).value_or(tsMs);
    }

    // These replies name a conversation. Keep them beside that conversation,
    // but do not guess which send caused ambiguous WHOIS/JOIN/PART errors.
    if ((numeric == 401 || numeric == 403 || numeric == 404 || numeric == 442) &&
        args.size() >= 2 && !args[0].isEmpty() && args[0] != QStringLiteral("*") &&
        std::none_of(args[0].begin(), args[0].end(), [](QChar c) {
            return c.isSpace() || c == ',' || c == ':' || c.unicode() == 0;
        })) {
        if (numeric == 404 && !isHistoricalMessage(msg)) {
            // ERR_CANNOTSENDTOCHAN rejects PRIVMSG, not NOTICE. Unlabelled IRC
            // responses are correlated in send order within the named target.
            for (int index = 0; index < m_pendingMessages.size(); ++index) {
                const auto& pending = m_pendingMessages[index];
                if (identity(pending.target) != identity(args[0]) ||
                    (pending.type != MessageType::NORMAL && pending.type != MessageType::ACTION)) continue;
                auto rejected = m_pendingMessages.takeAt(index);
                rejected.deliveryStatus = QStringLiteral("failed");
                Q_EMIT messageUpdated(m_serverId, rejected.id, rejected);
                break;
            }
        }
        IncomingMessage error;
        error.target = args[0];
        error.sender = m_config.serverName.isEmpty() ? m_config.hostname : m_config.serverName;
        error.content = QStringLiteral("[%1] %2").arg(numeric).arg(args.join(' '));
        error.type = MessageType::ERROR;
        error.timestamp = tsMs;
        deliverIncomingMessage(msg, error);
        return;
    }

    switch (numeric) {
        case 1: { // RPL_WELCOME
            if (!msg.params.isEmpty() && msg.params[0] != QStringLiteral("*")) {
                m_currentNickname = msg.params[0];
            }
            emitServerMessage(args.join(' '), MessageType::SERVER, tsMs);
            handleWelcomeSequence();
            break;
        }
        case 5: { // RPL_ISUPPORT
            parseIsupportTokens(args);
            emitServerMessage(args.join(' '), MessageType::SERVER, tsMs);
            break;
        }
        case 2:
        case 3:
        case 4:
        case 250:
        case 251:
        case 252:
        case 253:
        case 254:
        case 255:
        case 265:
        case 266: {
            emitServerMessage(args.join(' '), MessageType::SERVER, tsMs);
            break;
        }
        case 375:
        case 372:
        case 376: { // MOTD
            emitServerMessage(args.join(' '), MessageType::MOTD, tsMs);
            break;
        }
        case 321: { // RPL_LISTSTART
            m_channelListAccumulator.clear();
            m_channelListTruncated = false;
            break;
        }
        case 322: { // RPL_LIST: <channel> <# visible> :<topic>
            if (args.size() >= 2) {
                ChannelListItem item;
                item.channel = args[0];
                item.userCount = args[1].toInt();
                item.topic = args.size() >= 3 ? args[2] : QString();
                if (m_channelListAccumulator.size() < 2000) m_channelListAccumulator.append(item);
                else m_channelListTruncated = true;
            }
            break;
        }
        case 323: { // RPL_LISTEND
            Q_EMIT channelListUpdated(m_serverId, m_channelListAccumulator, true, m_channelListTruncated);
            break;
        }
        case 324: { // Current mode snapshot; arguments can contain credentials.
            if (args.size() >= 2 && m_channelNames.contains(identity(args[0]))) {
                QSet<QChar> privacy;
                bool adding = true;
                for (QChar c : args[1]) {
                    if (c == '+') adding = true;
                    else if (c == '-') adding = false;
                    else if (adding && QStringLiteral("kisp").contains(c)) privacy.insert(c);
                }
                m_channelPrivacyModes[identity(args[0])] = privacy;
            }
            emitServerMessage(QStringLiteral("Channel modes updated."));
            break;
        }
        case 331: { // RPL_NOTOPIC: <channel> :No topic is set
            if (args.size() >= 2) {
                Q_EMIT topicChanged(m_serverId, args[0], QString(), QString());
            }
            break;
        }
        case 332: { // RPL_TOPIC: <channel> :<topic>
            if (args.size() >= 2) {
                const QString ch = args[0];
                const QString topic = args[1];
                Q_EMIT topicChanged(m_serverId, ch, topic, QString());
            }
            break;
        }
        case 333: { // RPL_TOPICWHOTIME: <channel> <nick> <setat>
            break;
        }
        case 353: { // RPL_NAMREPLY: (=/*/@) <channel> :[prefix]nick[!user@host] ...
            if (args.size() >= 3) {
                const QString ch = args[1];
                if (isRetiredChannel(ch)) break;
                const QString chKey = channelKey(ch);
                const QStringList rawNicks = args.last().split(' ', Qt::SkipEmptyParts);
                if (!m_pendingNames.contains(chKey) && m_pendingNames.size() >= IrcLimits::ChannelsPerNetwork) {
                    failConnection(QStringLiteral("Too many channel rosters.")); return;
                }
                int pendingUsers = 0;
                for (const auto& users : m_pendingNames) pendingUsers += users.size();
                auto& pendingMap = m_pendingNames[chKey];
                if (!m_namesExpires.contains(chKey)) m_namesExpires.insert(chKey, QDeadlineTimer(30000));
                for (const QString& raw : rawNicks) {
                    ChannelUser user;
                    int idx = 0;
                    while (idx < raw.length()) {
                        auto mode = userModeFromPrefix(raw[idx]);
                        if (mode.has_value()) {
                            user.modes.insert(*mode);
                            ++idx;
                        } else {
                            break;
                        }
                    }
                    // Support IRCv3 userhost-in-names (!user@host)
                    const QString rem = raw.mid(idx);
                    const int bang = rem.indexOf('!');
                    if (bang != -1) {
                        user.nickname = rem.left(bang);
                        const int at = rem.indexOf('@', bang);
                        if (at != -1) user.host = rem.mid(at + 1);
                    } else {
                        user.nickname = rem;
                    }
                    if (!user.nickname.isEmpty()) {
                        const auto key = identity(user.nickname);
                        if (!m_namesRemoved[chKey].contains(key) && !pendingMap.contains(key)) {
                            if (pendingUsers >= IrcLimits::UsersPerNetwork || pendingMap.size() >= IrcLimits::UsersPerChannel) {
                                failConnection(QStringLiteral("User-list snapshot resource limit reached.")); return;
                            }
                            pendingMap.insert(key, user);
                            ++pendingUsers;
                        }
                    }
                }
            }
            break;
        }
        case 366: { // RPL_ENDOFNAMES: <channel> :End of /NAMES list.
            if (!args.isEmpty()) {
                const QString ch = args[0];
                if (isRetiredChannel(ch)) break;
                const QString chKey = channelKey(ch);
                if (m_pendingNames.contains(chKey)) {
                    int total = 0;
                    for (const auto& users : m_channelUsers) total += users.size();
                    total -= m_channelUsers.value(chKey).size();
                    if ((!m_channelUsers.contains(chKey) && m_channelUsers.size() >= IrcLimits::ChannelsPerNetwork) ||
                        total + m_pendingNames[chKey].size() > IrcLimits::UsersPerNetwork) {
                        failConnection(QStringLiteral("IRC roster resource limit reached.")); return;
                    }
                    m_channelUsers[chKey] = m_pendingNames.take(chKey);
                    m_namesRemoved.remove(chKey);
                    m_namesExpires.remove(chKey);
                }
                QList<ChannelUser> users = m_channelUsers.value(chKey).values();
                std::sort(users.begin(), users.end(), [](const ChannelUser& a, const ChannelUser& b) {
                    const int pa = userModePriority(a.primaryMode());
                    const int pb = userModePriority(b.primaryMode());
                    if (pa != pb) return pa < pb;
                    return a.nickname.compare(b.nickname, Qt::CaseInsensitive) < 0;
                });
                Q_EMIT userListUpdated(m_serverId, ch, users);
            }
            break;
        }
        // WHOIS / WHOWAS Numerics
        case 301: { // RPL_AWAY: <nick> :<away message>
            if (args.size() >= 2) {
                const QString key = identity(args[0]);
                m_pendingWhois[key].nickname = args[0];
                m_pendingWhois[key].awayMessage = args[1];
            }
            break;
        }
        case 311:
        case 314: { // RPL_WHOISUSER / RPL_WHOWASUSER: <nick> <user> <host> * :<realname>
            if (args.size() >= 5) {
                const QString key = identity(args[0]);
                WhoisInfo& info = m_pendingWhois[key];
                info.nickname = args[0];
                info.username = args[1];
                info.hostname = args[2];
                info.realname = args[4];
            }
            break;
        }
        case 312: { // RPL_WHOISSERVER: <nick> <server> :<server info>
            if (args.size() >= 2) {
                const QString key = identity(args[0]);
                m_pendingWhois[key].server = args[1];
                if (args.size() >= 3) m_pendingWhois[key].serverInfo = args[2];
            }
            break;
        }
        case 313: { // RPL_WHOISOPERATOR
            if (!args.isEmpty()) {
                m_pendingWhois[identity(args[0])].isOperator = true;
            }
            break;
        }
        case 317: { // RPL_WHOISIDLE: <nick> <idle_secs> [signon_epoch] :seconds idle...
            if (args.size() >= 2) {
                const QString key = identity(args[0]);
                m_pendingWhois[key].idleSeconds = args[1].toLongLong();
                if (args.size() >= 3) {
                    bool ok = false;
                    qint64 signon = args[2].toLongLong(&ok);
                    if (ok) m_pendingWhois[key].signonTime = signon;
                }
            }
            break;
        }
        case 319: { // RPL_WHOISCHANNELS: <nick> :@#chan1 +#chan2
            if (args.size() >= 2) {
                const QString key = identity(args[0]);
                m_pendingWhois[key].channels.append(args[1].split(' ', Qt::SkipEmptyParts));
                if (m_pendingWhois[key].channels.size() > 128) m_pendingWhois[key].channels = m_pendingWhois[key].channels.mid(0, 128);
            }
            break;
        }
        case 330: { // RPL_WHOISACCOUNT: <nick> <account> :is logged in as
            if (args.size() >= 2 && identity(args[0]) == identity(m_currentNickname) && args[1] != "*") confirmAccount();
            if (args.size() >= 2) {
                const QString key = identity(args[0]);
                m_pendingWhois[key].accountName = args[1];
                m_pendingWhois[key].isRegistered = true;
            }
            break;
        }
        case 307: { // RPL_WHOISREGNICK
            if (!args.isEmpty()) {
                m_pendingWhois[identity(args[0])].isRegistered = true;
            }
            break;
        }
        case 671: { // RPL_WHOISSECURE
            if (!args.isEmpty()) {
                m_pendingWhois[identity(args[0])].isSecureConnection = true;
            }
            break;
        }
        case 318:
        case 369: { // RPL_ENDOFWHOIS / RPL_ENDOFWHOWAS
            if (!args.isEmpty()) {
                const QString nick = args[0];
                const QString key = identity(nick);
                if (m_pendingWhois.contains(key)) {
                    WhoisInfo info = m_pendingWhois.take(key);
                    info.isComplete = true;
                    info.cachedAtMs = QDateTime::currentMSecsSinceEpoch();
                    if (m_whoisCache.size() >= MAX_WHOIS_CACHE_SIZE) {
                        m_whoisCache.erase(m_whoisCache.begin());
                    }
                    m_whoisCache.insert(key, info);
                    Q_EMIT whoisUpdated(m_serverId, info.nickname, info);

                    if (m_silentWhoisTargets.contains(key)) {
                        m_silentWhoisTargets.remove(key);
                    } else {
                        emitServerMessage(info.toFormattedMessage(), MessageType::WHOIS, tsMs);
                    }
                }
            }
            break;
        }
        case 432: { // Invalid configured nickname is not a transient network failure.
            emitServerMessage(QStringLiteral("Nickname rejected (432): choose a valid nickname."), MessageType::ERROR, tsMs);
            if (!m_isRegistered) failConnection(QStringLiteral("Nickname rejected (432): update the saved nickname."), RetryDisposition::UserActionRequired);
            break;
        }
        case 433: { // ERR_NICKNAMEINUSE
            handleNicknameInUse();
            break;
        }
        case 900: { // RPL_LOGGEDIN
            if (m_waitingNickServ) confirmAccount();
            emitServerMessage(args.join(' '), MessageType::SERVER, tsMs);
            break;
        }
        case 907: // Already authenticated
        case 903: { // RPL_SASLSUCCESS
            m_registration.authenticated = true;
            emitServerMessage(args.join(' '), MessageType::SERVER, tsMs);
            m_saslInProgress = false;
            finishCapNegotiationIfReady();
            break;
        }
        case 902:
        case 904:
        case 905:
        case 906: { // ERR_SASLFAIL
            emitServerMessage(QStringLiteral("SASL Error: ") + args.join(' '), MessageType::ERROR, tsMs);
            m_saslInProgress = false;
            if (m_config.useSasl) failConnection(QStringLiteral("Required SASL authentication failed. Check the saved credentials."), RetryDisposition::UserActionRequired);
            break;
        }
        case 464:
        case 465:
            failConnection(QStringLiteral("IRC authentication or host authorization was rejected."), RetryDisposition::UserActionRequired);
            break;
        default: {
            if (numeric >= 400 && numeric <= 599) {
                emitServerMessage(QStringLiteral("[%1] %2").arg(numeric).arg(args.join(' ')), MessageType::ERROR, tsMs);
            } else if (!args.isEmpty()) {
                emitServerMessage(args.join(' '), MessageType::SERVER, tsMs);
            }
            break;
        }
    }
}

void IrcClient::handleWelcomeSequence() {
    if (!m_registration.mayBecomeReady()) {
        failConnection(QStringLiteral("Server registered before required SASL authentication completed."), RetryDisposition::UserActionRequired); return;
    }
    if (m_isRegistered) return;
    m_registrationTimer.stop(); m_isRegistered = true; m_capNegotiating = false;
    m_keepAliveTimer.start();
    Q_EMIT connected(m_serverId);
    if (!m_config.nickServPassword.isEmpty() && !m_registration.authenticated) {
        m_waitingNickServ = true; m_nickServClock.start(); m_nickServTimer.start();
        QString command = m_config.nickServCommand.trimmed();
        if (command.isEmpty()) command = QStringLiteral("PRIVMSG NickServ :IDENTIFY {password}");
        command.replace(QStringLiteral("{password}"), m_config.nickServPassword);
        if (command.startsWith('/')) command.remove(0, 1);
        if (!sendRawCommand(command)) { failConnection(QStringLiteral("Identification command could not be queued.")); return; }
        startSilentWhois(m_currentNickname);
    } else finishPostConnect();
}

void IrcClient::confirmAccount() {
    if (!m_waitingNickServ) return;
    m_waitingNickServ = false; m_nickServTimer.stop();
    finishPostConnect();
}

void IrcClient::finishPostConnect() {
    if (!m_isRegistered || m_registration.postConnectStarted) return;
    m_registration.postConnectStarted = true;
    for (QString command : m_config.onConnectCommands.split('\n', Qt::SkipEmptyParts)) {
        command = command.trimmed(); if (command.startsWith('/')) command.remove(0, 1);
        if (!command.isEmpty() && !sendRawCommand(command)) { failConnection(QStringLiteral("Automatic commands exceeded the bounded outgoing queue.")); return; }
    }
    QSet<QString> joined;
    for (const auto& raw : m_config.autoJoinChannels.split(',', Qt::SkipEmptyParts)) {
        const auto name = raw.trimmed();
        if (name.isEmpty() || joined.contains(identity(name))) continue;
        joined.insert(identity(name));
        QString key;
        for (auto it = m_config.channelKeys.begin(); it != m_config.channelKeys.end(); ++it)
            if (identity(it.key()) == identity(name)) { key = it.value(); break; }
        joinChannel(name, key);
    }
}

void IrcClient::handleNicknameInUse() {
    if (m_isRegistered) {
        emitServerMessage(QStringLiteral("Nickname change rejected; the confirmed nickname is unchanged."), MessageType::ERROR); return;
    }
    if (++m_registration.nicknameAttempts > 1 || m_config.altNickname.trimmed().isEmpty() ||
        identity(m_config.altNickname) == identity(m_currentNickname)) {
        failConnection(QStringLiteral("Nickname is unavailable. The configured alternate was exhausted; choose a nickname or wait for the previous session to expire."), RetryDisposition::UserActionRequired); return;
    }
    // The 001 or own NICK event confirms the resulting identity.
    sendRawCommand(QStringLiteral("NICK %1").arg(m_config.altNickname.trimmed()));
}

} // namespace loungecat
