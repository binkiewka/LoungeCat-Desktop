#include "data/model/UrlPreview.h"
#include <QJsonValue>

namespace loungecat {

QJsonObject UrlPreview::toJson() const {
    QJsonObject obj;
    obj[QStringLiteral("url")] = url;
    if (title) obj[QStringLiteral("title")] = *title;
    if (description) obj[QStringLiteral("description")] = *description;
    if (imageUrl) obj[QStringLiteral("imageUrl")] = *imageUrl;
    if (siteName) obj[QStringLiteral("siteName")] = *siteName;
    if (faviconUrl) obj[QStringLiteral("faviconUrl")] = *faviconUrl;
    obj[QStringLiteral("isImage")] = isImage;
    obj[QStringLiteral("isLoading")] = isLoading;
    obj[QStringLiteral("failed")] = failed;
    return obj;
}

UrlPreview UrlPreview::fromJson(const QJsonObject& obj) {
    UrlPreview p;
    p.url = obj.value(QStringLiteral("url")).toString();
    if (obj.contains(QStringLiteral("title")) && !obj.value(QStringLiteral("title")).isNull())
        p.title = obj.value(QStringLiteral("title")).toString();
    if (obj.contains(QStringLiteral("description")) && !obj.value(QStringLiteral("description")).isNull())
        p.description = obj.value(QStringLiteral("description")).toString();
    if (obj.contains(QStringLiteral("imageUrl")) && !obj.value(QStringLiteral("imageUrl")).isNull())
        p.imageUrl = obj.value(QStringLiteral("imageUrl")).toString();
    if (obj.contains(QStringLiteral("siteName")) && !obj.value(QStringLiteral("siteName")).isNull())
        p.siteName = obj.value(QStringLiteral("siteName")).toString();
    if (obj.contains(QStringLiteral("faviconUrl")) && !obj.value(QStringLiteral("faviconUrl")).isNull())
        p.faviconUrl = obj.value(QStringLiteral("faviconUrl")).toString();
    p.isImage = obj.value(QStringLiteral("isImage")).toBool(false);
    p.isLoading = obj.value(QStringLiteral("isLoading")).toBool(false);
    p.failed = obj.value(QStringLiteral("failed")).toBool(false);
    return p;
}

} // namespace loungecat
