#include "data/model/IncomingMessage.h"
#include <QJsonArray>
#include <QJsonValue>

namespace loungecat {

QString messageTypeToString(MessageType type) {
    switch (type) {
        case MessageType::NORMAL: return QStringLiteral("NORMAL");
        case MessageType::ACTION: return QStringLiteral("ACTION");
        case MessageType::NOTICE: return QStringLiteral("NOTICE");
        case MessageType::JOIN:   return QStringLiteral("JOIN");
        case MessageType::PART:   return QStringLiteral("PART");
        case MessageType::QUIT:   return QStringLiteral("QUIT");
        case MessageType::KICK:   return QStringLiteral("KICK");
        case MessageType::NICK:   return QStringLiteral("NICK");
        case MessageType::TOPIC:  return QStringLiteral("TOPIC");
        case MessageType::MODE:   return QStringLiteral("MODE");
        case MessageType::SERVER: return QStringLiteral("SERVER");
        case MessageType::ERROR:  return QStringLiteral("ERROR");
        case MessageType::CTCP:   return QStringLiteral("CTCP");
        case MessageType::MOTD:   return QStringLiteral("MOTD");
        case MessageType::WHOIS:  return QStringLiteral("WHOIS");
    }
    return QStringLiteral("NORMAL");
}

MessageType messageTypeFromString(const QString& str) {
    const QString u = str.trimmed().toUpper();
    if (u == QStringLiteral("ACTION")) return MessageType::ACTION;
    if (u == QStringLiteral("NOTICE")) return MessageType::NOTICE;
    if (u == QStringLiteral("JOIN"))   return MessageType::JOIN;
    if (u == QStringLiteral("PART"))   return MessageType::PART;
    if (u == QStringLiteral("QUIT"))   return MessageType::QUIT;
    if (u == QStringLiteral("KICK"))   return MessageType::KICK;
    if (u == QStringLiteral("NICK"))   return MessageType::NICK;
    if (u == QStringLiteral("TOPIC"))  return MessageType::TOPIC;
    if (u == QStringLiteral("MODE"))   return MessageType::MODE;
    if (u == QStringLiteral("SERVER")) return MessageType::SERVER;
    if (u == QStringLiteral("ERROR"))  return MessageType::ERROR;
    if (u == QStringLiteral("CTCP"))   return MessageType::CTCP;
    if (u == QStringLiteral("MOTD"))   return MessageType::MOTD;
    if (u == QStringLiteral("WHOIS"))  return MessageType::WHOIS;
    return MessageType::NORMAL;
}

QJsonObject IncomingMessage::toJson() const {
    QJsonObject obj;
    obj[QStringLiteral("id")] = id;
    obj[QStringLiteral("target")] = target;
    obj[QStringLiteral("sender")] = sender;
    if (senderHost) obj[QStringLiteral("senderHost")] = *senderHost;
    if (senderAccount) obj[QStringLiteral("senderAccount")] = *senderAccount;
    obj[QStringLiteral("content")] = content;
    obj[QStringLiteral("timestamp")] = timestamp;
    obj[QStringLiteral("type")] = messageTypeToString(type);
    obj[QStringLiteral("isSelf")] = isSelf;
    obj[QStringLiteral("isHighlight")] = isHighlight;
    if (!deliveryStatus.isEmpty()) obj[QStringLiteral("deliveryStatus")] = deliveryStatus;
    if (!urlPreviews.isEmpty()) {
        QJsonArray arr;
        for (const auto& p : urlPreviews) arr.append(p.toJson());
        obj[QStringLiteral("urlPreviews")] = arr;
    }
    if (replyToMessageId) obj[QStringLiteral("replyToMessageId")] = *replyToMessageId;
    if (replyToSender) obj[QStringLiteral("replyToSender")] = *replyToSender;
    if (replyToPreview) obj[QStringLiteral("replyToPreview")] = *replyToPreview;
    if (!reactions.isEmpty()) {
        QJsonObject reactObj;
        for (auto it = reactions.begin(); it != reactions.end(); ++it) {
            QJsonArray usersArr;
            for (const QString& u : it.value()) usersArr.append(u);
            reactObj[it.key()] = usersArr;
        }
        obj[QStringLiteral("reactions")] = reactObj;
    }
    return obj;
}

IncomingMessage IncomingMessage::fromJson(const QJsonObject& obj) {
    IncomingMessage msg;
    msg.id = obj.value(QStringLiteral("id")).toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    msg.target = obj.value(QStringLiteral("target")).toString();
    msg.sender = obj.value(QStringLiteral("sender")).toString();
    if (obj.contains(QStringLiteral("senderHost")) && !obj.value(QStringLiteral("senderHost")).isNull()) {
        msg.senderHost = obj.value(QStringLiteral("senderHost")).toString();
    }
    if (obj.contains(QStringLiteral("senderAccount")) && !obj.value(QStringLiteral("senderAccount")).isNull()) {
        msg.senderAccount = obj.value(QStringLiteral("senderAccount")).toString();
    }
    msg.content = obj.value(QStringLiteral("content")).toString();
    msg.timestamp = obj.value(QStringLiteral("timestamp")).toInteger(QDateTime::currentMSecsSinceEpoch());
    msg.type = messageTypeFromString(obj.value(QStringLiteral("type")).toString(QStringLiteral("NORMAL")));
    msg.isSelf = obj.value(QStringLiteral("isSelf")).toBool(false);
    msg.isHighlight = obj.value(QStringLiteral("isHighlight")).toBool(false);
    msg.deliveryStatus = obj.value(QStringLiteral("deliveryStatus")).toString();
    if (obj.contains(QStringLiteral("urlPreviews")) && obj.value(QStringLiteral("urlPreviews")).isArray()) {
        const QJsonArray arr = obj.value(QStringLiteral("urlPreviews")).toArray();
        for (const auto& v : arr) {
            if (v.isObject()) msg.urlPreviews.append(UrlPreview::fromJson(v.toObject()));
        }
    }
    if (obj.contains(QStringLiteral("replyToMessageId")) && !obj.value(QStringLiteral("replyToMessageId")).isNull())
        msg.replyToMessageId = obj.value(QStringLiteral("replyToMessageId")).toString();
    if (obj.contains(QStringLiteral("replyToSender")) && !obj.value(QStringLiteral("replyToSender")).isNull())
        msg.replyToSender = obj.value(QStringLiteral("replyToSender")).toString();
    if (obj.contains(QStringLiteral("replyToPreview")) && !obj.value(QStringLiteral("replyToPreview")).isNull())
        msg.replyToPreview = obj.value(QStringLiteral("replyToPreview")).toString();
    if (obj.contains(QStringLiteral("reactions")) && obj.value(QStringLiteral("reactions")).isObject()) {
        const QJsonObject reactObj = obj.value(QStringLiteral("reactions")).toObject();
        for (auto it = reactObj.begin(); it != reactObj.end(); ++it) {
            if (it.value().isArray()) {
                QSet<QString> users;
                for (const auto& u : it.value().toArray()) users.insert(u.toString());
                msg.reactions.insert(it.key(), users);
            }
        }
    }
    return msg;
}

} // namespace loungecat
