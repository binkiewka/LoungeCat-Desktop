#include "ui/tray/SystemTrayController.h"
#include <QMenu>
#include <QIcon>
#include <QApplication>

namespace loungecat {

SystemTrayController::SystemTrayController(QMainWindow* mainWindow, bool headlessMode, QObject* parent)
    : QObject(parent), m_mainWindow(mainWindow) {
    if (headlessMode || !QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }

    m_trayIcon = new QSystemTrayIcon(QIcon(QStringLiteral(":/icons/tray_icon.png")), this);
    m_trayIcon->setToolTip(QStringLiteral("LoungeCat IRC Client"));

    auto* menu = new QMenu(m_mainWindow);
    menu->addAction(QStringLiteral("Show / Hide LoungeCat"), this, [this]() {
        if (!m_mainWindow) return;
        if (m_mainWindow->isVisible() && !m_mainWindow->isMinimized()) {
            m_mainWindow->hide();
        } else {
            m_mainWindow->showNormal();
            m_mainWindow->raise();
            m_mainWindow->activateWindow();
        }
    });
    menu->addSeparator();
    menu->addAction(QStringLiteral("Quit LoungeCat"), this, &SystemTrayController::quitRequested);

    m_trayIcon->setContextMenu(menu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger && m_mainWindow) {
            if (m_mainWindow->isVisible() && !m_mainWindow->isMinimized()) {
                m_mainWindow->hide();
            } else {
                m_mainWindow->showNormal();
                m_mainWindow->raise();
                m_mainWindow->activateWindow();
            }
        }
    });

    m_trayIcon->show();
}

void SystemTrayController::showNotification(const QString& title, const QString& message) {
    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->showMessage(title, message, QSystemTrayIcon::Information, 5000);
    }
}

} // namespace loungecat
