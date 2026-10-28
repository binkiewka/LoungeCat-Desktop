#include "util/PlatformUtils.h"
#include <QGuiApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>

namespace loungecat {

QDir PlatformUtils::resolveAppDataDirectory() {
    const auto overridePath = qEnvironmentVariable("LOUNGECAT_DATA_DIR");
    if (!overridePath.isEmpty()) {
        QDir directory(overridePath);
        directory.mkpath(QStringLiteral("."));
        return directory;
    }
    QString path;
#if defined(Q_OS_WIN)
    QByteArray appData = qgetenv("APPDATA");
    if (!appData.isEmpty()) {
        path = QDir(QString::fromLocal8Bit(appData)).filePath(QStringLiteral("LoungeCat"));
    } else {
        path = QDir::home().filePath(QStringLiteral(".loungecat"));
    }
#elif defined(Q_OS_MACOS)
    path = QDir::home().filePath(QStringLiteral("Library/Application Support/LoungeCat"));
#else
    path = QDir::home().filePath(QStringLiteral(".loungecat"));
#endif
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    return dir;
}

QString PlatformUtils::resolveDatabasePath() {
    QByteArray envPath = qgetenv("LOUNGECAT_DB_PATH");
    if (!envPath.isEmpty()) {
        return QString::fromLocal8Bit(envPath);
    }
    return resolveAppDataDirectory().filePath(QStringLiteral("loungecat.db"));
}

QString PlatformUtils::resolveConfigPath() {
    QByteArray envPath = qgetenv("LOUNGECAT_CONFIG");
    if (!envPath.isEmpty()) {
        return QString::fromLocal8Bit(envPath);
    }
    return resolveAppDataDirectory().filePath(QStringLiteral("preferences.json"));
}

bool PlatformUtils::isHeadlessMode() {
    QString val = QString::fromLocal8Bit(qgetenv("HEADLESS")).trimmed().toLower();
    return val == QStringLiteral("true") || val == QStringLiteral("1");
}

void PlatformUtils::copyToClipboard(const QString& text) {
    if (QClipboard* cb = QGuiApplication::clipboard()) {
        cb->setText(text);
    }
}

void PlatformUtils::openUrl(const QString& url) {
    QDesktopServices::openUrl(QUrl::fromUserInput(url));
}

} // namespace loungecat
