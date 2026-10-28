#include "util/ChatGrouping.h"
#include <QDateTime>

namespace loungecat {

bool ChatGrouping::isJoinPartQuitMessage(MessageType type) {
    return type == MessageType::JOIN ||
           type == MessageType::PART ||
           type == MessageType::QUIT ||
           type == MessageType::NICK;
}

QList<ChatViewItem> ChatGrouping::processMessages(
    const QList<IncomingMessage>& messages,
    JoinPartQuitDisplayMode mode,
    const QMap<QString, qint64>& lastActiveTimes,
    int smartHideMinutes,
    qint64 nowMs
) {
    QList<ChatViewItem> result;
    if (messages.isEmpty()) {
        return result;
    }

    if (nowMs <= 0) {
        nowMs = QDateTime::currentMSecsSinceEpoch();
    }
    const qint64 smartCutoffMs = static_cast<qint64>(smartHideMinutes) * 60LL * 1000LL;

    switch (mode) {
        case JoinPartQuitDisplayMode::SHOW_ALL: {
            for (const auto& m : messages) {
                result.append(MessageItemView{m});
            }
            break;
        }
        case JoinPartQuitDisplayMode::HIDE_ALL: {
            for (const auto& m : messages) {
                if (!isJoinPartQuitMessage(m.type)) {
                    result.append(MessageItemView{m});
                }
            }
            break;
        }
        case JoinPartQuitDisplayMode::SMART_HIDE: {
            for (const auto& m : messages) {
                if (!isJoinPartQuitMessage(m.type)) {
                    result.append(MessageItemView{m});
                    continue;
                }
                const QString key = m.sender.toLower();
                if (lastActiveTimes.contains(key)) {
                    const qint64 lastActive = lastActiveTimes.value(key);
                    if (m.timestamp - lastActive <= smartCutoffMs && m.timestamp >= lastActive) {
                        result.append(MessageItemView{m});
                    }
                }
            }
            break;
        }
        case JoinPartQuitDisplayMode::GROUPED: {
            QList<IncomingMessage> currentBatch;
            auto flushBatch = [&]() {
                if (currentBatch.isEmpty()) return;
                if (currentBatch.size() == 1) {
                    result.append(MessageItemView{currentBatch.first()});
                } else {
                    JoinPartGroupView group;
                    group.id = QStringLiteral("group_") + currentBatch.first().id;
                    group.messages = currentBatch;
                    group.timestamp = currentBatch.last().timestamp;
                    result.append(group);
                }
                currentBatch.clear();
            };

            for (const auto& m : messages) {
                if (isJoinPartQuitMessage(m.type)) {
                    currentBatch.append(m);
                } else {
                    flushBatch();
                    result.append(MessageItemView{m});
                }
            }
            flushBatch();
            break;
        }
    }

    return result;
}

} // namespace loungecat
