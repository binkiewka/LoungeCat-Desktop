#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QPlainTextEdit>
#include "data/model/ServerConfig.h"

namespace loungecat {

class ConnectServerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConnectServerDialog(const ServerConfig& initial = {}, bool isEditMode = false, QWidget* parent = nullptr);
    ~ConnectServerDialog() override = default;

    ServerConfig serverConfig() const;

private:
    ServerConfig m_initial;

    // General Tab
    QLineEdit* m_serverNameEdit = nullptr;
    QLineEdit* m_hostnameEdit = nullptr;
    QSpinBox* m_portSpin = nullptr;
    QCheckBox* m_useSslCheck = nullptr;
    QCheckBox* m_autoConnectCheck = nullptr;
    QLineEdit* m_nicknameEdit = nullptr;
    QLineEdit* m_altNicknameEdit = nullptr;
    QLineEdit* m_usernameEdit = nullptr;
    QLineEdit* m_realNameEdit = nullptr;

    // Auth & SASL Tab
    QCheckBox* m_useSaslCheck = nullptr;
    QLineEdit* m_saslUsernameEdit = nullptr;
    QLineEdit* m_saslPasswordEdit = nullptr;
    QLineEdit* m_nickServPassEdit = nullptr;
    QLineEdit* m_nickServCmdEdit = nullptr;

    // Channels & Commands Tab
    QLineEdit* m_autoJoinEdit = nullptr;
    QPlainTextEdit* m_onConnectCmdsEdit = nullptr;

    // Security & Proxy Tab
    QCheckBox* m_tofuCheck = nullptr;
    QLineEdit* m_pinnedFpEdit = nullptr;
    QComboBox* m_proxyTypeCombo = nullptr;
    QLineEdit* m_proxyHostEdit = nullptr;
    QSpinBox* m_proxyPortSpin = nullptr;
    QLineEdit* m_proxyUserEdit = nullptr;
    QLineEdit* m_proxyPassEdit = nullptr;
};

} // namespace loungecat
