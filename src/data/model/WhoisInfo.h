#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace loungecat {

struct WhoisInfo {
    QString nickname;
    std::optional<QString> username;
    std::optional<QString> hostname;
    std::optional<QString> realname;
    std::optional<QString> server;
    std::optional<QString> serverInfo;
    std::optional<QString> accountName;
    bool isSecureConnection = false;
    bool isRegistered = false;
    bool isOperator = false;
    std::optional<qint64> idleSeconds;
    std::optional<qint64> signonTime;
    QStringList channels;
    std::optional<QString> awayMessage;
    bool isComplete = false;
    qint64 cachedAtMs = 0;

    QString toFormattedMessage() const;
    QString toTooltipString() const;
    static QString formatIdleDuration(qint64 seconds);
};

} // namespace loungecat
