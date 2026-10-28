#include "service/ImageCache.h"
#include "util/NetworkSecurityUtils.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QBuffer>
#include <QImageReader>
#include <QCryptographicHash>
#include <QNetworkRequest>
#include <QUrl>
#include <QTimer>
#include <QFutureWatcher>
#include <QDirIterator>
#include <QDateTime>
#include <QMutex>
#include <QThread>
#include <QCoreApplication>
#include <QPointer>
#include <QtConcurrent>
#include <mutex>
#include <algorithm>

namespace {
struct ImageResult { QImage image; QString storageError; };
QMutex imageDiskMutex;
bool ensurePrivateImageDirectory(const QString& directory, QString& error) {
    if (!QDir().mkpath(directory) ||
        !QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
        error = QStringLiteral("Cannot prepare an owner-private image-cache directory.");
        return false;
    }
    return true;
}
bool trimImageDisk(const QString& directory, QString& error) {
    if (!ensurePrivateImageDirectory(directory, error)) return false;
    QList<QFileInfo> files;
    qint64 total = 0;
    QDirIterator iterator(directory, QDir::Files | QDir::NoSymLinks);
    while (iterator.hasNext()) {
        iterator.next();
        const auto info = iterator.fileInfo();
        if (info.fileName().size() != 64) continue;
        if (!QFile::setPermissions(info.absoluteFilePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            error = QStringLiteral("Cannot restrict an image-cache file to owner access.");
            return false;
        }
        if (files.size() >= 4096) { error = QStringLiteral("Image cache needs manual cleanup before new files can be saved."); return false; }
        files.append(info);
        total += info.size();
    }
    std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) { return left.lastModified() < right.lastModified(); });
    int retained = files.size();
    const auto expiry = QDateTime::currentDateTimeUtc().addDays(-30);
    for (const auto& info : files) {
        if (retained <= loungecat::ImageCache::MAX_DISK_IMAGES && total <= loungecat::ImageCache::MAX_DISK_BYTES &&
            info.lastModified() >= expiry && info.size() <= loungecat::ImageCache::MAX_IMAGE_BYTES) continue;
        if (!QFile::remove(info.absoluteFilePath())) { error = QStringLiteral("Cannot enforce the image-cache disk quota."); return false; }
        total -= info.size();
        --retained;
    }
    return true;
}
QImage decodeThumbnail(const QByteArray& data) {
    QBuffer buffer;
    buffer.setData(data);
    if (!buffer.open(QIODevice::ReadOnly)) return {};
    QImageReader reader(&buffer);
    reader.setAutoDetectImageFormat(true);
    const auto size = reader.size();
    if (size.isValid() && (size.width() > loungecat::ImageCache::MAX_THUMBNAIL_WIDTH ||
        size.height() > loungecat::ImageCache::MAX_THUMBNAIL_HEIGHT)) {
        auto target = size;
        target.scale(loungecat::ImageCache::MAX_THUMBNAIL_WIDTH, loungecat::ImageCache::MAX_THUMBNAIL_HEIGHT, Qt::KeepAspectRatio);
        reader.setScaledSize(target);
    }
    return reader.read();
}
}


namespace loungecat {

namespace { QPointer<ImageCache> imageSingleton; }
ImageCache& ImageCache::instance() {
    // Destroy network state with the application, before Qt's global shutdown.
    Q_ASSERT(QCoreApplication::instance());
    if (!imageSingleton) imageSingleton = new ImageCache(QCoreApplication::instance());
    return *imageSingleton;
}
void ImageCache::shutdown() { delete imageSingleton.data(); }

ImageCache::ImageCache(QObject* parent)
    : QObject(parent) {
    m_pool.setMaxThreadCount(2);
    static std::once_flag allocationLimit;
    std::call_once(allocationLimit, []() { QImageReader::setAllocationLimit(32); });
    const QString basePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    m_diskCacheDir = basePath.isEmpty()
        ? QDir::tempPath() + QStringLiteral("/loungecat_images")
        : basePath + QStringLiteral("/loungecat_images");
    QString directoryError;
    if (!ensurePrivateImageDirectory(m_diskCacheDir, directoryError)) {
        QTimer::singleShot(0, this, [this, directoryError]() { Q_EMIT storageError(directoryError); });
    }
}

void ImageCache::setProxy(const QNetworkProxy& proxy) {
    m_nam.setProxy(proxy);
}

QString ImageCache::diskCachePath(const QString& url) const {
    const QByteArray hash = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex();
    return m_diskCacheDir + QStringLiteral("/") + QString::fromLatin1(hash);
}

ImageCache::~ImageCache() {
    m_stopping = true;
    for (auto* reply : m_nam.findChildren<QNetworkReply*>()) { reply->disconnect(this); reply->abort(); }
    m_pool.clear();
    m_pool.waitForDone();
}

bool ImageCache::hasImage(const QString& url) const {
    if (url.trimmed().isEmpty()) return false;
    if (m_memoryCache.contains(url)) return true;
    return QFile::exists(diskCachePath(url));
}

QSize ImageCache::getImageSize(const QString& url) const {
    if (url.trimmed().isEmpty()) return QSize();
    auto it = m_memoryCache.find(url);
    if (it != m_memoryCache.end()) return it.value().size();
    return QSize();
}

void ImageCache::clearMemoryCache() {
    m_memoryCache.clear();
    m_lruKeys.clear();
    m_recentFailures.clear();
}

bool ImageCache::hasRecentFailure(const QString& url) const {
    const auto entry = m_recentFailures.constFind(url);
    return entry != m_recentFailures.constEnd() && !entry->hasExpired();
}

void ImageCache::rememberFailure(const QString& url) {
    for (auto entry = m_recentFailures.begin(); entry != m_recentFailures.end();) {
        if (entry->hasExpired()) entry = m_recentFailures.erase(entry);
        else ++entry;
    }
    if (m_recentFailures.size() >= MAX_MEMORY_IMAGES && !m_recentFailures.contains(url)) {
        m_recentFailures.erase(m_recentFailures.begin());
    }
    m_recentFailures.insert(url, QDeadlineTimer(FAILURE_RETRY_MS));
    Q_EMIT imageFailed(url);
}

void ImageCache::cacheImage(const QString& url, const QImage& image) {
    m_lruKeys.removeAll(url);
    while (!m_memoryCache.contains(url) && m_memoryCache.size() >= MAX_MEMORY_IMAGES && !m_lruKeys.isEmpty()) {
        m_memoryCache.remove(m_lruKeys.takeFirst());
    }
    m_memoryCache.insert(url, image);
    m_lruKeys.append(url);
    m_recentFailures.remove(url);
}

QImage ImageCache::getImage(const QString& url) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (url.trimmed().isEmpty()) return QImage();

    // Security check: drop private LAN / loopback / tracker URLs
    if (!NetworkSecurityUtils::isSafeWebUrl(url)) {
        return QImage();
    }

    auto it = m_memoryCache.find(url);
    if (it != m_memoryCache.end()) {
        m_lruKeys.removeAll(url);
        m_lruKeys.append(url);
        return it.value();
    }

    if (hasRecentFailure(url)) return QImage();
    if (!m_queuedUrls.contains(url) && !m_activeDownloads.contains(url)) {
        if (m_downloadQueue.size() >= MAX_PENDING_DOWNLOADS) {
            rememberFailure(url);
            return QImage();
        }
        m_queuedUrls.insert(url);
        m_downloadQueue.enqueue(url);
        processQueue();
    }

    return QImage();
}

void ImageCache::processQueue() {
    if (m_stopping) return;
    while (m_activeDownloads.size() < MAX_CONCURRENT_DOWNLOADS && !m_downloadQueue.isEmpty()) {
        const QString nextUrl = m_downloadQueue.dequeue();
        m_queuedUrls.remove(nextUrl);
        m_activeDownloads.insert(nextUrl);
        startDownload(nextUrl);
    }
}

void ImageCache::startDownload(const QString& url) {
    auto* watcher = new QFutureWatcher<ImageResult>(this);
    const auto path = diskCachePath(url);
    const auto directory = m_diskCacheDir;
    connect(watcher, &QFutureWatcher<ImageResult>::finished, this, [this, watcher, url]() {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (m_stopping) return;
        if (!result.storageError.isEmpty()) Q_EMIT storageError(result.storageError);
        if (result.image.isNull()) { startNetworkDownload(url); return; }
        m_activeDownloads.remove(url);
        cacheImage(url, result.image);
        Q_EMIT imageLoaded(url);
        processQueue();
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [path, directory]() {
        ImageResult result;
        QByteArray bytes;
        {
            QMutexLocker locker(&imageDiskMutex);
            if (!trimImageDisk(directory, result.storageError)) return result;
            QFile file(path);
            if (file.open(QIODevice::ReadOnly) && file.size() <= MAX_IMAGE_BYTES) bytes = file.read(MAX_IMAGE_BYTES + 1);
        }
        if (!bytes.isEmpty() && bytes.size() <= MAX_IMAGE_BYTES) result.image = decodeThumbnail(bytes);
        return result;
    }));
}
void ImageCache::startNetworkDownload(const QString& url) {
    if (!NetworkSecurityUtils::isSafeWebUrl(url)) {
        m_activeDownloads.remove(url);
        processQueue();
        return;
    }

    const QUrl targetUrl(url);
    QNetworkRequest req(targetUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, NetworkSecurityUtils::genericUserAgent());
    req.setRawHeader("Accept", "image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8");
    req.setTransferTimeout(10000);

    QNetworkReply* reply = m_nam.get(req);
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, deadline, &QTimer::stop);
    deadline->start(10000);

    // Abort if stream exceeds MAX_IMAGE_BYTES (5MB)
    connect(reply, &QNetworkReply::readyRead, this, [reply]() {
        if (reply->bytesAvailable() > MAX_IMAGE_BYTES) {
            reply->abort();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        const auto err = reply->error();
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
        const QByteArray data = err == QNetworkReply::NoError ? reply->read(MAX_IMAGE_BYTES) : QByteArray{};
        reply->deleteLater();

        if (err == QNetworkReply::NoError && !data.isEmpty()) {
            // Strictly verify Content-Type: reject executables, scripts, html, etc.
            if (!contentType.isEmpty() && !NetworkSecurityUtils::isSafeImageContentType(contentType)) {
                rememberFailure(url);
                m_activeDownloads.remove(url);
                processQueue();
                return;
            }

            // Reject SVGs containing XML entity declarations (Billion Laughs / XXE attack vector)
            if (contentType.contains(QStringLiteral("svg")) || url.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
                if (data.contains("<!ENTITY") || data.contains("<!DOCTYPE") || data.contains("SYSTEM \"")) {
                    rememberFailure(url);
                    m_activeDownloads.remove(url);
                    processQueue();
                    return;
                }
            }

            auto* watcher = new QFutureWatcher<ImageResult>(this);
            const auto path = diskCachePath(url);
            const auto directory = m_diskCacheDir;
            connect(watcher, &QFutureWatcher<ImageResult>::finished, this, [this, watcher, url]() {
                const auto result = watcher->result();
                watcher->deleteLater();
                if (m_stopping) return;
                m_activeDownloads.remove(url);
                if (!result.storageError.isEmpty()) Q_EMIT storageError(result.storageError);
                if (result.image.isNull()) rememberFailure(url);
                else { cacheImage(url, result.image); Q_EMIT imageLoaded(url); }
                processQueue();
            });
            watcher->setFuture(QtConcurrent::run(&m_pool, [data, path, directory]() {
                ImageResult result;
                result.image = decodeThumbnail(data);
                if (result.image.isNull()) return result;
                QMutexLocker locker(&imageDiskMutex);
                if (!trimImageDisk(directory, result.storageError)) return result;
                QSaveFile file(path);
                if (!file.open(QIODevice::WriteOnly) ||
                    !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
                    file.write(data) != data.size() || !file.commit())
                    result.storageError = QStringLiteral("Cannot save an owner-private image-cache file.");
                else trimImageDisk(directory, result.storageError);
                return result;
            }));
            return;
        } else {
            rememberFailure(url);
        }

        m_activeDownloads.remove(url);
        processQueue();
    });
}

} // namespace loungecat
