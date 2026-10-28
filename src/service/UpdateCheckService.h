#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QTimer>
#include <QNetworkAccessManager>
#include <optional>

namespace loungecat {

struct VersionInfo {
    QString version;
    QString releaseUrl;
    QString releaseNotes;
};

class UpdateCheckService : public QObject {
    Q_OBJECT
public:
    static constexpr int CHECK_INTERVAL_MS = 30 * 60 * 1000;

    explicit UpdateCheckService(QObject* parent = nullptr);
    ~UpdateCheckService() override = default;

    void startPeriodicChecks();
    void checkNow();

    static bool isNewerVersion(const QString& currentVersion, const QString& remoteVersion);
    static std::optional<VersionInfo> parseReleaseMetadata(const QByteArray& response,
                                                         const QString& currentVersion);

Q_SIGNALS:
    void updateAvailable(const loungecat::VersionInfo& info);

private:
    QNetworkAccessManager m_nam;
    QTimer m_timer;
    std::optional<VersionInfo> m_latestUpdate;
};

} // namespace loungecat
