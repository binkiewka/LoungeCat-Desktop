#include "util/IrcFormatParser.h"
#include <array>

namespace loungecat {

namespace {

constexpr std::array<uint32_t, 16> kMircBaseColors = {
    0xFFFFFF, // 00 White
    0x000000, // 01 Black
    0x00007F, // 02 Blue (Navy)
    0x009300, // 03 Green
    0xFF0000, // 04 Red
    0x7F0000, // 05 Brown (Maroon)
    0x9C009C, // 06 Purple
    0xFC7F00, // 07 Orange (Olive)
    0xFFFF00, // 08 Yellow
    0x00FC00, // 09 Light Green (Lime)
    0x009393, // 10 Teal (Cyan)
    0x00FFFF, // 11 Light Cyan (Aqua)
    0x0000FC, // 12 Light Blue (Royal)
    0xFF00FF, // 13 Pink (Fuchsia)
    0x7F7F7F, // 14 Grey
    0xD2D2D2  // 15 Light Grey (Silver)
};

} // namespace

std::optional<QColor> IrcFormatParser::mircColor(int code) {
    if (code >= 0 && code < 16) {
        return QColor::fromRgb(kMircBaseColors[code]);
    }
    return std::nullopt;
}

QList<IrcTextSpan> IrcFormatParser::parse(const QString& input) {
    QList<IrcTextSpan> spans;
    if (input.isEmpty()) {
        return spans;
    }

    IrcTextSpan current;
    int i = 0;
    const int len = input.length();

    const auto hasHexColor = [&](int start) {
        if (len - start < 6) return false;
        for (int at = start; at < start + 6; ++at) {
            const auto value = input[at];
            if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
                  (value >= 'A' && value <= 'F'))) return false;
        }
        return true;
    };

    auto flushCurrent = [&]() {
        if (!current.text.isEmpty()) {
            spans.append(current);
            current.text.clear();
        }
    };

    while (i < len) {
        const char16_t ch = input[i].unicode();
        switch (ch) {
            case BOLD:
                flushCurrent();
                current.bold = !current.bold;
                ++i;
                break;
            case ITALIC:
                flushCurrent();
                current.italic = !current.italic;
                ++i;
                break;
            case MONOSPACE:
                flushCurrent();
                current.monospace = !current.monospace;
                ++i;
                break;
            case UNDERLINE:
                flushCurrent();
                current.underline = !current.underline;
                ++i;
                break;
            case STRIKETHROUGH:
                flushCurrent();
                current.strikethrough = !current.strikethrough;
                ++i;
                break;
            case REVERSE:
                flushCurrent();
                current.reverse = !current.reverse;
                ++i;
                break;
            case RESET:
                flushCurrent();
                current.bold = false;
                current.italic = false;
                current.monospace = false;
                current.underline = false;
                current.strikethrough = false;
                current.reverse = false;
                current.foreground = std::nullopt;
                current.background = std::nullopt;
                ++i;
                break;
            case HEX_COLOR: {
                flushCurrent();
                ++i;
                if (!hasHexColor(i)) {
                    current.foreground = std::nullopt;
                    current.background = std::nullopt;
                } else {
                    current.foreground = QColor::fromRgb(input.mid(i, 6).toUInt(nullptr, 16));
                    i += 6;
                    if (i < len && input[i] == ',' && hasHexColor(i + 1)) {
                        current.background = QColor::fromRgb(input.mid(i + 1, 6).toUInt(nullptr, 16));
                        i += 7;
                    }
                }
                break;
            }
            case COLOR: {
                flushCurrent();
                ++i;
                // Parse up to 2 digits for foreground
                int fgDigits = 0;
                int fgCode = 0;
                while (i < len && fgDigits < 2 && (input[i] >= QLatin1Char('0') && input[i] <= QLatin1Char('9'))) {
                    fgCode = fgCode * 10 + input[i].digitValue();
                    ++i;
                    ++fgDigits;
                }
                if (fgDigits == 0) {
                    // Bare \x03 resets colors
                    current.foreground = std::nullopt;
                    current.background = std::nullopt;
                } else {
                    current.foreground = mircColor(fgCode);
                    // Check for optional ,bg
                    if (i + 1 < len && input[i] == ',' && (input[i + 1] >= QLatin1Char('0') && input[i + 1] <= QLatin1Char('9'))) {
                        ++i; // consume ','
                        int bgDigits = 0;
                        int bgCode = 0;
                        while (i < len && bgDigits < 2 && (input[i] >= QLatin1Char('0') && input[i] <= QLatin1Char('9'))) {
                            bgCode = bgCode * 10 + input[i].digitValue();
                            ++i;
                            ++bgDigits;
                        }
                        current.background = mircColor(bgCode);
                    }
                }
                break;
            }
            default:
                current.text.append(input[i]);
                ++i;
                break;
        }
    }

    flushCurrent();
    return spans;
}

QString IrcFormatParser::stripFormatting(const QString& input) {
    QString out;
    out.reserve(input.size());
    for (const auto& span : parse(input)) {
        out.append(span.text);
    }
    return out;
}

} // namespace loungecat
