#include "util/ChatExporter.h"
#include "util/IrcFormatParser.h"
#include <QFile>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QTimeZone>

namespace loungecat {

QString ChatExporter::formatMessages(
    const QString& channelName,
    const QList<IncomingMessage>& messages,
    ExportFormat format,
    const std::optional<QString>& timezone
) {
    if (format == ExportFormat::JSON) {
        QJsonArray arr;
        for (const auto& m : messages) {
            arr.append(m.toJson());
        }
        return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    }

    const QTimeZone requestedZone(timezone ? timezone->toUtf8() : QByteArray{});
    const QTimeZone zone = requestedZone.isValid() ? requestedZone : QTimeZone::systemTimeZone();
    QStringList lines;
    lines << QStringLiteral("Chat History for %1").arg(channelName);
    lines << QStringLiteral("Exported on %1").arg(QDateTime::currentDateTime().toTimeZone(zone).toString(Qt::TextDate));
    lines << QStringLiteral("----------------------------------------");
    lines << QString();

    for (const auto& m : messages) {
        const QString ts = QDateTime::fromMSecsSinceEpoch(m.timestamp, zone)
                               .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        const QString body = IrcFormatParser::stripFormatting(m.content);
        switch (m.type) {
            case MessageType::NORMAL:
                lines << QStringLiteral("[%1] <%2> %3").arg(ts, m.sender, body);
                break;
            case MessageType::ACTION:
                lines << QStringLiteral("[%1] * %2 %3").arg(ts, m.sender, body);
                break;
            case MessageType::NOTICE:
                lines << QStringLiteral("[%1] -%2- %3").arg(ts, m.sender, body);
                break;
            case MessageType::JOIN:
            case MessageType::PART:
            case MessageType::QUIT:
            case MessageType::KICK:
            case MessageType::NICK:
                lines << QStringLiteral("[%1] -Server- *** %2").arg(ts, body);
                break;
            default:
                lines << QStringLiteral("[%1] [System] %2").arg(ts, body);
                break;
        }
    }
    return lines.join('\n') + QStringLiteral("\n");
}

bool ChatExporter::exportToFile(
    const QString& filePath,
    const QString& channelName,
    const QList<IncomingMessage>& messages,
    ExportFormat format,
    const std::optional<QString>& timezone
) {
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    const QByteArray data = formatMessages(channelName, messages, format, timezone).toUtf8();
    return file.write(data) == data.size() && file.commit();
}

} // namespace loungecat
