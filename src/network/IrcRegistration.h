#pragma once
#include <QString>
#include <QStringList>

namespace loungecat {

// Confirmed identity is kept by the transport; candidate nicks never replace it before acknowledgement.
struct IrcRegistration {
    bool required = false;
    bool authenticated = false;
    bool failed = false;
    bool postConnectStarted = false;
    int nicknameAttempts = 0;

    bool mayBecomeReady() const { return !failed && (!required || authenticated); }
    static bool offersPlain(const QString& mechanisms) {
        return mechanisms.isEmpty() || mechanisms.split(',').contains(QStringLiteral("PLAIN"), Qt::CaseInsensitive);
    }
};
}
