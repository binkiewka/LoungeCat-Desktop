#pragma once

#include <QString>
#include <QDir>

namespace loungecat {

class PlatformUtils {
public:
    static QDir resolveAppDataDirectory();
    static QString resolveDatabasePath();
    static QString resolveConfigPath();
    static bool isHeadlessMode();
    static void copyToClipboard(const QString& text);
    static void openUrl(const QString& url);
};

} // namespace loungecat
