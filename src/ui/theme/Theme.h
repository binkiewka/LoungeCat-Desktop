#pragma once

#include <QString>
#include <QColor>
#include <QJsonObject>

namespace loungecat {

struct ThemeColors {
    QString name = QStringLiteral("Dark");
    bool isDark = true;

    QColor windowBackground;
    QColor currentLineBackground;
    QColor selectionBackground;
    QColor foreground;
    QColor comment;
    QColor cyan;
    QColor green;
    QColor orange;
    QColor pink;
    QColor purple;
    QColor red;
    QColor yellow;
    QColor border;
    QColor chatBackground;
    QColor inputBackground;
    QColor sidebarBackground;
    QColor timestampColor;
    QColor systemMessageColor;
    QColor actionMessageColor;
    QColor highlightBackground;
    QColor unreadBadge;
    QColor accentColor;

    static ThemeColors darkTheme();
    static ThemeColors lightTheme();
    static ThemeColors highVoltageTheme();
    static ThemeColors neonNightTheme();
    static ThemeColors retroWarmthTheme();
    static ThemeColors stormySeaTheme();
    static ThemeColors obsidianVeilTheme();

    QJsonObject toJson() const;
    static ThemeColors fromJson(const QJsonObject& obj, const QString& fallbackName = QStringLiteral("Custom"));
    QString generateStyleSheet(float fontScale = 1.0f) const;

    bool operator==(const ThemeColors& other) const = default;
};

} // namespace loungecat
