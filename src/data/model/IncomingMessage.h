#pragma once

#include <QString>
#include <QList>
#include <QMap>
#include <QSet>
#include <QJsonObject>
#include <QUuid>
#include <QDateTime>
#include <optional>
#include "data/model/UrlPreview.h"

namespace loungecat {

enum class MessageType {
    NORMAL,
    ACTION,
    NOTICE,
    JOIN,
    PART,
    QUIT,
    KICK,
    NICK,
    TOPIC,
    MODE,
    SERVER,
    ERROR,
    CTCP,
    MOTD,
    WHOIS
};

QString messageTypeToString(MessageType type);
MessageType messageTypeFromString(const QString& str);

struct IncomingMessage {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString target;
    QString sender;
    std::optional<QString> senderHost;
    std::optional<QString> senderAccount;
    QString content;
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    MessageType type = MessageType::NORMAL;
    bool isSelf = false;
    bool isHighlight = false;
    QString deliveryStatus;
    QList<UrlPreview> urlPreviews;
    std::optional<QString> replyToMessageId;
    std::optional<QString> replyToSender;
    std::optional<QString> replyToPreview;
    // IRCv3 +draft/react: emoji -> set of nicknames
    QMap<QString, QSet<QString>> reactions;

    QJsonObject toJson() const;
    static IncomingMessage fromJson(const QJsonObject& obj);

    bool operator==(const IncomingMessage& other) const = default;
};

} // namespace loungecat
