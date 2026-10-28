#pragma once

#include <QString>
#include <QJsonObject>
#include <optional>

namespace loungecat {

struct UrlPreview {
    QString url;
    std::optional<QString> title;
    std::optional<QString> description;
    std::optional<QString> imageUrl;
    std::optional<QString> siteName;
    std::optional<QString> faviconUrl;
    bool isImage = false;
    bool isLoading = false;
    bool failed = false;

    QJsonObject toJson() const;
    static UrlPreview fromJson(const QJsonObject& obj);

    bool operator==(const UrlPreview& other) const = default;
};

} // namespace loungecat

Q_DECLARE_METATYPE(loungecat::UrlPreview)
Q_DECLARE_METATYPE(QList<loungecat::UrlPreview>)
