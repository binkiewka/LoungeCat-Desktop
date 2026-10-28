#pragma once

#include <QWidget>
#include <QSplitter>
#include <array>
#include "data/model/UserPreferences.h"
#include "ui/chat/ChannelPaneWidget.h"

namespace loungecat {

class ChatGridContainer : public QWidget {
    Q_OBJECT
public:
    explicit ChatGridContainer(
        ConnectionManager* connectionManager,
        PreferencesManager* prefsManager,
        SpellChecker* spellChecker,
        QWidget* parent = nullptr
    );
    ~ChatGridContainer() override = default;

    GridMode gridMode() const { return m_gridMode; }
    void setGridMode(GridMode mode);
    void cycleGridMode();

    ChannelPaneWidget* primaryPane() const { return m_panes[0]; }
    std::array<ChannelPaneWidget*, 4> panes() const { return m_panes; }

    void refreshAllVisiblePanes();
    void applyTheme(const ThemeColors& theme);

private:
    void rebuildLayout();

    GridMode m_gridMode = GridMode::SINGLE;
    std::array<ChannelPaneWidget*, 4> m_panes{};
    QSplitter* m_outerSplitter = nullptr;
    QSplitter* m_topSplitter = nullptr;
    QSplitter* m_bottomSplitter = nullptr;
};

} // namespace loungecat
