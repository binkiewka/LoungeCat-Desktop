#include "ui/chat/ChannelPaneWidget.h"
#include "ui/theme/IconFactory.h"
#include "util/ChatGrouping.h"
#include "util/IrcCommandParser.h"
#include "util/IrcFormatParser.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QInputDialog>

namespace loungecat {

ChannelPaneWidget::ChannelPaneWidget(
    ConnectionManager* connectionManager,
    PreferencesManager* prefsManager,
    SpellChecker* spellChecker,
    bool showChannelSelector,
    QWidget* parent
)
    : QWidget(parent),
      m_connectionManager(connectionManager),
      m_prefsManager(prefsManager),
      m_spellChecker(spellChecker) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Top Channel Header Bar
    m_headerWidget = new QWidget(this);
    m_headerWidget->setObjectName(QStringLiteral("channelHeaderBar"));
    auto* headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(12, 8, 12, 8);
    headerLayout->setSpacing(8);

    m_channelSelector = new QComboBox(m_headerWidget);
    m_channelSelector->setVisible(showChannelSelector);
    connect(m_channelSelector, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) return;
        const QString dataStr = m_channelSelector->itemData(index).toString();
        const int sep = dataStr.indexOf(':');
        if (sep != -1) {
            const qint64 sId = dataStr.left(sep).toLongLong();
            const QString ch = dataStr.mid(sep + 1);
            setTargetChannel(sId, ch);
            Q_EMIT paneActivated(sId, ch);
        }
    });
    headerLayout->addWidget(m_channelSelector);

    m_channelIconLabel = new QLabel(m_headerWidget);
    m_channelIconLabel->setFixedSize(22, 22);
    headerLayout->addWidget(m_channelIconLabel);

    m_channelTitleLabel = new QLabel(QStringLiteral("No Channel"), m_headerWidget);
    m_channelTitleLabel->setStyleSheet(QStringLiteral("font-weight: 700; font-size: 11pt;"));
    headerLayout->addWidget(m_channelTitleLabel);

    m_topicLabel = new QLabel(m_headerWidget);
    m_topicLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    m_topicLabel->setOpenExternalLinks(true);
    m_topicLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    headerLayout->addWidget(m_topicLabel, 1);

    auto setupToolbarBtn = [](QToolButton* btn, const QString& tooltip) {
        btn->setToolTip(tooltip);
        btn->setAccessibleName(tooltip);
        btn->setIconSize(QSize(22, 22));
        btn->setFixedSize(36, 36);
        btn->setCursor(Qt::PointingHandCursor);
    };

    m_searchBtn = new QToolButton(m_headerWidget);
    setupToolbarBtn(m_searchBtn, QStringLiteral("Search Messages (Ctrl+F)"));
    connect(m_searchBtn, &QToolButton::clicked, this, &ChannelPaneWidget::openSearchBar);
    headerLayout->addWidget(m_searchBtn);

    m_exportBtn = new QToolButton(m_headerWidget);
    setupToolbarBtn(m_exportBtn, QStringLiteral("Export Channel History (.txt / .json)"));
    connect(m_exportBtn, &QToolButton::clicked, this, [this]() {
        if (m_serverId != 0 && !m_channelName.isEmpty()) {
            Q_EMIT exportChatRequested(m_serverId, m_channelName);
        }
    });
    headerLayout->addWidget(m_exportBtn);

    m_clearBtn = new QToolButton(m_headerWidget);
    setupToolbarBtn(m_clearBtn, QStringLiteral("Clear Messages (/clear)"));
    connect(m_clearBtn, &QToolButton::clicked, this, [this]() {
        if (m_connectionManager && m_serverId != 0 && !m_channelName.isEmpty()) {
            m_connectionManager->clearChannelMessages(m_serverId, m_channelName);
        }
    });
    headerLayout->addWidget(m_clearBtn);

    m_usersBtn = new QToolButton(m_headerWidget);
    setupToolbarBtn(m_usersBtn, QStringLiteral("Toggle User List Sidebar"));
    connect(m_usersBtn, &QToolButton::clicked, this, &ChannelPaneWidget::toggleUserListRequested);
    headerLayout->addWidget(m_usersBtn);

    mainLayout->addWidget(m_headerWidget);

    // Search Overlay Bar (hidden by default)
    m_searchBar = new SearchOverlayBar(this);
    m_searchBar->hide();
    connect(m_searchBar, &SearchOverlayBar::queryChanged, this, &ChannelPaneWidget::onSearchQueryChanged);
    connect(m_searchBar, &SearchOverlayBar::nextMatchRequested, this, &ChannelPaneWidget::onNextSearchMatch);
    connect(m_searchBar, &SearchOverlayBar::previousMatchRequested, this, &ChannelPaneWidget::onPrevSearchMatch);
    connect(m_searchBar, &SearchOverlayBar::closed, this, [this]() {
        onSearchQueryChanged(QString());
        m_inputWidget->focusInput();
    });
    mainLayout->addWidget(m_searchBar);

    // Message List View
    m_model = new MessageListModel(this);
    m_delegate = new MessageItemDelegate(this);
    m_listView = new MessageListView(m_model, m_delegate, this);
    mainLayout->addWidget(m_listView, 1);

    // Scroll to bottom banner button
    m_scrollToBottomBtn = new QPushButton(QStringLiteral("Jump to Latest Messages"), this);
    m_scrollToBottomBtn->setIconSize(QSize(18, 18));
    m_scrollToBottomBtn->setCursor(Qt::PointingHandCursor);
    m_scrollToBottomBtn->setVisible(false);
    connect(m_scrollToBottomBtn, &QPushButton::clicked, this, [this]() {
        m_listView->scrollToBottom();
        m_scrollToBottomBtn->setVisible(false);
    });
    connect(m_listView, &MessageListView::scrolledStateChanged, this, [this](bool atBottom) {
        m_scrollToBottomBtn->setVisible(!atBottom && m_model->rowCount() > 0);
    });
    mainLayout->addWidget(m_scrollToBottomBtn);

    // Typing Indicator Label (IRCv3 +typing)
    m_typingIndicatorLabel = new QLabel(this);
    m_typingIndicatorLabel->setContentsMargins(12, 2, 12, 2);
    m_typingIndicatorLabel->setStyleSheet(QStringLiteral("font-size: 9pt; font-style: italic; color: #908caa;"));
    m_typingIndicatorLabel->setVisible(false);
    mainLayout->addWidget(m_typingIndicatorLabel);

    // Input Widget
    m_inputWidget = new ChatInputWidget(m_spellChecker, this);
    connect(m_inputWidget, &ChatInputWidget::inputSubmitted, this, [this](const QString& text, const std::optional<IncomingMessage>& replyTo) {
        if (m_connectionManager && m_serverId != 0 && !m_channelName.isEmpty()) {
            if (m_connectionManager->sendUserInput(m_serverId, m_channelName, text, replyTo))
                m_inputWidget->clearSubmittedDraft(text);
            m_inputWidget->setHistory(m_connectionManager->inputHistory(m_serverId, m_channelName));
        }
    });
    connect(m_inputWidget, &ChatInputWidget::typingStateChanged, this, [this](const QString& state) {
        if (m_connectionManager && m_serverId != 0 && !m_channelName.isEmpty()) {
            m_connectionManager->sendTypingNotification(m_serverId, m_channelName, state);
        }
    });
    connect(m_inputWidget, &ChatInputWidget::uploadImageClicked, this, [this]() {
        if (m_serverId != 0 && !m_channelName.isEmpty()) {
            Q_EMIT uploadImageRequested(m_serverId, m_channelName);
        }
    });
    connect(m_listView, &MessageListView::replyToMessageRequested, m_inputWidget, &ChatInputWidget::setReplyContext);
    connect(m_listView, &MessageListView::reactToMessageRequested, this, [this](const IncomingMessage& msg, const QString& emoji) {
        if (m_connectionManager && m_serverId != 0 && !m_channelName.isEmpty() && !msg.id.isEmpty()) {
            m_connectionManager->sendReaction(m_serverId, m_channelName, msg.id, emoji);
        }
    });
    connect(m_listView, &MessageListView::senderClicked, m_inputWidget, &ChatInputWidget::appendMention);
    mainLayout->addWidget(m_inputWidget);

    connect(m_connectionManager, &ConnectionManager::inputFailed, this,
        [this](qint64 serverId, const QString& channel, const QString& text, const std::optional<IncomingMessage>& reply, const QString&) {
            const auto connection = m_connectionManager->connection(serverId);
            if (!connection || !connection->findChannel(channel)) return;
            const auto key = m_connectionManager->conversationKey(serverId, channel);
            if (m_serverId == serverId && connection->same(m_channelName, channel)) {
                if (m_inputWidget->draft().text.isEmpty()) m_inputWidget->restoreDraft({text, reply, static_cast<int>(text.size()), -1});
            } else if (!m_drafts.contains(key) || m_drafts[key].text.isEmpty()) {
                m_drafts.insert(key, {text, reply, static_cast<int>(text.size()), -1});
            }
        });
    connect(m_connectionManager, &ConnectionManager::queryRenamed, this,
        [this](qint64 serverId, const QString& oldName, const QString& newName) {
            const auto oldKey = m_connectionManager->conversationKey(serverId, oldName);
            const auto newKey = m_connectionManager->conversationKey(serverId, newName);
            if (m_drafts.contains(oldKey)) m_drafts.insert(newKey, m_drafts.take(oldKey));
            const auto connection = m_connectionManager->connection(serverId);
            if (connection && m_serverId == serverId && connection->same(m_channelName, oldName)) m_channelName = newName;
        });

    connect(m_connectionManager, &ConnectionManager::identityKeysChanged, this,
        [this](qint64 serverId, const QMap<QString, QString>& redirects) {
            const auto old = m_drafts;
            for (auto it = redirects.begin(); it != redirects.end(); ++it) if (old.contains(it.key())) m_drafts.remove(it.key());
            for (auto it = redirects.begin(); it != redirects.end(); ++it) if (old.contains(it.key())) {
                const auto draft = old.value(it.key());
                if (!m_drafts.contains(it.value())) m_drafts.insert(it.value(), draft);
                else if (!draft.text.isEmpty() && draft.text != m_drafts[it.value()].text) {
                    auto& merged = m_drafts[it.value()]; merged.text += QStringLiteral("\n") + draft.text; merged.cursor = static_cast<int>(merged.text.size());
                }
            }
            if (m_serverId == serverId) if (const auto connection = m_connectionManager->connection(serverId))
                if (const auto* channel = connection->findChannel(m_channelName)) m_channelName = channel->name;
        });
    connect(m_connectionManager, &ConnectionManager::connectionsChanged, this, [this]() {
        for (const auto& key : m_drafts.keys()) {
            const int split = key.indexOf(':');
            const auto connection = m_connectionManager->connection(key.left(split).toLongLong());
            if (!connection || !connection->findChannel(key.mid(split + 1))) m_drafts.remove(key);
        }
    });
    applyTheme(m_theme);
}

void ChannelPaneWidget::setShowChannelSelector(bool show) {
    m_channelSelector->setVisible(show);
    if (show) {
        rebuildSelectorOptions();
    }
}

void ChannelPaneWidget::setTargetChannel(qint64 serverId, const QString& channelName) {
    const auto targetConnection = m_connectionManager->connection(serverId);
    if (m_serverId == serverId && targetConnection && targetConnection->same(m_channelName, channelName)) { refreshPane(); return; }
    const auto oldKey = m_connectionManager->conversationKey(m_serverId, m_channelName);
    const auto previous = m_inputWidget->draft();
    const auto oldConnection = m_connectionManager->connection(m_serverId);
    if (oldConnection && oldConnection->findChannel(m_channelName) && (!previous.text.isEmpty() || previous.reply)) m_drafts.insert(oldKey, previous);
    else m_drafts.remove(oldKey);
    if (m_connectionManager && m_serverId != 0 && !previous.text.isEmpty())
        m_connectionManager->sendTypingNotification(m_serverId, m_channelName, QStringLiteral("done"));
    const auto newKey = m_connectionManager->conversationKey(serverId, channelName);
    m_serverId = serverId;
    const auto* target = targetConnection ? targetConnection->findChannel(channelName) : nullptr;
    m_channelName = target ? target->name : channelName;
    refreshPane();
    m_inputWidget->restoreDraft(m_drafts.take(newKey));
}

void ChannelPaneWidget::rebuildSelectorOptions() {
    if (!m_connectionManager || !m_channelSelector->isVisible()) return;
    m_channelSelector->blockSignals(true);
    m_channelSelector->clear();

    int selectedIdx = -1;
    int i = 0;
    for (const ServerConnection& conn : m_connectionManager->connections()) {
        const QString srvName = conn.config.serverName.isEmpty() ? conn.config.hostname : conn.config.serverName;
        for (const Channel& ch : conn.channels) {
            const QString label = QStringLiteral("[%1] %2").arg(srvName, ch.name);
            const QString val = QStringLiteral("%1:%2").arg(conn.serverId).arg(ch.name);
            m_channelSelector->addItem(label, val);
            if (conn.serverId == m_serverId && conn.same(ch.name, m_channelName)) {
                selectedIdx = i;
            }
            ++i;
        }
    }
    if (selectedIdx >= 0) {
        m_channelSelector->setCurrentIndex(selectedIdx);
    }
    m_channelSelector->blockSignals(false);
}

void ChannelPaneWidget::refreshPane() {
    if (!m_connectionManager) return;
    rebuildSelectorOptions();

    const auto connOpt = m_connectionManager->connection(m_serverId);
    if (!connOpt.has_value()) {
        m_channelIconLabel->setPixmap(IconFactory::create(IconType::ServerPulse, m_theme.comment, 22).pixmap(22, 22));
        m_channelTitleLabel->setText(QStringLiteral("No Server Connected"));
        m_topicLabel->clear();
        m_typingIndicatorLabel->setVisible(false);
        m_model->setItems({});
        m_renderedMessages.clear();
        m_renderedActivity.clear();
        return;
    }

    const ServerConnection& conn = *connOpt;
    const Channel* ch = conn.findChannel(m_channelName);
    if (!ch) {
        m_channelIconLabel->setPixmap(IconFactory::create(IconType::ChannelHash, m_theme.comment, 22).pixmap(22, 22));
        m_channelTitleLabel->setText(m_channelName);
        m_topicLabel->clear();
        m_typingIndicatorLabel->setVisible(false);
        m_model->setItems({});
        m_renderedMessages.clear();
        m_renderedActivity.clear();
        return;
    }

    const bool wasAtBottom = m_listView->isScrolledToBottom() || m_model->rowCount() == 0;

    IconType chIconType = IconType::ChannelHash;
    if (ch->type == ChannelType::SERVER) {
        chIconType = IconType::ServerPulse;
    } else if (ch->type == ChannelType::QUERY) {
        chIconType = IconType::DirectMessage;
    }
    m_channelIconLabel->setPixmap(IconFactory::create(chIconType, m_theme.accentColor, 22).pixmap(22, 22));

    QString displayTitle = ch->name;
    if (ch->type == ChannelType::CHANNEL && displayTitle.startsWith('#')) {
        displayTitle = displayTitle.mid(1);
    }
    m_channelTitleLabel->setText(displayTitle);

    const QString topicText = ch->topic.value_or(QString());
    m_topicLabel->setText(topicText.isEmpty() ? QString() : QStringLiteral("│  %1").arg(topicText.toHtmlEscaped()));
    m_topicLabel->setToolTip(topicText);

    // Update IRCv3 typing indicator bar
    if (ch->currentTypingUsers.isEmpty()) {
        m_typingIndicatorLabel->setVisible(false);
    } else {
        QStringList typingList = ch->currentTypingUsers;
        std::sort(typingList.begin(), typingList.end(), [](const QString& a, const QString& b) {
            return a.compare(b, Qt::CaseInsensitive) < 0;
        });
        QString typingText;
        if (typingList.size() == 1) {
            typingText = QStringLiteral("• %1 is typing...").arg(typingList.first());
        } else if (typingList.size() <= 3) {
            typingText = QStringLiteral("• %1 are typing...").arg(typingList.join(QStringLiteral(", ")));
        } else {
            typingText = QStringLiteral("• %1 and %2 others are typing...")
                             .arg(typingList.mid(0, 2).join(QStringLiteral(", ")))
                             .arg(typingList.size() - 2);
        }
        m_typingIndicatorLabel->setText(typingText);
        m_typingIndicatorLabel->setVisible(true);
    }

    const UserPreferences prefs = m_prefsManager ? m_prefsManager->preferences() : UserPreferences{};
    const auto activeTimes = m_connectionManager->lastActiveTimes(m_serverId, m_channelName);
    if (m_renderedMessages != ch->messages || m_renderedPreferences != prefs || m_renderedActivity != activeTimes) {
    const QList<ChatViewItem> viewItems = ChatGrouping::processMessages(
        ch->messages,
        prefs.joinPartQuitMode,
        activeTimes,
        prefs.smartHideMinutes
    );

    m_listView->setPreferencesAndTheme(prefs, m_theme);
    m_model->setItems(viewItems);
    m_renderedMessages = ch->messages;
    m_renderedPreferences = prefs;
    m_renderedActivity = activeTimes;
    if (m_searchBar->isVisible()) findSearchMatches(m_delegate->searchHighlightQuery());
    }

    if (wasAtBottom) {
        m_listView->scrollToBottom();
    }

    // Update input badge, completion context, and history
    const QString myNick = m_connectionManager->client(m_serverId)
        ? m_connectionManager->client(m_serverId)->getNickname()
        : conn.config.nickname;
    m_inputWidget->setNicknameBadge(myNick);

    QStringList nicks;
    for (const auto& u : ch->users) nicks.append(u.nickname);
    QStringList chans;
    for (const auto& c : conn.channels) {
        if (c.isChannel()) chans.append(c.name);
    }
    m_inputWidget->setCompletionContext(nicks, chans, IrcCommandParser::availableCommands());
    m_inputWidget->setHistory(m_connectionManager->inputHistory(m_serverId, m_channelName));
}

void ChannelPaneWidget::applyTheme(const ThemeColors& theme) {
    m_theme = theme;

    m_headerWidget->setStyleSheet(QStringLiteral(
        "QWidget#channelHeaderBar { background-color: %1; border-bottom: 1px solid %2; }"
    ).arg(theme.chatBackground.darker(108).name(), theme.border.name()));

    m_channelTitleLabel->setStyleSheet(QStringLiteral(
        "color: %1; font-weight: 700; font-size: 11pt; background: transparent; border: none;"
    ).arg(theme.foreground.name()));

    m_topicLabel->setStyleSheet(QStringLiteral(
        "color: %1; font-size: 9.5pt; background: transparent; border: none;"
    ).arg(theme.comment.name()));

    const QColor iconColor = theme.foreground;
    m_searchBtn->setIcon(IconFactory::create(IconType::Search, iconColor, 22));
    m_exportBtn->setIcon(IconFactory::create(IconType::Download, iconColor, 22));
    m_clearBtn->setIcon(IconFactory::create(IconType::Trash, iconColor, 22));
    m_usersBtn->setIcon(IconFactory::create(IconType::Users, iconColor, 22));
    m_scrollToBottomBtn->setIcon(IconFactory::create(IconType::ChevronDown, theme.foreground, 18));

    m_typingIndicatorLabel->setStyleSheet(QStringLiteral(
        "font-size: 9pt; font-style: italic; color: %1; background-color: %2;"
    ).arg(theme.comment.name(), theme.chatBackground.name()));

    m_searchBar->applyTheme(theme);
    m_inputWidget->applyTheme(theme);

    const UserPreferences prefs = m_prefsManager ? m_prefsManager->preferences() : UserPreferences{};
    m_listView->setPreferencesAndTheme(prefs, m_theme);
    m_inputWidget->rehighlightSpellCheck();
    refreshPane();
}

void ChannelPaneWidget::openSearchBar() {
    m_searchBar->focusSearchInput();
}

void ChannelPaneWidget::appendMentionToInput(const QString& nickname) {
    m_inputWidget->appendMention(nickname);
}

void ChannelPaneWidget::onSearchQueryChanged(const QString& query) {
    m_delegate->setSearchHighlightQuery(query);
    findSearchMatches(query);
    m_listView->viewport()->update();
}

void ChannelPaneWidget::findSearchMatches(const QString& query) {
    m_searchMatchRows.clear();
    m_currentSearchMatchIdx = -1;

    const QString needle = query.trimmed();
    if (needle.isEmpty()) {
        m_searchBar->setMatchStatus(0, 0);
        return;
    }

    for (int r = 0; r < m_model->rowCount(); ++r) {
        const QModelIndex idx = m_model->index(r, 0);
        const QString content = IrcFormatParser::stripFormatting(idx.data(MessageListModel::ContentRole).toString());
        const QString sender = idx.data(MessageListModel::SenderRole).toString();
        if (content.contains(needle, Qt::CaseInsensitive) || sender.contains(needle, Qt::CaseInsensitive)) {
            m_searchMatchRows.append(r);
        }
    }

    if (!m_searchMatchRows.isEmpty()) {
        m_currentSearchMatchIdx = m_searchMatchRows.size() - 1;
        m_listView->scrollTo(m_model->index(m_searchMatchRows[m_currentSearchMatchIdx], 0), QAbstractItemView::PositionAtCenter);
        m_searchBar->setMatchStatus(m_currentSearchMatchIdx + 1, m_searchMatchRows.size());
    } else {
        m_searchBar->setMatchStatus(0, 0);
    }
}

void ChannelPaneWidget::onNextSearchMatch() {
    if (m_searchMatchRows.isEmpty()) return;
    m_currentSearchMatchIdx = (m_currentSearchMatchIdx + 1) % m_searchMatchRows.size();
    m_listView->scrollTo(m_model->index(m_searchMatchRows[m_currentSearchMatchIdx], 0), QAbstractItemView::PositionAtCenter);
    m_searchBar->setMatchStatus(m_currentSearchMatchIdx + 1, m_searchMatchRows.size());
}

void ChannelPaneWidget::onPrevSearchMatch() {
    if (m_searchMatchRows.isEmpty()) return;
    m_currentSearchMatchIdx = (m_currentSearchMatchIdx - 1 + m_searchMatchRows.size()) % m_searchMatchRows.size();
    m_listView->scrollTo(m_model->index(m_searchMatchRows[m_currentSearchMatchIdx], 0), QAbstractItemView::PositionAtCenter);
    m_searchBar->setMatchStatus(m_currentSearchMatchIdx + 1, m_searchMatchRows.size());
}

} // namespace loungecat
