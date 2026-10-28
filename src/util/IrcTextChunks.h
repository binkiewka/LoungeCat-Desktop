#pragma once
#include <QStringList>
#include <QTextBoundaryFinder>
#include <optional>

namespace loungecat {
// Bounded grapheme-aware chunks. Reject the entire input before enqueueing any part.
inline std::optional<QStringList> ircTextChunks(const QString& text, qsizetype budget) {
    if (text.trimmed().isEmpty() || text.size() > 8192 || budget < 4 ||
        text.contains('\r') || text.contains('\n') || text.contains(QChar(0))) return std::nullopt;
    for (qsizetype at = 0; at < text.size();) {
        qsizetype count = 1;
        if (text[at].isHighSurrogate()) {
            if (at + 1 >= text.size() || !text[at + 1].isLowSurrogate()) return std::nullopt;
            count = 2;
        } else if (text[at].isLowSurrogate()) return std::nullopt;
        at += count;
    }
    QStringList result;
    QString part;
    qsizetype bytes = 0;
    QTextBoundaryFinder boundaries(QTextBoundaryFinder::Grapheme, text);
    qsizetype start = 0;
    for (auto end = boundaries.toNextBoundary(); end != -1; end = boundaries.toNextBoundary()) {
        const auto cluster = text.mid(start, end - start);
        const auto size = cluster.toUtf8().size();
        if (size > budget) return std::nullopt;
        if (bytes + size > budget) {
            result.append(part); part.clear(); bytes = 0;
            if (result.size() >= 32) return std::nullopt;
        }
        part += cluster; bytes += size; start = end;
    }
    if (!part.isEmpty()) result.append(part);
    return result;
}
}
