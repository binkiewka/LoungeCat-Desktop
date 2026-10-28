#pragma once

#include <QObject>
#include <QReadWriteLock>
#include <QString>
#include <functional>
#include "data/model/UserPreferences.h"

namespace loungecat {

class PreferencesManager : public QObject {
    Q_OBJECT
public:
    explicit PreferencesManager(const QString& configFilePath = {}, QObject* parent = nullptr);
    ~PreferencesManager() override = default;

    UserPreferences preferences() const { QReadLocker lock(&m_lock); return m_preferences; }
    bool setPreferences(const UserPreferences& prefs);
    bool update(const std::function<void(UserPreferences&)>& mutator);
    QString lastError() const { return m_lastError; }

    bool addIgnoredUser(const QString& nickname, const QString& mapping = QStringLiteral("rfc1459"));
    bool removeIgnoredUser(const QString& nickname, const QString& mapping = QStringLiteral("rfc1459"));
    bool isUserIgnored(const QString& nickname, const QString& mapping = QStringLiteral("rfc1459")) const;

    bool load();
    bool save() const;

    QString configFilePath() const { return m_configFilePath; }

Q_SIGNALS:
    void preferencesChanged(const loungecat::UserPreferences& preferences);
    void storageError(const QString& error);

private:
    bool saveSnapshot(const UserPreferences& preferences) const;
    QString m_configFilePath;
    UserPreferences m_preferences;
    mutable QReadWriteLock m_lock;
    mutable QString m_lastError;
};

} // namespace loungecat
