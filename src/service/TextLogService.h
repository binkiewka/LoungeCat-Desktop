#pragma once

#include <QString>
#include <QDir>
#include <QMutex>
#include <QObject>
#include <QThreadPool>
#include "data/model/IncomingMessage.h"

namespace loungecat {

class TextLogService : public QObject {
    Q_OBJECT
public:
    explicit TextLogService(const QString& logsRootPath = {}, QObject* parent = nullptr);
    ~TextLogService() override;
    static constexpr qint64 MAX_LOG_FILE_BYTES = 2 * 1024 * 1024;
    static constexpr qint64 MAX_TOTAL_LOG_BYTES = 128 * 1024 * 1024;
    static constexpr int MAX_LOG_FILES = 512;

    static QString sanitizeServerName(const QString& serverName);
    static QString sanitizeChannelName(const QString& channelName);

    bool migrateLegacyFolders();
    bool logMessage(const QString& serverName, const QString& channelName, const IncomingMessage& message, qint64 serverId = 0);
    QString lastError() const { return m_lastError; }
    bool flushSync();

    QString resolveLogFilePath(const QString& serverName, const QString& channelName, qint64 serverId = 0) const;
    QString logsRootPath() const { return m_logsRootPath; }

private:
    static QString appendLog(const QString& root, const QString& server, const QString& channel, const IncomingMessage& message, qint64 serverId);
    QString m_logsRootPath;
    QString m_lastError;
    int m_queued = 0;
    mutable QMutex m_mutex;
    QThreadPool m_pool;
Q_SIGNALS:
    void storageError(const QString& error);
};

} // namespace loungecat
