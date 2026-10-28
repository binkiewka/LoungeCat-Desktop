#include "ui/chat/MessageItemDelegate.h"
#include "ui/chat/MessageListModel.h"
#include "util/IrcFormatParser.h"
#include "util/IrcDisplayText.h"
#include "util/NickColorGenerator.h"
#include "util/TextColorContrast.h"
#include "service/LinkPreviewService.h"
#include "service/ImageCache.h"
#include <QPainter>
#include <QAbstractTextDocumentLayout>
#include <QRegularExpression>
#include <QUrl>
#include <QTimeZone>
#include <cmath>
#include <functional>
#include <algorithm>
#include "service/ImageCache.h"

namespace loungecat {

namespace {

QString linkifyAndHighlight(const QString& text, const QColor& linkColor, const QString& searchQuery,
    const std::function<QString(const QString&, qsizetype, bool)>& renderText) {
    static const QRegularExpression urlRe(QStringLiteral(R"((https?://[^\s<>"'`]+))"), QRegularExpression::CaseInsensitiveOption);
    const QString query = searchQuery.trimmed();
    const QRegularExpression searchRe(QRegularExpression::escape(query), QRegularExpression::CaseInsensitiveOption);
    // Match only source text. Searching generated HTML can corrupt hrefs, styles and entities.
    auto highlight = [&](const QString& value, qsizetype sourceStart) {
        if (query.isEmpty()) return renderText(value, sourceStart, false);
        QString html;
        qsizetype offset = 0;
        auto matches = searchRe.globalMatch(value);
        while (matches.hasNext()) {
            const auto match = matches.next();
            html += renderText(value.mid(offset, match.capturedStart() - offset), sourceStart + offset, false);
            html += renderText(match.captured(), sourceStart + match.capturedStart(), true);
            offset = match.capturedEnd();
        }
        return html + renderText(value.mid(offset), sourceStart + offset, false);
    };
    QString out;
    qsizetype offset = 0;
    auto urls = urlRe.globalMatch(text);
    while (urls.hasNext()) {
        const auto match = urls.next();
        QString url = match.captured();
        const bool hasQueryOrFragment = url.contains('?') || url.contains('#');
        const auto openParens = url.count('('), openBrackets = url.count('['), openBraces = url.count('{');
        auto closeParens = url.count(')'), closeBrackets = url.count(']'), closeBraces = url.count('}');
        while (!url.isEmpty()) {
            const auto last = url.back();
            if (last == ')' && closeParens > openParens) --closeParens;
            else if (last == ']' && closeBrackets > openBrackets) --closeBrackets;
            else if (last == '}' && closeBraces > openBraces) --closeBraces;
            else if (hasQueryOrFragment || !QStringLiteral(".,;!").contains(last)) break;
            url.chop(1);
        }
        out += highlight(text.mid(offset, match.capturedStart() - offset), offset);
        const QUrl parsed(url);
        if (parsed.isValid() && !parsed.host().isEmpty() && !hasDirectionalControls(url)) {
            out += QStringLiteral("<a href=\"%1\" style=\"color:%2; text-decoration:underline;\">%3</a>")
                       .arg(url.toHtmlEscaped(), linkColor.name(), highlight(url, match.capturedStart()));
        } else {
            out += highlight(url, match.capturedStart());
        }
        out += highlight(match.captured().mid(url.size()), match.capturedStart() + url.size());
        offset = match.capturedEnd();
    }
    return out + highlight(text.mid(offset), offset);
}

QString formatIrcSpansToHtml(const QString& rawContent, const ThemeColors& theme, const QString& searchQuery) {
    const QList<IrcTextSpan> spans = IrcFormatParser::parse(rawContent);
    struct StyledRange { qsizetype start, end; QString style; };
    QList<StyledRange> ranges;
    QString plain;
    for (const auto& span : spans) {
        QStringList styles;
        if (span.bold) styles << QStringLiteral("font-weight:bold");
        if (span.italic) styles << QStringLiteral("font-style:italic");
        if (span.monospace) styles << QStringLiteral("font-family:monospace");
        if (span.underline && span.strikethrough) styles << QStringLiteral("text-decoration:underline line-through");
        else if (span.underline) styles << QStringLiteral("text-decoration:underline");
        else if (span.strikethrough) styles << QStringLiteral("text-decoration:line-through");
        if (span.reverse) {
            styles << QStringLiteral("color:%1").arg(theme.chatBackground.name());
            styles << QStringLiteral("background-color:%1").arg(theme.foreground.name());
        } else {
            if (span.foreground || span.background) {
                const auto background = span.background.value_or(theme.chatBackground);
                const auto foreground = readableTextColor(span.foreground.value_or(theme.foreground),
                    background, theme.foreground);
                styles << QStringLiteral("color:%1").arg(foreground.name());
            }
            if (span.background) styles << QStringLiteral("background-color:%1").arg(span.background->name());
        }
        const auto start = plain.size();
        plain += span.text;
        ranges.append({start, plain.size(), styles.join(';')});
    }
    // Find URLs and search terms in one visible-text coordinate space. IRC
    // style boundaries are not URL boundaries and must never alter hrefs.
    auto renderText = [&](const QString& value, qsizetype start, bool highlighted) {
        QString html;
        const auto end = start + value.size();
        auto it = std::lower_bound(ranges.cbegin(), ranges.cend(), start,
            [](const StyledRange& range, qsizetype position) { return range.end <= position; });
        for (; it != ranges.cend() && it->start < end; ++it) {
            const auto left = std::max(start, it->start), right = std::min(end, it->end);
            QString text = visibleDirectionalControls(value.mid(left - start, right - left)).toHtmlEscaped();
            text.replace('\n', QStringLiteral("<br/>"));
            QString style = it->style;
            if (highlighted) style += QStringLiteral(";background-color:%1;color:#000000;font-weight:bold").arg(theme.yellow.name());
            html += style.isEmpty() ? text : QStringLiteral("<span style=\"%1\">%2</span>").arg(style, text);
        }
        return html;
    };
    return linkifyAndHighlight(plain, theme.cyan, searchQuery, renderText);
}

QString formatPreviewsToHtml(
    const QModelIndex& index,
    const ThemeColors& theme,
    const UserPreferences& prefs
) {
    if (prefs.urlImageDisplayMode == UrlImageDisplayMode::DISABLED) {
        return QString();
    }
    if (!index.data(MessageListModel::HasUrlPreviewRole).toBool()) {
        return QString();
    }

    QList<UrlPreview> previews;
    const QVariant prevVar = index.data(MessageListModel::UrlPreviewsRole);
    if (prevVar.canConvert<QList<UrlPreview>>()) {
        previews = prevVar.value<QList<UrlPreview>>();
    }
    if (previews.isEmpty()) {
        const QString u = index.data(MessageListModel::FirstPreviewUrlRole).toString();
        if (!u.isEmpty()) {
            UrlPreview p;
            p.url = u;
            p.title = index.data(MessageListModel::FirstPreviewTitleRole).toString();
            if (LinkPreviewService::isImageUrl(u)) {
                p.imageUrl = u;
                p.isImage = true;
            }
            previews.append(p);
        }
    }

    if (previews.isEmpty()) return QString();

    QStringList cards;
    const int maxPreviews = 3;
    for (int i = 0; i < std::min(maxPreviews, static_cast<int>(previews.size())); ++i) {
        const UrlPreview& p = previews[i];
        if (p.url.trimmed().isEmpty() || hasDirectionalControls(p.url)) continue;

        if (prefs.urlImageDisplayMode == UrlImageDisplayMode::COMPACT) {
            const QString site = visibleDirectionalControls(p.siteName.value_or(QStringLiteral("Link"))).toHtmlEscaped();
            const QString title = visibleDirectionalControls(p.title.value_or(p.url)).toHtmlEscaped();
            QString desc = visibleDirectionalControls(p.description.value_or(QString())).toHtmlEscaped();
            if (desc.size() > 120) {
                desc = desc.left(117) + QStringLiteral("...");
            }
            cards.append(QStringLiteral(
                "<div style=\"margin-top:2px; font-size:9pt; color:%1;\">"
                "↗ <span style=\"font-weight:bold; color:%2;\">[%3]</span> "
                "<a href=\"%4\" style=\"color:%5; text-decoration:underline; font-weight:bold;\">%6</a>"
                "%7"
                "</div>"
            ).arg(theme.comment.name(),
                 theme.accentColor.name(),
                 site,
                 p.url.toHtmlEscaped(),
                 theme.cyan.name(),
                 title,
                 desc.isEmpty() ? QString() : QStringLiteral(" <span style=\"color:%1;\">- %2</span>").arg(theme.foreground.name(), desc)));
            continue;
        }

        // Inline Previews
        const bool isImg = p.isImage || LinkPreviewService::isImageUrl(p.url) ||
                           (p.imageUrl.has_value() && LinkPreviewService::isImageUrl(*p.imageUrl));

        if (isImg && (p.isImage || !p.title.has_value())) {
            // Standalone direct image preview
            const QString imgUrl = p.imageUrl.value_or(p.url);
            const QImage img = ImageCache::instance().getImage(imgUrl);
            if (!img.isNull()) {
                QSize dispSize = img.size();
                if (dispSize.width() > 360 || dispSize.height() > 240) {
                    dispSize.scale(360, 240, Qt::KeepAspectRatio);
                }
                cards.append(QStringLiteral(
                    "<div style=\"margin-top:4px; margin-bottom:4px;\">"
                    "<a href=\"%1\"><img src=\"%2\" width=\"%3\" height=\"%4\" style=\"border:1px solid %5;\" /></a>"
                    "</div>"
                ).arg(p.url.toHtmlEscaped(), imgUrl.toHtmlEscaped(),
                     QString::number(dispSize.width()), QString::number(dispSize.height()),
                     theme.border.name()));
            } else {
                const QString status = ImageCache::instance().hasRecentFailure(imgUrl)
                    ? QStringLiteral("Image preview unavailable")
                    : QStringLiteral("Loading image preview");
                cards.append(QStringLiteral(
                    "<div style=\"margin-top:3px; margin-bottom:3px; font-size:9pt; color:%1;\">"
                    "🖼 <i>%5: <a href=\"%2\" style=\"color:%3; text-decoration:underline;\">%4</a></i>"
                    "</div>"
                ).arg(theme.comment.name(), p.url.toHtmlEscaped(), theme.cyan.name(), p.url.toHtmlEscaped(), status));
            }
        } else {
            // OpenGraph Card (Article / YouTube / Web Page)
            const QString site = visibleDirectionalControls(p.siteName.value_or(QUrl(p.url).host()).toUpper()).toHtmlEscaped();
            const QString title = visibleDirectionalControls(p.title.value_or(p.url)).toHtmlEscaped();
            QString desc = visibleDirectionalControls(p.description.value_or(QString())).toHtmlEscaped();
            if (desc.size() > 180) {
                desc = desc.left(177) + QStringLiteral("...");
            }

            QString thumbHtml;
            if (p.imageUrl.has_value() && !p.imageUrl->isEmpty()) {
                const QString thumbUrl = *p.imageUrl;
                const QImage thumbImg = ImageCache::instance().getImage(thumbUrl);
                if (!thumbImg.isNull()) {
                    QSize tSize = thumbImg.size();
                    if (tSize.width() > 320 || tSize.height() > 180) {
                        tSize.scale(320, 180, Qt::KeepAspectRatio);
                    }
                    thumbHtml = QStringLiteral(
                        "<div style=\"margin-top:5px;\"><a href=\"%1\"><img src=\"%2\" width=\"%3\" height=\"%4\" style=\"border:1px solid %5;\" /></a></div>"
                    ).arg(p.url.toHtmlEscaped(), thumbUrl.toHtmlEscaped(),
                         QString::number(tSize.width()), QString::number(tSize.height()),
                         theme.border.name());
                }
            }

            const QString descHtml = desc.isEmpty() ? QString() : QStringLiteral(
                "<div style=\"color:%1; font-size:9pt; margin-top:2px; margin-bottom:2px;\"><a href=\"%2\" style=\"color:%1; text-decoration:none;\">%3</a></div>"
            ).arg(theme.foreground.name(), p.url.toHtmlEscaped(), desc);

            cards.append(QStringLiteral(
                "<table style=\"margin-top:4px; margin-bottom:4px; background-color:%1; border:1px solid %2; border-left:3px solid %3;\" cellpadding=\"6\" cellspacing=\"0\">"
                "<tr><td>"
                "<div style=\"font-size:8pt; font-weight:bold; color:%4;\"><a href=\"%5\" style=\"color:%4; text-decoration:none;\">%6</a></div>"
                "<div style=\"margin-top:2px; margin-bottom:2px;\"><a href=\"%5\" style=\"color:%7; font-weight:bold; font-size:10pt; text-decoration:none;\">%8</a></div>"
                "%9"
                "%10"
                "</td></tr></table>"
            ).arg(theme.currentLineBackground.name(),
                 theme.border.name(),
                 theme.accentColor.name(),
                 theme.comment.name(),
                 p.url.toHtmlEscaped(),
                 site,
                 theme.cyan.name(),
                 title,
                 descHtml,
                 thumbHtml));
        }
    }

    return cards.join(QString());
}

} // namespace

MessageItemDelegate::MessageItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {
    auto invalidate = [this](const QString&) { m_documents.clear(); };
    connect(&ImageCache::instance(), &ImageCache::imageLoaded, this, invalidate);
    connect(&ImageCache::instance(), &ImageCache::imageFailed, this, invalidate);
}

void MessageItemDelegate::setThemeAndPreferences(const ThemeColors& theme, const UserPreferences& prefs) {
    if (m_theme == theme && m_prefs == prefs) return;
    m_documents.clear();
    m_theme = theme;
    m_prefs = prefs;
}

void MessageItemDelegate::setSearchHighlightQuery(const QString& query) {
    if (m_searchQuery == query) return;
    m_searchQuery = query;
    m_documents.clear();
}

void MessageItemDelegate::pruneDocumentsForModel(const QAbstractItemModel* model) {
    if (!model) {
        m_documents.clear();
        return;
    }
    QSet<qulonglong> liveRevisions;
    for (int row = 0; row < model->rowCount(); ++row) {
        liveRevisions.insert(model->index(row, 0).data(MessageListModel::RenderRevisionRole).toULongLong());
    }
    const QString prefix = QString::number(reinterpret_cast<quintptr>(model)) + QLatin1Char(':');
    // Iterate a key snapshot: remove() deletes the owned document and changes the cache.
    for (const auto& key : m_documents.keys()) {
        if (!key.startsWith(prefix)) continue;
        const auto end = key.indexOf(QLatin1Char(':'), prefix.size());
        bool valid = false;
        const auto revision = key.mid(prefix.size(), end - prefix.size()).toULongLong(&valid);
        if (!valid || !liveRevisions.contains(revision)) m_documents.remove(key);
    }
}

void MessageItemDelegate::populateDocumentResources(
    QTextDocument& doc,
    const QModelIndex& index,
    const UserPreferences& prefs
) {
    if (prefs.urlImageDisplayMode == UrlImageDisplayMode::DISABLED) return;
    if (!index.data(MessageListModel::HasUrlPreviewRole).toBool()) return;

    QList<UrlPreview> previews;
    const QVariant prevVar = index.data(MessageListModel::UrlPreviewsRole);
    if (prevVar.canConvert<QList<UrlPreview>>()) {
        previews = prevVar.value<QList<UrlPreview>>();
    }
    if (previews.isEmpty()) {
        const QString u = index.data(MessageListModel::FirstPreviewUrlRole).toString();
        if (!u.isEmpty()) {
            UrlPreview p;
            p.url = u;
            if (LinkPreviewService::isImageUrl(u)) {
                p.imageUrl = u;
                p.isImage = true;
            }
            previews.append(p);
        }
    }

    for (const auto& p : previews) {
        if (p.imageUrl.has_value() && !p.imageUrl->isEmpty()) {
            const QImage img = ImageCache::instance().getImage(*p.imageUrl);
            if (!img.isNull()) {
                doc.addResource(QTextDocument::ImageResource, QUrl(*p.imageUrl), img);
            }
        }
        if (p.isImage && !p.url.isEmpty()) {
            const QImage img = ImageCache::instance().getImage(p.url);
            if (!img.isNull()) {
                doc.addResource(QTextDocument::ImageResource, QUrl(p.url), img);
            }
        }
    }
}

QString MessageItemDelegate::anchorAt(
    const QModelIndex& index,
    const QRect& itemRect,
    const QPoint& viewPos,
    const QFont& baseFont
) const {
    if (!index.isValid()) return QString();
    if (index.data(MessageListModel::IsGroupRole).toBool()) return QString();

    // The cached painted document is authoritative: raw IRC controls can
    // split even the scheme, so a raw-text URL prefilter misses visible links.
    const int docLeft = itemRect.left() + 6;
    const int docTop = itemRect.top() + 1;
    const QPointF docPos(viewPos.x() - docLeft, viewPos.y() - docTop);
    if (docPos.x() < 0 || docPos.y() < 0) return QString();

    return documentFor(index, std::max(100, itemRect.width() - 12), baseFont)->documentLayout()->anchorAt(docPos);
}

QString MessageItemDelegate::buildRichTextHtml(
    const QModelIndex& index,
    const ThemeColors& theme,
    const UserPreferences& prefs,
    const QString& searchQuery
) {
    const qint64 tsMs = index.data(MessageListModel::TimestampRole).toLongLong();
    const QString tsStr = MessageListModel::formatTimestamp(tsMs, prefs.timestampFormat, prefs.timezone);
    const QString tsHtml = QStringLiteral("<span style=\"color:%1;\">[%2]</span> ")
                               .arg(theme.timestampColor.name(), tsStr.toHtmlEscaped());

    if (index.data(MessageListModel::IsGroupRole).toBool()) {
        const QString display = visibleDirectionalControls(index.data(Qt::DisplayRole).toString()).toHtmlEscaped().replace('\n', QStringLiteral("<br/>"));
        return QStringLiteral("%1<span style=\"color:%2; font-style:italic;\">%3</span>")
            .arg(tsHtml, theme.systemMessageColor.name(), display);
    }

    const QString sender = index.data(MessageListModel::SenderRole).toString();
    const QString rawContent = index.data(MessageListModel::ContentRole).toString();
    const auto msgType = static_cast<MessageType>(index.data(MessageListModel::MessageTypeRole).toInt());

    const QColor nickColor = readableTextColor(prefs.coloredNicknames
        ? NickColorGenerator::getColorForNickname(sender, theme.isDark)
        : theme.accentColor, theme.chatBackground, theme.foreground);

    QString bodyHtml = formatIrcSpansToHtml(rawContent, theme, searchQuery);
    const auto status = index.data(MessageListModel::DeliveryStatusRole).toString();
    if (!status.isEmpty() && status != QStringLiteral("delivered"))
        bodyHtml += QStringLiteral(" <span style=\"color:%1; font-size:8pt;\">[%2]</span>")
            .arg(theme.comment.name(), status.toHtmlEscaped());

    QString replyHtml;
    const QString replySender = index.data(MessageListModel::ReplySenderRole).toString();
    const QString replyPreview = index.data(MessageListModel::ReplyPreviewRole).toString();
    if (!replySender.isEmpty() || !replyPreview.isEmpty()) {
        const QString who = replySender.isEmpty() ? QStringLiteral("message") : QStringLiteral("@%1").arg(visibleDirectionalControls(replySender).toHtmlEscaped());
        replyHtml = QStringLiteral(
            "<div style=\"margin-bottom:2px; font-size:9pt; color:%1;\">↱ <b>%2</b>: <i>%3</i></div>"
        ).arg(theme.comment.name(), who, visibleDirectionalControls(replyPreview).toHtmlEscaped());
    }

    const QString previewHtml = formatPreviewsToHtml(index, theme, prefs);

    QString reactionsHtml;
    const QString reactionsSummary = index.data(MessageListModel::ReactionsSummaryRole).toString();
    if (!reactionsSummary.isEmpty()) {
        reactionsHtml = QStringLiteral(
            "<div style=\"margin-top:2px; font-size:9pt; color:%1;\">%2</div>"
        ).arg(theme.accentColor.name(), visibleDirectionalControls(reactionsSummary).toHtmlEscaped());
    }

    switch (msgType) {
        case MessageType::NORMAL:
            return QStringLiteral("%1%2<span style=\"color:%3; font-weight:bold;\">&lt;%4&gt;</span> <span style=\"color:%5;\">%6</span>%7%8")
                .arg(replyHtml, tsHtml, nickColor.name(), visibleDirectionalControls(sender).toHtmlEscaped(), theme.foreground.name(), bodyHtml, previewHtml, reactionsHtml);
        case MessageType::ACTION:
            return QStringLiteral("%1%2<span style=\"color:%3; font-style:italic;\">* <b>%4</b> %5</span>%6%7")
                .arg(replyHtml, tsHtml, theme.actionMessageColor.name(), visibleDirectionalControls(sender).toHtmlEscaped(), bodyHtml, previewHtml, reactionsHtml);
        case MessageType::NOTICE:
            return QStringLiteral("%1%2<span style=\"color:%3; font-weight:bold;\">-%4-</span> <span style=\"color:%5;\">%6</span>%7")
                .arg(replyHtml, tsHtml, theme.orange.name(), visibleDirectionalControls(sender).toHtmlEscaped(), theme.foreground.name(), bodyHtml, reactionsHtml);
        case MessageType::ERROR:
            return QStringLiteral("%1<span style=\"color:%2; font-weight:bold;\">⚠ %3</span>")
                .arg(tsHtml, theme.red.name(), bodyHtml);
        case MessageType::WHOIS:
        case MessageType::MOTD:
            return QStringLiteral("%1<span style=\"color:%2; font-family:monospace;\">%3</span>")
                .arg(tsHtml, theme.cyan.name(), bodyHtml);
        default:
            return QStringLiteral("%1<span style=\"color:%2;\">• %3</span>")
                .arg(tsHtml, theme.systemMessageColor.name(), bodyHtml);
    }
}

void MessageItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save();

    const bool isSelected = (option.state & QStyle::State_Selected);
    const bool isHighlight = index.data(MessageListModel::IsHighlightRole).toBool();

    if (isSelected) {
        painter->fillRect(option.rect, m_theme.selectionBackground);
    } else if (isHighlight) {
        painter->fillRect(option.rect, m_theme.highlightBackground);
        painter->fillRect(QRect(option.rect.left(), option.rect.top(), 3, option.rect.height()), m_theme.unreadBadge);
    }

    auto* document = documentFor(index, std::max(100, option.rect.width() - 12), option.font);
    painter->translate(option.rect.left() + 6, option.rect.top() + 1);
    document->drawContents(painter);

    painter->restore();
}

QTextDocument* MessageItemDelegate::documentFor(const QModelIndex& index, int width, const QFont& baseFont) const {
    // Formatting can follow the system timezone even when preferences/model revisions
    // have not changed. Include the effective zone so old rows cannot reuse stale text.
    const QTimeZone requested(m_prefs.timezone.value_or(QString()).toUtf8());
    const QByteArray zoneId = requested.isValid() ? requested.id() : QTimeZone::systemTimeZoneId();
    const QString timeContext = QStringLiteral(":tz=") + QString::fromUtf8(zoneId)
        + (m_prefs.timestampFormat == TimestampFormat::RELATIVE
            ? QString::number(QDateTime::currentMSecsSinceEpoch() / 60000) : QString());
    if (m_documentTimeContext != timeContext) {
        // Old relative-minute/timezone documents can no longer be reused.
        m_documents.clear();
        m_documentTimeContext = timeContext;
    }
    const QString key = QStringLiteral("%1:%2:%3:%4").arg(reinterpret_cast<quintptr>(index.model()))
        .arg(index.data(MessageListModel::RenderRevisionRole).toULongLong()).arg(width).arg(baseFont.toString())
        + timeContext;
    if (auto* existing = m_documents.object(key)) return existing;
    auto* document = new QTextDocument;
    document->setDocumentMargin(4);
    auto font = baseFont;
    font.setPointSizeF(std::max(8.5, 11.0 * static_cast<double>(fontSizeMultiplier(m_prefs.fontSize))));
    document->setDefaultFont(font);
    document->setTextWidth(width);
    populateDocumentResources(*document, index, m_prefs);
    document->setHtml(buildRichTextHtml(index, m_theme, m_prefs, m_searchQuery));
    qsizetype cost = std::max(1, document->characterCount() * 8);
    const auto previews = index.data(MessageListModel::UrlPreviewsRole).value<QList<UrlPreview>>();
    for (const auto& preview : previews) {
        const auto size = ImageCache::instance().getImageSize(preview.imageUrl.value_or(preview.url));
        cost += std::max(0, size.width()) * std::max(0, size.height()) * 4;
    }
    cost = std::min(cost, m_documents.maxCost());
    m_documents.insert(key, document, cost);
    return document;
}
QSize MessageItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    const int width = option.rect.width() > 100 ? option.rect.width() - 12 : 680;
    const auto* document = documentFor(index, width, option.font);
    return QSize(width, static_cast<int>(std::ceil(document->size().height())) + 2);
}

} // namespace loungecat
