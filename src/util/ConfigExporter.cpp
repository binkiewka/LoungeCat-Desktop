#include "util/ConfigExporter.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QSaveFile>
#include <cmath>

namespace loungecat {

QString ConfigExporter::serializeBackup(const UserPreferences& preferences, const QList<ServerConfig>& servers) {
    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("timestamp")] = QDateTime::currentMSecsSinceEpoch();
    root[QStringLiteral("secretsOmitted")] = true;
    auto portablePreferences = preferences.toJson();
    portablePreferences.remove(QStringLiteral("imgbbApiKey"));
    root[QStringLiteral("preferences")] = portablePreferences;

    QJsonArray srvArr;
    for (const auto& s : servers) {
        auto portableServer = s.toJson();
        // Raw commands may embed credentials, so portable backups omit them too.
        for (const auto& key : {QStringLiteral("saslPassword"), QStringLiteral("nickServPassword"),
                QStringLiteral("proxyPassword"), QStringLiteral("channelKeys"),
                QStringLiteral("onConnectCommands"), QStringLiteral("nickServCommand")})
            portableServer.remove(key);
        portableServer[QStringLiteral("autoConnect")] = false;
        srvArr.append(portableServer);
    }
    root[QStringLiteral("servers")] = srvArr;

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

std::optional<BackupConfig> ConfigExporter::parseBackup(const QString& jsonString) {
    if (jsonString.toUtf8().size() > 1024 * 1024) return std::nullopt;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(jsonString.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("version")).toDouble(-1) != 1 ||
        !root.value(QStringLiteral("preferences")).isObject() || !root.value(QStringLiteral("servers")).isArray() ||
        !root.value(QStringLiteral("timestamp")).isDouble() || root.value(QStringLiteral("timestamp")).toDouble() < 0 ||
        !std::isfinite(root.value(QStringLiteral("timestamp")).toDouble()) ||
        root.value(QStringLiteral("timestamp")).toDouble() > 9007199254740991.0 ||
        std::floor(root.value(QStringLiteral("timestamp")).toDouble()) != root.value(QStringLiteral("timestamp")).toDouble() ||
        root.value(QStringLiteral("servers")).toArray().size() > 64 ||
        !UserPreferences::validateJson(root.value(QStringLiteral("preferences")).toObject())) return std::nullopt;
    if (root.contains(QStringLiteral("secretsOmitted")) &&
        !root.value(QStringLiteral("secretsOmitted")).isBool()) return std::nullopt;
    BackupConfig cfg;
    cfg.secretsOmitted = root.value(QStringLiteral("secretsOmitted")).toBool(false);
    cfg.version = root.value(QStringLiteral("version")).toInt(1);
    cfg.timestamp = root.value(QStringLiteral("timestamp")).toInteger(0);
    if (root.contains(QStringLiteral("preferences")) && root.value(QStringLiteral("preferences")).isObject()) {
        cfg.preferences = UserPreferences::fromJson(root.value(QStringLiteral("preferences")).toObject());
    }
    if (root.contains(QStringLiteral("servers")) && root.value(QStringLiteral("servers")).isArray()) {
        for (const auto& v : root.value(QStringLiteral("servers")).toArray()) {
            if (!v.isObject()) return std::nullopt;
            const auto object = v.toObject();
            if (object.contains(QStringLiteral("id")) && object.value(QStringLiteral("id")).toInteger(-1) < 0)
                return std::nullopt;
            if (!object.value(QStringLiteral("hostname")).isString() || !object.value(QStringLiteral("nickname")).isString()) return std::nullopt;
            for (const auto& key : {QStringLiteral("port"), QStringLiteral("proxyPort")}) if (object.contains(key)) {
                const auto value = object.value(key);
                const auto port = value.toDouble(-1);
                if (!value.isDouble() || port < 1 || port > 65535 || std::floor(port) != port) return std::nullopt;
            }
            const auto defaults = ServerConfig{}.toJson();
            for (auto entry = object.begin(); entry != object.end(); ++entry) {
                if (entry.value().isString() && entry.value().toString().size() > 8192) return std::nullopt;
                if (defaults.contains(entry.key()) && !defaults.value(entry.key()).isNull() &&
                    defaults.value(entry.key()).type() != entry.value().type()) return std::nullopt;
            }
            if (object.contains(QStringLiteral("proxyType")) &&
                !QStringList{QStringLiteral("NONE"), QStringLiteral("HTTP"), QStringLiteral("SOCKS5")}
                    .contains(object.value(QStringLiteral("proxyType")).toString().trimmed().toUpper())) return std::nullopt;
            const auto server = ServerConfig::fromJson(v.toObject());
            if (server.hostname.trimmed().isEmpty() || server.nickname.trimmed().isEmpty() ||
                server.port < 1 || server.port > 65535 || server.proxyPort < 1 || server.proxyPort > 65535 ||
                server.hostname.size() > 253 || server.nickname.size() > 64) return std::nullopt;
            cfg.servers.append(server);
        }
    }
    return cfg;
}

bool ConfigExporter::exportToFile(const QString& filePath, const UserPreferences& preferences, const QList<ServerConfig>& servers) {
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) return false;
    const QByteArray bytes = serializeBackup(preferences, servers).toUtf8();
    return file.write(bytes) == bytes.size() && file.commit();
}

std::optional<BackupConfig> ConfigExporter::importFromFile(const QString& filePath) {
    QFile file(filePath);
    if (file.size() > 1024 * 1024) return std::nullopt;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }
    return parseBackup(QString::fromUtf8(file.read(1024 * 1024 + 1)));
}

} // namespace loungecat
