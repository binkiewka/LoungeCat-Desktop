#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QQueue>
#include <QNetworkAccessManager>
#include <functional>
#include "data/model/UrlPreview.h"
#include "data/model/ServerConfig.h"

namespace loungecat {

class LinkPreviewService : public QObject {
    Q_OBJECT
public:
    static constexpr int MAX_CONCURRENT_REQUESTS = 3;
    static constexpr int MAX_CACHE_ENTRIES = 200;
    static constexpr int MAX_PENDING_REQUESTS = 64;
    static constexpr int MAX_HTML_BYTES = 64 * 1024;

    explicit LinkPreviewService(QObject* parent = nullptr);
    ~LinkPreviewService() override;

    static QStringList extractUrls(const QString& text);
    static bool isImageUrl(const QString& url);
    static bool isYoutubeUrl(const QString& url);
    static bool isVimeoUrl(const QString& url);
    static UrlPreview parseOpenGraphHtml(const QString& url, const QString& html);

    using PreviewCallback = std::function<void(const UrlPreview&)>;
    void fetchPreview(
        const QString& url,
        const ServerConfig& serverConfig,
        bool useProxy,
        PreviewCallback callback
    );

private:
    struct PendingTask {
        QString url;
        ServerConfig serverConfig;
        bool useProxy = false;
        PreviewCallback callback;
    };

    void processNextInQueue();
    void startRequest(const PendingTask& task);

    QNetworkAccessManager m_nam;
    QMap<QString, UrlPreview> m_cache;
    QQueue<PendingTask> m_queue;
    int m_activeRequests = 0;
};

} // namespace loungecat
