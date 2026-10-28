#include "ui/chat/MessageListModel.h"
#include "util/IrcFormatParser.h"
#include "service/LinkPreviewService.h"
#include <QDateTime>
#include <QTimeZone>

namespace loungecat {

MessageListModel::MessageListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int MessageListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_items.size();
}

const ChatViewItem* MessageListModel::itemAt(int row) const {
    if (row < 0 || row >= m_items.size()) return nullptr;
    return &m_items[row];
}

void MessageListModel::setItems(const QList<ChatViewItem>& items) {
    auto key = [](const ChatViewItem& item) {
        return std::visit([](const auto& value) {
            using Type = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Type, MessageItemView>) return value.message.id;
            else return value.id;
        }, item);
    };
    QSet<QString> retained;
    for (int row = 0; row < items.size(); ++row) {
        const auto identity = key(items[row]);
        retained.insert(identity);
        if (row >= m_items.size() || key(m_items[row]) != identity) {
            int existing = row + 1;
            while (existing < m_items.size() && key(m_items[existing]) != identity) ++existing;
            if (existing < m_items.size()) {
                beginMoveRows({}, existing, existing, {}, row);
                m_items.move(existing, row);
                endMoveRows();
            } else {
                beginInsertRows({}, row, row);
                m_items.insert(row, items[row]);
                m_revisions.insert(identity, ++m_nextRevision);
                endInsertRows();
            }
        }
        if (m_items[row] != items[row]) {
            m_items[row] = items[row];
            m_revisions.insert(identity, ++m_nextRevision);
            Q_EMIT dataChanged(index(row), index(row));
        }
    }
    if (m_items.size() > items.size()) {
        beginRemoveRows({}, items.size(), m_items.size() - 1);
        m_items.remove(items.size(), m_items.size() - items.size());
        endRemoveRows();
    }
    for (const auto& identity : m_revisions.keys()) if (!retained.contains(identity)) m_revisions.remove(identity);
    m_expandedGroupIds.intersect(retained);
}

void MessageListModel::toggleGroupExpanded(const QString& groupId) {
    if (m_expandedGroupIds.contains(groupId)) {
        m_expandedGroupIds.remove(groupId);
    } else {
        m_expandedGroupIds.insert(groupId);
    }
    m_revisions.insert(groupId, ++m_nextRevision);
    if (!m_items.isEmpty()) {
        Q_EMIT dataChanged(index(0), index(m_items.size() - 1));
    }
}

QString MessageListModel::formatTimestamp(
    qint64 timestampMs,
    TimestampFormat fmt,
    const std::optional<QString>& tzId
) {
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(timestampMs);
    if (tzId.has_value() && !tzId->isEmpty()) {
        const QTimeZone tz(tzId->toUtf8());
        if (tz.isValid()) {
            dt = dt.toTimeZone(tz);
        }
    }

    switch (fmt) {
        case TimestampFormat::HOURS_12:
            return dt.toString(QStringLiteral("hh:mm AP"));
        case TimestampFormat::RELATIVE: {
            const qint64 diffSecs = (QDateTime::currentMSecsSinceEpoch() - timestampMs) / 1000;
            if (diffSecs < 60) return QStringLiteral("now");
            if (diffSecs < 3600) return QStringLiteral("%1m").arg(diffSecs / 60);
            if (diffSecs < 86400) return QStringLiteral("%1h").arg(diffSecs / 3600);
            return dt.toString(QStringLiteral("MM/dd"));
        }
        case TimestampFormat::HOURS_24:
        default:
            return dt.toString(QStringLiteral("HH:mm"));
    }
}

QString MessageListModel::formatPlainTextRow(int row, const UserPreferences& prefs) const {
    const ChatViewItem* item = itemAt(row);
    if (!item) return QString();

    if (std::holds_alternative<JoinPartGroupView>(*item)) {
        const auto& grp = std::get<JoinPartGroupView>(*item);
        const QString ts = formatTimestamp(grp.timestamp, prefs.timestampFormat, prefs.timezone);
        return QStringLiteral("[%1] • %2").arg(ts, grp.summary());
    }

    const auto& m = std::get<MessageItemView>(*item).message;
    const QString ts = formatTimestamp(m.timestamp, prefs.timestampFormat, prefs.timezone);
    const QString body = IrcFormatParser::stripFormatting(m.content);
    switch (m.type) {
        case MessageType::NORMAL:
            return QStringLiteral("[%1] <%2> %3").arg(ts, m.sender, body);
        case MessageType::ACTION:
            return QStringLiteral("[%1] * %2 %3").arg(ts, m.sender, body);
        case MessageType::NOTICE:
            return QStringLiteral("[%1] -%2- %3").arg(ts, m.sender, body);
        default:
            return QStringLiteral("[%1] %2").arg(ts, body);
    }
}

QVariant MessageListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return {};
    }

    const ChatViewItem& item = m_items[index.row()];
    const bool isGroup = std::holds_alternative<JoinPartGroupView>(item);
    if (role == RenderRevisionRole) {
        const auto identity = isGroup ? std::get<JoinPartGroupView>(item).id : std::get<MessageItemView>(item).message.id;
        return QVariant::fromValue(m_revisions.value(identity));
    }

    if (role == IsGroupRole) return isGroup;

    if (isGroup) {
        const auto& grp = std::get<JoinPartGroupView>(item);
        const bool expanded = m_expandedGroupIds.contains(grp.id);
        switch (role) {
            case GroupIdRole:       return grp.id;
            case GroupSummaryRole:  return grp.summary();
            case GroupExpandedRole: return expanded;
            case TimestampRole:     return grp.timestamp;
            case Qt::DisplayRole: {
                if (!expanded) {
                    return QStringLiteral("▸ %1 (%2 events)").arg(grp.summary()).arg(grp.messages.size());
                }
                QStringList details;
                details << QStringLiteral("▾ %1:").arg(grp.summary());
                for (const auto& m : grp.messages) {
                    details << QStringLiteral("    • %1").arg(m.content);
                }
                return details.join('\n');
            }
            default:
                return {};
        }
    }

    const IncomingMessage& msg = std::get<MessageItemView>(item).message;
    switch (role) {
        case DeliveryStatusRole: return msg.deliveryStatus;
        case MessageIdRole:      return msg.id;
        case SenderRole:         return msg.sender;
        case ContentRole:        return msg.content;
        case TimestampRole:      return msg.timestamp;
        case MessageTypeRole:    return static_cast<int>(msg.type);
        case IsSelfRole:         return msg.isSelf;
        case IsHighlightRole:    return msg.isHighlight;
        case HasUrlPreviewRole: {
            if (!msg.urlPreviews.isEmpty()) return true;
            const QStringList urls = LinkPreviewService::extractUrls(msg.content);
            for (const QString& u : urls) {
                if (LinkPreviewService::isImageUrl(u)) return true;
            }
            return false;
        }
        case UrlPreviewsRole: {
            QList<UrlPreview> previews = msg.urlPreviews;
            if (previews.isEmpty()) {
                const QStringList urls = LinkPreviewService::extractUrls(msg.content);
                for (const QString& u : urls) {
                    if (LinkPreviewService::isImageUrl(u)) {
                        UrlPreview ip;
                        ip.url = u;
                        ip.imageUrl = u;
                        ip.isImage = true;
                        previews.append(ip);
                    }
                }
            }
            return QVariant::fromValue(previews);
        }
        case FirstPreviewTitleRole: {
            if (msg.urlPreviews.isEmpty()) return {};
            const auto& p = msg.urlPreviews.first();
            return p.title.value_or(p.url);
        }
        case FirstPreviewUrlRole: {
            if (msg.urlPreviews.isEmpty()) return {};
            return msg.urlPreviews.first().url;
        }
        case ReplySenderRole:
            return msg.replyToSender.value_or(QString());
        case ReplyPreviewRole:
            return msg.replyToPreview.value_or(msg.replyToMessageId.value_or(QString()));
        case ReactionsSummaryRole: {
            if (msg.reactions.isEmpty()) return QString();
            QStringList pills;
            for (auto it = msg.reactions.begin(); it != msg.reactions.end(); ++it) {
                if (!it.value().isEmpty()) {
                    const QStringList nicks = it.value().values();
                    pills << QStringLiteral("%1 %2 (%3)")
                                 .arg(it.key())
                                 .arg(it.value().size())
                                 .arg(nicks.join(QStringLiteral(", ")));
                }
            }
            return pills.join(QStringLiteral("   "));
        }
        case Qt::DisplayRole:
            return msg.content;
        default:
            return {};
    }
}

} // namespace loungecat
