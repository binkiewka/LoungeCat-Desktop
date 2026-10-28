#include "data/model/ChannelUser.h"

namespace loungecat {

QString userModePrefix(UserMode mode) {
    switch (mode) {
        case UserMode::OWNER:  return QStringLiteral("~");
        case UserMode::ADMIN:  return QStringLiteral("&");
        case UserMode::OP:     return QStringLiteral("@");
        case UserMode::HALFOP: return QStringLiteral("%");
        case UserMode::VOICE:  return QStringLiteral("+");
        case UserMode::NONE:   return QString();
    }
    return QString();
}

int userModePriority(UserMode mode) {
    switch (mode) {
        case UserMode::OWNER:  return 0;
        case UserMode::ADMIN:  return 1;
        case UserMode::OP:     return 2;
        case UserMode::HALFOP: return 3;
        case UserMode::VOICE:  return 4;
        case UserMode::NONE:   return 5;
    }
    return 5;
}

std::optional<UserMode> userModeFromPrefix(QChar prefix) {
    switch (prefix.unicode()) {
        case '~': return UserMode::OWNER;
        case '&': return UserMode::ADMIN;
        case '@': return UserMode::OP;
        case '%': return UserMode::HALFOP;
        case '+': return UserMode::VOICE;
        default:  return std::nullopt;
    }
}

std::optional<UserMode> userModeFromModeChar(QChar modeChar) {
    switch (modeChar.unicode()) {
        case 'q': return UserMode::OWNER;
        case 'a': return UserMode::ADMIN;
        case 'o': return UserMode::OP;
        case 'h': return UserMode::HALFOP;
        case 'v': return UserMode::VOICE;
        default:  return std::nullopt;
    }
}

UserMode ChannelUser::primaryMode() const {
    UserMode best = UserMode::NONE;
    for (UserMode m : modes) {
        if (userModePriority(m) < userModePriority(best)) {
            best = m;
        }
    }
    return best;
}

QString ChannelUser::displayPrefix() const {
    return userModePrefix(primaryMode());
}

} // namespace loungecat
