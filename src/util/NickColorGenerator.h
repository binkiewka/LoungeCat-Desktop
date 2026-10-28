#pragma once

#include <QString>
#include <QColor>
#include <cstdint>

namespace loungecat {

class NickColorGenerator {
public:
    // Exact Java String.hashCode() over UTF-16 code units: s[0]*31^(n-1) + ... + s[n-1]
    static int32_t javaStringHashCode(const QString& str);

    static QColor getColorForNickname(const QString& nickname, bool isDarkTheme = true);
    static int getPaletteIndex(const QString& nickname);
};

} // namespace loungecat
