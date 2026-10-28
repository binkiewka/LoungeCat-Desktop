#pragma once

#include <QListView>
#include "data/model/IncomingMessage.h"
#include "data/model/UserPreferences.h"
#include "ui/theme/Theme.h"

namespace loungecat {

class MessageListModel;
class MessageItemDelegate;

class MessageListView : public QListView {
    Q_OBJECT
public:
    explicit MessageListView(MessageListModel* model, MessageItemDelegate* delegate, QWidget* parent = nullptr);
    ~MessageListView() override = default;

    void setPreferencesAndTheme(const UserPreferences& prefs, const ThemeColors& theme);
    bool isScrolledToBottom() const;

Q_SIGNALS:
    void scrolledStateChanged(bool atBottom);
    void replyToMessageRequested(const loungecat::IncomingMessage& message);
    void reactToMessageRequested(const loungecat::IncomingMessage& message, const QString& emoji);
    void urlClicked(const QString& url);
    void senderClicked(const QString& nickname);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void copySelectedRows() const;
    void scheduleDocumentPrune();

    MessageListModel* m_model;
    MessageItemDelegate* m_delegate;
    UserPreferences m_prefs;
    ThemeColors m_theme = ThemeColors::darkTheme();
    QPoint m_pressPos;
    bool m_documentPrunePending = false;
};

} // namespace loungecat
