#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QMutex>
#include <optional>
#include <QThreadPool>
#include <functional>
#include <atomic>
#include <memory>
#include "data/model/IncomingMessage.h"

namespace loungecat {

struct CachedChannel {
    qint64 serverId = 0;
    QString channelName;
    std::optional<QString> topic;
    QList<IncomingMessage> messages;
    bool dirty = false;
    bool readable = true;
    quint64 revision = 0;
};

class MessageCache : public QObject {
    Q_OBJECT
public:
    static constexpr int MAX_MESSAGES_PER_CHANNEL = 2000;
    static constexpr int MAX_IN_MEMORY_MESSAGES = 500;
    static constexpr int SAVE_DEBOUNCE_MS = 2000;
    static constexpr int PRUNE_INTERVAL_MS = 15 * 60 * 1000;
    static constexpr int MAX_CACHED_CHANNELS = 32;
    static constexpr int MAX_LEGACY_FILES = 1024;
    static constexpr qint64 MAX_CACHE_FILE_BYTES = 32 * 1024 * 1024;

    explicit MessageCache(const QString& cacheDirPath = {}, QObject* parent = nullptr, bool timers = true);
    ~MessageCache() override;

    static bool shouldCacheMessage(MessageType type);
    static QString sanitizeChannelFileName(qint64 serverId, const QString& channelName);

    void cacheMessage(qint64 serverId, const QString& channelName, const IncomingMessage& message);
    void cacheMessages(qint64 serverId, const QString& channelName, const QList<IncomingMessage>& messages);
    void updateTopic(qint64 serverId, const QString& channelName, const QString& topic);
    void setCaseMapping(qint64 serverId, const QString& mapping);
    bool renameChannel(qint64 serverId, const QString& oldName, const QString& newName);
    QString lastError() const { return m_lastError; }
    void loadChannelAsync(qint64 serverId, const QString& name, int limit, std::function<void(const CachedChannel&)> callback);

    QList<IncomingMessage> getCachedMessages(qint64 serverId, const QString& channelName, int maxLines = MAX_MESSAGES_PER_CHANNEL);
    std::optional<QString> getCachedTopic(qint64 serverId, const QString& channelName);

    QList<IncomingMessage> searchMessages(
        qint64 serverId,
        const QString& query,
        const std::optional<QString>& channelFilter = std::nullopt,
        int maxResults = 100
    );

    void clearChannelCache(qint64 serverId, const QString& channelName);
    void retireChannel(qint64 serverId, const QString& channelName);
    void clearServerCache(qint64 serverId);
    bool flushSync();
    void pruneInMemoryCaches();

Q_SIGNALS:
    void storageError(const QString& error);

private:
    QString channelKey(qint64 serverId, const QString& channelName) const;
    QString channelFilePath(qint64 serverId, const QString& channelName) const;
    CachedChannel& loadOrCreateChannel(qint64 serverId, const QString& channelName, bool readDisk = true);
    void scheduleSave(const QString& key);
    bool writeChannelToDisk(CachedChannel& channel);
    bool readChannel(qint64 serverId, const QString& channelName, CachedChannel& result);
    QString canonicalName(qint64 serverId, const QString& name) const;
    void reportError(const QString& error);
    void startWrite(const QString& key);
    bool hasMemoryCapacity(const IncomingMessage& message) const;

    QString m_cacheDirPath;
    QMap<QString, CachedChannel> m_channels;
    QMap<QString, QTimer*> m_saveTimers;
    QTimer m_pruneTimer;
    QStringList m_lruKeys;
    QSet<QString> m_retiredChannels;
    QMap<qint64, QString> m_caseMappings;
    CachedChannel m_overflowChannel;
    QString m_lastError;
    QMap<QString, std::shared_ptr<std::atomic_bool>> m_pendingWrites;
    QMap<QString, std::shared_ptr<std::atomic_bool>> m_pendingLoads;
    QMap<QString, QString> m_aliasSources;
    QThreadPool m_pool;
};

} // namespace loungecat
