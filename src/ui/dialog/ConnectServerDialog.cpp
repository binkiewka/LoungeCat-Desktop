#include "ui/dialog/ConnectServerDialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QMessageBox>
#include "util/ServerAddress.h"

namespace loungecat {

ConnectServerDialog::ConnectServerDialog(const ServerConfig& initial, bool isEditMode, QWidget* parent)
    : QDialog(parent), m_initial(initial) {
    setWindowTitle(isEditMode ? QStringLiteral("Edit IRC Server") : QStringLiteral("Add IRC Server"));
    setMinimumSize(480, 440);

    auto* mainLayout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);

    // 1. General Tab
    auto* genPage = new QWidget(tabs);
    auto* genForm = new QFormLayout(genPage);
    m_serverNameEdit = new QLineEdit(initial.serverName, genPage);
    m_serverNameEdit->setPlaceholderText(QStringLiteral("e.g. Libera.Chat"));
    genForm->addRow(QStringLiteral("Server Name:"), m_serverNameEdit);

    m_hostnameEdit = new QLineEdit(initial.hostname, genPage);
    m_hostnameEdit->setPlaceholderText(QStringLiteral("e.g. irc.libera.chat"));
    genForm->addRow(QStringLiteral("Hostname:"), m_hostnameEdit);

    m_portSpin = new QSpinBox(genPage);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(initial.port > 0 ? initial.port : 6697);
    genForm->addRow(QStringLiteral("Port:"), m_portSpin);

    m_useSslCheck = new QCheckBox(QStringLiteral("Use TLS / SSL (Recommended)"), genPage);
    m_useSslCheck->setChecked(initial.useSsl);
    connect(m_useSslCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked && m_portSpin->value() == 6667) m_portSpin->setValue(6697);
        else if (!checked && m_portSpin->value() == 6697) m_portSpin->setValue(6667);
    });
    genForm->addRow(QString(), m_useSslCheck);

    m_autoConnectCheck = new QCheckBox(QStringLiteral("Connect automatically on startup"), genPage);
    m_autoConnectCheck->setChecked(initial.autoConnect);
    genForm->addRow(QString(), m_autoConnectCheck);

    m_nicknameEdit = new QLineEdit(initial.nickname, genPage);
    m_nicknameEdit->setPlaceholderText(QStringLiteral("Your primary nickname"));
    genForm->addRow(QStringLiteral("Nickname:"), m_nicknameEdit);

    m_altNicknameEdit = new QLineEdit(initial.altNickname, genPage);
    m_altNicknameEdit->setPlaceholderText(QStringLiteral("Fallback nickname if in use"));
    genForm->addRow(QStringLiteral("Alt Nickname:"), m_altNicknameEdit);

    m_usernameEdit = new QLineEdit(initial.username, genPage);
    m_usernameEdit->setPlaceholderText(QStringLiteral("Optional (defaults to nickname)"));
    genForm->addRow(QStringLiteral("Username:"), m_usernameEdit);

    m_realNameEdit = new QLineEdit(initial.realName, genPage);
    m_realNameEdit->setPlaceholderText(QStringLiteral("Optional real name"));
    genForm->addRow(QStringLiteral("Real Name:"), m_realNameEdit);
    tabs->addTab(genPage, QStringLiteral("General"));

    // 2. Auth & SASL Tab
    auto* authPage = new QWidget(tabs);
    auto* authForm = new QFormLayout(authPage);
    m_useSaslCheck = new QCheckBox(QStringLiteral("Enable SASL PLAIN Authentication"), authPage);
    m_useSaslCheck->setChecked(initial.useSasl);
    authForm->addRow(QString(), m_useSaslCheck);

    m_saslUsernameEdit = new QLineEdit(initial.saslUsername, authPage);
    authForm->addRow(QStringLiteral("SASL Username:"), m_saslUsernameEdit);

    m_saslPasswordEdit = new QLineEdit(initial.saslPassword, authPage);
    m_saslPasswordEdit->setEchoMode(QLineEdit::Password);
    authForm->addRow(QStringLiteral("SASL Password:"), m_saslPasswordEdit);

    m_nickServPassEdit = new QLineEdit(initial.nickServPassword, authPage);
    m_nickServPassEdit->setEchoMode(QLineEdit::Password);
    authForm->addRow(QStringLiteral("NickServ Password:"), m_nickServPassEdit);

    m_nickServCmdEdit = new QLineEdit(initial.nickServCommand, authPage);
    m_nickServCmdEdit->setPlaceholderText(QStringLiteral("PRIVMSG NickServ :IDENTIFY {password}"));
    authForm->addRow(QStringLiteral("NickServ Command:"), m_nickServCmdEdit);
    tabs->addTab(authPage, QStringLiteral("Authentication"));

    // 3. Channels & Commands Tab
    auto* chanPage = new QWidget(tabs);
    auto* chanForm = new QFormLayout(chanPage);
    m_autoJoinEdit = new QLineEdit(initial.autoJoinChannels, chanPage);
    m_autoJoinEdit->setPlaceholderText(QStringLiteral("#linux, #c++, #qt"));
    chanForm->addRow(QStringLiteral("Auto-Join Channels:"), m_autoJoinEdit);

    m_onConnectCmdsEdit = new QPlainTextEdit(initial.onConnectCommands, chanPage);
    m_onConnectCmdsEdit->setPlaceholderText(QStringLiteral("One IRC command per line (e.g. /mode +i)"));
    chanForm->addRow(QStringLiteral("On-Connect Commands:"), m_onConnectCmdsEdit);
    tabs->addTab(chanPage, QStringLiteral("Channels & Commands"));

    // 4. Security & Proxy Tab
    auto* secPage = new QWidget(tabs);
    auto* secForm = new QFormLayout(secPage);
    m_tofuCheck = new QCheckBox(QStringLiteral("Trust Certificate on First Use (TOFU)"), secPage);
    m_tofuCheck->setChecked(initial.trustOnFirstUse);
    secForm->addRow(QString(), m_tofuCheck);

    m_pinnedFpEdit = new QLineEdit(initial.pinnedCertFingerprint.value_or(QString()), secPage);
    m_pinnedFpEdit->setPlaceholderText(QStringLiteral("Optional SHA-256 hex certificate fingerprint"));
    secForm->addRow(QStringLiteral("Pinned SHA-256:"), m_pinnedFpEdit);

    m_proxyTypeCombo = new QComboBox(secPage);
    m_proxyTypeCombo->addItem(QStringLiteral("None"), static_cast<int>(ProxyType::NONE));
    m_proxyTypeCombo->addItem(QStringLiteral("SOCKS5"), static_cast<int>(ProxyType::SOCKS5));
    m_proxyTypeCombo->addItem(QStringLiteral("HTTP"), static_cast<int>(ProxyType::HTTP));
    m_proxyTypeCombo->setCurrentIndex(static_cast<int>(initial.proxyType));
    secForm->addRow(QStringLiteral("Proxy Type:"), m_proxyTypeCombo);

    m_proxyHostEdit = new QLineEdit(initial.proxyHost, secPage);
    m_proxyHostEdit->setPlaceholderText(QStringLiteral("127.0.0.1"));
    secForm->addRow(QStringLiteral("Proxy Host:"), m_proxyHostEdit);

    m_proxyPortSpin = new QSpinBox(secPage);
    m_proxyPortSpin->setRange(1, 65535);
    m_proxyPortSpin->setValue(initial.proxyPort > 0 ? initial.proxyPort : 1080);
    secForm->addRow(QStringLiteral("Proxy Port:"), m_proxyPortSpin);

    m_proxyUserEdit = new QLineEdit(initial.proxyUsername, secPage);
    secForm->addRow(QStringLiteral("Proxy Username:"), m_proxyUserEdit);

    m_proxyPassEdit = new QLineEdit(initial.proxyPassword, secPage);
    m_proxyPassEdit->setEchoMode(QLineEdit::Password);
    secForm->addRow(QStringLiteral("Proxy Password:"), m_proxyPassEdit);
    tabs->addTab(secPage, QStringLiteral("TLS & Proxy"));

    mainLayout->addWidget(tabs, 1);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    btnBox->button(QDialogButtonBox::Ok)->setText(isEditMode ? QStringLiteral("Save") : QStringLiteral("Connect"));
    connect(btnBox, &QDialogButtonBox::accepted, this, [this]() {
        if (m_hostnameEdit->text().trimmed().isEmpty() || m_nicknameEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Missing Fields"),
                                 QStringLiteral("Please enter both Hostname and Nickname."));
            return;
        }
        if (!normalizedServerHost(m_hostnameEdit->text())) {
            QMessageBox::warning(this, QStringLiteral("Invalid Hostname"),
                QStringLiteral("Enter a hostname or IP address, not a URL, path or hostname:port. Use the separate Port field."));
            return;
        }
        accept();
    });
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(btnBox);
}

ServerConfig ConnectServerDialog::serverConfig() const {
    ServerConfig cfg = m_initial;
    cfg.serverName = m_serverNameEdit->text().trimmed();
    if (cfg.serverName.isEmpty()) cfg.serverName = m_hostnameEdit->text().trimmed();
    cfg.hostname = normalizedServerHost(m_hostnameEdit->text()).value_or(m_hostnameEdit->text().trimmed());
    cfg.port = m_portSpin->value();
    cfg.useSsl = m_useSslCheck->isChecked();
    cfg.autoConnect = m_autoConnectCheck->isChecked();
    cfg.nickname = m_nicknameEdit->text().trimmed();
    cfg.altNickname = m_altNicknameEdit->text().trimmed();
    cfg.username = m_usernameEdit->text().trimmed();
    cfg.realName = m_realNameEdit->text().trimmed();

    cfg.useSasl = m_useSaslCheck->isChecked();
    cfg.saslUsername = m_saslUsernameEdit->text().trimmed();
    cfg.saslPassword = m_saslPasswordEdit->text();
    cfg.nickServPassword = m_nickServPassEdit->text();
    cfg.nickServCommand = m_nickServCmdEdit->text().trimmed();

    cfg.autoJoinChannels = m_autoJoinEdit->text().trimmed();
    cfg.onConnectCommands = m_onConnectCmdsEdit->toPlainText().trimmed();

    cfg.trustOnFirstUse = m_tofuCheck->isChecked();
    const QString fp = m_pinnedFpEdit->text().trimmed();
    cfg.pinnedCertFingerprint = fp.isEmpty() ? std::nullopt : std::make_optional(fp);

    cfg.proxyType = static_cast<ProxyType>(m_proxyTypeCombo->currentData().toInt());
    cfg.proxyHost = m_proxyHostEdit->text().trimmed();
    cfg.proxyPort = m_proxyPortSpin->value();
    cfg.proxyUsername = m_proxyUserEdit->text().trimmed();
    cfg.proxyPassword = m_proxyPassEdit->text();
    return cfg;
}

} // namespace loungecat
