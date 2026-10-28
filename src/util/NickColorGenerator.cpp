#include "util/NickColorGenerator.h"
#include <array>
#include <cmath>

namespace loungecat {

namespace {

constexpr std::array<uint32_t, 16> kDarkColors = {
    0xFF6B6B, // Coral Red
    0x4ECDC4, // Medium Turquoise
    0xFFE66D, // Pastel Yellow
    0x95E1D3, // Mint
    0xF38181, // Salmon
    0xAA96DA, // Lavender
    0xFCBAD3, // Pink
    0xA8D8EA, // Light Blue
    0xFF9F43, // Orange
    0xEE5A24, // Burnt Orange
    0x009432, // Green
    0x0652DD, // Bright Blue
    0x9980FA, // Purple
    0x833471, // Magenta
    0xEA2027, // Red
    0x006266  // Teal
};

constexpr std::array<uint32_t, 16> kLightColors = {
    0xD63031, // Dark Red
    0x00B894, // Dark Mint
    0x0984E3, // Dark Blue
    0x6C5CE7, // Dark Purple
    0xE17055, // Terra Cotta
    0x00CEC9, // Dark Cyan
    0xFD79A8, // Dark Pink
    0xE84393, // Deep Pink
    0x2D3436, // Charcoal
    0x636E72, // Slate
    0xB2BEC3, // Grey
    0x74B9FF, // Sky Blue
    0xA29BFE, // Periwinkle
    0x55EFC4, // Seafoam
    0x81ECEC, // Aqua
    0xFAB1A0  // Peach
};

} // namespace

int32_t NickColorGenerator::javaStringHashCode(const QString& str) {
    uint32_t h = 0;
    for (QChar ch : str) {
        h = 31u * h + static_cast<uint32_t>(ch.unicode());
    }
    return static_cast<int32_t>(h);
}

int NickColorGenerator::getPaletteIndex(const QString& nickname) {
    int64_t hash = static_cast<int64_t>(javaStringHashCode(nickname));
    int64_t absHash = hash < 0 ? -hash : hash;
    return static_cast<int>(absHash % 16);
}

QColor NickColorGenerator::getColorForNickname(const QString& nickname, bool isDarkTheme) {
    int idx = getPaletteIndex(nickname);
    uint32_t rgb = isDarkTheme ? kDarkColors[idx] : kLightColors[idx];
    return QColor::fromRgb(rgb);
}

} // namespace loungecat
