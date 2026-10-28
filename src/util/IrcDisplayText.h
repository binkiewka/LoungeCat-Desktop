#pragma once
#include <QString>

namespace loungecat {
// These controls can make untrusted nicknames or URL filenames visually impersonate
// other text. Expose them in display only; never alter stored or transmitted content.
inline QString directionalControlLabel(QChar ch) {
    switch (ch.unicode()) {
        case 0x202a: return QStringLiteral("[LRE]");
        case 0x202b: return QStringLiteral("[RLE]");
        case 0x202c: return QStringLiteral("[PDF]");
        case 0x202d: return QStringLiteral("[LRO]");
        case 0x202e: return QStringLiteral("[RLO]");
        case 0x2066: return QStringLiteral("[LRI]");
        case 0x2067: return QStringLiteral("[RLI]");
        case 0x2068: return QStringLiteral("[FSI]");
        case 0x2069: return QStringLiteral("[PDI]");
        default: return {};
    }
}
inline bool hasDirectionalControls(const QString& text) {
    for (QChar ch : text) if (!directionalControlLabel(ch).isEmpty()) return true;
    return false;
}
inline QString visibleDirectionalControls(const QString& text) {
    QString result;
    result.reserve(text.size());
    for (QChar ch : text) {
        const auto label = directionalControlLabel(ch);
        if (label.isEmpty()) result += ch;
        else result += label;
    }
    return result;
}
}
