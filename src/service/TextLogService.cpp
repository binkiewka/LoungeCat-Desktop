#include "service/TextLogService.h"
#include "util/PlatformUtils.h"
#include "util/IrcFormatParser.h"
#include <QFile>
#include <QCryptographicHash>
#include <QDateTime>
#include <QRegularExpression>
#include <QDirIterator>
#include <QFutureWatcher>
#include <QThread>
#include <QtConcurrent>

namespace loungecat {
TextLogService::TextLogService(const QString& root, QObject* parent)
    : QObject(parent), m_logsRootPath(root.isEmpty()
        ? PlatformUtils::resolveAppDataDirectory().filePath(QStringLiteral("logs")) : root) {
    m_pool.setMaxThreadCount(1);
    if (!QDir().mkpath(m_logsRootPath)) m_lastError = QStringLiteral("Cannot create the text-log directory.");
    else migrateLegacyFolders();
}
TextLogService::~TextLogService() { m_pool.waitForDone(); }
static QString logComponent(const QString& name, const QRegularExpression& forbidden,
    const QString& fallback) {
    auto label = name.trimmed();
    label.replace(forbidden, QStringLiteral("_"));
    while (label.endsWith('_')) label.chop(1);
    if (label.isEmpty() || label == QStringLiteral(".") || label == QStringLiteral("..")) label = fallback;
    // Readable, bounded ASCII names; identity is the full original UTF-8 value.
    const auto digest = QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex();
    return label.left(64) + QStringLiteral("-") + QString::fromLatin1(digest);
}
QString TextLogService::sanitizeServerName(const QString& name) {
    return logComponent(name, QRegularExpression(QStringLiteral("[^a-zA-Z0-9.-]")), QStringLiteral("network"));
}
QString TextLogService::sanitizeChannelName(const QString& name) {
    return logComponent(name, QRegularExpression(QStringLiteral("[^a-zA-Z0-9#._-]")), QStringLiteral("channel"));
}
bool TextLogService::migrateLegacyFolders() {
    Q_ASSERT(QThread::currentThread() == thread());
    QDir root(m_logsRootPath);
    for (const auto& info : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        auto clean = info.fileName();
        if (!clean.endsWith('_')) continue;
        while (clean.endsWith('_')) clean.chop(1);
        if (clean.isEmpty()) continue;
        if (root.exists(clean)) {
            m_lastError = QStringLiteral("Both legacy and current log folders exist. Both were preserved; no automatic merge was performed.");
            return false;
        }
        if (!root.rename(info.fileName(), clean)) {
            m_lastError = QStringLiteral("A legacy log folder could not be renamed. The source was preserved.");
            return false;
        }
    }
    return true;
}
static QString existingLogPath(const QString& rootPath, const QString& serverName,
    const QString& channelName, qint64 serverId) {
    // Old sanitized paths have ambiguous ownership. Preserve them; never append
    // new conversation data to a potentially shared legacy file.
    const QString network = serverId > 0 ? QStringLiteral("network-%1").arg(serverId)
        : TextLogService::sanitizeServerName(serverName);
    const QDir directory(QDir(rootPath).filePath(QStringLiteral("v2/") + network));
    return directory.filePath(TextLogService::sanitizeChannelName(channelName) + QStringLiteral(".log"));
}
QString TextLogService::resolveLogFilePath(const QString& serverName, const QString& channelName,
    qint64 serverId) const {
    return existingLogPath(m_logsRootPath, serverName, channelName, serverId);
}
QString TextLogService::appendLog(const QString& root, const QString& serverName, const QString& channelName,
    const IncomingMessage& message, qint64 serverId) {
    qint64 bytes = 0;
    int count = 0;
    QDirIterator iterator(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        if (++count > MAX_LOG_FILES) return QStringLiteral("The text-log file quota was reached. Archive old logs to resume logging.");
        bytes += iterator.fileInfo().size();
        if (bytes >= MAX_TOTAL_LOG_BYTES) return QStringLiteral("The text-log byte quota was reached. Existing logs were preserved.");
    }
    const auto path = existingLogPath(root, serverName, channelName, serverId);
    const auto directory = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(directory)) return QStringLiteral("Cannot create a server log directory.");
    const auto timestamp = QDateTime::fromMSecsSinceEpoch(message.timestamp).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    const auto text = IrcFormatParser::stripFormatting(message.content);
    const auto line = (message.type == MessageType::ACTION
        ? QStringLiteral("[%1] * %2 %3\n").arg(timestamp, message.sender, text)
        : QStringLiteral("[%1] <%2> %3\n").arg(timestamp, message.sender, text)).toUtf8();
    if (line.size() > 64 * 1024 || bytes + line.size() > MAX_TOTAL_LOG_BYTES)
        return QStringLiteral("Text-log quota reached; the message remains in chat history.");
    const bool exists = QFile::exists(path);
    if (!exists && count >= MAX_LOG_FILES) return QStringLiteral("No space remains in the text-log file quota.");
    if (exists && QFileInfo(path).size() + line.size() > MAX_LOG_FILE_BYTES) {
        bool rotated = false;
        for (int index = 1; index <= 3; ++index) {
            const auto destination = path + QStringLiteral(".%1").arg(index);
            if (!QFile::exists(destination)) {
                if (count >= MAX_LOG_FILES) break;
                rotated = QFile::rename(path, destination);
                break;
            }
        }
        if (!rotated) return QStringLiteral("Log rotation is full. Archive older segments to resume logging; no history was deleted.");
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append) || file.write(line) != line.size() || !file.flush())
        return QStringLiteral("Cannot append to a text log. The message remains in chat history.");
    return {};
}
bool TextLogService::logMessage(const QString& serverName, const QString& channelName, const IncomingMessage& message, qint64 serverId) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (message.type != MessageType::NORMAL && message.type != MessageType::ACTION) return true;
    if (m_queued >= 64) {
        m_lastError = QStringLiteral("The text-log writer is busy; additional work was not queued.");
        Q_EMIT storageError(m_lastError);
        return false;
    }
    ++m_queued;
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
        --m_queued;
        const auto error = watcher->result();
        watcher->deleteLater();
        // A successful append ends the failure episode. A later identical
        // failure must be reported again, rather than suppressed forever.
        if (error.isEmpty()) m_lastError.clear();
        else if (m_lastError != error) { m_lastError = error; Q_EMIT storageError(error); }
    });
    const auto root = m_logsRootPath;
    watcher->setFuture(QtConcurrent::run(&m_pool, [root, serverName, channelName, message, serverId]() {
        return appendLog(root, serverName, channelName, message, serverId);
    }));
    return true;
}
bool TextLogService::flushSync() {
    m_pool.waitForDone();
    bool success = m_lastError.isEmpty();
    // Template watchers have no Q_OBJECT of their own. Newer Qt versions
    // correctly reject them as findChildren's meta-object search type.
    for (auto* base : findChildren<QFutureWatcherBase*>()) {
        auto* watcher = dynamic_cast<QFutureWatcher<QString>*>(base);
        if (watcher && watcher->isFinished() && !watcher->result().isEmpty()) { m_lastError = watcher->result(); success = false; }
    }
    return success;
}
}
