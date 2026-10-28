#pragma once
#include <QHostAddress>
#include <QUrl>
#include <QString>
#include <optional>

namespace loungecat {
// Syntax only: never resolve names while a connection form is being edited.
inline std::optional<QString> normalizedServerHost(const QString& input) {
    for (const auto ch : input) if (ch.category() == QChar::Other_Control) return std::nullopt;
    QString host = input.trimmed();
    if (host.isEmpty() || host.size() > 1024) return std::nullopt;
    for (const auto ch : host) if (ch.isSpace()) return std::nullopt;
    const bool bracketed = host.startsWith('[') || host.endsWith(']');
    if (bracketed) {
        if (!host.startsWith('[') || !host.endsWith(']')) return std::nullopt;
        host = host.mid(1, host.size() - 2);
        if (!host.contains(':')) return std::nullopt;
    }
    if (host.contains(':')) {
        QHostAddress address;
        if (!address.setAddress(host)) return std::nullopt;
        return host;
    }
    const QByteArray ace = QUrl::toAce(host);
    if (ace.isEmpty()) return std::nullopt;
    QByteArray name = ace;
    if (name.endsWith('.')) name.chop(1);
    if (name.isEmpty() || name.size() > 253) return std::nullopt;
    bool numeric = true;
    for (char c : name) if (c != '.' && (c < '0' || c > '9')) numeric = false;
    if (numeric) {
        const auto parts = name.split('.');
        if (parts.size() != 4) return std::nullopt;
        for (const auto& part : parts) {
            bool ok = false; const int value = part.toInt(&ok);
            if (!ok || value < 0 || value > 255 || (part.size() > 1 && part.startsWith('0'))) return std::nullopt;
        }
    }
    for (const auto& label : name.split('.')) {
        if (label.isEmpty() || label.size() > 63 || label.startsWith('-') || label.endsWith('-')) return std::nullopt;
        for (char c : label) if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '-') return std::nullopt;
    }
    return QString::fromLatin1(ace).toLower();
}
}
