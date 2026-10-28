#pragma once

#include <QString>
#include <QList>
#include <QMutex>
#include <QThread>
#include <optional>
#include "data/model/ServerConfig.h"

namespace loungecat {

class DatabaseService {
public:
    explicit DatabaseService(const QString& dbFilePath = {});
    ~DatabaseService();

    bool initialize();
    bool isReady() const { return m_ready && QThread::currentThread() == m_ownerThread; }
    QString lastError() const { return m_lastError; }
    QList<ServerConfig> getAllServerConfigs();
    std::optional<ServerConfig> getServerConfig(qint64 id);
    qint64 insertServerConfig(const ServerConfig& config);
    bool updateServerConfig(const ServerConfig& config);
    bool deleteServerConfig(qint64 id);

    QString dbFilePath() const { return m_dbFilePath; }

private:
    bool runMigrations();
    Q_DISABLE_COPY_MOVE(DatabaseService)

    QString m_dbFilePath;
    QString m_connectionName;
    mutable QMutex m_mutex;
    QThread* m_ownerThread = QThread::currentThread();
    bool m_ready = false;
    QString m_lastError;
};

} // namespace loungecat
