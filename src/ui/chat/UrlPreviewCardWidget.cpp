#include "ui/chat/UrlPreviewCardWidget.h"
#include "util/PlatformUtils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace loungecat {

UrlPreviewCardWidget::UrlPreviewCardWidget(const UrlPreview& preview, const ThemeColors& theme, QWidget* parent)
    : QFrame(parent), m_preview(preview) {
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(QStringLiteral(
        "QFrame { background-color: %1; border: 1px solid %2; border-left: 3px solid %3; border-radius: 6px; }"
    ).arg(theme.currentLineBackground.name(), theme.border.name(), theme.accentColor.name()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(3);

    if (preview.siteName && !preview.siteName->isEmpty()) {
        m_siteLabel = new QLabel(*preview.siteName, this);
        m_siteLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 9pt; border: none;").arg(theme.comment.name()));
        layout->addWidget(m_siteLabel);
    }

    const QString titleText = (preview.title && !preview.title->isEmpty()) ? *preview.title : preview.url;
    m_titleLabel = new QLabel(QStringLiteral("<b>%1</b>").arg(titleText.toHtmlEscaped()), this);
    m_titleLabel->setCursor(Qt::PointingHandCursor);
    m_titleLabel->setStyleSheet(QStringLiteral("color: %1; border: none;").arg(theme.cyan.name()));
    layout->addWidget(m_titleLabel);

    if (preview.description && !preview.description->isEmpty()) {
        m_descLabel = new QLabel(preview.description->left(220), this);
        m_descLabel->setWordWrap(true);
        m_descLabel->setStyleSheet(QStringLiteral("color: %1; border: none;").arg(theme.foreground.name()));
        layout->addWidget(m_descLabel);
    }
}

} // namespace loungecat
