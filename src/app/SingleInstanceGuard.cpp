#include "app/SingleInstanceGuard.h"
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace loungecat {

SingleInstanceGuard::SingleInstanceGuard(const QString& lockFilePath)
    : m_lockFilePath(lockFilePath) {}

SingleInstanceGuard::~SingleInstanceGuard() {
    release();
}

bool SingleInstanceGuard::tryAcquire() {
    if (m_acquired) {
        return true;
    }

    QFileInfo fi(m_lockFilePath);
    fi.dir().mkpath(QStringLiteral("."));

    const QByteArray pathBytes = m_lockFilePath.toLocal8Bit();
    m_fd = ::open(pathBytes.constData(), O_CREAT | O_RDWR, 0600);
    if (m_fd < 0) {
        return false;
    }

    // Use POSIX record lock (F_SETLK), which is what JVM FileChannel.tryLock() uses on Linux,
    // so this both ignores stale/empty app.lock files on disk AND detects if a live Kotlin or C++ instance holds the lock.
    struct flock fl{};
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    if (::fcntl(m_fd, F_SETLK, &fl) == -1) {
        ::close(m_fd);
        m_fd = -1;
        return false;
    }

    // Write current PID for diagnostics
    (void)::ftruncate(m_fd, 0);
    const QByteArray pidStr = QByteArray::number(QCoreApplication::applicationPid()) + '\n';
    (void)::write(m_fd, pidStr.constData(), static_cast<size_t>(pidStr.size()));

    m_acquired = true;
    return true;
}

void SingleInstanceGuard::release() {
    if (m_fd >= 0) {
        if (m_acquired) {
            struct flock fl{};
            fl.l_type = F_UNLCK;
            fl.l_whence = SEEK_SET;
            fl.l_start = 0;
            fl.l_len = 0;
            (void)::fcntl(m_fd, F_SETLK, &fl);
            m_acquired = false;
        }
        ::close(m_fd);
        m_fd = -1;
    }
}

} // namespace loungecat
