#pragma once

#include <QString>
#include <QJsonObject>
#include <QMap>
#include <optional>

namespace loungecat {

enum class ProxyType {
    NONE,
    SOCKS5,
    HTTP
};

QString proxyTypeToString(ProxyType type);
ProxyType proxyTypeFromString(const QString& str);

struct ServerConfig {
    qint64 id = 0;
    QString serverName;
    QString hostname;
    int port = 6697;
    bool useSsl = true;
    QString nickname;
    QString altNickname;
    QString username;
    QString realName;
    bool useSasl = false;
    QString saslUsername;
    QString saslPassword;
    QString autoJoinChannels;
    QMap<QString, QString> channelKeys;
    QString onConnectCommands;
    QString nickServPassword;
    QString nickServCommand;
    std::optional<QString> pinnedCertFingerprint;
    bool trustOnFirstUse = false;
    bool autoConnect = true;
    ProxyType proxyType = ProxyType::NONE;
    QString proxyHost;
    int proxyPort = 1080;
    QString proxyUsername;
    QString proxyPassword;

    QJsonObject toJson() const;
    static ServerConfig fromJson(const QJsonObject& obj);

    bool operator==(const ServerConfig& other) const = default;
};

} // namespace loungecat
