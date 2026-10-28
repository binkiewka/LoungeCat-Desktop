#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QSet>
#include "data/model/IncomingMessage.h"
#include "data/model/UserPreferences.h"

namespace loungecat {

class MessageListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IsGroupRole = Qt::UserRole + 1,
        GroupIdRole,
        GroupSummaryRole,
        GroupExpandedRole,
        MessageIdRole,
        SenderRole,
        ContentRole,
        TimestampRole,
        MessageTypeRole,
        IsSelfRole,
        IsHighlightRole,
        HasUrlPreviewRole,
        UrlPreviewsRole,
        FirstPreviewTitleRole,
        FirstPreviewUrlRole,
        ReplySenderRole,
        ReplyPreviewRole,
        ReactionsSummaryRole,
        DeliveryStatusRole,
        RenderRevisionRole
    };

    explicit MessageListModel(QObject* parent = nullptr);
    ~MessageListModel() override = default;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    void setItems(const QList<ChatViewItem>& items);
    const ChatViewItem* itemAt(int row) const;
    void toggleGroupExpanded(const QString& groupId);

    QString formatPlainTextRow(int row, const UserPreferences& prefs) const;
    static QString formatTimestamp(qint64 timestampMs, TimestampFormat fmt, const std::optional<QString>& tzId = std::nullopt);

private:
    QList<ChatViewItem> m_items;
    QSet<QString> m_expandedGroupIds;
    QMap<QString, quint64> m_revisions;
    quint64 m_nextRevision = 0;
};

} // namespace loungecat
