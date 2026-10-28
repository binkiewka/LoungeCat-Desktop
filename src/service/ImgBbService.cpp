#include "service/ImgBbService.h"
#include <QFile>
#include <QBuffer>
#include <QGuiApplication>
#include <QClipboard>
#include <QImageReader>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFutureWatcher>
#include <QTimer>
#include <QThread>
#include <QtConcurrent>
#include <QtEndian>
#include <memory>
#include "util/UploadResult.h"

namespace loungecat {
namespace {
bool isAnimatedPng(QFile& file) {
    // Qt versions without APNG support report an animated PNG as a one-frame
    // PNG. Inspect its container declaration before the still-image encoder.
    if (file.peek(8) != QByteArray::fromHex("89504e470d0a1a0a")) return false;
    const auto initialPosition = file.pos();
    bool animated = false;
    if (file.seek(initialPosition + 8)) {
        while (!file.atEnd()) {
            const auto header = file.read(8);
            if (header.size() != 8) break;
            const auto length = qFromBigEndian<quint32>(header.constData());
            const auto remaining = file.size() - file.pos();
            if (remaining < 4 || static_cast<qint64>(length) > remaining - 4) break;
            const auto type = header.mid(4, 4);
            if (type == "acTL" && length == 8) {
                // Even one animation frame can differ from the default poster
                // image that a non-APNG decoder reads. Reject this container.
                animated = true;
                break;
            }
            if (type == "IDAT" || type == "IEND") break;
            if (!file.seek(file.pos() + static_cast<qint64>(length) + 4)) break;
        }
    }
    file.seek(initialPosition);
    return animated;
}
} // namespace

ImgBbService::ImgBbService(QObject* parent) : QObject(parent) { m_pool.setMaxThreadCount(1); }
ImgBbService::~ImgBbService() {
    for (auto* reply : m_nam.findChildren<QNetworkReply*>()) { reply->disconnect(this); reply->abort(); }
    m_pool.clear();
    m_pool.waitForDone();
}
bool ImgBbService::admit(const QString& apiKey, const UploadCallback& callback) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (apiKey.trimmed().isEmpty()) { callback(false, QStringLiteral("ImgBB API key is not configured in Settings")); return false; }
    if (m_activeUploads >= 2) { callback(false, QStringLiteral("An image upload is already queued. Try again when it finishes.")); return false; }
    ++m_activeUploads;
    return true;
}
void ImgBbService::uploadFromFile(const QString& filePath, const QString& apiKey, UploadCallback callback) {
    if (!admit(apiKey, callback)) return;
    auto preparationError = std::make_shared<QString>();
    prepare([filePath, preparationError]() {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > MAX_UPLOAD_BYTES) return QByteArray{};
        const bool animatedPng = isAnimatedPng(file);
        QImageReader reader(&file);
        reader.setAutoTransform(true);
        const auto sourceFormat = reader.format().toLower();
        const auto dimensions = reader.size();
        if (!dimensions.isValid() || static_cast<qint64>(dimensions.width()) * dimensions.height() > 8 * 1024 * 1024) return QByteArray{};
        if (animatedPng || (reader.supportsAnimation() && reader.imageCount() > 1)) {
            *preparationError = QStringLiteral("Animated image uploads are not supported by the metadata-safe encoder. Choose a still image.");
            return QByteArray{};
        }
        auto image = reader.read();
        if (image.isNull() || image.sizeInBytes() > MAX_CLIPBOARD_BYTES) return QByteArray{};
        for (const auto& key : image.textKeys()) image.setText(key, QString());
        QByteArray bytes;
        QBuffer output(&bytes);
        if (!output.open(QIODevice::WriteOnly) || !image.save(&output, !image.hasAlphaChannel() && (sourceFormat == "jpeg" || sourceFormat == "jpg") ? "JPEG" : "PNG", 92)) return QByteArray{};
        return bytes.size() <= MAX_UPLOAD_BYTES ? bytes : QByteArray{};
    }, apiKey, [callback = std::move(callback), preparationError](bool success, const QString& result) {
        callback(success, !success && !preparationError->isEmpty() ? *preparationError : result);
    });
}
void ImgBbService::uploadFromClipboard(const QString& apiKey, UploadCallback callback) {
    Q_ASSERT(QThread::currentThread() == thread());
    const auto* clipboard = QGuiApplication::clipboard();
    const auto image = clipboard ? clipboard->image() : QImage{};
    if (image.isNull() || image.sizeInBytes() > MAX_CLIPBOARD_BYTES) {
        callback(false, QStringLiteral("Clipboard image is unavailable or exceeds 32 MiB.")); return;
    }
    if (!admit(apiKey, callback)) return;
    prepare([image]() {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG") || bytes.size() > MAX_UPLOAD_BYTES) return QByteArray{};
        return bytes;
    }, apiKey, std::move(callback));
}
void ImgBbService::uploadImageBytes(const QByteArray& bytes, const QString& apiKey, UploadCallback callback) {
    if (bytes.isEmpty() || bytes.size() > MAX_UPLOAD_BYTES) { callback(false, QStringLiteral("Image upload exceeds the 8 MiB limit.")); return; }
    if (!admit(apiKey, callback)) return;
    prepare([bytes]() { return bytes; }, apiKey, std::move(callback));
}
void ImgBbService::prepare(std::function<QByteArray()> source, const QString& apiKey, UploadCallback callback) {
    auto* watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [this, watcher, callback]() {
        const auto body = watcher->result();
        watcher->deleteLater();
        if (body.isEmpty()) {
            --m_activeUploads;
            callback(false, QStringLiteral("Image could not be read or encoded within the supported limits."));
            return;
        }
        QNetworkRequest request(QUrl(QStringLiteral("https://api.imgbb.com/1/upload")));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
        request.setTransferTimeout(20000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        auto* reply = m_nam.post(request, body);
        auto* deadline = new QTimer(reply);
        deadline->setSingleShot(true);
        connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
        connect(reply, &QNetworkReply::finished, deadline, &QTimer::stop);
        deadline->start(20000);
        connect(reply, &QNetworkReply::readyRead, reply, [reply]() {
            if (reply->bytesAvailable() > 256 * 1024) reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
            --m_activeUploads;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool success = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300 && reply->bytesAvailable() <= 256 * 1024;
            const auto response = success ? reply->read(256 * 1024) : QByteArray{};
            const auto error = reply->errorString();
            reply->deleteLater();
            if (success) {
                const auto data = QJsonDocument::fromJson(response).object().value(QStringLiteral("data")).toObject();
                const auto url = data.value(QStringLiteral("url")).toString();
                if (validUploadResult(url, {QStringLiteral("i.ibb.co")})) { callback(true, url); return; }
            }
            callback(false, success ? QStringLiteral("ImgBB returned an invalid response.") : error);
        });
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [source = std::move(source), apiKey]() {
        const auto bytes = source();
        if (bytes.isEmpty() || bytes.size() > MAX_UPLOAD_BYTES) return QByteArray{};
        QUrlQuery form;
        form.addQueryItem(QStringLiteral("key"), apiKey.trimmed());
        form.addQueryItem(QStringLiteral("image"), QString::fromLatin1(bytes.toBase64().toPercentEncoding()));
        return form.toString(QUrl::FullyEncoded).toUtf8();
    }));
}
}
