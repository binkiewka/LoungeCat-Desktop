#pragma once

#include <QIcon>
#include <QColor>

namespace loungecat {

enum class IconType {
    Plus,
    Grid,
    Settings,
    Backup,
    Help,
    ChevronLeft,
    ChevronRight,
    ChevronUp,
    ChevronDown,
    Search,
    Download,
    Trash,
    Users,
    Image,
    Send,
    Close,
    ChannelHash,
    ServerPulse,
    DirectMessage
};

class IconFactory {
public:
    static QIcon create(IconType type, const QColor& color, int size = 18);
    static QIcon createStatusDot(const QColor& color, bool hollow = false, int size = 14);
};

} // namespace loungecat
