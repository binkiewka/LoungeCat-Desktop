#include "service/LinkPreviewService.h"
#include "service/ImageCache.h"
#include "util/NetworkSecurityUtils.h"
#include <QRegularExpression>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace loungecat {

LinkPreviewService::LinkPreviewService(QObject* parent)
    : QObject(parent) {}
LinkPreviewService::~LinkPreviewService() {
    for (auto* reply : m_nam.findChildren<QNetworkReply*>()) { reply->disconnect(this); reply->abort(); }
}

QStringList LinkPreviewService::extractUrls(const QString& text) {
    QStringList urls;
    static const QRegularExpression urlRegex(
        QStringLiteral(R"(https?://[^\s<>"'`\)\]]+)"),
        QRegularExpression::CaseInsensitiveOption
    );
    auto it = urlRegex.globalMatch(text);
    while (it.hasNext()) {
        QString url = it.next().captured(0);
        while (url.endsWith('.') || url.endsWith(',') || url.endsWith(';') || url.endsWith(':')) {
            url.chop(1);
        }
        if (!url.isEmpty() && !urls.contains(url)) {
            urls.append(url);
        }
    }
    return urls;
}

bool LinkPreviewService::isImageUrl(const QString& url) {
    const QString path = QUrl(url).path().toLower();
    return path.endsWith(QStringLiteral(".jpg")) ||
           path.endsWith(QStringLiteral(".jpeg")) ||
           path.endsWith(QStringLiteral(".png")) ||
           path.endsWith(QStringLiteral(".gif")) ||
           path.endsWith(QStringLiteral(".webp")) ||
           path.endsWith(QStringLiteral(".bmp")) ||
           path.endsWith(QStringLiteral(".svg"));
}

bool LinkPreviewService::isYoutubeUrl(const QString& url) {
    const QString host = QUrl(url).host().toLower();
    return host.contains(QStringLiteral("youtube.com")) || host.contains(QStringLiteral("youtu.be"));
}

bool LinkPreviewService::isVimeoUrl(const QString& url) {
    const QString host = QUrl(url).host().toLower();
    return host.contains(QStringLiteral("vimeo.com"));
}

UrlPreview LinkPreviewService::parseOpenGraphHtml(const QString& url, const QString& html) {
    UrlPreview preview;
    preview.url = url;

    auto extractMeta = [&](const QString& propName) -> std::optional<QString> {
        QRegularExpression re1(
            QStringLiteral(R"(<meta[^>]+(?:property|name)\s*=\s*["']%1["'][^>]+content\s*=\s*["']([^"']+)["'])").arg(QRegularExpression::escape(propName)),
            QRegularExpression::CaseInsensitiveOption
        );
        auto m1 = re1.match(html);
        if (m1.hasMatch()) return m1.captured(1).trimmed();

        QRegularExpression re2(
            QStringLiteral(R"(<meta[^>]+content\s*=\s*["']([^"']+)["'][^>]+(?:property|name)\s*=\s*["']%1["'])").arg(QRegularExpression::escape(propName)),
            QRegularExpression::CaseInsensitiveOption
        );
        auto m2 = re2.match(html);
        if (m2.hasMatch()) return m2.captured(1).trimmed();
        return std::nullopt;
    };

    preview.title = extractMeta(QStringLiteral("og:title"));
    if (!preview.title) {
        static const QRegularExpression titleRe(
            QStringLiteral(R"(<title[^>]*>([^<]+)</title>)"),
            QRegularExpression::CaseInsensitiveOption
        );
        auto tm = titleRe.match(html);
        if (tm.hasMatch()) {
            preview.title = tm.captured(1).simplified();
        }
    }

    preview.description = extractMeta(QStringLiteral("og:description"));
    if (!preview.description) {
        preview.description = extractMeta(QStringLiteral("description"));
    }

    preview.imageUrl = extractMeta(QStringLiteral("og:image"));
    preview.siteName = extractMeta(QStringLiteral("og:site_name"));
    if (!preview.siteName) {
        const QString host = QUrl(url).host();
        if (!host.isEmpty()) preview.siteName = host;
    }

    return preview;
}

void LinkPreviewService::fetchPreview(
    const QString& url,
    const ServerConfig& serverConfig,
    bool useProxy,
    PreviewCallback callback
) {
    if (!NetworkSecurityUtils::isSafeWebUrl(url)) {
        UrlPreview unsafePrev;
        unsafePrev.url = url;
        unsafePrev.failed = true;
        callback(unsafePrev);
        return;
    }

    if (m_cache.contains(url)) {
        callback(m_cache.value(url));
        return;
    }

    if (isImageUrl(url)) {
        UrlPreview imgPrev;
        imgPrev.url = url;
        imgPrev.imageUrl = url;
        imgPrev.isImage = true;
        if (m_cache.size() >= MAX_CACHE_ENTRIES) {
            m_cache.erase(m_cache.begin());
        }
        m_cache.insert(url, imgPrev);
        callback(imgPrev);
        return;
    }

    if (m_queue.size() >= MAX_PENDING_REQUESTS) {
        UrlPreview result;
        result.url = url;
        result.failed = true;
        callback(result);
        return;
    }
    m_queue.enqueue(PendingTask{url, serverConfig, useProxy, std::move(callback)});
    processNextInQueue();
}

void LinkPreviewService::processNextInQueue() {
    while (m_activeRequests < MAX_CONCURRENT_REQUESTS && !m_queue.isEmpty()) {
        PendingTask task = m_queue.dequeue();
        if (m_cache.contains(task.url)) {
            task.callback(m_cache.value(task.url));
            continue;
        }
        ++m_activeRequests;
        startRequest(task);
    }
}

void LinkPreviewService::startRequest(const PendingTask& task) {
    QNetworkProxy proxy = QNetworkProxy::NoProxy;
    if (task.useProxy && task.serverConfig.proxyType == ProxyType::SOCKS5 && !task.serverConfig.proxyHost.isEmpty()) {
        proxy = QNetworkProxy(
            QNetworkProxy::Socks5Proxy,
            task.serverConfig.proxyHost,
            static_cast<quint16>(task.serverConfig.proxyPort > 0 ? task.serverConfig.proxyPort : 1080),
            task.serverConfig.proxyUsername,
            task.serverConfig.proxyPassword
        );
    } else if (task.useProxy && task.serverConfig.proxyType == ProxyType::HTTP && !task.serverConfig.proxyHost.isEmpty()) {
        proxy = QNetworkProxy(
            QNetworkProxy::HttpProxy,
            task.serverConfig.proxyHost,
            static_cast<quint16>(task.serverConfig.proxyPort > 0 ? task.serverConfig.proxyPort : 8080),
            task.serverConfig.proxyUsername,
            task.serverConfig.proxyPassword
        );
    }

    m_nam.setProxy(proxy);
    ImageCache::instance().setProxy(proxy);

    QUrl requestUrl(task.url);
    bool isOembed = false;
    if (isYoutubeUrl(task.url)) {
        requestUrl = QUrl(QStringLiteral("https://www.youtube.com/oembed?url=%1&format=json")
                              .arg(QString::fromLatin1(QUrl::toPercentEncoding(task.url))));
        isOembed = true;
    } else if (isVimeoUrl(task.url)) {
        requestUrl = QUrl(QStringLiteral("https://vimeo.com/api/oembed.json?url=%1")
                              .arg(QString::fromLatin1(QUrl::toPercentEncoding(task.url))));
        isOembed = true;
    }

    QNetworkRequest req(requestUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, NetworkSecurityUtils::genericUserAgent());
    req.setRawHeader("Range", "bytes=0-65535");
    req.setTransferTimeout(10000);

    QNetworkReply* reply = m_nam.get(req);
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, deadline, &QTimer::stop);
    deadline->start(10000);
    connect(reply, &QNetworkReply::readyRead, this, [reply]() {
        if (reply->bytesAvailable() > MAX_HTML_BYTES) {
            reply->abort();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, task, isOembed]() {
        --m_activeRequests;
        const auto error = reply->error();
        const QByteArray data = error == QNetworkReply::NoError ? reply->read(MAX_HTML_BYTES) : QByteArray{};
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
        reply->deleteLater();

        UrlPreview result;
        result.url = task.url;

        if (error != QNetworkReply::NoError || data.isEmpty()) {
            result.failed = true;
        } else if (isOembed) {
            const QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isObject()) {
                const QJsonObject obj = doc.object();
                result.title = obj.value(QStringLiteral("title")).toString();
                result.description = obj.value(QStringLiteral("author_name")).toString();
                result.imageUrl = obj.value(QStringLiteral("thumbnail_url")).toString();
                result.siteName = obj.value(QStringLiteral("provider_name")).toString(QStringLiteral("YouTube"));
            } else {
                result.failed = true;
            }
        } else if (contentType.startsWith(QStringLiteral("image/"))) {
            result.imageUrl = task.url;
            result.isImage = true;
        } else {
            result = parseOpenGraphHtml(task.url, QString::fromUtf8(data));
            if (!result.title && !result.description && !result.imageUrl) {
                result.failed = true;
            }
        }

        if (m_cache.size() >= MAX_CACHE_ENTRIES) {
            m_cache.erase(m_cache.begin());
        }
        m_cache.insert(task.url, result);
        task.callback(result);
        processNextInQueue();
    });
}

} // namespace loungecat
