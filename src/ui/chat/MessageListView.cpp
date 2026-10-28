#include "ui/chat/MessageListView.h"
#include "ui/chat/MessageListModel.h"
#include "ui/chat/MessageItemDelegate.h"
#include "util/PlatformUtils.h"
#include "util/IrcFormatParser.h"
#include "service/LinkPreviewService.h"
#include "service/ImageCache.h"
#include "ui/theme/IconFactory.h"
#include <QScrollBar>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QApplication>
#include <QPointer>
#include <QTimer>
#include <QPainter>
#include <algorithm>

namespace loungecat {

MessageListView::MessageListView(MessageListModel* model, MessageItemDelegate* delegate, QWidget* parent)
    : QListView(parent), m_model(model), m_delegate(delegate) {
    m_brandLogo = IconFactory::brandLogo(m_theme.isDark);
    setModel(m_model);
    setItemDelegate(m_delegate);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWordWrap(true);
    setUniformItemSizes(false);
    setSpacing(1);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);

    connect(m_model, &QAbstractItemModel::dataChanged, this, &MessageListView::scheduleDocumentPrune);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, &MessageListView::scheduleDocumentPrune);
    connect(m_model, &QAbstractItemModel::modelReset, this, &MessageListView::scheduleDocumentPrune);
    connect(m_model, &QObject::destroyed, m_delegate, [delegate]() {
        delegate->pruneDocumentsForModel(nullptr);
    });

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
        // The watermark is fixed to the viewport, not the scrolled message rows.
        viewport()->update();
        Q_EMIT scrolledStateChanged(isScrolledToBottom());
    });

    auto refreshImages = [this](const QString&) {
        const bool wasAtBottom = isScrolledToBottom();
        scheduleDelayedItemsLayout();
        viewport()->update();
        if (wasAtBottom) {
            scrollToBottom();
        }
    };
    connect(&ImageCache::instance(), &ImageCache::imageLoaded, this, refreshImages);
    connect(&ImageCache::instance(), &ImageCache::imageFailed, this, refreshImages);
}

void MessageListView::scheduleDocumentPrune() {
    if (m_documentPrunePending) return;
    m_documentPrunePending = true;
    const QPointer<MessageListModel> model(m_model);
    const QPointer<MessageItemDelegate> delegate(m_delegate);
    // Coalesce a whole model update; never delete a document during its paint/layout.
    QTimer::singleShot(0, this, [this, model, delegate]() {
        m_documentPrunePending = false;
        if (model && delegate) delegate->pruneDocumentsForModel(model);
    });
}

void MessageListView::setPreferencesAndTheme(const UserPreferences& prefs, const ThemeColors& theme) {
    if (m_prefs == prefs && m_theme == theme) return;
    m_prefs = prefs;
    m_theme = theme;
    m_brandLogo = IconFactory::brandLogo(theme.isDark);
    m_delegate->setThemeAndPreferences(theme, prefs);
    scheduleDelayedItemsLayout();
    viewport()->update();
}

bool MessageListView::isScrolledToBottom() const {
    const QScrollBar* sb = verticalScrollBar();
    return sb->value() >= sb->maximum() - 16;
}

void MessageListView::paintEvent(QPaintEvent* event) {
    {
        QPainter painter(viewport());
        painter.fillRect(viewport()->rect(), m_theme.chatBackground);
        if (!m_brandLogo.isNull()) {
            // Match the Kotlin client's subtle, centred chat watermark. Paint
            // before the message delegate so text and selections stay above it.
            QSizeF size(m_brandLogo.size());
            size.scale(QSizeF(viewport()->size()) * 0.6, Qt::KeepAspectRatio);
            const QRectF target((viewport()->width() - size.width()) / 2.0,
                                (viewport()->height() - size.height()) / 2.0,
                                size.width(), size.height());
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.setOpacity(0.15);
            painter.drawPixmap(target, m_brandLogo, QRectF(m_brandLogo.rect()));
        }
    }
    QListView::paintEvent(event);
}

void MessageListView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        m_pressPos = event->pos();
    }
    QListView::mousePressEvent(event);
}

void MessageListView::mouseReleaseEvent(QMouseEvent* event) {
    const bool isClick = (event->pos() - m_pressPos).manhattanLength() < QApplication::startDragDistance();

    if (event->button() == Qt::LeftButton && isClick) {
        const QModelIndex idx = indexAt(event->pos());
        if (idx.isValid()) {
            if (idx.data(MessageListModel::IsGroupRole).toBool()) {
                const QString grpId = idx.data(MessageListModel::GroupIdRole).toString();
                m_model->toggleGroupExpanded(grpId);
                scheduleDelayedItemsLayout();
                event->accept();
                return;
            } else {
                const QRect r = visualRect(idx);
                const QString anchor = m_delegate->anchorAt(idx, r, event->pos(), font());
                if (!anchor.isEmpty()) {
                    PlatformUtils::openUrl(anchor);
                    Q_EMIT urlClicked(anchor);
                    event->accept();
                    return;
                }
            }
        }
    } else if (event->button() == Qt::MiddleButton && isClick) {
        const QModelIndex idx = indexAt(event->pos());
        if (idx.isValid() && !idx.data(MessageListModel::IsGroupRole).toBool()) {
            const QRect r = visualRect(idx);
            const QString anchor = m_delegate->anchorAt(idx, r, event->pos(), font());
            if (!anchor.isEmpty()) {
                PlatformUtils::openUrl(anchor);
                Q_EMIT urlClicked(anchor);
                event->accept();
                return;
            }
        }
    }
    QListView::mouseReleaseEvent(event);
}

void MessageListView::mouseMoveEvent(QMouseEvent* event) {
    QListView::mouseMoveEvent(event);
    const QModelIndex idx = indexAt(event->pos());
    if (idx.isValid() && !idx.data(MessageListModel::IsGroupRole).toBool()) {
        const QRect r = visualRect(idx);
        const QString anchor = m_delegate->anchorAt(idx, r, event->pos(), font());
        if (!anchor.isEmpty()) {
            viewport()->setCursor(Qt::PointingHandCursor);
            return;
        }
    }
    viewport()->setCursor(Qt::ArrowCursor);
}

void MessageListView::leaveEvent(QEvent* event) {
    QListView::leaveEvent(event);
    viewport()->setCursor(Qt::ArrowCursor);
}

void MessageListView::contextMenuEvent(QContextMenuEvent* event) {
    const QModelIndex idx = indexAt(event->pos());
    QMenu menu(this);

    if (idx.isValid() && !idx.data(MessageListModel::IsGroupRole).toBool()) {
        const QRect r = visualRect(idx);
        const QString clickedAnchor = m_delegate->anchorAt(idx, r, event->pos(), font());
        if (!clickedAnchor.isEmpty()) {
            menu.addAction(QStringLiteral("Open Link"), this, [clickedAnchor]() {
                PlatformUtils::openUrl(clickedAnchor);
            });
            menu.addAction(QStringLiteral("Copy Link Address"), this, [clickedAnchor]() {
                PlatformUtils::copyToClipboard(clickedAnchor);
            });
            menu.addSeparator();
        }
    }

    if (!selectionModel()->selectedIndexes().isEmpty()) {
        menu.addAction(QStringLiteral("Copy Selected Messages (Ctrl+C)"), this, [this]() {
            copySelectedRows();
        });
    }

    if (idx.isValid() && !idx.data(MessageListModel::IsGroupRole).toBool()) {
        const ChatViewItem* item = m_model->itemAt(idx.row());
        if (item && std::holds_alternative<MessageItemView>(*item)) {
            const IncomingMessage& msg = std::get<MessageItemView>(*item).message;
            menu.addAction(QStringLiteral("Copy Message Text"), this, [msg]() {
                PlatformUtils::copyToClipboard(IrcFormatParser::stripFormatting(msg.content));
            });
            menu.addAction(QStringLiteral("Reply to %1").arg(msg.sender), this, [this, msg]() {
                Q_EMIT replyToMessageRequested(msg);
            });
            QMenu* reactMenu = menu.addMenu(QStringLiteral("Add Reaction"));
            const QList<QPair<QString, QString>> quickReactions = {
                {QStringLiteral("👍"), QStringLiteral("+1  Thumbs Up")},
                {QStringLiteral("❤️"), QStringLiteral("<3  Heart")},
                {QStringLiteral("😂"), QStringLiteral(":D  Laugh")},
                {QStringLiteral("🎉"), QStringLiteral("*   Celebrate")},
                {QStringLiteral("🚀"), QStringLiteral("^   Rocket")},
                {QStringLiteral("👀"), QStringLiteral("o_o Eyes")},
                {QStringLiteral("🔥"), QStringLiteral("!   Fire")},
                {QStringLiteral("💯"), QStringLiteral("100 Perfect")}
            };
            for (const auto& pair : quickReactions) {
                const QString emoji = pair.first;
                reactMenu->addAction(pair.second, this, [this, msg, emoji]() {
                    Q_EMIT reactToMessageRequested(msg, emoji);
                });
            }
            menu.addAction(QStringLiteral("Mention @%1").arg(msg.sender), this, [this, msg]() {
                Q_EMIT senderClicked(msg.sender);
            });

            const QStringList urls = LinkPreviewService::extractUrls(msg.content);
            for (const QString& url : urls) {
                menu.addSeparator();
                menu.addAction(QStringLiteral("Open %1").arg(url.left(50)), this, [url]() {
                    PlatformUtils::openUrl(url);
                });
                menu.addAction(QStringLiteral("Copy URL"), this, [url]() {
                    PlatformUtils::copyToClipboard(url);
                });
            }
        }
    }

    if (!menu.isEmpty()) {
        menu.exec(event->globalPos());
    }
}

void MessageListView::keyPressEvent(QKeyEvent* event) {
    if (event->matches(QKeySequence::Copy)) {
        copySelectedRows();
        event->accept();
        return;
    }
    QListView::keyPressEvent(event);
}

void MessageListView::resizeEvent(QResizeEvent* event) {
    const bool wasAtBottom = isScrolledToBottom();
    QListView::resizeEvent(event);
    scheduleDelayedItemsLayout();
    if (wasAtBottom) {
        scrollToBottom();
    }
}

void MessageListView::copySelectedRows() const {
    QModelIndexList selected = selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    std::sort(selected.begin(), selected.end(), [](const QModelIndex& a, const QModelIndex& b) {
        return a.row() < b.row();
    });

    QStringList lines;
    for (const QModelIndex& idx : selected) {
        const QString line = m_model->formatPlainTextRow(idx.row(), m_prefs);
        if (!line.isEmpty()) lines.append(line);
    }
    PlatformUtils::copyToClipboard(lines.join('\n'));
}

} // namespace loungecat
