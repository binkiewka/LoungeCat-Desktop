#pragma once

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QToolButton>
#include <QPushButton>
#include "network/ConnectionManager.h"
#include "ui/chat/MessageListModel.h"
#include "ui/chat/MessageItemDelegate.h"
#include "ui/chat/MessageListView.h"
#include "ui/chat/SearchOverlayBar.h"
#include "ui/chat/ChatInputWidget.h"
#include "util/SpellChecker.h"
#include "ui/theme/Theme.h"

namespace loungecat {

class ChannelPaneWidget : public QWidget {
    Q_OBJECT
public:
    explicit ChannelPaneWidget(
        ConnectionManager* connectionManager,
        PreferencesManager* prefsManager,
        SpellChecker* spellChecker,
        bool showChannelSelector = false,
        QWidget* parent = nullptr
    );
    ~ChannelPaneWidget() override = default;

    qint64 serverId() const { return m_serverId; }
    QString channelName() const { return m_channelName; }

    void setTargetChannel(qint64 serverId, const QString& channelName);
    void setShowChannelSelector(bool show);
    void refreshPane();
    void applyTheme(const ThemeColors& theme);
    void openSearchBar();
    void appendMentionToInput(const QString& nickname);

Q_SIGNALS:
    void toggleUserListRequested();
    void exportChatRequested(qint64 serverId, const QString& channelName);
    void uploadImageRequested(qint64 serverId, const QString& channelName);
    void paneActivated(qint64 serverId, const QString& channelName);

private Q_SLOTS:
    void onSearchQueryChanged(const QString& query);
    void onNextSearchMatch();
    void onPrevSearchMatch();

private:
    void rebuildSelectorOptions();
    void findSearchMatches(const QString& query);

    ConnectionManager* m_connectionManager;
    PreferencesManager* m_prefsManager;
    SpellChecker* m_spellChecker;

    qint64 m_serverId = 0;
    QString m_channelName;
    ThemeColors m_theme = ThemeColors::darkTheme();

    QWidget* m_headerWidget = nullptr;
    QComboBox* m_channelSelector = nullptr;
    QLabel* m_channelIconLabel = nullptr;
    QLabel* m_channelTitleLabel = nullptr;
    QLabel* m_topicLabel = nullptr;
    QToolButton* m_searchBtn = nullptr;
    QToolButton* m_exportBtn = nullptr;
    QToolButton* m_clearBtn = nullptr;
    QToolButton* m_usersBtn = nullptr;

    SearchOverlayBar* m_searchBar = nullptr;
    MessageListModel* m_model = nullptr;
    MessageItemDelegate* m_delegate = nullptr;
    MessageListView* m_listView = nullptr;
    QPushButton* m_scrollToBottomBtn = nullptr;
    QLabel* m_typingIndicatorLabel = nullptr;
    ChatInputWidget* m_inputWidget = nullptr;
    QMap<QString, InputDraft> m_drafts;
    QList<IncomingMessage> m_renderedMessages;
    UserPreferences m_renderedPreferences;
    QMap<QString, qint64> m_renderedActivity;

    QList<int> m_searchMatchRows;
    int m_currentSearchMatchIdx = -1;
};

} // namespace loungecat
