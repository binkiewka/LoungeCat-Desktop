#pragma once

#include <QString>
#include <QSet>
#include <optional>

namespace loungecat {

enum class UserMode {
    OWNER,   // ~ (q) - priority 0
    ADMIN,   // & (a) - priority 1
    OP,      // @ (o) - priority 2
    HALFOP,  // % (h) - priority 3
    VOICE,   // + (v) - priority 4
    NONE     //       - priority 5
};

QString userModePrefix(UserMode mode);
int userModePriority(UserMode mode);
std::optional<UserMode> userModeFromPrefix(QChar prefix);
std::optional<UserMode> userModeFromModeChar(QChar modeChar);

struct ChannelUser {
    QString nickname;
    QSet<UserMode> modes;
    bool isAway = false;
    std::optional<QString> account;
    std::optional<QString> realname;
    std::optional<QString> host;

    UserMode primaryMode() const;
    QString displayPrefix() const;

    bool operator==(const ChannelUser& other) const = default;
};

} // namespace loungecat
