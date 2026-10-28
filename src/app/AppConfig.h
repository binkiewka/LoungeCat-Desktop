#pragma once

#include <QString>
#include <QDir>

namespace loungecat {

struct AppConfig {
    QDir appDataDir;
    QString databasePath;
    QString preferencesPath;
    bool headless = false;

    static AppConfig fromEnvironment();
};

} // namespace loungecat
