#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <functional>

namespace loungecat {

class PastebinService : public QObject {
    Q_OBJECT
public:
    explicit PastebinService(QObject* parent = nullptr);
    ~PastebinService() override;

    using UploadCallback = std::function<void(bool success, const QString& urlOrError)>;
    void upload(const QString& content, UploadCallback callback);

private:
    QNetworkAccessManager m_nam;
};

} // namespace loungecat
