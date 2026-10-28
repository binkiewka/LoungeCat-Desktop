#include "service/UrlShortenerService.h"
#include "service/LinkPreviewService.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QTimer>
#include <QPointer>
#include <memory>

namespace loungecat {

UrlShortenerService::UrlShortenerService(QObject* parent)
    : QObject(parent) {}
UrlShortenerService::~UrlShortenerService() {
    for (auto* reply : m_nam.findChildren<QNetworkReply*>()) { reply->disconnect(this); reply->abort(); }
}

void UrlShortenerService::shortenUrl(const QString& originalUrl, ShortenCallback callback) {
    if (m_cache.contains(originalUrl)) {
        callback(m_cache.value(originalUrl));
        return;
    }

    const QUrl apiUrl(QStringLiteral("https://is.gd/create.php?format=simple&url=%1")
                          .arg(QString::fromLatin1(QUrl::toPercentEncoding(originalUrl))));
    QNetworkRequest req(apiUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LoungeCat-IRC/1.0"));
    req.setTransferTimeout(8000);

    QNetworkReply* reply = m_nam.get(req);
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, deadline, &QTimer::stop);
    deadline->start(8000);
    connect(reply, &QIODevice::readyRead, reply, [reply]() {
        if (reply->bytesAvailable() > 8192) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, originalUrl, callback]() {
        const bool ok = reply->error() == QNetworkReply::NoError && reply->bytesAvailable() <= 8192;
        const QByteArray resp = ok ? reply->read(8192) : QByteArray();
        reply->deleteLater();

        if (ok) {
            const QString shortened = QString::fromUtf8(resp).trimmed();
            if (shortened.startsWith(QStringLiteral("http"))) {
                if (m_cache.size() >= 256) m_cache.erase(m_cache.begin());
                m_cache.insert(originalUrl, shortened);
                callback(shortened);
                return;
            }
        }
        callback(originalUrl);
    });
}

void UrlShortenerService::processMessageUrls(const QString& message, int threshold, ShortenCallback callback) {
    const QStringList urls = LinkPreviewService::extractUrls(message);
    QStringList longUrls;
    for (const QString& u : urls) {
        if (u.length() >= threshold) {
            longUrls.append(u);
        }
    }
    if (longUrls.isEmpty()) {
        callback(message);
        return;
    }

    // Each original URL is processed once; a provider cannot create a recursion loop.
    longUrls = longUrls.mid(0, 32);
    struct Work { QString text; QStringList urls; int index = 0; };
    auto work = std::make_shared<Work>(Work{message, longUrls, 0});
    QPointer<UrlShortenerService> owner(this);
    auto advance = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weakAdvance = advance;
    *advance = [owner, work, weakAdvance, callback]() {
        if (!owner) return;
        if (work->index >= work->urls.size()) { callback(work->text); return; }
        const auto url = work->urls[work->index++];
        auto keepAlive = weakAdvance.lock();
        owner->shortenUrl(url, [work, keepAlive, url](const QString& shortened) {
            work->text.replace(url, shortened);
            if (keepAlive) (*keepAlive)();
        });
    };
    (*advance)();
}

} // namespace loungecat
