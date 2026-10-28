#pragma once

#include <QString>

namespace loungecat {

class SingleInstanceGuard {
public:
    explicit SingleInstanceGuard(const QString& lockFilePath);
    ~SingleInstanceGuard();

    bool tryAcquire();
    void release();

private:
    QString m_lockFilePath;
    int m_fd = -1;
    bool m_acquired = false;
};

} // namespace loungecat
