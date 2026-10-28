#include <QApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <QMessageBox>
#include <QIcon>
#include "app/AppConfig.h"
#include "app/SingleInstanceGuard.h"
#include "data/database/DatabaseService.h"
#include "data/cache/MessageCache.h"
#include "data/preferences/PreferencesManager.h"
#include "service/TextLogService.h"
#include "service/SoundService.h"
#include "service/UpdateCheckService.h"
#include "service/ImageCache.h"
#include "network/ConnectionManager.h"
#include "ui/theme/ThemeManager.h"
#include "ui/window/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("LoungeCat"));
    app.setOrganizationName(QStringLiteral("LoungeCat"));
    app.setApplicationVersion(QStringLiteral(APP_VERSION));
    // QApplication consumes Qt's built-in GUI options first. Validate the
    // remaining application arguments before touching profiles or networking.
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("LoungeCat IRC client"));
    parser.addHelpOption();
    parser.addVersionOption();
    if (!parser.parse(app.arguments())) {
        QTextStream(stderr) << "LoungeCat: " << parser.errorText().left(512)
                            << "\nUse --help for supported options.\n";
        return 1;
    }
    if (!parser.positionalArguments().isEmpty()) {
        QTextStream(stderr) << "LoungeCat: positional arguments are not supported.\nUse --help for supported options.\n";
        return 1;
    }
    if (parser.isSet(QStringLiteral("help")) || parser.isSet(QStringLiteral("help-all"))) {
        QTextStream(stdout) << parser.helpText();
        return 0;
    }
    if (parser.isSet(QStringLiteral("version"))) {
        QTextStream(stdout) << app.applicationName() << ' ' << app.applicationVersion() << '\n';
        return 0;
    }
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/icon.png")));

    const loungecat::AppConfig appCfg = loungecat::AppConfig::fromEnvironment();

    // Acquire ~/.loungecat/app.lock single-instance lock
    loungecat::SingleInstanceGuard instanceGuard(appCfg.appDataDir.filePath(QStringLiteral("app.lock")));
    if (!instanceGuard.tryAcquire()) {
        QMessageBox::critical(
            nullptr,
            QStringLiteral("LoungeCat Already Running"),
            QStringLiteral("Another instance of LoungeCat is already running.")
        );
        return 1;
    }

    loungecat::DatabaseService dbService(appCfg.databasePath);
    if (!dbService.isReady()) {
        QMessageBox::critical(nullptr, QStringLiteral("Storage Unavailable"), QStringLiteral("The database could not be opened or migrated. Existing files were preserved."));
        return 1;
    }
    loungecat::MessageCache messageCache(appCfg.appDataDir.filePath(QStringLiteral("cache/messages")));
    loungecat::PreferencesManager prefsManager(appCfg.preferencesPath);
    if (!prefsManager.lastError().isEmpty()) {
        QMessageBox::critical(nullptr, QStringLiteral("Settings Unavailable"), prefsManager.lastError());
        return 1;
    }
    loungecat::ThemeManager themeManager(appCfg.appDataDir.filePath(QStringLiteral("themes")));
    themeManager.setActiveTheme(prefsManager.preferences().theme);

    loungecat::TextLogService textLogService(appCfg.appDataDir.filePath(QStringLiteral("logs")));
    loungecat::SoundService soundService;
    loungecat::UpdateCheckService updateCheckService;

    loungecat::ConnectionManager connectionManager(
        &dbService,
        &messageCache,
        &prefsManager,
        &textLogService,
        &soundService
    );

    loungecat::MainWindow mainWindow(
        &dbService,
        &prefsManager,
        &themeManager,
        &soundService,
        &updateCheckService,
        &connectionManager,
        appCfg.headless
    );

    if (appCfg.headless) {
        mainWindow.showMaximized();
    } else {
        mainWindow.show();
    }

    connectionManager.initializeFromDatabase(true);
    updateCheckService.startPeriodicChecks();

    const int exitCode = app.exec();
    loungecat::ImageCache::shutdown();
    const bool historySaved = messageCache.flushSync();
    const bool logsSaved = textLogService.flushSync();
    if (!historySaved || !logsSaved) {
        const auto error = QStringLiteral("Some history or logs could not be saved. Existing files were preserved. Check storage availability before the next session.");
        qCritical("%s", qPrintable(error));
        if (!appCfg.headless) QMessageBox::warning(&mainWindow, QStringLiteral("Storage failure during shutdown"), error);
        return 1;
    }
    return exitCode;
}
