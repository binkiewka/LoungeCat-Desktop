#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include "data/model/IncomingMessage.h"
#include "data/model/UserPreferences.h"

namespace loungecat {

class ChatGrouping {
public:
    static bool isJoinPartQuitMessage(MessageType type);

    static QList<ChatViewItem> processMessages(
        const QList<IncomingMessage>& messages,
        JoinPartQuitDisplayMode mode,
        const QMap<QString, qint64>& lastActiveTimes = {},
        int smartHideMinutes = 10,
        qint64 nowMs = 0
    );
};

} // namespace loungecat
