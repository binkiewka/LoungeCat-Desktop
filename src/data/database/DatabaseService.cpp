#include "data/database/DatabaseService.h"
#include "util/PlatformUtils.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDateTime>
#include <QUuid>
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <QMutexLocker>
#include <QFile>
#include <QJsonDocument>

namespace loungecat {

namespace {

QString nonNullString(const QString& s) { return s.isNull() ? QStringLiteral("") : s; }

QVariant nullableString(const QString& s) {
    return s.isEmpty() ? QVariant(QMetaType::fromType<QString>()) : QVariant(s);
}

QVariant nullableOptString(const std::optional<QString>& s) {
    return (!s.has_value() || s->isEmpty()) ? QVariant(QMetaType::fromType<QString>()) : QVariant(*s);
}

ServerConfig rowToServerConfig(const QSqlQuery& q) {
    ServerConfig cfg;
    cfg.id = q.value(QStringLiteral("id")).toLongLong();
    cfg.serverName = q.value(QStringLiteral("server_name")).toString();
    cfg.hostname = q.value(QStringLiteral("hostname")).toString();
    cfg.port = q.value(QStringLiteral("port")).toInt();
    if (cfg.port <= 0) cfg.port = 6697;
    cfg.useSsl = q.value(QStringLiteral("use_ssl")).toInt() != 0;
    cfg.nickname = q.value(QStringLiteral("nickname")).toString();
    cfg.altNickname = q.value(QStringLiteral("alt_nickname")).toString();
    cfg.username = q.value(QStringLiteral("username")).toString();
    cfg.realName = q.value(QStringLiteral("real_name")).toString();
    cfg.useSasl = q.value(QStringLiteral("use_sasl")).toInt() != 0;
    cfg.saslUsername = q.value(QStringLiteral("sasl_username")).toString();
    cfg.saslPassword = q.value(QStringLiteral("sasl_password")).toString();
    cfg.autoJoinChannels = q.value(QStringLiteral("auto_join_channels")).toString();
    const auto keys = QJsonDocument::fromJson(q.value(QStringLiteral("channel_keys")).toByteArray()).object();
    if (keys.size() <= 128) for (auto it = keys.begin(); it != keys.end(); ++it)
        if (it.value().isString()) cfg.channelKeys.insert(it.key(), it.value().toString());
    cfg.onConnectCommands = q.value(QStringLiteral("on_connect_commands")).toString();
    cfg.nickServPassword = q.value(QStringLiteral("nick_serv_password")).toString();
    cfg.nickServCommand = q.value(QStringLiteral("nick_serv_command")).toString();

    const QVariant fpVar = q.value(QStringLiteral("pinned_cert_fingerprint"));
    if (!fpVar.isNull() && !fpVar.toString().isEmpty()) {
        cfg.pinnedCertFingerprint = fpVar.toString();
    }
    cfg.trustOnFirstUse = q.value(QStringLiteral("trust_on_first_use")).toInt() != 0;
    const QVariant autoConnVar = q.value(QStringLiteral("auto_connect"));
    cfg.autoConnect = autoConnVar.isNull() ? true : (autoConnVar.toInt() != 0);

    cfg.proxyType = proxyTypeFromString(q.value(QStringLiteral("proxy_type")).toString());
    cfg.proxyHost = q.value(QStringLiteral("proxy_host")).toString();
    const int pPort = q.value(QStringLiteral("proxy_port")).toInt();
    cfg.proxyPort = (pPort > 0) ? pPort : 1080;
    cfg.proxyUsername = q.value(QStringLiteral("proxy_username")).toString();
    cfg.proxyPassword = q.value(QStringLiteral("proxy_password")).toString();
    return cfg;
}

} // namespace

DatabaseService::DatabaseService(const QString& dbFilePath)
    : m_dbFilePath(dbFilePath.isEmpty() ? PlatformUtils::resolveDatabasePath() : dbFilePath),
      m_connectionName(QStringLiteral("loungecat_sqlite_") + QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    initialize();
}

DatabaseService::~DatabaseService() {
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool DatabaseService::initialize() {
    if (QThread::currentThread() != m_ownerThread) return false;
    QMutexLocker locker(&m_mutex);
    QFileInfo fi(m_dbFilePath);
    if (!fi.dir().exists()) {
        fi.dir().mkpath(QStringLiteral("."));
    }

    QSqlDatabase db = QSqlDatabase::contains(m_connectionName)
        ? QSqlDatabase::database(m_connectionName)
        : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_dbFilePath);

    if (!db.open()) {
        m_ready = false;
        m_lastError = db.lastError().text();
        return false;
    }

    if (!QFile::setPermissions(m_dbFilePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        m_ready = false; m_lastError = QStringLiteral("Database credentials require owner-only file permissions."); db.close(); return false;
    }
    QSqlQuery query(db);
    const bool ok = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS server_configs ("
        "id INTEGER PRIMARY KEY,"
        "server_name TEXT NOT NULL,"
        "hostname TEXT NOT NULL,"
        "port INTEGER NOT NULL DEFAULT 6697,"
        "use_ssl INTEGER NOT NULL DEFAULT 1,"
        "nickname TEXT NOT NULL,"
        "alt_nickname TEXT DEFAULT '',"
        "username TEXT,"
        "real_name TEXT,"
        "use_sasl INTEGER NOT NULL DEFAULT 0,"
        "sasl_username TEXT,"
        "sasl_password TEXT,"
        "auto_join_channels TEXT,"
        "on_connect_commands TEXT,"
        "nick_serv_password TEXT,"
        "nick_serv_command TEXT,"
        "pinned_cert_fingerprint TEXT,"
        "trust_on_first_use INTEGER DEFAULT 0,"
        "auto_connect INTEGER DEFAULT 1,"
        "proxy_type TEXT,"
        "proxy_host TEXT,"
        "proxy_port INTEGER,"
        "proxy_username TEXT,"
        "proxy_password TEXT"
        ")"
    ));

    m_ready = ok && runMigrations();
    m_lastError = m_ready ? QString() : QStringLiteral("Database initialization or migration failed.");
    return m_ready;
}

bool DatabaseService::runMigrations() {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery pragma(db);
    if (!pragma.exec(QStringLiteral("PRAGMA table_info(server_configs)"))) {
        return false;
    }

    QSet<QString> existingCols;
    while (pragma.next()) {
        existingCols.insert(pragma.value(1).toString().toLower());
    }

    const QList<std::pair<QString, QString>> migrations = {
        {QStringLiteral("alt_nickname"), QStringLiteral("TEXT DEFAULT ''")},
        {QStringLiteral("use_sasl"), QStringLiteral("INTEGER NOT NULL DEFAULT 0")},
        {QStringLiteral("sasl_username"), QStringLiteral("TEXT")},
        {QStringLiteral("sasl_password"), QStringLiteral("TEXT")},
        {QStringLiteral("on_connect_commands"), QStringLiteral("TEXT")},
        {QStringLiteral("pinned_cert_fingerprint"), QStringLiteral("TEXT")},
        {QStringLiteral("trust_on_first_use"), QStringLiteral("INTEGER DEFAULT 0")},
        {QStringLiteral("auto_connect"), QStringLiteral("INTEGER DEFAULT 1")},
        {QStringLiteral("proxy_type"), QStringLiteral("TEXT")},
        {QStringLiteral("proxy_host"), QStringLiteral("TEXT")},
        {QStringLiteral("proxy_port"), QStringLiteral("INTEGER")},
        {QStringLiteral("proxy_username"), QStringLiteral("TEXT")},
        {QStringLiteral("proxy_password"), QStringLiteral("TEXT")},
        {QStringLiteral("channel_keys"), QStringLiteral("TEXT NOT NULL DEFAULT '{}'")}
    };

    if (!db.transaction()) return false;
    for (const auto& [col, def] : migrations) {
        if (!existingCols.contains(col)) {
            QSqlQuery alter(db);
            if (!alter.exec(QStringLiteral("ALTER TABLE server_configs ADD COLUMN %1 %2").arg(col, def))) {
                db.rollback();
                return false;
            }
        }
    }
    return db.commit();
}

QList<ServerConfig> DatabaseService::getAllServerConfigs() {
    if (!isReady()) return {};
    QMutexLocker locker(&m_mutex);
    QList<ServerConfig> list;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT * FROM server_configs ORDER BY id ASC"))) {
        while (q.next()) {
            list.append(rowToServerConfig(q));
        }
    }
    return list;
}

std::optional<ServerConfig> DatabaseService::getServerConfig(qint64 id) {
    if (!isReady()) return std::nullopt;
    QMutexLocker locker(&m_mutex);
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT * FROM server_configs WHERE id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        return rowToServerConfig(q);
    }
    return std::nullopt;
}

qint64 DatabaseService::insertServerConfig(const ServerConfig& config) {
    if (!isReady()) return -1;
    QMutexLocker locker(&m_mutex);
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    qint64 id = config.id != 0 ? config.id : QDateTime::currentMSecsSinceEpoch();

    // Ensure unique timestamp ID if called rapidly
    while (true) {
        QSqlQuery check(db);
        check.prepare(QStringLiteral("SELECT 1 FROM server_configs WHERE id = ?"));
        check.addBindValue(id);
        if (!check.exec()) { m_lastError = check.lastError().text(); return -1; }
        if (!check.next()) break;
        ++id;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO server_configs ("
        "id, server_name, hostname, port, use_ssl, nickname, alt_nickname, username, real_name, "
        "use_sasl, sasl_username, sasl_password, auto_join_channels, on_connect_commands, "
        "nick_serv_password, nick_serv_command, pinned_cert_fingerprint, trust_on_first_use, "
        "auto_connect, proxy_type, proxy_host, proxy_port, proxy_username, proxy_password, channel_keys"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    ));

    q.addBindValue(id);
    q.addBindValue(nonNullString(config.serverName));
    q.addBindValue(nonNullString(config.hostname));
    q.addBindValue(config.port);
    q.addBindValue(config.useSsl ? 1 : 0);
    q.addBindValue(nonNullString(config.nickname));
    q.addBindValue(nonNullString(config.altNickname));
    q.addBindValue(nullableString(config.username));
    q.addBindValue(nullableString(config.realName));
    q.addBindValue(config.useSasl ? 1 : 0);
    q.addBindValue(nullableString(config.saslUsername));
    q.addBindValue(nullableString(config.saslPassword));
    q.addBindValue(nullableString(config.autoJoinChannels));
    q.addBindValue(nullableString(config.onConnectCommands));
    q.addBindValue(nullableString(config.nickServPassword));
    q.addBindValue(nullableString(config.nickServCommand));
    q.addBindValue(nullableOptString(config.pinnedCertFingerprint));
    q.addBindValue(config.trustOnFirstUse ? 1 : 0);
    q.addBindValue(config.autoConnect ? 1 : 0);
    q.addBindValue(proxyTypeToString(config.proxyType));
    q.addBindValue(nullableString(config.proxyHost));
    q.addBindValue(config.proxyPort);
    q.addBindValue(nullableString(config.proxyUsername));
    q.addBindValue(nullableString(config.proxyPassword));
    q.addBindValue(QJsonDocument(config.toJson().value(QStringLiteral("channelKeys")).toObject()).toJson(QJsonDocument::Compact));

    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return -1;
    }
    return id;
}

bool DatabaseService::updateServerConfig(const ServerConfig& config) {
    if (!isReady()) return false;
    QMutexLocker locker(&m_mutex);
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "UPDATE server_configs SET "
        "server_name = ?, hostname = ?, port = ?, use_ssl = ?, nickname = ?, alt_nickname = ?, "
        "username = ?, real_name = ?, use_sasl = ?, sasl_username = ?, sasl_password = ?, "
        "auto_join_channels = ?, on_connect_commands = ?, nick_serv_password = ?, "
        "nick_serv_command = ?, pinned_cert_fingerprint = ?, trust_on_first_use = ?, "
        "auto_connect = ?, proxy_type = ?, proxy_host = ?, proxy_port = ?, "
        "proxy_username = ?, proxy_password = ?, channel_keys = ? "
        "WHERE id = ?"
    ));

    q.addBindValue(nonNullString(config.serverName));
    q.addBindValue(nonNullString(config.hostname));
    q.addBindValue(config.port);
    q.addBindValue(config.useSsl ? 1 : 0);
    q.addBindValue(nonNullString(config.nickname));
    q.addBindValue(nonNullString(config.altNickname));
    q.addBindValue(nullableString(config.username));
    q.addBindValue(nullableString(config.realName));
    q.addBindValue(config.useSasl ? 1 : 0);
    q.addBindValue(nullableString(config.saslUsername));
    q.addBindValue(nullableString(config.saslPassword));
    q.addBindValue(nullableString(config.autoJoinChannels));
    q.addBindValue(nullableString(config.onConnectCommands));
    q.addBindValue(nullableString(config.nickServPassword));
    q.addBindValue(nullableString(config.nickServCommand));
    q.addBindValue(nullableOptString(config.pinnedCertFingerprint));
    q.addBindValue(config.trustOnFirstUse ? 1 : 0);
    q.addBindValue(config.autoConnect ? 1 : 0);
    q.addBindValue(proxyTypeToString(config.proxyType));
    q.addBindValue(nullableString(config.proxyHost));
    q.addBindValue(config.proxyPort);
    q.addBindValue(nullableString(config.proxyUsername));
    q.addBindValue(nullableString(config.proxyPassword));
    q.addBindValue(QJsonDocument(config.toJson().value(QStringLiteral("channelKeys")).toObject()).toJson(QJsonDocument::Compact));
    q.addBindValue(config.id);

    return q.exec() && q.numRowsAffected() == 1;
}

bool DatabaseService::deleteServerConfig(qint64 id) {
    if (!isReady()) return false;
    QMutexLocker locker(&m_mutex);
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM server_configs WHERE id = ?"));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() == 1;
}

} // namespace loungecat
