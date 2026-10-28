#pragma once

#include <QString>
#include <QSet>

namespace loungecat {

class HighlightMatcher {
public:
    static bool containsHighlight(
        const QString& message,
        const QString& currentNickname,
        const QSet<QString>& highlightWords,
        const QString& mapping = QStringLiteral("rfc1459")
    );

    static bool containsNickMention(const QString& message, const QString& nickname,
        const QString& mapping = QStringLiteral("rfc1459"));

    static bool containsWord(const QString& text, const QString& word);
};

} // namespace loungecat
