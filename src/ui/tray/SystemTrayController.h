#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <QMainWindow>

namespace loungecat {

class SystemTrayController : public QObject {
    Q_OBJECT
public:
    explicit SystemTrayController(QMainWindow* mainWindow, bool headlessMode = false, QObject* parent = nullptr);
    ~SystemTrayController() override = default;

    void showNotification(const QString& title, const QString& message);

Q_SIGNALS:
    void quitRequested();

private:
    QMainWindow* m_mainWindow;
    QSystemTrayIcon* m_trayIcon = nullptr;
};

} // namespace loungecat
