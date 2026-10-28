#include "data/model/UserPreferences.h"
#include <QJsonArray>
#include <QJsonValue>
#include <algorithm>
#include <cmath>

namespace loungecat {

float fontSizeMultiplier(FontSizePreference pref) {
    switch (pref) {
        case FontSizePreference::SMALL:  return 0.85f;
        case FontSizePreference::LARGE:  return 1.15f;
        case FontSizePreference::MEDIUM:
        default:                         return 1.0f;
    }
}

QString fontSizeToString(FontSizePreference pref) {
    switch (pref) {
        case FontSizePreference::SMALL:  return QStringLiteral("SMALL");
        case FontSizePreference::LARGE:  return QStringLiteral("LARGE");
        case FontSizePreference::MEDIUM:
        default:                         return QStringLiteral("MEDIUM");
    }
}

FontSizePreference fontSizeFromString(const QString& str) {
    const QString u = str.trimmed().toUpper();
    if (u == QStringLiteral("SMALL")) return FontSizePreference::SMALL;
    if (u == QStringLiteral("LARGE")) return FontSizePreference::LARGE;
    return FontSizePreference::MEDIUM;
}

QString timestampFormatToString(TimestampFormat fmt) {
    switch (fmt) {
        case TimestampFormat::HOURS_12: return QStringLiteral("HOURS_12");
        case TimestampFormat::RELATIVE: return QStringLiteral("RELATIVE");
        case TimestampFormat::HOURS_24:
        default:                        return QStringLiteral("HOURS_24");
    }
}

TimestampFormat timestampFormatFromString(const QString& str) {
    const QString u = str.trimmed().toUpper();
    if (u == QStringLiteral("HOURS_12")) return TimestampFormat::HOURS_12;
    if (u == QStringLiteral("RELATIVE")) return TimestampFormat::RELATIVE;
    return TimestampFormat::HOURS_24;
}

QString urlImageDisplayModeToString(UrlImageDisplayMode mode) {
    switch (mode) {
        case UrlImageDisplayMode::COMPACT:  return QStringLiteral("COMPACT");
        case UrlImageDisplayMode::DISABLED: return QStringLiteral("DISABLED");
        case UrlImageDisplayMode::INLINE:
        default:                            return QStringLiteral("INLINE");
    }
}

UrlImageDisplayMode urlImageDisplayModeFromString(const QString& str) {
    const QString u = str.trimmed().toUpper();
    if (u == QStringLiteral("COMPACT"))  return UrlImageDisplayMode::COMPACT;
    if (u == QStringLiteral("DISABLED")) return UrlImageDisplayMode::DISABLED;
    return UrlImageDisplayMode::INLINE;
}

QString joinPartQuitModeToString(JoinPartQuitDisplayMode mode) {
    switch (mode) {
        case JoinPartQuitDisplayMode::HIDE_ALL:   return QStringLiteral("HIDE_ALL");
        case JoinPartQuitDisplayMode::GROUPED:    return QStringLiteral("GROUPED");
        case JoinPartQuitDisplayMode::SMART_HIDE: return QStringLiteral("SMART_HIDE");
        case JoinPartQuitDisplayMode::SHOW_ALL:
        default:                                  return QStringLiteral("SHOW_ALL");
    }
}

JoinPartQuitDisplayMode joinPartQuitModeFromString(const QString& str) {
    const QString u = str.trimmed().toUpper();
    if (u == QStringLiteral("HIDE_ALL"))   return JoinPartQuitDisplayMode::HIDE_ALL;
    if (u == QStringLiteral("GROUPED"))    return JoinPartQuitDisplayMode::GROUPED;
    if (u == QStringLiteral("SMART_HIDE")) return JoinPartQuitDisplayMode::SMART_HIDE;
    return JoinPartQuitDisplayMode::SHOW_ALL;
}

QString JoinPartGroupView::summary() const {
    int joins = 0;
    int parts = 0;
    int quits = 0;
    int nicks = 0;
    for (const auto& m : messages) {
        switch (m.type) {
            case MessageType::JOIN: ++joins; break;
            case MessageType::PART: ++parts; break;
            case MessageType::QUIT: ++quits; break;
            case MessageType::NICK: ++nicks; break;
            default: break;
        }
    }
    QStringList partsList;
    if (joins > 0) partsList.append(QStringLiteral("%1 joined").arg(joins));
    if (parts > 0) partsList.append(QStringLiteral("%1 left").arg(parts));
    if (quits > 0) partsList.append(QStringLiteral("%1 quit").arg(quits));
    if (nicks > 0) partsList.append(QStringLiteral("%1 changed nick").arg(nicks));
    return partsList.join(QStringLiteral(", "));
}

static QJsonArray setToSortedJsonArray(const QSet<QString>& set) {
    QStringList list = set.values();
    std::sort(list.begin(), list.end());
    QJsonArray arr;
    for (const auto& s : list) arr.append(s);
    return arr;
}

static QSet<QString> jsonArrayToSet(const QJsonValue& val, bool lowerCase) {
    QSet<QString> res;
    if (val.isArray()) {
        for (const auto& item : val.toArray()) {
            QString s = item.toString().trimmed();
            if (!s.isEmpty()) {
                res.insert(lowerCase ? s.toLower() : s);
            }
        }
    }
    return res;
}

QJsonObject UserPreferences::toJson() const {
    QJsonObject obj;
    obj[QStringLiteral("fontSize")] = fontSizeToString(fontSize);
    obj[QStringLiteral("timestampFormat")] = timestampFormatToString(timestampFormat);
    obj[QStringLiteral("urlImageDisplayMode")] = urlImageDisplayModeToString(urlImageDisplayMode);
    obj[QStringLiteral("coloredNicknames")] = coloredNicknames;
    obj[QStringLiteral("joinPartQuitMode")] = joinPartQuitModeToString(joinPartQuitMode);
    obj[QStringLiteral("smartHideMinutes")] = smartHideMinutes;
    obj[QStringLiteral("ignoredUsers")] = setToSortedJsonArray(ignoredUsers);
    obj[QStringLiteral("activityIndicatorMinutes")] = activityIndicatorMinutes;
    obj[QStringLiteral("timezone")] = timezone.has_value() ? QJsonValue(*timezone) : QJsonValue(QJsonValue::Null);
    obj[QStringLiteral("spellCheckEnabled")] = spellCheckEnabled;
    obj[QStringLiteral("spellCheckLanguage")] = spellCheckLanguage;
    obj[QStringLiteral("theme")] = theme;
    obj[QStringLiteral("pastebinEnabled")] = pastebinEnabled;
    obj[QStringLiteral("pastebinThreshold")] = pastebinThreshold;
    obj[QStringLiteral("highlightWords")] = setToSortedJsonArray(highlightWords);
    obj[QStringLiteral("notificationsEnabled")] = notificationsEnabled;
    obj[QStringLiteral("notifyOnMention")] = notifyOnMention;
    obj[QStringLiteral("notifyOnHighlight")] = notifyOnHighlight;
    obj[QStringLiteral("notifyOnPrivateMessage")] = notifyOnPrivateMessage;
    obj[QStringLiteral("soundAlertsEnabled")] = soundAlertsEnabled;
    obj[QStringLiteral("soundOnMention")] = soundOnMention;
    obj[QStringLiteral("soundOnHighlight")] = soundOnHighlight;
    obj[QStringLiteral("soundOnPrivateMessage")] = soundOnPrivateMessage;
    obj[QStringLiteral("soundVolume")] = static_cast<double>(soundVolume);
    obj[QStringLiteral("splitViewEnabled")] = splitViewEnabled;
    obj[QStringLiteral("inputHistorySize")] = inputHistorySize;
    obj[QStringLiteral("loggingEnabled")] = loggingEnabled;
    obj[QStringLiteral("historyReplayLines")] = historyReplayLines;
    obj[QStringLiteral("processLinkPreviewsFromOthers")] = processLinkPreviewsFromOthers;
    obj[QStringLiteral("trustedPreviewUsers")] = setToSortedJsonArray(trustedPreviewUsers);
    obj[QStringLiteral("useProxyForPreviews")] = useProxyForPreviews;
    obj[QStringLiteral("imgbbApiKey")] = imgbbApiKey.has_value() ? QJsonValue(*imgbbApiKey) : QJsonValue(QJsonValue::Null);
    obj[QStringLiteral("urlShorteningEnabled")] = urlShorteningEnabled;
    obj[QStringLiteral("urlShorteningThreshold")] = urlShorteningThreshold;
    return obj;
}

UserPreferences UserPreferences::fromJson(const QJsonObject& obj) {
    UserPreferences p;
    if (obj.contains(QStringLiteral("fontSize")))
        p.fontSize = fontSizeFromString(obj.value(QStringLiteral("fontSize")).toString());
    if (obj.contains(QStringLiteral("timestampFormat")))
        p.timestampFormat = timestampFormatFromString(obj.value(QStringLiteral("timestampFormat")).toString());
    if (obj.contains(QStringLiteral("urlImageDisplayMode")))
        p.urlImageDisplayMode = urlImageDisplayModeFromString(obj.value(QStringLiteral("urlImageDisplayMode")).toString());
    p.coloredNicknames = obj.value(QStringLiteral("coloredNicknames")).toBool(true);
    if (obj.contains(QStringLiteral("joinPartQuitMode")))
        p.joinPartQuitMode = joinPartQuitModeFromString(obj.value(QStringLiteral("joinPartQuitMode")).toString());
    p.smartHideMinutes = obj.value(QStringLiteral("smartHideMinutes")).toInt(10);
    p.ignoredUsers = jsonArrayToSet(obj.value(QStringLiteral("ignoredUsers")), false);
    p.activityIndicatorMinutes = obj.value(QStringLiteral("activityIndicatorMinutes")).toInt(5);
    if (obj.contains(QStringLiteral("timezone")) && !obj.value(QStringLiteral("timezone")).isNull()) {
        QString tz = obj.value(QStringLiteral("timezone")).toString();
        if (!tz.isEmpty()) p.timezone = tz;
    }
    p.spellCheckEnabled = obj.value(QStringLiteral("spellCheckEnabled")).toBool(true);
    p.spellCheckLanguage = obj.value(QStringLiteral("spellCheckLanguage")).toString(QStringLiteral("en_US"));
    p.theme = obj.value(QStringLiteral("theme")).toString(QStringLiteral("Obsidian Veil"));
    p.pastebinEnabled = obj.value(QStringLiteral("pastebinEnabled")).toBool(true);
    p.pastebinThreshold = obj.value(QStringLiteral("pastebinThreshold")).toInt(400);
    p.highlightWords = jsonArrayToSet(obj.value(QStringLiteral("highlightWords")), true);
    p.notificationsEnabled = obj.value(QStringLiteral("notificationsEnabled")).toBool(true);
    p.notifyOnMention = obj.value(QStringLiteral("notifyOnMention")).toBool(true);
    p.notifyOnHighlight = obj.value(QStringLiteral("notifyOnHighlight")).toBool(true);
    p.notifyOnPrivateMessage = obj.value(QStringLiteral("notifyOnPrivateMessage")).toBool(true);
    p.soundAlertsEnabled = obj.value(QStringLiteral("soundAlertsEnabled")).toBool(false);
    p.soundOnMention = obj.value(QStringLiteral("soundOnMention")).toBool(true);
    p.soundOnHighlight = obj.value(QStringLiteral("soundOnHighlight")).toBool(true);
    p.soundOnPrivateMessage = obj.value(QStringLiteral("soundOnPrivateMessage")).toBool(true);
    p.soundVolume = static_cast<float>(obj.value(QStringLiteral("soundVolume")).toDouble(0.7));
    p.splitViewEnabled = obj.value(QStringLiteral("splitViewEnabled")).toBool(false);
    p.inputHistorySize = obj.value(QStringLiteral("inputHistorySize")).toInt(50);
    p.loggingEnabled = obj.value(QStringLiteral("loggingEnabled")).toBool(false);
    p.historyReplayLines = obj.value(QStringLiteral("historyReplayLines")).toInt(50);
    p.processLinkPreviewsFromOthers = obj.value(QStringLiteral("processLinkPreviewsFromOthers")).toBool(true);
    p.trustedPreviewUsers = jsonArrayToSet(obj.value(QStringLiteral("trustedPreviewUsers")), false);
    p.useProxyForPreviews = obj.value(QStringLiteral("useProxyForPreviews")).toBool(true);
    if (obj.contains(QStringLiteral("imgbbApiKey")) && !obj.value(QStringLiteral("imgbbApiKey")).isNull()) {
        QString key = obj.value(QStringLiteral("imgbbApiKey")).toString();
        if (!key.isEmpty()) p.imgbbApiKey = key;
    }
    p.urlShorteningEnabled = obj.value(QStringLiteral("urlShorteningEnabled")).toBool(true);
    p.urlShorteningThreshold = obj.value(QStringLiteral("urlShorteningThreshold")).toInt(200);
    p.smartHideMinutes = std::clamp(p.smartHideMinutes, 1, 1440);
    p.activityIndicatorMinutes = std::clamp(p.activityIndicatorMinutes, 1, 1440);
    p.inputHistorySize = std::clamp(p.inputHistorySize, 0, 500);
    p.historyReplayLines = std::clamp(p.historyReplayLines, 0, 2000);
    p.pastebinThreshold = std::clamp(p.pastebinThreshold, 100, 5000);
    p.urlShorteningThreshold = std::clamp(p.urlShorteningThreshold, 50, 5000);
    p.soundVolume = std::isfinite(p.soundVolume) ? std::clamp(p.soundVolume, 0.0f, 1.0f) : 0.7f;
    return p;
}

bool UserPreferences::validateJson(const QJsonObject& object, QString* error) {
    auto fail = [&](const QString& reason) { if (error) *error = reason; return false; };
    const auto defaults = UserPreferences{}.toJson();
    const QMap<QString, QStringList> choices{
        {QStringLiteral("fontSize"), {QStringLiteral("SMALL"), QStringLiteral("MEDIUM"), QStringLiteral("LARGE")}},
        {QStringLiteral("timestampFormat"), {QStringLiteral("HOURS_12"), QStringLiteral("HOURS_24"), QStringLiteral("RELATIVE")}},
        {QStringLiteral("urlImageDisplayMode"), {QStringLiteral("INLINE"), QStringLiteral("COMPACT"), QStringLiteral("DISABLED")}},
        {QStringLiteral("joinPartQuitMode"), {QStringLiteral("SHOW_ALL"), QStringLiteral("HIDE_ALL"), QStringLiteral("GROUPED"), QStringLiteral("SMART_HIDE")}}};
    for (auto entry = choices.begin(); entry != choices.end(); ++entry)
        if (object.contains(entry.key()) && !entry.value().contains(object.value(entry.key()).toString()))
            return fail(QStringLiteral("Unknown settings value: %1").arg(entry.key()));
    for (auto entry = object.begin(); entry != object.end(); ++entry) {
        if (!defaults.contains(entry.key())) continue;
        const auto reference = defaults.value(entry.key());
        if (entry.value().isString() && entry.value().toString().size() > 4096) return fail(QStringLiteral("Settings text is too long."));
        if (!reference.isNull() && entry.value().type() != reference.type())
            return fail(QStringLiteral("Invalid settings field type: %1").arg(entry.key()));
        if (entry.value().isArray()) {
            if (entry.value().toArray().size() > 512) return fail(QStringLiteral("Too many settings entries."));
            for (const auto& value : entry.value().toArray()) if (!value.isString() || value.toString().size() > 1024)
                return fail(QStringLiteral("Invalid settings list entry."));
        }
    }
    for (const auto& key : {QStringLiteral("timezone"), QStringLiteral("imgbbApiKey")})
        if (object.contains(key) && !object.value(key).isNull() && !object.value(key).isString())
            return fail(QStringLiteral("Invalid optional settings field."));
    const QMap<QString, QPair<double, double>> ranges{
        {QStringLiteral("smartHideMinutes"), {1, 1440}}, {QStringLiteral("activityIndicatorMinutes"), {1, 1440}},
        {QStringLiteral("inputHistorySize"), {0, 500}}, {QStringLiteral("historyReplayLines"), {0, 2000}},
        {QStringLiteral("pastebinThreshold"), {100, 5000}}, {QStringLiteral("urlShorteningThreshold"), {50, 5000}},
        {QStringLiteral("soundVolume"), {0, 1}}};
    for (auto entry = ranges.begin(); entry != ranges.end(); ++entry) if (object.contains(entry.key())) {
        const double value = object.value(entry.key()).toDouble(-1);
        if (!std::isfinite(value) || value < entry->first || value > entry->second ||
            (entry.key() != QStringLiteral("soundVolume") && std::floor(value) != value))
            return fail(QStringLiteral("Settings value out of range: %1").arg(entry.key()));
    }
    return true;
}

} // namespace loungecat
