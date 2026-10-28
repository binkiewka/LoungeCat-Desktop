#pragma once

#include <QString>
#include <QUrl>
#include <QHostAddress>
#include <QSet>

namespace loungecat {

class NetworkSecurityUtils {
public:
    // Returns true if the URL is safe for preview fetching (http/https, public IP, non-tracker, safe port)
    static bool isSafeWebUrl(const QString& urlStr);

    // Returns true if the host is a private, loopback, link-local, or cloud metadata address
    static bool isPrivateOrLoopbackHost(const QString& host);

    // Returns true if the host is a known IP grabber / tracking service
    static bool isKnownTrackerOrLogger(const QString& host);

    // Returns a generic, privacy-preserving browser User-Agent
    static QString genericUserAgent();

    // Returns true if the HTTP Content-Type is a safe image MIME type
    static bool isSafeImageContentType(const QString& contentType);
};

} // namespace loungecat
