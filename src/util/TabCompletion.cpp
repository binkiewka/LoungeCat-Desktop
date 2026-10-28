#include "util/TabCompletion.h"
#include <algorithm>

namespace loungecat {

void TabCompletion::reset() {
    m_active = false;
    m_originalPrefix.clear();
    m_candidates.clear();
    m_currentIndex = 0;
    m_lastResultText.clear();
    m_originalText.clear();
    m_originalCursor = m_lastCursor = 0;
}

TabCompletionResult TabCompletion::complete(
    const QString& currentText, int cursorPosition,
    const QStringList& nicknames, const QStringList& channels,
    const QStringList& commands, bool reverse
) {
    if (cursorPosition < 0 || cursorPosition > currentText.length()) cursorPosition = currentText.length();
    const bool cycling = m_active && currentText == m_lastResultText && cursorPosition == m_lastCursor;
    const auto previousCandidate = cycling ? m_candidates.value(m_currentIndex) : QString();
    if (!cycling) {
        reset();
        int start = cursorPosition, end = cursorPosition;
        while (start > 0 && !currentText[start - 1].isSpace()) --start;
        while (end < currentText.size() && !currentText[end].isSpace()) ++end;
        m_originalPrefix = currentText.mid(start, cursorPosition - start);
        if (m_originalPrefix.isEmpty()) return {currentText, cursorPosition};
        m_originalText = currentText;
        m_originalCursor = cursorPosition;
        m_wordStart = start;
        m_textBeforeWord = currentText.left(start);
        m_textAfterWord = currentText.mid(end);
    }

    // Rebuild against the current context even during a cycle: a departed or
    // renamed user must not survive solely in the cached candidate list.
    m_candidates.clear();
    const auto& word = m_originalPrefix;
    const bool atLineStart = m_wordStart == 0;
    const bool hasSeparator = !m_textAfterWord.isEmpty() && m_textAfterWord.front().isSpace();
    const QString space = hasSeparator ? QString() : QStringLiteral(" ");
    if (atLineStart && word.startsWith('/')) {
        for (const auto& cmd : commands) {
            const auto normalized = cmd.startsWith('/') ? cmd : QStringLiteral("/") + cmd;
            if (normalized.startsWith(word, Qt::CaseInsensitive)) m_candidates.append(normalized + space);
        }
    } else if (word.startsWith('#') || word.startsWith('&')) {
        for (const auto& channel : channels)
            if (channel.startsWith(word, Qt::CaseInsensitive)) m_candidates.append(channel + space);
    } else {
        const bool hasAt = word.startsWith('@');
        const auto prefix = hasAt ? word.mid(1) : word;
        auto sorted = nicknames;
        std::sort(sorted.begin(), sorted.end(), [](const QString& a, const QString& b) {
            return a.compare(b, Qt::CaseInsensitive) < 0;
        });
        for (const auto& nick : sorted) if (nick.startsWith(prefix, Qt::CaseInsensitive)) {
            m_candidates.append((hasAt ? QStringLiteral("@") : QString()) + nick +
                (!hasAt && atLineStart ? QStringLiteral(":") : QString()) + space);
        }
    }
    m_candidates.removeDuplicates();
    if (m_candidates.isEmpty()) {
        const TabCompletionResult original{m_originalText, m_originalCursor};
        reset();
        return original;
    }
    const int previousIndex = cycling ? m_candidates.indexOf(previousCandidate) : -1;
    m_currentIndex = previousIndex < 0 ? (reverse ? m_candidates.size() - 1 : 0) :
        (previousIndex + (reverse ? -1 : 1) + m_candidates.size()) % m_candidates.size();
    m_active = true;
    const auto& replacement = m_candidates[m_currentIndex];
    m_lastResultText = m_textBeforeWord + replacement + m_textAfterWord;
    m_lastCursor = m_textBeforeWord.size() + replacement.size() + (hasSeparator ? 1 : 0);
    return {m_lastResultText, m_lastCursor};
}

} // namespace loungecat
