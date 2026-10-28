#pragma once

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QStringList>
#include "data/model/ChannelUser.h"

namespace loungecat {

class ModerationDialog : public QDialog {
    Q_OBJECT
public:
    ModerationDialog(const QString& channelName, const ChannelUser& user, QWidget* parent = nullptr);
    ~ModerationDialog() override = default;

    QStringList selectedCommands() const;

private:
    QString m_channelName;
    ChannelUser m_user;
    QComboBox* m_actionCombo = nullptr;
    QComboBox* m_maskCombo = nullptr;
    QLineEdit* m_reasonEdit = nullptr;
};

} // namespace loungecat
