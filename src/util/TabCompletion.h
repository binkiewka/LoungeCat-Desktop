#pragma once

#include <QString>
#include <QStringList>

namespace loungecat {

struct TabCompletionResult {
    QString text;
    int cursorPosition = 0;
};

class TabCompletion {
public:
    TabCompletionResult complete(
        const QString& currentText,
        int cursorPosition,
        const QStringList& nicknames,
        const QStringList& channels,
        const QStringList& commands,
        bool reverse = false
    );

    void reset();

private:
    bool m_active = false;
    QString m_originalPrefix;
    int m_wordStart = 0;
    QString m_textBeforeWord;
    QString m_textAfterWord;
    QStringList m_candidates;
    int m_currentIndex = 0;
    QString m_lastResultText;
    QString m_originalText;
    int m_originalCursor = 0;
    int m_lastCursor = 0;
};

} // namespace loungecat
