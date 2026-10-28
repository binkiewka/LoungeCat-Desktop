#include "ui/chat/ChatGridContainer.h"
#include <QVBoxLayout>

namespace loungecat {

ChatGridContainer::ChatGridContainer(
    ConnectionManager* connectionManager,
    PreferencesManager* prefsManager,
    SpellChecker* spellChecker,
    QWidget* parent
)
    : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_outerSplitter = new QSplitter(Qt::Vertical, this);
    m_outerSplitter->setChildrenCollapsible(false);

    m_topSplitter = new QSplitter(Qt::Horizontal, m_outerSplitter);
    m_topSplitter->setChildrenCollapsible(false);

    m_bottomSplitter = new QSplitter(Qt::Horizontal, m_outerSplitter);
    m_bottomSplitter->setChildrenCollapsible(false);

    for (int i = 0; i < 4; ++i) {
        m_panes[i] = new ChannelPaneWidget(connectionManager, prefsManager, spellChecker, i > 0, this);
    }

    m_topSplitter->addWidget(m_panes[0]);
    m_topSplitter->addWidget(m_panes[1]);
    m_bottomSplitter->addWidget(m_panes[2]);
    m_bottomSplitter->addWidget(m_panes[3]);

    mainLayout->addWidget(m_outerSplitter);
    rebuildLayout();
}

void ChatGridContainer::setGridMode(GridMode mode) {
    if (m_gridMode == mode) return;
    m_gridMode = mode;
    rebuildLayout();
    refreshAllVisiblePanes();
}

void ChatGridContainer::cycleGridMode() {
    switch (m_gridMode) {
        case GridMode::SINGLE:
            setGridMode(GridMode::SPLIT_HORIZONTAL);
            break;
        case GridMode::SPLIT_HORIZONTAL:
            setGridMode(GridMode::SPLIT_VERTICAL);
            break;
        case GridMode::SPLIT_VERTICAL:
            setGridMode(GridMode::GRID_2X2);
            break;
        case GridMode::GRID_2X2:
            setGridMode(GridMode::SINGLE);
            break;
    }
}

void ChatGridContainer::rebuildLayout() {
    switch (m_gridMode) {
        case GridMode::SINGLE:
            m_topSplitter->setOrientation(Qt::Horizontal);
            m_panes[0]->setShowChannelSelector(false);
            m_panes[0]->setVisible(true);
            m_panes[1]->setVisible(false);
            m_bottomSplitter->setVisible(false);
            break;
        case GridMode::SPLIT_HORIZONTAL:
            m_topSplitter->setOrientation(Qt::Horizontal);
            m_panes[0]->setShowChannelSelector(true);
            m_panes[1]->setShowChannelSelector(true);
            m_panes[0]->setVisible(true);
            m_panes[1]->setVisible(true);
            m_bottomSplitter->setVisible(false);
            break;
        case GridMode::SPLIT_VERTICAL:
            m_topSplitter->setOrientation(Qt::Vertical);
            m_panes[0]->setShowChannelSelector(true);
            m_panes[1]->setShowChannelSelector(true);
            m_panes[0]->setVisible(true);
            m_panes[1]->setVisible(true);
            m_bottomSplitter->setVisible(false);
            break;
        case GridMode::GRID_2X2:
            m_topSplitter->setOrientation(Qt::Horizontal);
            m_bottomSplitter->setOrientation(Qt::Horizontal);
            for (int i = 0; i < 4; ++i) {
                m_panes[i]->setShowChannelSelector(true);
                m_panes[i]->setVisible(true);
            }
            m_bottomSplitter->setVisible(true);
            break;
    }
}

void ChatGridContainer::refreshAllVisiblePanes() {
    for (auto* p : m_panes) {
        if (p && p->isVisible()) {
            p->refreshPane();
        }
    }
}

void ChatGridContainer::applyTheme(const ThemeColors& theme) {
    for (auto* p : m_panes) {
        if (p) p->applyTheme(theme);
    }
}

} // namespace loungecat
