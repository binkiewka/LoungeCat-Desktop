#pragma once

#include <QSplitter>
#include "ui/chat/ChannelPaneWidget.h"

namespace loungecat {

class SplitViewContainer : public QSplitter {
    Q_OBJECT
public:
    explicit SplitViewContainer(
        Qt::Orientation orientation,
        ChannelPaneWidget* firstPane,
        ChannelPaneWidget* secondPane,
        QWidget* parent = nullptr
    );
    ~SplitViewContainer() override = default;
};

} // namespace loungecat
