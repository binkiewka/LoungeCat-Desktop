#include "util/SystemInfoCollector.h"
#include <QFile>
#include <QTextStream>
#include <QSysInfo>
#include <QStorageInfo>
#include <QProcess>
#include <QSet>

namespace loungecat {

QString SystemInfoCollector::getSystemInfoSummary() {
    QStringList parts;
    parts << QStringLiteral("Client: LoungeCat %1").arg(QStringLiteral(APP_VERSION));
    parts << QStringLiteral("OS: %1").arg(readOsInfo());
    parts << QStringLiteral("CPU: %1").arg(readCpuInfo());
    parts << QStringLiteral("Memory: %1").arg(readMemoryInfo());
    parts << QStringLiteral("Storage: %1").arg(readStorageInfo());
    const QString gpu = readGpuInfo();
    if (!gpu.isEmpty()) {
        parts << QStringLiteral("VGA: %1").arg(gpu);
    }
    parts << QStringLiteral("Uptime: %1").arg(readUptimeInfo());
    return parts.join(QStringLiteral(" • "));
}

QString SystemInfoCollector::readOsInfo() {
    QString pretty = QSysInfo::prettyProductName();
    return QStringLiteral("%1 (%2 %3)")
        .arg(pretty, QSysInfo::kernelVersion(), QSysInfo::currentCpuArchitecture());
}

QString SystemInfoCollector::readCpuInfo() {
    QFile f(QStringLiteral("/proc/cpuinfo"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString content = QString::fromUtf8(f.readAll());
        for (const QString& line : content.split('\n')) {
            if (line.startsWith(QStringLiteral("model name"))) {
                const int colon = line.indexOf(':');
                if (colon != -1) {
                    return line.mid(colon + 1).trimmed();
                }
            }
        }
    }
    return QSysInfo::currentCpuArchitecture();
}

QString SystemInfoCollector::readMemoryInfo() {
    qint64 memTotalKb = 0;
    qint64 memAvailKb = 0;
    QFile f(QStringLiteral("/proc/meminfo"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString content = QString::fromUtf8(f.readAll());
        for (const QString& line : content.split('\n')) {
            if (line.startsWith(QStringLiteral("MemTotal:"))) {
                memTotalKb = line.simplified().section(' ', 1, 1).toLongLong();
            } else if (line.startsWith(QStringLiteral("MemAvailable:"))) {
                memAvailKb = line.simplified().section(' ', 1, 1).toLongLong();
            }
        }
    }
    const double totalGib = static_cast<double>(memTotalKb) / (1024.0 * 1024.0);
    const double freeGib = static_cast<double>(memAvailKb) / (1024.0 * 1024.0);
    return QStringLiteral("%1 GiB Total (%2 GiB Free)")
        .arg(totalGib, 0, 'f', 1)
        .arg(freeGib, 0, 'f', 1);
}

QString SystemInfoCollector::readStorageInfo() {
    qint64 totalBytes = 0;
    qint64 freeBytes = 0;
    QSet<QByteArray> seenDevices;

    for (const QStorageInfo& storage : QStorageInfo::mountedVolumes()) {
        if (!storage.isValid() || !storage.isReady() || storage.isReadOnly()) continue;
        const QByteArray dev = storage.device();
        if (!dev.startsWith("/dev/") || dev.startsWith("/dev/loop")) continue;
        if (seenDevices.contains(dev)) continue;
        seenDevices.insert(dev);

        totalBytes += storage.bytesTotal();
        freeBytes += storage.bytesAvailable();
    }

    const double totalGib = static_cast<double>(totalBytes) / (1024.0 * 1024.0 * 1024.0);
    const double freeGib = static_cast<double>(freeBytes) / (1024.0 * 1024.0 * 1024.0);
    const double usedGib = std::max(0.0, totalGib - freeGib);

    return QStringLiteral("%1 GiB / %2 GiB (%3 GiB Free)")
        .arg(usedGib, 0, 'f', 1)
        .arg(totalGib, 0, 'f', 1)
        .arg(freeGib, 0, 'f', 1);
}

QString SystemInfoCollector::readGpuInfo() {
    QProcess proc;
    proc.start(QStringLiteral("lspci"), {});
    if (!proc.waitForFinished(1500)) {
        return QString();
    }
    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    QStringList gpus;
    for (const QString& line : out.split('\n')) {
        if (line.contains(QStringLiteral("VGA compatible controller"), Qt::CaseInsensitive) ||
            line.contains(QStringLiteral("3D controller"), Qt::CaseInsensitive)) {
            const int idx = line.indexOf(QStringLiteral(": "));
            if (idx != -1) {
                gpus << line.mid(idx + 2).trimmed();
            }
        }
    }
    return gpus.join(QStringLiteral(", "));
}

QString SystemInfoCollector::readUptimeInfo() {
    QFile f(QStringLiteral("/proc/uptime"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString content = QString::fromUtf8(f.readAll()).trimmed();
        const double upSecs = content.section(' ', 0, 0).toDouble();
        qint64 total = static_cast<qint64>(upSecs);
        qint64 d = total / 86400;
        qint64 h = (total % 86400) / 3600;
        qint64 m = (total % 3600) / 60;
        qint64 s = total % 60;
        return QStringLiteral("%1d %2h %3m %4s").arg(d).arg(h).arg(m).arg(s);
    }
    return QStringLiteral("N/A");
}

} // namespace loungecat
