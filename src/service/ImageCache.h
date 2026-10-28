#pragma once

#include <QObject>
#include <QString>
#include <QImage>
#include <QMap>
#include <QQueue>
#include <QSet>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QDeadlineTimer>
#include <QThreadPool>

namespace loungecat {

class ImageCache : public QObject {
    Q_OBJECT
public:
    static ImageCache& instance();
    static void shutdown();

    static constexpr int MAX_MEMORY_IMAGES = 150;
    static constexpr int MAX_CONCURRENT_DOWNLOADS = 4;
    static constexpr int MAX_PENDING_DOWNLOADS = 64;
    static constexpr int FAILURE_RETRY_MS = 60000;
    static constexpr qint64 MAX_IMAGE_BYTES = 5 * 1024 * 1024; // 5MB limit
    static constexpr int MAX_THUMBNAIL_WIDTH = 480;
    static constexpr int MAX_THUMBNAIL_HEIGHT = 320;
    static constexpr qint64 MAX_DISK_BYTES = 128 * 1024 * 1024;
    static constexpr int MAX_DISK_IMAGES = 256;

    explicit ImageCache(QObject* parent = nullptr);
    ~ImageCache() override;

    // Returns cached image, or null QImage if not yet loaded (and begins async fetch)
    QImage getImage(const QString& url);
    bool hasImage(const QString& url) const;
    QSize getImageSize(const QString& url) const;
    bool hasRecentFailure(const QString& url) const;
    void clearMemoryCache();

    // Sets proxy for all image preview network downloads
    void setProxy(const QNetworkProxy& proxy);

Q_SIGNALS:
    void imageLoaded(const QString& url);
    void imageFailed(const QString& url);
    void storageError(const QString& error);

private:
    void processQueue();
    void startDownload(const QString& url);
    void startNetworkDownload(const QString& url);
    QString diskCachePath(const QString& url) const;
    void cacheImage(const QString& url, const QImage& image);
    void rememberFailure(const QString& url);

    QNetworkAccessManager m_nam;
    QMap<QString, QImage> m_memoryCache;
    QMap<QString, QDeadlineTimer> m_recentFailures;
    QList<QString> m_lruKeys;
    QQueue<QString> m_downloadQueue;
    QSet<QString> m_queuedUrls;
    QSet<QString> m_activeDownloads;
    QString m_diskCacheDir;
    bool m_stopping = false;
    QThreadPool m_pool;
};

} // namespace loungecat
