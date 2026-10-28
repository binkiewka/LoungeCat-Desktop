#include "app/AppConfig.h"
#include "util/PlatformUtils.h"

namespace loungecat {

AppConfig AppConfig::fromEnvironment() {
    AppConfig cfg;
    cfg.appDataDir = PlatformUtils::resolveAppDataDirectory();
    cfg.databasePath = PlatformUtils::resolveDatabasePath();
    cfg.preferencesPath = PlatformUtils::resolveConfigPath();
    cfg.headless = PlatformUtils::isHeadlessMode();
    return cfg;
}

} // namespace loungecat
