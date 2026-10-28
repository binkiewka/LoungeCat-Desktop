#pragma once
#include <QString>
#include <QStringList>
#include <QUrl>

namespace loungecat {
// Remote responses are data, not user-entered URLs: reject rather than repair.
inline bool validUploadResult(const QString& value, const QStringList& hosts) {
    if (value.isEmpty() || value.size() > 2048) return false;
    for (const auto c : value) if (c.isSpace() || c.unicode() < 32 || c.unicode() == 127) return false;
    const QUrl url(value, QUrl::StrictMode);
    return url.isValid() && url.scheme() == QStringLiteral("https") &&
        hosts.contains(url.host()) && url.userInfo().isEmpty() && url.port(-1) == -1 &&
        !url.path().isEmpty() && !url.hasFragment();
}
}
