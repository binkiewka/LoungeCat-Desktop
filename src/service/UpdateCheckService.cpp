#include "service/UpdateCheckService.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVersionNumber>

namespace loungecat {

UpdateCheckService::UpdateCheckService(QObject* parent)
    : QObject(parent) {
    m_timer.setInterval(CHECK_INTERVAL_MS);
    connect(&m_timer, &QTimer::timeout, this, &UpdateCheckService::checkNow);
}

void UpdateCheckService::startPeriodicChecks() {
    m_timer.start();
    checkNow();
}

bool UpdateCheckService::isNewerVersion(const QString& currentVersion, const QString& remoteVersion) {
    QString cleanCur = currentVersion.trimmed();
    if (cleanCur.startsWith('v', Qt::CaseInsensitive)) cleanCur.remove(0, 1);

    QString cleanRem = remoteVersion.trimmed();
    if (cleanRem.startsWith('v', Qt::CaseInsensitive)) cleanRem.remove(0, 1);

    const QVersionNumber vCur = QVersionNumber::fromString(cleanCur);
    const QVersionNumber vRem = QVersionNumber::fromString(cleanRem);
    if (vCur.isNull() || vRem.isNull()) {
        return false;
    }
    return vRem > vCur;
}

std::optional<VersionInfo> UpdateCheckService::parseReleaseMetadata(
    const QByteArray& response, const QString& currentVersion) {
    const auto document = QJsonDocument::fromJson(response);
    if (!document.isObject()) return std::nullopt;
    const auto object = document.object();
    auto version = object.value(QStringLiteral("tag_name")).toString();
    if (version.startsWith('v', Qt::CaseInsensitive)) version.remove(0, 1);
    if (!isNewerVersion(currentVersion, version)) return std::nullopt;

    const auto releaseUrl = object.value(QStringLiteral("html_url")).toString();
    // A version alone cannot produce an actionable update notice. Require the
    // absolute HTTPS release-page URL supplied by the GitHub metadata contract.
    const QUrl url(releaseUrl, QUrl::StrictMode);
    if (releaseUrl.size() > 2048 || !url.isValid() || url.isRelative()
        || url.scheme() != QStringLiteral("https") || url.host() != QStringLiteral("github.com")
        || !url.userInfo().isEmpty() || (url.port(-1) != -1 && url.port() != 443)
        || url.path().isEmpty()) return std::nullopt;

    return VersionInfo{version, releaseUrl, object.value(QStringLiteral("body")).toString()};
}

void UpdateCheckService::checkNow() {
    QNetworkRequest req(QUrl(QStringLiteral("https://api.github.com/repos/binkiewka/LoungeCat-Desktop/releases/latest")));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LoungeCat-IRC"));
    req.setTransferTimeout(10000);

    QNetworkReply* reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray resp = reply->readAll();
        const bool ok = (reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
        if (!ok) return;

        if (auto info = parseReleaseMetadata(resp, QStringLiteral(APP_VERSION))) {
            m_latestUpdate = *info;
            Q_EMIT updateAvailable(*info);
        }
    });
}

} // namespace loungecat
