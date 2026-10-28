#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QLineEdit>
#include <QLabel>
#include "network/ChatClient.h"

namespace loungecat {

class ChannelListDialog : public QDialog {
    Q_OBJECT
public:
    explicit ChannelListDialog(qint64 serverId, const QString& serverFilter = {}, QWidget* parent = nullptr);
    ~ChannelListDialog() override = default;

    void setChannels(const QList<ChannelListItem>& items, bool truncated = false);

Q_SIGNALS:
    void joinChannelRequested(qint64 serverId, const QString& channelName);

private Q_SLOTS:
    void applyFilter();

private:
    qint64 m_serverId;
    QList<ChannelListItem> m_allItems;
    bool m_listTruncated = false;
    QLineEdit* m_filterEdit = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTableWidget* m_table = nullptr;
};

} // namespace loungecat
