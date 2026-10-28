#pragma once

#include <QObject>
#include <QString>
#include <QImage>
#include <QNetworkAccessManager>
#include <functional>
#include <QThreadPool>

namespace loungecat {

class ImgBbService : public QObject {
    Q_OBJECT
public:
    explicit ImgBbService(QObject* parent = nullptr);
    ~ImgBbService() override;
    static constexpr qint64 MAX_UPLOAD_BYTES = 8 * 1024 * 1024;
    static constexpr qint64 MAX_CLIPBOARD_BYTES = 32 * 1024 * 1024;

    using UploadCallback = std::function<void(bool success, const QString& urlOrError)>;

    void uploadFromFile(const QString& filePath, const QString& apiKey, UploadCallback callback);
    void uploadFromClipboard(const QString& apiKey, UploadCallback callback);
    void uploadImageBytes(const QByteArray& rawBytes, const QString& apiKey, UploadCallback callback);

private:
    bool admit(const QString& apiKey, const UploadCallback& callback);
    void prepare(std::function<QByteArray()> source, const QString& apiKey, UploadCallback callback);
    int m_activeUploads = 0;
    QNetworkAccessManager m_nam;
    QThreadPool m_pool;
};

} // namespace loungecat
