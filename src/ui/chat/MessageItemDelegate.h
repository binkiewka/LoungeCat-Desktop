#pragma once

#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QCache>
#include "data/model/UserPreferences.h"
#include "ui/theme/Theme.h"

namespace loungecat {

class MessageItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit MessageItemDelegate(QObject* parent = nullptr);
    ~MessageItemDelegate() override = default;

    void setThemeAndPreferences(const ThemeColors& theme, const UserPreferences& prefs);
    void setSearchHighlightQuery(const QString& query);
    QString searchHighlightQuery() const { return m_searchQuery; }
    void pruneDocumentsForModel(const QAbstractItemModel* model);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    static QString buildRichTextHtml(
        const QModelIndex& index,
        const ThemeColors& theme,
        const UserPreferences& prefs,
        const QString& searchQuery = {}
    );

    QString anchorAt(
        const QModelIndex& index,
        const QRect& itemRect,
        const QPoint& viewPos,
        const QFont& baseFont
    ) const;

    static void populateDocumentResources(
        QTextDocument& doc,
        const QModelIndex& index,
        const UserPreferences& prefs
    );

private:
    QTextDocument* documentFor(const QModelIndex& index, int width, const QFont& font) const;
    mutable QCache<QString, QTextDocument> m_documents{8 * 1024 * 1024};
    mutable QString m_documentTimeContext;
    ThemeColors m_theme = ThemeColors::darkTheme();
    UserPreferences m_prefs;
    QString m_searchQuery;
};

} // namespace loungecat
