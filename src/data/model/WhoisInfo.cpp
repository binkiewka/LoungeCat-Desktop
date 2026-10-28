#include "data/model/WhoisInfo.h"
#include <QDateTime>

namespace loungecat {

QString WhoisInfo::formatIdleDuration(qint64 seconds) {
    if (seconds < 60) {
        return QStringLiteral("%1s").arg(seconds);
    }
    qint64 days = seconds / 86400;
    qint64 hours = (seconds % 86400) / 3600;
    qint64 mins = (seconds % 3600) / 60;
    qint64 secs = seconds % 60;
    QStringList parts;
    if (days > 0) parts << QStringLiteral("%1d").arg(days);
    if (hours > 0) parts << QStringLiteral("%1h").arg(hours);
    if (mins > 0) parts << QStringLiteral("%1m").arg(mins);
    if (secs > 0 && days == 0) parts << QStringLiteral("%1s").arg(secs);
    return parts.join(' ');
}

QString WhoisInfo::toFormattedMessage() const {
    QStringList lines;
    lines << QStringLiteral("─── WHOIS: %1 ───").arg(nickname);
    if (username || hostname) {
        lines << QStringLiteral("  User: %1@%2").arg(username.value_or(QStringLiteral("*")),
                                                    hostname.value_or(QStringLiteral("*")));
    }
    if (realname && !realname->isEmpty()) {
        lines << QStringLiteral("  Real Name: %1").arg(*realname);
    }
    if (accountName && !accountName->isEmpty()) {
        lines << QStringLiteral("  Account: %1 (Registered)").arg(*accountName);
    } else if (isRegistered) {
        lines << QStringLiteral("  Account: Registered nickname");
    }
    if (server && !server->isEmpty()) {
        if (serverInfo && !serverInfo->isEmpty()) {
            lines << QStringLiteral("  Server: %1 (%2)").arg(*server, *serverInfo);
        } else {
            lines << QStringLiteral("  Server: %1").arg(*server);
        }
    }
    if (!channels.isEmpty()) {
        lines << QStringLiteral("  Channels: %1").arg(channels.join(' '));
    }
    if (awayMessage && !awayMessage->isEmpty()) {
        lines << QStringLiteral("  Away: %1").arg(*awayMessage);
    }
    if (isOperator) {
        lines << QStringLiteral("  Status: IRC Operator");
    }
    if (isSecureConnection) {
        lines << QStringLiteral("  Connection: Secure (TLS/SSL)");
    }
    if (idleSeconds.has_value()) {
        QString line = QStringLiteral("  Idle: %1").arg(formatIdleDuration(*idleSeconds));
        if (signonTime.has_value() && *signonTime > 0) {
            QDateTime dt = QDateTime::fromSecsSinceEpoch(*signonTime);
            line += QStringLiteral(" (Signon: %1)").arg(dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        }
        lines << line;
    }
    lines << QStringLiteral("──────────────────────");
    return lines.join('\n');
}

QString WhoisInfo::toTooltipString() const {
    QStringList lines;
    lines << QStringLiteral("Nickname: %1").arg(nickname);
    if (username || hostname) {
        lines << QStringLiteral("Host: %1@%2").arg(username.value_or(QStringLiteral("?")),
                                                   hostname.value_or(QStringLiteral("?")));
    }
    if (realname && !realname->isEmpty()) {
        lines << QStringLiteral("Real name: %1").arg(*realname);
    }
    if (accountName && !accountName->isEmpty()) {
        lines << QStringLiteral("Account: %1").arg(*accountName);
    }
    if (server && !server->isEmpty()) {
        lines << QStringLiteral("Server: %1").arg(*server);
    }
    if (awayMessage && !awayMessage->isEmpty()) {
        lines << QStringLiteral("Away: %1").arg(*awayMessage);
    }
    if (idleSeconds.has_value()) {
        lines << QStringLiteral("Idle: %1").arg(formatIdleDuration(*idleSeconds));
    }
    if (isSecureConnection) {
        lines << QStringLiteral("Secure Connection (TLS)");
    }
    return lines.join('\n');
}

} // namespace loungecat
