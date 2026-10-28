#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <QNetworkAccessManager>
#include <functional>

namespace loungecat {

class UrlShortenerService : public QObject {
    Q_OBJECT
public:
    explicit UrlShortenerService(QObject* parent = nullptr);
    ~UrlShortenerService() override;

    using ShortenCallback = std::function<void(const QString& resultUrl)>;
    void shortenUrl(const QString& originalUrl, ShortenCallback callback);
    void processMessageUrls(const QString& message, int threshold, ShortenCallback callback);

private:
    QNetworkAccessManager m_nam;
    QMap<QString, QString> m_cache;
};

} // namespace loungecat
