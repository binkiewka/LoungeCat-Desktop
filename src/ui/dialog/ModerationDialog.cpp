#include "ui/dialog/ModerationDialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>

namespace loungecat {

ModerationDialog::ModerationDialog(const QString& channelName, const ChannelUser& user, QWidget* parent)
    : QDialog(parent), m_channelName(channelName), m_user(user) {
    setWindowTitle(QStringLiteral("Moderate %1 in %2").arg(user.nickname, channelName));
    setMinimumWidth(400);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    m_actionCombo = new QComboBox(this);
    m_actionCombo->addItems({
        QStringLiteral("Kick"),
        QStringLiteral("Ban (+b)"),
        QStringLiteral("Kick + Ban"),
        QStringLiteral("Quiet / Mute (+q)"),
        QStringLiteral("Unquiet (-q)"),
        QStringLiteral("Unban (-b)")
    });
    form->addRow(QStringLiteral("Action:"), m_actionCombo);

    m_maskCombo = new QComboBox(this);
    m_maskCombo->setEditable(true);
    const QString host = user.host.value_or(QStringLiteral("*"));
    m_maskCombo->addItem(QStringLiteral("*!*@%1").arg(host));
    m_maskCombo->addItem(QStringLiteral("%1!*@*").arg(user.nickname));
    m_maskCombo->addItem(QStringLiteral("%1!*@%2").arg(user.nickname, host));
    form->addRow(QStringLiteral("Ban/Quiet Mask:"), m_maskCombo);

    m_reasonEdit = new QLineEdit(this);
    m_reasonEdit->setPlaceholderText(QStringLiteral("Optional reason"));
    form->addRow(QStringLiteral("Reason:"), m_reasonEdit);

    layout->addLayout(form);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(btnBox);
}

QStringList ModerationDialog::selectedCommands() const {
    QStringList cmds;
    const int idx = m_actionCombo->currentIndex();
    const QString mask = m_maskCombo->currentText().trimmed();
    const QString reason = m_reasonEdit->text().trimmed();

    switch (idx) {
        case 0: // Kick
            cmds << (reason.isEmpty()
                ? QStringLiteral("/kick %1").arg(m_user.nickname)
                : QStringLiteral("/kick %1 %2").arg(m_user.nickname, reason));
            break;
        case 1: // Ban
            cmds << QStringLiteral("/ban %1").arg(mask);
            break;
        case 2: // Kick + Ban
            cmds << QStringLiteral("/ban %1").arg(mask);
            cmds << (reason.isEmpty()
                ? QStringLiteral("/kick %1").arg(m_user.nickname)
                : QStringLiteral("/kick %1 %2").arg(m_user.nickname, reason));
            break;
        case 3: // Quiet
            cmds << QStringLiteral("/quiet %1").arg(mask);
            break;
        case 4: // Unquiet
            cmds << QStringLiteral("/unquiet %1").arg(mask);
            break;
        case 5: // Unban
            cmds << QStringLiteral("/unban %1").arg(mask);
            break;
    }
    return cmds;
}

} // namespace loungecat
