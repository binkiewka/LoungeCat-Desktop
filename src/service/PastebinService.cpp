#include "service/PastebinService.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QUrl>
#include <QTimer>
#include "util/UploadResult.h"

namespace loungecat {

PastebinService::PastebinService(QObject* parent)
    : QObject(parent) {}
PastebinService::~PastebinService() {
    for (auto* reply : m_nam.findChildren<QNetworkReply*>()) { reply->disconnect(this); reply->abort(); }
}

void PastebinService::upload(const QString& content, UploadCallback callback) {
    auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("text/plain; charset=utf-8")));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"paste.txt\"")));
    filePart.setBody(content.toUtf8());
    multiPart->append(filePart);

    QNetworkRequest req(QUrl(QStringLiteral("https://0x0.st")));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LoungeCat-IRC/1.0"));
    req.setTransferTimeout(15000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);

    QNetworkReply* reply = m_nam.post(req, multiPart);
    multiPart->setParent(reply);
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, deadline, &QTimer::stop);
    deadline->start(15000);
    connect(reply, &QIODevice::readyRead, reply, [reply]() {
        if (reply->bytesAvailable() > 8192) reply->abort();
    });

    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300 && reply->bytesAvailable() <= 8192;
        const QByteArray resp = ok ? reply->read(8192) : QByteArray();
        const QString errStr = reply->errorString();
        reply->deleteLater();

        if (ok) {
            const QString url = QString::fromUtf8(resp).trimmed();
            if (validUploadResult(url, {QStringLiteral("0x0.st")})) {
                callback(true, url);
                return;
            }
        }
        callback(false, ok ? QStringLiteral("Paste service returned an invalid URL.") :
            (status ? QStringLiteral("Paste upload failed (HTTP %1).").arg(status) :
             (errStr.isEmpty() ? QStringLiteral("Pastebin upload failed") : errStr)));
    });
}

} // namespace loungecat
