#include "util/NetworkSecurityUtils.h"

namespace loungecat {

bool NetworkSecurityUtils::isPrivateOrLoopbackHost(const QString& host) {
    const QString h = host.trimmed().toLower();
    if (h.isEmpty()) return true;

    // Direct hostname string checks
    if (h == QStringLiteral("localhost") ||
        h.endsWith(QStringLiteral(".localhost")) ||
        h.endsWith(QStringLiteral(".local")) ||
        h.endsWith(QStringLiteral(".internal")) ||
        h.endsWith(QStringLiteral(".lan")) ||
        h.endsWith(QStringLiteral(".home")) ||
        h.endsWith(QStringLiteral(".home.arpa")) ||
        h.endsWith(QStringLiteral(".corp")) ||
        h.endsWith(QStringLiteral(".onion"))) {
        return true;
    }

    // IP address checks
    QHostAddress addr(h);
    if (!addr.isNull()) {
        if (addr.isLoopback()) return true;
        if (addr.isLinkLocal()) return true;
        if (addr.isMulticast()) return true;
        if (addr.isBroadcast()) return true;

        bool ok = false;
        const quint32 ip = addr.toIPv4Address(&ok);
        if (ok) {
            // 0.0.0.0/8
            if ((ip & 0xFF000000) == 0x00000000) return true;
            // 10.0.0.0/8 (RFC 1918)
            if ((ip & 0xFF000000) == 0x0A000000) return true;
            // 100.64.0.0/10 (Carrier-grade NAT)
            if ((ip & 0xFFC00000) == 0x64400000) return true;
            // 127.0.0.0/8 (Loopback)
            if ((ip & 0xFF000000) == 0x7F000000) return true;
            // 169.254.0.0/16 (Link Local & Cloud Metadata 169.254.169.254)
            if ((ip & 0xFFFF0000) == 0xA9FE0000) return true;
            // 172.16.0.0/12 (RFC 1918)
            if ((ip & 0xFFF00000) == 0xAC100000) return true;
            // 192.168.0.0/16 (RFC 1918)
            if ((ip & 0xFFFF0000) == 0xC0A80000) return true;
            // 192.0.2.0/24 (TEST-NET-1)
            if ((ip & 0xFFFFFF00) == 0xC0000200) return true;
            // 198.51.100.0/24 (TEST-NET-2)
            if ((ip & 0xFFFFFF00) == 0xC6336400) return true;
            // 203.0.113.0/24 (TEST-NET-3)
            if ((ip & 0xFFFFFF00) == 0xCB007100) return true;
            // 224.0.0.0/4 (Multicast)
            if ((ip & 0xF0000000) == 0xE0000000) return true;
            // 240.0.0.0/4 (Reserved)
            if ((ip & 0xF0000000) == 0xF0000000) return true;
        }

        // IPv6 Unique Local (fc00::/7)
        const Q_IPV6ADDR ip6 = addr.toIPv6Address();
        if ((ip6[0] & 0xFE) == 0xFC) return true;
    }

    return false;
}

bool NetworkSecurityUtils::isKnownTrackerOrLogger(const QString& host) {
    const QString h = host.trimmed().toLower();
    if (h.isEmpty()) return false;

    static const QSet<QString> trackers = {
        QStringLiteral("grabify.link"),
        QStringLiteral("iplogger.org"),
        QStringLiteral("iplogger.com"),
        QStringLiteral("iplogger.ru"),
        QStringLiteral("2no.co"),
        QStringLiteral("yip.su"),
        QStringLiteral("blasze.com"),
        QStringLiteral("canarytokens.org"),
        QStringLiteral("canarytokens.com"),
        QStringLiteral("curiouscat.live"),
        QStringLiteral("ps3cfw.com"),
        QStringLiteral("leancoding.co"),
        QStringLiteral("spottyfly.com"),
        QStringLiteral("stopify.co"),
        QStringLiteral("screenshare.host"),
        QStringLiteral("monochrome.host"),
        QStringLiteral("iplis.ru"),
        QStringLiteral("ezstat.ru"),
        QStringLiteral("whatsmydna.com"),
        QStringLiteral("partpicker.shop"),
        QStringLiteral("location.mobi"),
        QStringLiteral("trackip.net"),
        QStringLiteral("spying.me"),
        QStringLiteral("trax.to")
    };

    if (trackers.contains(h)) return true;
    for (const auto& t : trackers) {
        if (h.endsWith(QStringLiteral(".") + t)) return true;
    }

    return false;
}

bool NetworkSecurityUtils::isSafeWebUrl(const QString& urlStr) {
    if (urlStr.trimmed().isEmpty()) return false;

    const QUrl url(urlStr);
    if (!url.isValid()) return false;

    // 1. Enforce HTTP or HTTPS only (block file://, ftp://, javascript:, data:, etc.)
    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        return false;
    }

    // 2. Reject credentials in URL (user:pass@host)
    if (!url.userName().isEmpty() || !url.password().isEmpty()) {
        return false;
    }

    // 3. Reject empty host
    const QString host = url.host();
    if (host.isEmpty()) return false;

    // 4. Reject private, loopback, or metadata network destinations (SSRF defense)
    if (isPrivateOrLoopbackHost(host)) {
        return false;
    }

    // 5. Reject known IP grabber / logger services (Anti-tracking defense)
    if (isKnownTrackerOrLogger(host)) {
        return false;
    }

    // 6. Restrict to standard web ports
    const int port = url.port(scheme == QStringLiteral("https") ? 443 : 80);
    if (port != 80 && port != 443 && port != 8080 && port != 8443) {
        return false;
    }

    return true;
}

QString NetworkSecurityUtils::genericUserAgent() {
    // Standard generic Chrome browser User-Agent without leaking client, OS distro, or architecture
    return QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36");
}

bool NetworkSecurityUtils::isSafeImageContentType(const QString& contentType) {
    const QString ct = contentType.toLower().trimmed();
    return ct.startsWith(QStringLiteral("image/png")) ||
           ct.startsWith(QStringLiteral("image/jpeg")) ||
           ct.startsWith(QStringLiteral("image/jpg")) ||
           ct.startsWith(QStringLiteral("image/webp")) ||
           ct.startsWith(QStringLiteral("image/gif")) ||
           ct.startsWith(QStringLiteral("image/bmp")) ||
           ct.startsWith(QStringLiteral("image/svg+xml")) ||
           ct.startsWith(QStringLiteral("image/x-icon"));
}

} // namespace loungecat
