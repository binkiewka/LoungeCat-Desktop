#pragma once

#include <QString>

namespace loungecat {

class SystemInfoCollector {
public:
    static QString getSystemInfoSummary();

private:
    static QString readOsInfo();
    static QString readCpuInfo();
    static QString readMemoryInfo();
    static QString readStorageInfo();
    static QString readGpuInfo();
    static QString readUptimeInfo();
};

} // namespace loungecat
