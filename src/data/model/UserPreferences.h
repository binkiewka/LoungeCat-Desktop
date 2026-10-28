#pragma once

#include <QString>
#include <QSet>
#include <QList>
#include <QJsonObject>
#include <optional>
#include <variant>
#include "data/model/IncomingMessage.h"

namespace loungecat {

enum class FontSizePreference {
    SMALL,
    MEDIUM,
    LARGE
};

float fontSizeMultiplier(FontSizePreference pref);
QString fontSizeToString(FontSizePreference pref);
FontSizePreference fontSizeFromString(const QString& str);

enum class TimestampFormat {
    HOURS_12,
    HOURS_24,
    RELATIVE
};

QString timestampFormatToString(TimestampFormat fmt);
TimestampFormat timestampFormatFromString(const QString& str);

enum class UrlImageDisplayMode {
    INLINE,
    COMPACT,
    DISABLED
};

QString urlImageDisplayModeToString(UrlImageDisplayMode mode);
UrlImageDisplayMode urlImageDisplayModeFromString(const QString& str);

enum class JoinPartQuitDisplayMode {
    SHOW_ALL,
    HIDE_ALL,
    GROUPED,
    SMART_HIDE
};

QString joinPartQuitModeToString(JoinPartQuitDisplayMode mode);
JoinPartQuitDisplayMode joinPartQuitModeFromString(const QString& str);

enum class SplitDirection {
    HORIZONTAL,
    VERTICAL
};

enum class GridMode {
    SINGLE,
    SPLIT_HORIZONTAL,
    SPLIT_VERTICAL,
    GRID_2X2
};

struct MessageItemView {
    IncomingMessage message;
    bool operator==(const MessageItemView&) const = default;
};

struct JoinPartGroupView {
    QString id;
    QList<IncomingMessage> messages;
    qint64 timestamp = 0;

    QString summary() const;
    bool operator==(const JoinPartGroupView&) const = default;
};

using ChatViewItem = std::variant<MessageItemView, JoinPartGroupView>;

struct UserPreferences {
    FontSizePreference fontSize = FontSizePreference::MEDIUM;
    TimestampFormat timestampFormat = TimestampFormat::HOURS_24;
    UrlImageDisplayMode urlImageDisplayMode = UrlImageDisplayMode::INLINE;
    bool coloredNicknames = true;
    JoinPartQuitDisplayMode joinPartQuitMode = JoinPartQuitDisplayMode::SHOW_ALL;
    int smartHideMinutes = 10;
    QSet<QString> ignoredUsers;
    int activityIndicatorMinutes = 5;
    std::optional<QString> timezone;
    bool spellCheckEnabled = true;
    QString spellCheckLanguage = QStringLiteral("en_US");
    QString theme = QStringLiteral("Obsidian Veil");
    bool pastebinEnabled = true;
    int pastebinThreshold = 400;
    QSet<QString> highlightWords;
    bool notificationsEnabled = true;
    bool notifyOnMention = true;
    bool notifyOnHighlight = true;
    bool notifyOnPrivateMessage = true;
    bool soundAlertsEnabled = false;
    bool soundOnMention = true;
    bool soundOnHighlight = true;
    bool soundOnPrivateMessage = true;
    float soundVolume = 0.7f;
    bool splitViewEnabled = false;
    int inputHistorySize = 50;
    bool loggingEnabled = false;
    int historyReplayLines = 50;
    bool processLinkPreviewsFromOthers = true;
    QSet<QString> trustedPreviewUsers;
    bool useProxyForPreviews = true;
    std::optional<QString> imgbbApiKey;
    bool urlShorteningEnabled = true;
    int urlShorteningThreshold = 200;

    QJsonObject toJson() const;
    static UserPreferences fromJson(const QJsonObject& obj);
    static bool validateJson(const QJsonObject& object, QString* error = nullptr);

    bool operator==(const UserPreferences& other) const = default;
};

} // namespace loungecat
