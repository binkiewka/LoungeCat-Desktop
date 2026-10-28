#include "ui/chat/SearchOverlayBar.h"
#include "ui/theme/IconFactory.h"
#include <QHBoxLayout>
#include <QKeyEvent>

namespace loungecat {

SearchOverlayBar::SearchOverlayBar(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(6);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setAccessibleName(QStringLiteral("Search messages"));
    m_searchEdit->setPlaceholderText(QStringLiteral("Search in channel (Enter = Next, Esc = Close)..."));
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &SearchOverlayBar::queryChanged);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &SearchOverlayBar::nextMatchRequested);
    layout->addWidget(m_searchEdit, 1);

    m_statusLabel = new QLabel(QStringLiteral("0/0"), this);
    m_statusLabel->setMinimumWidth(42);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_statusLabel);

    m_prevBtn = new QToolButton(this);
    m_prevBtn->setFixedSize(32, 32);
    m_prevBtn->setIconSize(QSize(18, 18));
    m_prevBtn->setToolTip(QStringLiteral("Previous Match"));
    m_prevBtn->setAccessibleName(QStringLiteral("Previous Match"));
    connect(m_prevBtn, &QToolButton::clicked, this, &SearchOverlayBar::previousMatchRequested);
    layout->addWidget(m_prevBtn);

    m_nextBtn = new QToolButton(this);
    m_nextBtn->setFixedSize(32, 32);
    m_nextBtn->setIconSize(QSize(18, 18));
    m_nextBtn->setToolTip(QStringLiteral("Next Match"));
    m_nextBtn->setAccessibleName(QStringLiteral("Next Match"));
    connect(m_nextBtn, &QToolButton::clicked, this, &SearchOverlayBar::nextMatchRequested);
    layout->addWidget(m_nextBtn);

    m_closeBtn = new QToolButton(this);
    m_closeBtn->setFixedSize(32, 32);
    m_closeBtn->setIconSize(QSize(18, 18));
    m_closeBtn->setToolTip(QStringLiteral("Close Search (Esc)"));
    m_closeBtn->setAccessibleName(QStringLiteral("Close Search"));
    connect(m_closeBtn, &QToolButton::clicked, this, [this]() {
        m_searchEdit->clear();
        hide();
        Q_EMIT closed();
    });
    layout->addWidget(m_closeBtn);

    applyTheme(ThemeColors::darkTheme());
}

void SearchOverlayBar::applyTheme(const ThemeColors& theme) {
    m_prevBtn->setIcon(IconFactory::create(IconType::ChevronUp, theme.foreground, 18));
    m_nextBtn->setIcon(IconFactory::create(IconType::ChevronDown, theme.foreground, 18));
    m_closeBtn->setIcon(IconFactory::create(IconType::Close, theme.foreground, 18));
}

QString SearchOverlayBar::query() const {
    return m_searchEdit->text().trimmed();
}

void SearchOverlayBar::focusSearchInput() {
    show();
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

void SearchOverlayBar::setMatchStatus(int currentMatchOneBased, int totalMatches) {
    m_statusLabel->setText(QStringLiteral("%1/%2").arg(currentMatchOneBased).arg(totalMatches));
}

void SearchOverlayBar::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        m_searchEdit->clear();
        hide();
        Q_EMIT closed();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace loungecat
