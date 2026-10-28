#include "ui/chat/SplitViewContainer.h"

namespace loungecat {

SplitViewContainer::SplitViewContainer(
    Qt::Orientation orientation,
    ChannelPaneWidget* firstPane,
    ChannelPaneWidget* secondPane,
    QWidget* parent
)
    : QSplitter(orientation, parent) {
    setChildrenCollapsible(false);
    addWidget(firstPane);
    addWidget(secondPane);
    setStretchFactor(0, 1);
    setStretchFactor(1, 1);
}

} // namespace loungecat
