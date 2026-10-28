#pragma once
#include <QCryptographicHash>
#include <QHash>
#include <QStringList>
#include <QUuid>
#include <algorithm>
#include <limits>

namespace loungecat {
// Timestamp-only history has no globally unique identity. Match exact timestamp
// and payload occurrences, not text alone; preserve repeated identical frames.
// Caller supplies a validated server time, or empty to retain random identities.
class TimestampReplayIdentity {
public:
    explicit TimestampReplayIdentity(int limit = 8192) : m_limit(std::clamp(limit, 1, 8192)) {}
    QString next(const QString& serverTime, const QStringList& fields, const QString& historyScope = {}) {
        if (serverTime.isEmpty()) return randomId();
        QByteArray input;
        for (const auto& field : QStringList{serverTime} + fields) {
            const auto bytes = field.toUtf8();
            input += QByteArray::number(bytes.size()) + ':' + bytes;
        }
        const auto signature = QCryptographicHash::hash(input, QCryptographicHash::Sha256);
        quint64 ordinal = 0;
        if (!historyScope.isEmpty()) {
            if (!m_history.contains(historyScope) && m_history.size() >= 64) return randomId();
            auto& counts = m_history[historyScope];
            if (!counts.contains(signature) && m_historyEntries >= m_limit) return randomId();
            if (!counts.contains(signature)) { counts.insert(signature, 0); ++m_historyEntries; }
            auto& count = counts[signature];
            if (count == std::numeric_limits<quint64>::max()) return randomId();
            ordinal = count++;
            retain(signature);
            m_live[signature] = std::max(m_live.value(signature), count);
        } else {
            retain(signature);
            auto& count = m_live[signature];
            if (count == std::numeric_limits<quint64>::max()) return randomId();
            ordinal = count++;
        }
        return QStringLiteral("time:") + QString::fromLatin1(QCryptographicHash::hash(
            signature + ':' + QByteArray::number(ordinal), QCryptographicHash::Sha256).toHex());
    }
    void end(const QString& scope) { m_historyEntries -= m_history.take(scope).size(); }
    void clear() { m_live.clear(); m_history.clear(); m_historyEntries = 0; }
    int retainedEntries() const { return m_live.size() + m_historyEntries; }
private:
    static QString randomId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
    void retain(const QByteArray& signature) {
        if (!m_live.contains(signature)) {
            if (m_live.size() >= m_limit) m_live.erase(m_live.begin());
            m_live.insert(signature, 0);
        }
    }
    int m_limit;
    int m_historyEntries = 0;
    QHash<QByteArray, quint64> m_live;
    QHash<QString, QHash<QByteArray, quint64>> m_history;
};
}
