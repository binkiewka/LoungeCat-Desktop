#pragma once

#include <QString>
#include <QList>
#include <optional>
#include "data/model/UserPreferences.h"
#include "data/model/ServerConfig.h"

namespace loungecat {

struct BackupConfig {
    int version = 1;
    qint64 timestamp = 0;
    bool secretsOmitted = false;
    UserPreferences preferences;
    QList<ServerConfig> servers;
};

class ConfigExporter {
public:
    static QString serializeBackup(const UserPreferences& preferences, const QList<ServerConfig>& servers);
    static std::optional<BackupConfig> parseBackup(const QString& jsonString);

    static bool exportToFile(const QString& filePath, const UserPreferences& preferences, const QList<ServerConfig>& servers);
    static std::optional<BackupConfig> importFromFile(const QString& filePath);
};

} // namespace loungecat
