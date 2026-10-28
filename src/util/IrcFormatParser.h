#pragma once

#include <QString>
#include <QList>
#include <QColor>
#include <optional>

namespace loungecat {

struct IrcTextSpan {
    QString text;
    bool bold = false;
    bool italic = false;
    bool monospace = false;
    bool underline = false;
    bool strikethrough = false;
    bool reverse = false;
    std::optional<QColor> foreground;
    std::optional<QColor> background;

    bool operator==(const IrcTextSpan& other) const = default;
};

class IrcFormatParser {
public:
    static constexpr char16_t BOLD = 0x02;
    static constexpr char16_t COLOR = 0x03;
    static constexpr char16_t HEX_COLOR = 0x04;
    static constexpr char16_t RESET = 0x0F;
    static constexpr char16_t MONOSPACE = 0x11;
    static constexpr char16_t REVERSE = 0x16;
    static constexpr char16_t ITALIC = 0x1D;
    static constexpr char16_t STRIKETHROUGH = 0x1E;
    static constexpr char16_t UNDERLINE = 0x1F;

    static QList<IrcTextSpan> parse(const QString& input);
    static QString stripFormatting(const QString& input);
    static std::optional<QColor> mircColor(int code);
};

} // namespace loungecat
