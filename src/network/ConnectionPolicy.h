#pragma once
#include <QtGlobal>
#include <algorithm>

namespace loungecat {

enum class RetryDisposition { Transient, UserActionRequired };

// Platform-independent session intent. Timers and transports are adapters, not policy owners.
struct ConnectionPolicy {
    bool desired = false;
    bool paused = false;
    bool dialing = false;
    bool ready = false;
    int attempts = 0;
    qint64 readySince = -1;

    bool begin() {
        if (dialing || ready) return false;
        desired = true; paused = false; dialing = true;
        return true;
    }
    void connected(qint64 now) { dialing = false; ready = true; readySince = now; }
    void failed(RetryDisposition disposition) {
        dialing = false; ready = false; readySince = -1;
        if (disposition == RetryDisposition::UserActionRequired) paused = true;
    }
    void stop() { *this = {}; }
    bool canRetry() const { return desired && !paused && !dialing && !ready; }
    void stable(qint64 now, qint64 required = 120000) {
        if (ready && readySince >= 0 && now - readySince >= required) attempts = 0;
    }
    static int retryDelay(int attempt, double random) {
        const int delays[] = {5000, 15000, 30000, 60000, 300000};
        const auto ceiling = delays[std::clamp(attempt, 0, 4)];
        return std::clamp(int(ceiling * (0.75 + std::clamp(random, 0.0, 1.0) * 0.5)), 1000, 300000);
    }
};
}
