#pragma once
#include <QString>

namespace loungecat {
inline QString ircIdentity(QString name, const QString& mapping = QStringLiteral("rfc1459")) {
    for (QChar& character : name) {
        const auto code = character.unicode();
        if (code >= 'A' && code <= 'Z') character = QChar(code + 32);
        else if (mapping != QStringLiteral("ascii")) {
            if (character == '[') character = '{';
            else if (character == ']') character = '}';
            else if (character == '\\') character = '|';
            else if (character == '^' && mapping == QStringLiteral("rfc1459")) character = '~';
        }
    }
    return name;
}
}
