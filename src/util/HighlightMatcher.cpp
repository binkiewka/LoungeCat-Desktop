#include "util/HighlightMatcher.h"
#include "util/IrcIdentity.h"
#include "util/IrcFormatParser.h"

namespace loungecat {

namespace {

bool isNickWordChar(QChar c) {
    return c.isLetterOrNumber() || c == '_' || c == '-' || c == '[' || c == ']' ||
           c == '\\' || c == '`' || c == '^' || c == '{' || c == '}' || c == '|' || c == '~';
}

} // namespace

bool HighlightMatcher::containsWord(const QString& rawText, const QString& word) {
    const QString text = IrcFormatParser::stripFormatting(rawText);
    const QString trimmed = word.trimmed();
    if (trimmed.isEmpty() || text.isEmpty()) {
        return false;
    }

    int pos = 0;
    while ((pos = text.indexOf(trimmed, pos, Qt::CaseInsensitive)) != -1) {
        const bool leftBoundary = (pos == 0) || !isNickWordChar(text[pos - 1]);
        const int endPos = pos + trimmed.length();
        const bool rightBoundary = (endPos >= text.length()) || !isNickWordChar(text[endPos]);
        if (leftBoundary && rightBoundary) {
            return true;
        }
        pos += 1;
    }
    return false;
}

bool HighlightMatcher::containsNickMention(const QString& message, const QString& nickname, const QString& mapping) {
    if (nickname.isEmpty()) return false;
    const auto text = ircIdentity(IrcFormatParser::stripFormatting(message), mapping);
    const auto nick = ircIdentity(nickname, mapping);
    int from = 0;
    while ((from = text.indexOf(nick, from, Qt::CaseSensitive)) != -1) {
        const auto end = from + nick.size();
        if ((from == 0 || !isNickWordChar(text[from - 1])) &&
            (end == text.size() || !isNickWordChar(text[end]))) return true;
        ++from;
    }
    return false;
}

bool HighlightMatcher::containsHighlight(
    const QString& message,
    const QString& currentNickname,
    const QSet<QString>& highlightWords,
    const QString& mapping
) {
    if (message.isEmpty()) {
        return false;
    }
    if (!currentNickname.isEmpty() && containsNickMention(message, currentNickname, mapping)) {
        return true;
    }
    for (const QString& word : highlightWords) {
        if (!word.isEmpty() && containsWord(message, word)) {
            return true;
        }
    }
    return false;
}

} // namespace loungecat
