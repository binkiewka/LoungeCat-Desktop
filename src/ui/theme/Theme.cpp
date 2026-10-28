#include "ui/theme/Theme.h"
#include <cmath>

namespace loungecat {

namespace {

QColor parseHexColor(const QString& hex, const QColor& fallback) {
    const QString s = hex.trimmed();
    if (s.isEmpty()) return fallback;
    QColor c(s);
    return c.isValid() ? c : fallback;
}

QString colorToHex(const QColor& c) {
    if (c.alpha() < 255) {
        return c.name(QColor::HexArgb);
    }
    return c.name(QColor::HexRgb);
}

} // namespace

ThemeColors ThemeColors::darkTheme() {
    ThemeColors t;
    t.name = QStringLiteral("Dark");
    t.isDark = true;
    t.windowBackground = QColor(0x19, 0x17, 0x24);
    t.currentLineBackground = QColor(0x26, 0x23, 0x3a);
    t.selectionBackground = QColor(0x40, 0x3d, 0x52);
    t.foreground = QColor(0xe0, 0xde, 0xf4);
    t.comment = QColor(0x90, 0x8c, 0xaa);
    t.cyan = QColor(0x9c, 0xcf, 0xd8);
    t.green = QColor(0x31, 0x74, 0x8f);
    t.orange = QColor(0xeb, 0xbc, 0xba);
    t.pink = QColor(0xc4, 0xa7, 0xe7);
    t.purple = QColor(0xc4, 0xa7, 0xe7);
    t.red = QColor(0xeb, 0x6f, 0x92);
    t.yellow = QColor(0xf6, 0xc1, 0x77);
    t.border = QColor(0x26, 0x23, 0x3a);
    t.chatBackground = QColor(0x1f, 0x1d, 0x2e);
    t.inputBackground = QColor(0x19, 0x17, 0x24);
    t.sidebarBackground = QColor(0x19, 0x17, 0x24);
    t.timestampColor = QColor(0x6e, 0x6a, 0x86);
    t.systemMessageColor = QColor(0x90, 0x8c, 0xaa);
    t.actionMessageColor = QColor(0xc4, 0xa7, 0xe7);
    t.highlightBackground = QColor(0xeb, 0xbc, 0xba, 0x33);
    t.unreadBadge = QColor(0xeb, 0x6f, 0x92);
    t.accentColor = QColor(0xc4, 0xa7, 0xe7);
    return t;
}

ThemeColors ThemeColors::lightTheme() {
    ThemeColors t;
    t.name = QStringLiteral("Light");
    t.isDark = false;
    t.windowBackground = QColor(0xfa, 0xf4, 0xed);
    t.currentLineBackground = QColor(0xf2, 0xe9, 0xe1);
    t.selectionBackground = QColor(0xdf, 0xda, 0xd9);
    t.foreground = QColor(0x57, 0x52, 0x79);
    t.comment = QColor(0x79, 0x75, 0x93);
    t.cyan = QColor(0x56, 0x94, 0x9f);
    t.green = QColor(0x28, 0x69, 0x83);
    t.orange = QColor(0xd7, 0x82, 0x7e);
    t.pink = QColor(0x90, 0x7a, 0xa9);
    t.purple = QColor(0x90, 0x7a, 0xa9);
    t.red = QColor(0xb4, 0x63, 0x7a);
    t.yellow = QColor(0xea, 0x9d, 0x34);
    t.border = QColor(0xdf, 0xda, 0xd9);
    t.chatBackground = QColor(0xff, 0xfa, 0xf3);
    t.inputBackground = QColor(0xf2, 0xe9, 0xe1);
    t.sidebarBackground = QColor(0xf2, 0xe9, 0xe1);
    t.timestampColor = QColor(0x98, 0x93, 0xa5);
    t.systemMessageColor = QColor(0x79, 0x75, 0x93);
    t.actionMessageColor = QColor(0x90, 0x7a, 0xa9);
    t.highlightBackground = QColor(0xd7, 0x82, 0x7e, 0x33);
    t.unreadBadge = QColor(0xb4, 0x63, 0x7a);
    t.accentColor = QColor(0x90, 0x7a, 0xa9);
    return t;
}

ThemeColors ThemeColors::highVoltageTheme() {
    ThemeColors t = darkTheme();
    t.name = QStringLiteral("High Voltage");
    t.windowBackground = QColor(0x0D, 0x0E, 0x15);
    t.currentLineBackground = QColor(0x1A, 0x1C, 0x29);
    t.selectionBackground = QColor(0x2B, 0x30, 0x45);
    t.foreground = QColor(0xEC, 0xEF, 0xF4);
    t.chatBackground = QColor(0x13, 0x15, 0x20);
    t.inputBackground = QColor(0x0D, 0x0E, 0x15);
    t.sidebarBackground = QColor(0x0D, 0x0E, 0x15);
    t.border = QColor(0x1F, 0x24, 0x37);
    t.accentColor = QColor(0x00, 0xFF, 0x9F);
    t.cyan = QColor(0x00, 0xE5, 0xFF);
    t.yellow = QColor(0xFF, 0xE6, 0x00);
    t.unreadBadge = QColor(0xFF, 0x2A, 0x6D);
    return t;
}

ThemeColors ThemeColors::neonNightTheme() {
    ThemeColors t = darkTheme();
    t.name = QStringLiteral("Neon Night");
    t.windowBackground = QColor(0x09, 0x05, 0x14);
    t.currentLineBackground = QColor(0x1B, 0x10, 0x35);
    t.selectionBackground = QColor(0x2E, 0x1A, 0x59);
    t.foreground = QColor(0xF5, 0xE9, 0xFF);
    t.chatBackground = QColor(0x11, 0x09, 0x26);
    t.inputBackground = QColor(0x09, 0x05, 0x14);
    t.sidebarBackground = QColor(0x09, 0x05, 0x14);
    t.border = QColor(0x24, 0x14, 0x47);
    t.accentColor = QColor(0xFF, 0x2A, 0x85);
    t.cyan = QColor(0x05, 0xD5, 0xFF);
    t.pink = QColor(0xFF, 0x2A, 0x85);
    t.unreadBadge = QColor(0xFF, 0x2A, 0x85);
    return t;
}

ThemeColors ThemeColors::retroWarmthTheme() {
    ThemeColors t = darkTheme();
    t.name = QStringLiteral("Retro Warmth");
    t.windowBackground = QColor(0x1d, 0x20, 0x21);
    t.currentLineBackground = QColor(0x3c, 0x38, 0x36);
    t.selectionBackground = QColor(0x50, 0x49, 0x45);
    t.foreground = QColor(0xeb, 0xdb, 0xb2);
    t.comment = QColor(0x92, 0x83, 0x74);
    t.chatBackground = QColor(0x28, 0x28, 0x28);
    t.inputBackground = QColor(0x1d, 0x20, 0x21);
    t.sidebarBackground = QColor(0x1d, 0x20, 0x21);
    t.border = QColor(0x3c, 0x38, 0x36);
    t.accentColor = QColor(0xd7, 0x99, 0x21);
    t.yellow = QColor(0xfa, 0xbd, 0x2f);
    t.green = QColor(0x98, 0x97, 0x1a);
    t.red = QColor(0xcc, 0x24, 0x1d);
    t.unreadBadge = QColor(0xfb, 0x49, 0x34);
    return t;
}

ThemeColors ThemeColors::stormySeaTheme() {
    ThemeColors t = darkTheme();
    t.name = QStringLiteral("Stormy Sea");
    t.windowBackground = QColor(0x24, 0x29, 0x33);
    t.currentLineBackground = QColor(0x3B, 0x42, 0x52);
    t.selectionBackground = QColor(0x43, 0x4C, 0x5E);
    t.foreground = QColor(0xEC, 0xEF, 0xF4);
    t.comment = QColor(0x61, 0x6E, 0x88);
    t.chatBackground = QColor(0x2E, 0x34, 0x40);
    t.inputBackground = QColor(0x24, 0x29, 0x33);
    t.sidebarBackground = QColor(0x24, 0x29, 0x33);
    t.border = QColor(0x3B, 0x42, 0x52);
    t.accentColor = QColor(0x88, 0xC0, 0xD0);
    t.cyan = QColor(0x8F, 0xBC, 0xBB);
    t.unreadBadge = QColor(0xBF, 0x61, 0x6A);
    return t;
}

ThemeColors ThemeColors::obsidianVeilTheme() {
    ThemeColors t = darkTheme();
    t.name = QStringLiteral("Obsidian Veil");
    t.isDark = true;
    t.windowBackground = QColor(0x00, 0x00, 0x00);
    t.chatBackground = QColor(0x00, 0x00, 0x00);
    t.sidebarBackground = QColor(0x05, 0x05, 0x08);
    t.inputBackground = QColor(0x05, 0x05, 0x08);
    t.currentLineBackground = QColor(0x0d, 0x0d, 0x12);
    t.selectionBackground = QColor(0x1a, 0x1a, 0x24);
    t.border = QColor(0x1a, 0x1a, 0x24);
    t.foreground = QColor(0xe2, 0xe2, 0xe9);
    t.comment = QColor(0x80, 0x80, 0x90);
    t.timestampColor = QColor(0x55, 0x55, 0x66);
    t.systemMessageColor = QColor(0x80, 0x80, 0x90);
    t.actionMessageColor = QColor(0xa8, 0x55, 0xf7);
    t.accentColor = QColor(0x00, 0xf2, 0xff);
    t.cyan = QColor(0x00, 0xf2, 0xff);
    t.purple = QColor(0xa8, 0x55, 0xf7);
    t.pink = QColor(0xa8, 0x55, 0xf7);
    t.green = QColor(0x00, 0xe6, 0x76);
    t.yellow = QColor(0xff, 0xd6, 0x00);
    t.orange = QColor(0xff, 0x91, 0x00);
    t.red = QColor(0xff, 0x3b, 0x30);
    t.unreadBadge = QColor(0x00, 0xe6, 0x76);
    t.highlightBackground = QColor(0x00, 0xf2, 0xff, 0x1f);
    return t;
}

QJsonObject ThemeColors::toJson() const {
    QJsonObject obj;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("isDark")] = isDark;
    obj[QStringLiteral("windowBackground")] = colorToHex(windowBackground);
    obj[QStringLiteral("currentLineBackground")] = colorToHex(currentLineBackground);
    obj[QStringLiteral("selectionBackground")] = colorToHex(selectionBackground);
    obj[QStringLiteral("foreground")] = colorToHex(foreground);
    obj[QStringLiteral("comment")] = colorToHex(comment);
    obj[QStringLiteral("cyan")] = colorToHex(cyan);
    obj[QStringLiteral("green")] = colorToHex(green);
    obj[QStringLiteral("orange")] = colorToHex(orange);
    obj[QStringLiteral("pink")] = colorToHex(pink);
    obj[QStringLiteral("purple")] = colorToHex(purple);
    obj[QStringLiteral("red")] = colorToHex(red);
    obj[QStringLiteral("yellow")] = colorToHex(yellow);
    obj[QStringLiteral("border")] = colorToHex(border);
    obj[QStringLiteral("chatBackground")] = colorToHex(chatBackground);
    obj[QStringLiteral("inputBackground")] = colorToHex(inputBackground);
    obj[QStringLiteral("sidebarBackground")] = colorToHex(sidebarBackground);
    obj[QStringLiteral("timestampColor")] = colorToHex(timestampColor);
    obj[QStringLiteral("systemMessageColor")] = colorToHex(systemMessageColor);
    obj[QStringLiteral("actionMessageColor")] = colorToHex(actionMessageColor);
    obj[QStringLiteral("highlightBackground")] = colorToHex(highlightBackground);
    obj[QStringLiteral("unreadBadge")] = colorToHex(unreadBadge);
    obj[QStringLiteral("accentColor")] = colorToHex(accentColor);
    return obj;
}

ThemeColors ThemeColors::fromJson(const QJsonObject& obj, const QString& fallbackName) {
    const bool isDark = obj.value(QStringLiteral("isDark")).toBool(true);
    ThemeColors base = isDark ? darkTheme() : lightTheme();
    base.name = obj.value(QStringLiteral("name")).toString(fallbackName);
    base.isDark = isDark;

    auto readColor = [&](const QString& key, const QColor& def) {
        return parseHexColor(obj.value(key).toString(), def);
    };

    base.windowBackground = readColor(QStringLiteral("windowBackground"), base.windowBackground);
    base.currentLineBackground = readColor(QStringLiteral("currentLineBackground"), base.currentLineBackground);
    base.selectionBackground = readColor(QStringLiteral("selectionBackground"), base.selectionBackground);
    base.foreground = readColor(QStringLiteral("foreground"), base.foreground);
    base.comment = readColor(QStringLiteral("comment"), base.comment);
    base.cyan = readColor(QStringLiteral("cyan"), base.cyan);
    base.green = readColor(QStringLiteral("green"), base.green);
    base.orange = readColor(QStringLiteral("orange"), base.orange);
    base.pink = readColor(QStringLiteral("pink"), base.pink);
    base.purple = readColor(QStringLiteral("purple"), base.purple);
    base.red = readColor(QStringLiteral("red"), base.red);
    base.yellow = readColor(QStringLiteral("yellow"), base.yellow);
    base.border = readColor(QStringLiteral("border"), base.border);
    base.chatBackground = readColor(QStringLiteral("chatBackground"), base.chatBackground);
    base.inputBackground = readColor(QStringLiteral("inputBackground"), base.inputBackground);
    base.sidebarBackground = readColor(QStringLiteral("sidebarBackground"), base.sidebarBackground);
    base.timestampColor = readColor(QStringLiteral("timestampColor"), base.timestampColor);
    base.systemMessageColor = readColor(QStringLiteral("systemMessageColor"), base.systemMessageColor);
    base.actionMessageColor = readColor(QStringLiteral("actionMessageColor"), base.actionMessageColor);
    base.highlightBackground = readColor(QStringLiteral("highlightBackground"), base.highlightBackground);
    base.unreadBadge = readColor(QStringLiteral("unreadBadge"), base.unreadBadge);
    base.accentColor = readColor(QStringLiteral("accentColor"), base.accentColor);
    return base;
}

QString ThemeColors::generateStyleSheet(float fontScale) const {
    const int basePt = std::max(9, static_cast<int>(std::round(11.0f * fontScale)));
    return QStringLiteral(R"(
        QMainWindow, QDialog {
            background-color: %1;
            color: %2;
            font-size: %8pt;
        }
        QWidget {
            color: %2;
            selection-background-color: %3;
            selection-color: %2;
        }
        QTreeWidget, QListWidget {
            background-color: %4;
            border: 1px solid %5;
            border-radius: 6px;
            outline: none;
            padding: 2px;
        }
        QListView {
            background-color: %10;
            border: none;
            outline: none;
        }
        QTableWidget {
            background-color: %4;
            border: 1px solid %5;
            border-radius: 6px;
            outline: none;
        }
        QTreeWidget::item, QListWidget::item {
            padding: 6px 10px;
            margin: 1px 2px;
            border-radius: 5px;
        }
        QTreeWidget::item:selected, QListWidget::item:selected {
            background-color: %3;
            color: %2;
        }
        QTreeWidget::item:hover:!selected, QListWidget::item:hover:!selected {
            background-color: %6;
        }
        QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QComboBox {
            background-color: %7;
            color: %2;
            border: 1px solid %5;
            border-radius: 6px;
            padding: 5px 8px;
        }
        QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus {
            border: 1px solid %9;
        }
        QPushButton {
            background-color: %6;
            color: %2;
            border: 1px solid %5;
            border-radius: 6px;
            padding: 5px 12px;
        }
        QPushButton:hover {
            background-color: %3;
            border-color: %9;
        }
        QPushButton:pressed {
            background-color: %5;
        }
        QPushButton#primaryButton, QToolButton#primaryButton {
            background-color: %9;
            color: %1;
            font-weight: bold;
            border: none;
            padding: 4px 12px;
        }
        QToolButton {
            background-color: %6;
            color: %2;
            border: 1px solid %5;
            border-radius: 6px;
            padding: 2px;
        }
        QToolButton:hover {
            background-color: %3;
            border-color: %9;
        }
        QToolButton:pressed {
            background-color: %5;
        }
        QSplitter::handle {
            background-color: %5;
            width: 1px;
            height: 1px;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 8px;
            margin: 2px 1px;
        }
        QScrollBar::handle:vertical {
            background: %3;
            min-height: 24px;
            border-radius: 3px;
        }
        QScrollBar::handle:vertical:hover {
            background: %9;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QTabWidget::pane {
            border: 1px solid %5;
            border-radius: 6px;
            background: %1;
        }
        QTabBar::tab {
            background: %6;
            color: %2;
            padding: 6px 14px;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            margin-right: 2px;
        }
        QTabBar::tab:selected {
            background: %9;
            color: %1;
            font-weight: bold;
        }
        QMenu {
            background-color: %1;
            color: %2;
            border: 1px solid %5;
            border-radius: 6px;
            padding: 4px;
        }
        QMenu::item {
            padding: 6px 20px;
            border-radius: 4px;
        }
        QMenu::item:selected {
            background-color: %3;
        }
        QToolTip {
            background-color: %6;
            color: %2;
            border: 1px solid %5;
            padding: 4px 8px;
            border-radius: 4px;
        }
    )")
        .arg(windowBackground.name(),
             foreground.name(),
             selectionBackground.name(),
             sidebarBackground.name(),
             border.name(),
             currentLineBackground.name(),
             inputBackground.name())
        .arg(basePt)
        .arg(accentColor.name(), chatBackground.name());
}

} // namespace loungecat
