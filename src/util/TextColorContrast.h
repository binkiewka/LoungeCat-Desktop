#pragma once
#include <QColor>
#include <algorithm>
#include <cmath>

namespace loungecat {
inline double textContrast(const QColor& foreground, const QColor& background) {
    auto luminance = [](const QColor& color) {
        auto linear = [](double value) {
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) +
            0.0722 * linear(color.blueF());
    };
    const auto first = luminance(foreground), second = luminance(background);
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

inline QColor readableTextColor(const QColor& requested, const QColor& background, const QColor& fallback) {
    if (textContrast(requested, background) >= 4.5) return requested;
    if (textContrast(fallback, background) >= 4.5) return fallback;
    return textContrast(Qt::black, background) >= textContrast(Qt::white, background)
        ? QColor(Qt::black) : QColor(Qt::white);
}

} // namespace loungecat
