#include "data/model/ServerConfig.h"
#include <QJsonValue>

namespace loungecat {

QString proxyTypeToString(ProxyType type) {
    switch (type) {
        case ProxyType::SOCKS5: return QStringLiteral("SOCKS5");
        case ProxyType::HTTP:   return QStringLiteral("HTTP");
        case ProxyType::NONE:
        default:                return QStringLiteral("NONE");
    }
}

ProxyType proxyTypeFromString(const QString& str) {
    const QString upper = str.trimmed().toUpper();
    if (upper == QStringLiteral("SOCKS5")) return ProxyType::SOCKS5;
    if (upper == QStringLiteral("HTTP"))   return ProxyType::HTTP;
    return ProxyType::NONE;
}

QJsonObject ServerConfig::toJson() const {
    QJsonObject obj;
    obj[QStringLiteral("id")] = id;
    obj[QStringLiteral("serverName")] = serverName;
    obj[QStringLiteral("hostname")] = hostname;
    obj[QStringLiteral("port")] = port;
    obj[QStringLiteral("useSsl")] = useSsl;
    obj[QStringLiteral("nickname")] = nickname;
    obj[QStringLiteral("altNickname")] = altNickname;
    obj[QStringLiteral("username")] = username;
    obj[QStringLiteral("realName")] = realName;
    obj[QStringLiteral("useSasl")] = useSasl;
    obj[QStringLiteral("saslUsername")] = saslUsername;
    obj[QStringLiteral("saslPassword")] = saslPassword;
    obj[QStringLiteral("autoJoinChannels")] = autoJoinChannels;
    QJsonObject keys;
    for (auto it = channelKeys.begin(); it != channelKeys.end(); ++it) keys[it.key()] = it.value();
    obj[QStringLiteral("channelKeys")] = keys;
    obj[QStringLiteral("onConnectCommands")] = onConnectCommands;
    obj[QStringLiteral("nickServPassword")] = nickServPassword;
    obj[QStringLiteral("nickServCommand")] = nickServCommand;
    obj[QStringLiteral("pinnedCertFingerprint")] = pinnedCertFingerprint.has_value()
        ? QJsonValue(*pinnedCertFingerprint)
        : QJsonValue(QJsonValue::Null);
    obj[QStringLiteral("trustOnFirstUse")] = trustOnFirstUse;
    obj[QStringLiteral("autoConnect")] = autoConnect;
    obj[QStringLiteral("proxyType")] = proxyTypeToString(proxyType);
    obj[QStringLiteral("proxyHost")] = proxyHost;
    obj[QStringLiteral("proxyPort")] = proxyPort;
    obj[QStringLiteral("proxyUsername")] = proxyUsername;
    obj[QStringLiteral("proxyPassword")] = proxyPassword;
    return obj;
}

ServerConfig ServerConfig::fromJson(const QJsonObject& obj) {
    ServerConfig cfg;
    cfg.id = obj.value(QStringLiteral("id")).toInteger(0);
    cfg.serverName = obj.value(QStringLiteral("serverName")).toString();
    cfg.hostname = obj.value(QStringLiteral("hostname")).toString();
    cfg.port = obj.value(QStringLiteral("port")).toInt(6697);
    cfg.useSsl = obj.value(QStringLiteral("useSsl")).toBool(true);
    cfg.nickname = obj.value(QStringLiteral("nickname")).toString();
    cfg.altNickname = obj.value(QStringLiteral("altNickname")).toString();
    cfg.username = obj.value(QStringLiteral("username")).toString();
    cfg.realName = obj.value(QStringLiteral("realName")).toString();
    cfg.useSasl = obj.value(QStringLiteral("useSasl")).toBool(false);
    cfg.saslUsername = obj.value(QStringLiteral("saslUsername")).toString();
    cfg.saslPassword = obj.value(QStringLiteral("saslPassword")).toString();
    cfg.autoJoinChannels = obj.value(QStringLiteral("autoJoinChannels")).toString();
    const auto keys = obj.value(QStringLiteral("channelKeys")).toObject();
    if (keys.size() <= 128) for (auto it = keys.begin(); it != keys.end(); ++it)
        if (it.value().isString() && it.key().size() <= 200 && it.value().toString().size() <= 200) cfg.channelKeys.insert(it.key(), it.value().toString());
    cfg.onConnectCommands = obj.value(QStringLiteral("onConnectCommands")).toString();
    cfg.nickServPassword = obj.value(QStringLiteral("nickServPassword")).toString();
    cfg.nickServCommand = obj.value(QStringLiteral("nickServCommand")).toString();
    if (obj.contains(QStringLiteral("pinnedCertFingerprint")) &&
        !obj.value(QStringLiteral("pinnedCertFingerprint")).isNull()) {
        QString fp = obj.value(QStringLiteral("pinnedCertFingerprint")).toString();
        if (!fp.isEmpty()) {
            cfg.pinnedCertFingerprint = fp;
        }
    }
    cfg.trustOnFirstUse = obj.value(QStringLiteral("trustOnFirstUse")).toBool(false);
    cfg.autoConnect = obj.value(QStringLiteral("autoConnect")).toBool(true);
    cfg.proxyType = proxyTypeFromString(obj.value(QStringLiteral("proxyType")).toString(QStringLiteral("NONE")));
    cfg.proxyHost = obj.value(QStringLiteral("proxyHost")).toString();
    cfg.proxyPort = obj.value(QStringLiteral("proxyPort")).toInt(1080);
    if (cfg.proxyPort <= 0) cfg.proxyPort = 1080;
    cfg.proxyUsername = obj.value(QStringLiteral("proxyUsername")).toString();
    cfg.proxyPassword = obj.value(QStringLiteral("proxyPassword")).toString();
    return cfg;
}

} // namespace loungecat
