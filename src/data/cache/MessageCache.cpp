#include "data/cache/MessageCache.h"
#include "util/PlatformUtils.h"
#include "util/IrcIdentity.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QThread>
#include <algorithm>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QMutexLocker>


namespace loungecat {
namespace {
QRecursiveMutex cacheFileMutex;
qint64 messageBytes(const IncomingMessage& message) {
    qint64 result = 1024 + 2 * (message.content.size() + message.sender.size() + message.target.size() + message.id.size());
    for (const auto& preview : message.urlPreviews)
        result += 2 * (preview.url.size() + preview.title.value_or(QString()).size() +
            preview.description.value_or(QString()).size());
    for (const auto& senders : message.reactions) for (const auto& sender : senders) result += 128 + sender.size() * 2;
    return result;
}
void trimCleanWindow(CachedChannel& channel) {
    qint64 bytes = 0;
    int retained = 0;
    for (auto it = channel.messages.crbegin(); it != channel.messages.crend(); ++it) {
        bytes += messageBytes(*it);
        if (++retained > MessageCache::MAX_IN_MEMORY_MESSAGES || bytes > 1024 * 1024) { --retained; break; }
    }
    if (channel.messages.size() > retained) channel.messages.remove(0, channel.messages.size() - retained);
}
void mergeMessages(QList<IncomingMessage>& destination, const QList<IncomingMessage>& incoming) {
    QMap<QString, int> positions;
    for (int index = 0; index < destination.size(); ++index) positions.insert(destination[index].id, index);
    for (const auto& message : incoming) {
        const auto existing = positions.constFind(message.id);
        if (existing == positions.constEnd()) {
            positions.insert(message.id, destination.size());
            destination.append(message);
        } else destination[*existing] = message;
    }
    std::stable_sort(destination.begin(), destination.end(), [](const auto& left, const auto& right) {
        return left.timestamp < right.timestamp;
    });
    if (destination.size() > MessageCache::MAX_MESSAGES_PER_CHANNEL)
        destination.remove(0, destination.size() - MessageCache::MAX_MESSAGES_PER_CHANNEL);
}
}

MessageCache::MessageCache(const QString& cacheDirPath, QObject* parent, bool timers)
    : QObject(parent), m_cacheDirPath(cacheDirPath.isEmpty()
        ? PlatformUtils::resolveAppDataDirectory().filePath(QStringLiteral("cache/messages")) : cacheDirPath) {
    if (!QDir().mkpath(m_cacheDirPath)) reportError(QStringLiteral("Cannot create the message-cache directory."));
    m_pool.setMaxThreadCount(2);
    m_pruneTimer.setInterval(PRUNE_INTERVAL_MS);
    connect(&m_pruneTimer, &QTimer::timeout, this, &MessageCache::pruneInMemoryCaches);
    if (timers) m_pruneTimer.start();
}
MessageCache::~MessageCache() { flushSync(); }

void MessageCache::reportError(const QString& error) {
    if (m_lastError != error) { m_lastError = error; Q_EMIT storageError(error); }
}
bool MessageCache::shouldCacheMessage(MessageType type) {
    return type != MessageType::JOIN && type != MessageType::PART &&
        type != MessageType::QUIT && type != MessageType::MOTD;
}
QString MessageCache::sanitizeChannelFileName(qint64 serverId, const QString& channelName) {
    const auto digest = QCryptographicHash::hash(ircIdentity(channelName).toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("%1_%2.json").arg(serverId).arg(QString::fromLatin1(digest));
}
QString MessageCache::canonicalName(qint64 serverId, const QString& name) const {
    return ircIdentity(name, m_caseMappings.value(serverId, QStringLiteral("rfc1459")));
}
void MessageCache::setCaseMapping(qint64 serverId, const QString& mapping) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (mapping != QStringLiteral("ascii") && mapping != QStringLiteral("rfc1459") &&
        mapping != QStringLiteral("strict-rfc1459")) return;
    if (m_caseMappings.value(serverId, QStringLiteral("rfc1459")) == mapping) return;
    if (!flushSync()) return;
    const auto prefix = QStringLiteral("%1:").arg(serverId);
    for (const auto& key : m_pendingLoads.keys()) if (key.startsWith(prefix)) m_pendingLoads.take(key)->store(false);
    for (const auto& key : m_retiredChannels.values()) if (key.startsWith(prefix)) m_retiredChannels.remove(key);
    for (const auto& key : m_channels.keys()) {
        if (m_channels[key].serverId == serverId) {
            m_channels.remove(key);
            m_lruKeys.removeAll(key);
            delete m_saveTimers.take(key);
            m_aliasSources.remove(key);
        }
    }
    m_caseMappings.insert(serverId, mapping);
}
QString MessageCache::channelKey(qint64 serverId, const QString& channelName) const {
    return QStringLiteral("%1:%2").arg(serverId).arg(canonicalName(serverId, channelName));
}
QString MessageCache::channelFilePath(qint64 serverId, const QString& channelName) const {
    const auto digest = QCryptographicHash::hash(canonicalName(serverId, channelName).toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(m_cacheDirPath).filePath(QStringLiteral("%1_%2.json").arg(serverId).arg(QString::fromLatin1(digest)));
}
bool MessageCache::readChannel(qint64 serverId, const QString& channelName, CachedChannel& result) {
    QMutexLocker locker(&cacheFileMutex);
    result.serverId = serverId;
    result.channelName = channelName;
    QDir directory(m_cacheDirPath);
    const QString canonical = channelFilePath(serverId, channelName);
    QStringList files = directory.entryList({QStringLiteral("%1_*.json").arg(serverId)}, QDir::Files, QDir::Name);
    if (files.size() > MAX_LEGACY_FILES) {
        reportError(QStringLiteral("Too many cache files to migrate safely."));
        return false;
    }
    const QString canonicalFile = QFileInfo(canonical).fileName();
    files.removeAll(canonicalFile);
    if (QFile::exists(canonical)) files.append(canonicalFile);
    for (const auto& filename : files) {
        QFile file(directory.filePath(filename));
        if (!file.open(QIODevice::ReadOnly) || file.size() > MAX_CACHE_FILE_BYTES) {
            if (file.fileName() == canonical) { reportError(QStringLiteral("Cannot read the existing message cache.")); return false; }
            continue;
        }
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.read(MAX_CACHE_FILE_BYTES + 1), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            if (file.fileName() == canonical) { reportError(QStringLiteral("The existing message cache is invalid; it was preserved.")); return false; }
            continue;
        }
        const auto object = document.object();
        if (object.value(QStringLiteral("serverId")).toInteger(-1) != serverId ||
            canonicalName(serverId, object.value(QStringLiteral("channelName")).toString()) != canonicalName(serverId, channelName)) {
            if (file.fileName() == canonical) { reportError(QStringLiteral("Message-cache identity mismatch; the file was preserved.")); return false; }
            continue;
        }
        QList<IncomingMessage> messages;
        const auto entries = object.value(QStringLiteral("messages")).toArray();
        for (int index = std::max(0, static_cast<int>(entries.size()) - MAX_MESSAGES_PER_CHANNEL); index < entries.size(); ++index) {
            if (!entries[index].isObject()) continue;
            auto messageObject = entries[index].toObject();
            if (messageObject.value(QStringLiteral("id")).toString().isEmpty())
                messageObject.insert(QStringLiteral("id"), QString::fromLatin1(QCryptographicHash::hash(
                    QJsonDocument(messageObject).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex()));
            auto message = IncomingMessage::fromJson(messageObject);
            if (message.deliveryStatus == QStringLiteral("pending")) message.deliveryStatus = QStringLiteral("failed");
            messages.append(message);
        }
        mergeMessages(result.messages, messages);
        if (object.value(QStringLiteral("topic")).isString()) result.topic = object.value(QStringLiteral("topic")).toString();
    }
    return true;
}
CachedChannel& MessageCache::loadOrCreateChannel(qint64 serverId, const QString& channelName, bool readDisk) {
    Q_ASSERT(QThread::currentThread() == thread());
    const auto key = channelKey(serverId, channelName);
    m_lruKeys.removeAll(key);
    m_retiredChannels.remove(key);
    m_lruKeys.append(key);
    auto existing = m_channels.find(key);
    if (existing != m_channels.end()) return existing.value();
    while (m_channels.size() >= MAX_CACHED_CHANNELS && !m_lruKeys.isEmpty()) {
        QString victim;
        for (const auto& candidate : m_lruKeys) {
            if (candidate != key && m_channels.contains(candidate) &&
                !m_channels[candidate].dirty) { victim = candidate; break; }
        }
        if (victim.isEmpty()) {
            reportError(QStringLiteral("Message-cache storage is unavailable; new channels cannot be cached."));
            m_lruKeys.removeAll(key);
            m_overflowChannel = CachedChannel{};
            m_overflowChannel.serverId = serverId;
            m_overflowChannel.channelName = channelName;
            return m_overflowChannel;
        }
        m_channels.remove(victim);
        m_lruKeys.removeAll(victim);
        delete m_saveTimers.take(victim);
        m_aliasSources.remove(victim);
        m_retiredChannels.remove(victim);
    }
    CachedChannel channel;
    channel.serverId = serverId;
    channel.channelName = channelName;
    if (readDisk) channel.readable = readChannel(serverId, channelName, channel);
    trimCleanWindow(channel);
    return m_channels.insert(key, channel).value();
}
void MessageCache::scheduleSave(const QString& key) {
    if (!m_channels.contains(key)) return;
    auto* timer = m_saveTimers.value(key);
    if (!timer) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, key]() {
            auto entry = m_channels.find(key);
            if (entry != m_channels.end() && entry->dirty) startWrite(key);
        });
        m_saveTimers.insert(key, timer);
    }
    if (!timer->isActive()) timer->start(SAVE_DEBOUNCE_MS);
}
bool MessageCache::writeChannelToDisk(CachedChannel& channel) {
    if (!channel.readable) return false;
    CachedChannel durable;
    if (!readChannel(channel.serverId, channel.channelName, durable)) return false;
    const auto key = channelKey(channel.serverId, channel.channelName);
    if (m_aliasSources.contains(key)) {
        CachedChannel previous;
        if (!readChannel(channel.serverId, m_aliasSources.value(key), previous)) return false;
        mergeMessages(durable.messages, previous.messages);
        for (auto& message : durable.messages) message.target = channel.channelName;
    }
    mergeMessages(durable.messages, channel.messages);
    durable.topic = channel.topic;
    QJsonArray messages;
    for (const auto& message : durable.messages) messages.append(message.toJson());
    QJsonObject object{{QStringLiteral("version"), 2}, {QStringLiteral("serverId"), channel.serverId},
        {QStringLiteral("channelName"), channel.channelName},
        {QStringLiteral("topic"), channel.topic ? QJsonValue(*channel.topic) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("messages"), messages}};
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (bytes.size() > MAX_CACHE_FILE_BYTES) {
        reportError(QStringLiteral("Message-cache file limit reached; the existing history was preserved."));
        return false;
    }
    QSaveFile file(channelFilePath(channel.serverId, channel.channelName));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        reportError(QStringLiteral("Cannot save message history; unsaved data is retained in memory."));
        return false;
    }
    channel.dirty = false;
    m_aliasSources.remove(key);
    m_lastError.clear();
    return true;
}
void MessageCache::cacheMessage(qint64 serverId, const QString& channelName, const IncomingMessage& message) {
    if (!shouldCacheMessage(message.type)) return;
    if (!hasMemoryCapacity(message)) { reportError(QStringLiteral("Message-cache memory limit reached; existing history was preserved.")); return; }
    auto& channel = loadOrCreateChannel(serverId, channelName);
    mergeMessages(channel.messages, {message});
    ++channel.revision;
    channel.dirty = true;
    scheduleSave(channelKey(serverId, channelName));
}
void MessageCache::cacheMessages(qint64 serverId, const QString& channelName, const QList<IncomingMessage>& messages) {
    auto& channel = loadOrCreateChannel(serverId, channelName);
    QList<IncomingMessage> filtered;
    for (const auto& message : messages) if (shouldCacheMessage(message.type)) filtered.append(message);
    for (const auto& message : filtered) {
        if (!hasMemoryCapacity(message)) { reportError(QStringLiteral("Message-cache memory limit reached.")); break; }
        mergeMessages(channel.messages, {message});
    }
    ++channel.revision;
    if (!filtered.isEmpty()) { channel.dirty = true; scheduleSave(channelKey(serverId, channelName)); }
}
void MessageCache::updateTopic(qint64 serverId, const QString& channelName, const QString& topic) {
    auto& channel = loadOrCreateChannel(serverId, channelName);
    channel.topic = topic;
    ++channel.revision;
    channel.dirty = true;
    scheduleSave(channelKey(serverId, channelName));
}
QList<IncomingMessage> MessageCache::getCachedMessages(qint64 serverId, const QString& channelName, int maxLines) {
    if (maxLines <= 0) return {};
    auto& channel = loadOrCreateChannel(serverId, channelName);
    CachedChannel durable;
    if (!readChannel(serverId, channelName, durable)) return channel.messages;
    mergeMessages(durable.messages, channel.messages);
    return durable.messages.mid(std::max(0, static_cast<int>(durable.messages.size()) - std::min(maxLines, MAX_MESSAGES_PER_CHANNEL)));
}
std::optional<QString> MessageCache::getCachedTopic(qint64 serverId, const QString& channelName) {
    return loadOrCreateChannel(serverId, channelName).topic;
}
QList<IncomingMessage> MessageCache::searchMessages(qint64 serverId, const QString& query,
    const std::optional<QString>& channelFilter, int maxResults) {
    QList<IncomingMessage> results;
    if (query.trimmed().isEmpty() || maxResults <= 0) return results;
    const auto keys = m_channels.keys();
    for (const auto& key : keys) {
        const auto channel = m_channels.value(key);
        if (channel.serverId != serverId || (channelFilter &&
            canonicalName(serverId, *channelFilter) != canonicalName(serverId, channel.channelName))) continue;
        const auto messages = getCachedMessages(serverId, channel.channelName);
        for (auto entry = messages.crbegin(); entry != messages.crend(); ++entry) {
            if (entry->content.contains(query, Qt::CaseInsensitive) || entry->sender.contains(query, Qt::CaseInsensitive)) {
                results.append(*entry);
                if (results.size() >= std::min(maxResults, 1000)) return results;
            }
        }
    }
    return results;
}
void MessageCache::retireChannel(qint64 serverId, const QString& name) {
    const auto key = channelKey(serverId, name);
    if (m_pendingLoads.contains(key)) m_pendingLoads.take(key)->store(false);
    if (!m_channels.contains(key)) return;
    delete m_saveTimers.take(key);
    if (m_channels[key].dirty) {
        m_retiredChannels.insert(key);
        startWrite(key);
    } else {
        m_channels.remove(key);
        m_lruKeys.removeAll(key);
        m_aliasSources.remove(key);
    }
}
void MessageCache::clearChannelCache(qint64 serverId, const QString& channelName) {
    const auto key = channelKey(serverId, channelName);
    if (m_pendingWrites.contains(key)) m_pendingWrites.take(key)->store(false);
    if (m_pendingLoads.contains(key)) m_pendingLoads.take(key)->store(false);
    QMutexLocker locker(&cacheFileMutex);
    const auto files = QDir(m_cacheDirPath).entryInfoList({QStringLiteral("%1_*.json").arg(serverId)}, QDir::Files);
    for (const auto& info : files) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly) || file.size() > MAX_CACHE_FILE_BYTES) continue;
        const auto object = QJsonDocument::fromJson(file.read(MAX_CACHE_FILE_BYTES)).object();
        file.close();
        if (canonicalName(serverId, object.value(QStringLiteral("channelName")).toString()) == canonicalName(serverId, channelName) &&
            !QFile::remove(info.absoluteFilePath())) { reportError(QStringLiteral("Cannot clear message history.")); return; }
    }
    delete m_saveTimers.take(key);
    m_channels.remove(key);
    m_lruKeys.removeAll(key);
    m_aliasSources.remove(key);
    m_retiredChannels.remove(key);
}
void MessageCache::clearServerCache(qint64 serverId) {
    const auto prefix = QStringLiteral("%1:").arg(serverId);
    for (const auto& key : m_pendingWrites.keys()) if (key.startsWith(prefix)) m_pendingWrites.take(key)->store(false);
    for (const auto& key : m_pendingLoads.keys()) if (key.startsWith(prefix)) m_pendingLoads.take(key)->store(false);
    for (const auto& key : m_retiredChannels.values()) if (key.startsWith(prefix)) m_retiredChannels.remove(key);
    QMutexLocker locker(&cacheFileMutex);
    for (const auto& key : m_channels.keys()) if (m_channels[key].serverId == serverId) {
        delete m_saveTimers.take(key); m_channels.remove(key); m_lruKeys.removeAll(key); m_aliasSources.remove(key);
    }
    QDir directory(m_cacheDirPath);
    for (const auto& filename : directory.entryList({QStringLiteral("%1_*.json").arg(serverId)}, QDir::Files))
        if (!directory.remove(filename)) reportError(QStringLiteral("Cannot clear a server's cached history."));
    m_caseMappings.remove(serverId);
}
bool MessageCache::flushSync() {
    Q_ASSERT(QThread::currentThread() == thread());
    for (const auto& token : m_pendingWrites) token->store(false);
    m_pool.waitForDone();
    m_pendingWrites.clear();
    QMutexLocker locker(&cacheFileMutex);
    bool success = true;
    for (auto* timer : m_saveTimers) timer->stop();
    for (auto& channel : m_channels) if (channel.dirty && !writeChannelToDisk(channel)) success = false;
    return success;
}
void MessageCache::pruneInMemoryCaches() {
    for (auto& channel : m_channels) {
        if (channel.dirty) { startWrite(channelKey(channel.serverId, channel.channelName)); continue; }
        trimCleanWindow(channel);
    }
}
bool MessageCache::renameChannel(qint64 serverId, const QString& oldName, const QString& newName) {
    const auto oldKey = channelKey(serverId, oldName);
    const auto newKey = channelKey(serverId, newName);
    if (oldKey == newKey) return true;
    if (m_pendingWrites.contains(oldKey)) m_pendingWrites.take(oldKey)->store(false);
    if (m_pendingLoads.contains(oldKey)) m_pendingLoads.take(oldKey)->store(false);
    const auto previous = m_channels.take(oldKey);
    const auto source = m_aliasSources.take(oldKey);
    delete m_saveTimers.take(oldKey);
    m_lruKeys.removeAll(oldKey);
    auto& target = loadOrCreateChannel(serverId, newName, false);
    auto moved = previous.messages;
    for (auto& message : moved) message.target = newName;
    mergeMessages(target.messages, moved);
    if (!target.topic) target.topic = previous.topic;
    target.dirty = true;
    ++target.revision;
    m_aliasSources.insert(newKey, source.isEmpty() ? oldName : source);
    scheduleSave(newKey);
    return true;
}

bool MessageCache::hasMemoryCapacity(const IncomingMessage& message) const {
    qint64 bytes = messageBytes(message);
    int count = 1;
    for (const auto& channel : m_channels) for (const auto& existing : channel.messages) {
        bytes += messageBytes(existing);
        ++count;
    }
    return bytes <= 32 * 1024 * 1024 && count <= 16000;
}
void MessageCache::startWrite(const QString& key) {
    if (m_pendingWrites.contains(key) || !m_channels.contains(key)) return;
    const auto snapshot = m_channels.value(key);
    const auto directory = m_cacheDirPath;
    const auto mapping = m_caseMappings.value(snapshot.serverId, QStringLiteral("rfc1459"));
    const auto source = m_aliasSources.value(key);
    auto token = std::make_shared<std::atomic_bool>(true);
    m_pendingWrites.insert(key, token);
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, key, token, watcher, revision = snapshot.revision]() {
        const auto error = watcher->result();
        watcher->deleteLater();
        if (m_pendingWrites.value(key) != token) return;
        m_pendingWrites.remove(key);
        if (!m_channels.contains(key)) return;
        if (!error.isEmpty()) { reportError(error); return; }
        auto& channel = m_channels[key];
        m_aliasSources.remove(key);
        if (channel.revision == revision) {
            channel.dirty = false;
            trimCleanWindow(channel);
            if (m_retiredChannels.remove(key)) {
                delete m_saveTimers.take(key);
                m_channels.remove(key);
                m_lruKeys.removeAll(key);
            }
        } else scheduleSave(key);
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [snapshot, directory, mapping, token, source, key]() mutable {
        QMutexLocker locker(&cacheFileMutex);
        if (!token->load()) return QString();
        MessageCache disk(directory, nullptr, false);
        disk.m_caseMappings.insert(snapshot.serverId, mapping);
        if (!source.isEmpty()) disk.m_aliasSources.insert(key, source);
        auto current = snapshot;
        const bool success = disk.writeChannelToDisk(current);
        return success ? QString() : disk.lastError().isEmpty() ? QStringLiteral("History could not be saved.") : disk.lastError();
    }));
}
void MessageCache::loadChannelAsync(qint64 serverId, const QString& name, int limit, std::function<void(const CachedChannel&)> callback) {
    Q_ASSERT(QThread::currentThread() == thread());
    const auto key = channelKey(serverId, name);
    if (m_pendingLoads.size() >= MAX_CACHED_CHANNELS) { reportError(QStringLiteral("History loader is busy.")); return; }
    loadOrCreateChannel(serverId, name, false);
    const auto directory = m_cacheDirPath;
    const auto mapping = m_caseMappings.value(serverId, QStringLiteral("rfc1459"));
    auto token = std::make_shared<std::atomic_bool>(true);
    if (m_pendingLoads.contains(key)) m_pendingLoads.take(key)->store(false);
    m_pendingLoads.insert(key, token);
    auto* watcher = new QFutureWatcher<CachedChannel>(this);
    connect(watcher, &QFutureWatcher<CachedChannel>::finished, this, [this, key, token, watcher, limit, callback]() {
        auto loaded = watcher->result();
        watcher->deleteLater();
        if (m_pendingLoads.value(key) != token || !token->load()) return;
        m_pendingLoads.remove(key);
        if (!loaded.readable) { reportError(QStringLiteral("Stored history could not be read; the original file was preserved.")); return; }
        if (m_channels.contains(key)) {
            auto& memory = m_channels[key];
            auto combined = loaded.messages;
            mergeMessages(combined, memory.messages);
            loaded.messages = combined;
            memory.messages = combined;
            if (!memory.dirty) trimCleanWindow(memory);
            if (!memory.topic) memory.topic = loaded.topic;
            loaded.topic = memory.topic;
        }
        loaded.messages = loaded.messages.mid(std::max(0, static_cast<int>(loaded.messages.size()) - std::clamp(limit, 0, MAX_MESSAGES_PER_CHANNEL)));
        callback(loaded);
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [directory, serverId, name, mapping, token]() {
        CachedChannel loaded;
        if (!token->load()) return loaded;
        MessageCache disk(directory, nullptr, false);
        disk.m_caseMappings.insert(serverId, mapping);
        loaded.readable = disk.readChannel(serverId, name, loaded);
        return loaded;
    }));
}

}
