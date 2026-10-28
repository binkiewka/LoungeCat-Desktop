#include "ui/dialog/ChannelListDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QSignalBlocker>
#include <algorithm>

namespace loungecat {

ChannelListDialog::ChannelListDialog(qint64 serverId, const QString& serverFilter, QWidget* parent)
    : QDialog(parent), m_serverId(serverId) {
    setWindowTitle(QStringLiteral("Server Channel List (/list)"));
    resize(680, 480);

    auto* layout = new QVBoxLayout(this);

    auto* topRow = new QHBoxLayout();
    // The IRC server already applied its LIST expression. Local search is a
    // separate literal channel/topic filter, not a second interpretation of it.
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(QStringLiteral("Search returned channels or topics..."));
    m_filterEdit->setToolTip(serverFilter.isEmpty()
        ? QStringLiteral("Filter the returned channels and topics")
        : QStringLiteral("Filter returned results. Server filter: %1").arg(serverFilter));
    m_filterEdit->setClearButtonEnabled(true);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &ChannelListDialog::applyFilter);
    topRow->addWidget(m_filterEdit, 1);

    m_statusLabel = new QLabel(QStringLiteral("Fetching channels from server..."), this);
    topRow->addWidget(m_statusLabel);
    layout->addLayout(topRow);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({QStringLiteral("Channel"), QStringLiteral("Users"), QStringLiteral("Topic")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (auto* item = m_table->item(row, 0)) {
            Q_EMIT joinChannelRequested(m_serverId, item->text());
            accept();
        }
    });
    layout->addWidget(m_table, 1);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* joinBtn = new QPushButton(QStringLiteral("Join Selected"), this);
    connect(joinBtn, &QPushButton::clicked, this, [this]() {
        const int row = m_table->currentRow();
        if (row >= 0 && m_table->item(row, 0)) {
            Q_EMIT joinChannelRequested(m_serverId, m_table->item(row, 0)->text());
            accept();
        }
    });
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(joinBtn);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);
}

void ChannelListDialog::setChannels(const QList<ChannelListItem>& items, bool truncated) {
    m_listTruncated = truncated;
    m_allItems = items;
    std::sort(m_allItems.begin(), m_allItems.end(), [](const ChannelListItem& a, const ChannelListItem& b) {
        return a.userCount > b.userCount;
    });
    applyFilter();
}

void ChannelListDialog::applyFilter() {
    const auto* selected = m_table->item(m_table->currentRow(), 0);
    const QString selectedChannel = selected ? selected->text() : QString();
    const QSignalBlocker selectionSignals(m_table);
    int selectedRow = -1;
    const QString filter = m_filterEdit->text().trimmed();
    QList<ChannelListItem> filtered;
    for (const auto& item : m_allItems) {
        if (filter.isEmpty() ||
            item.channel.contains(filter, Qt::CaseInsensitive) ||
            item.topic.contains(filter, Qt::CaseInsensitive)) {
            filtered.append(item);
        }
    }

    m_table->setRowCount(filtered.size());
    for (int i = 0; i < filtered.size(); ++i) {
        const auto& it = filtered[i];
        m_table->setItem(i, 0, new QTableWidgetItem(it.channel));
        m_table->setItem(i, 1, new QTableWidgetItem(QString::number(it.userCount)));
        m_table->setItem(i, 2, new QTableWidgetItem(it.topic));
        if (!selectedChannel.isEmpty() && it.channel == selectedChannel) selectedRow = i;
    }
    m_table->clearSelection();
    m_table->setCurrentCell(selectedRow, selectedRow >= 0 ? 0 : -1);
    m_statusLabel->setText((m_listTruncated
        ? QStringLiteral("%1 channels (list truncated)")
        : QStringLiteral("%1 channels")).arg(filtered.size()));
}

} // namespace loungecat
