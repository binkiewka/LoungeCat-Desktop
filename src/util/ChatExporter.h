#pragma once

#include <QString>
#include <QList>
#include <optional>
#include "data/model/IncomingMessage.h"

namespace loungecat {

enum class ExportFormat {
    TXT,
    JSON
};

class ChatExporter {
public:
    static QString formatMessages(
        const QString& channelName,
        const QList<IncomingMessage>& messages,
        ExportFormat format,
        const std::optional<QString>& timezone = std::nullopt
    );

    static bool exportToFile(
        const QString& filePath,
        const QString& channelName,
        const QList<IncomingMessage>& messages,
        ExportFormat format,
        const std::optional<QString>& timezone = std::nullopt
    );
};

} // namespace loungecat
