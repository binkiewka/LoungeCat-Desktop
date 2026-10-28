#include "data/preferences/PreferencesManager.h"
#include "util/IrcIdentity.h"
#include "util/PlatformUtils.h"
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

namespace loungecat {

PreferencesManager::PreferencesManager(const QString& configFilePath, QObject* parent)
    : QObject(parent),
      m_configFilePath(configFilePath.isEmpty() ? PlatformUtils::resolveConfigPath() : configFilePath) {
    load();
}

bool PreferencesManager::load() {
    Q_ASSERT(QThread::currentThread() == thread());
    m_lastError.clear();
    QFile file(m_configFilePath);
    if (!file.exists()) {
        { QWriteLocker lock(&m_lock); m_preferences = UserPreferences{}; }
        return save();
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastError = QStringLiteral("Cannot open settings. The existing file was preserved.");
        return false;
    }
    // Preferences can contain upload credentials. Harden existing files before reading.
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        m_lastError = QStringLiteral("Settings credentials require owner-only file permissions. The existing file was preserved.");
        return false;
    }
    if (file.size() > 1024 * 1024) { m_lastError = QStringLiteral("Settings file exceeds the supported size."); return false; }
    const QByteArray bytes = file.read(1024 * 1024 + 1);
    file.close();

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        m_lastError = QStringLiteral("Invalid settings JSON. The existing file was preserved.");
        return false;
    }

    if (!UserPreferences::validateJson(doc.object(), &m_lastError)) return false;
    QWriteLocker lock(&m_lock);
    m_preferences = UserPreferences::fromJson(doc.object());
    return true;
}

bool PreferencesManager::save() const {
    return saveSnapshot(preferences());
}

bool PreferencesManager::saveSnapshot(const UserPreferences& snapshot) const {
    Q_ASSERT(QThread::currentThread() == thread());
    QFileInfo fi(m_configFilePath);
    if (!fi.dir().exists()) {
        fi.dir().mkpath(QStringLiteral("."));
    }

    QSaveFile file(m_configFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_lastError = QStringLiteral("Cannot write settings. Your previous settings were preserved.");
        return false;
    }
    // Set permissions on the temporary file before any secret bytes are written.
    // Available via QFileDevice on Qt 6.4; keep QSaveFile's atomic commit/fallback policy.
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        file.cancelWriting();
        m_lastError = QStringLiteral("Settings credentials could not be saved with owner-only permissions.");
        return false;
    }

    const QJsonDocument doc(snapshot.toJson());
    const auto bytes = doc.toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        m_lastError = QStringLiteral("Settings could not be saved.");
        return false;
    }
    m_lastError.clear();
    return true;
}

bool PreferencesManager::setPreferences(const UserPreferences& prefs) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!UserPreferences::validateJson(prefs.toJson(), &m_lastError)) { Q_EMIT storageError(m_lastError); return false; }
    const auto previous = preferences();
    if (previous == prefs) return true;
    if (!saveSnapshot(prefs)) {
        Q_EMIT storageError(m_lastError);
        return false;
    }
    { QWriteLocker lock(&m_lock); m_preferences = prefs; }
    Q_EMIT preferencesChanged(prefs);
    return true;
}

bool PreferencesManager::update(const std::function<void(UserPreferences&)>& mutator) {
    UserPreferences copy = preferences();
    mutator(copy);
    return setPreferences(copy);
}

bool PreferencesManager::addIgnoredUser(const QString& nickname, const QString& mapping) {
    const QString name = nickname.trimmed();
    if (name.isEmpty()) return false;
    return update([&](UserPreferences& p) {
        p.ignoredUsers.removeIf([&](const QString& value) { return ircIdentity(value, mapping) == ircIdentity(name, mapping); });
        p.ignoredUsers.insert(name);
    });
}

bool PreferencesManager::removeIgnoredUser(const QString& nickname, const QString& mapping) {
    const QString name = nickname.trimmed();
    return update([&](UserPreferences& p) {
        p.ignoredUsers.removeIf([&](const QString& value) { return ircIdentity(value, mapping) == ircIdentity(name, mapping); });
    });
}

bool PreferencesManager::isUserIgnored(const QString& nickname, const QString& mapping) const {
    const auto prefs = preferences();
    for (const auto& value : prefs.ignoredUsers) if (ircIdentity(value, mapping) == ircIdentity(nickname.trimmed(), mapping)) return true;
    return false;
}

} // namespace loungecat
