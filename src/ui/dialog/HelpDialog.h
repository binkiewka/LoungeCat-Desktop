#pragma once

#include <QDialog>
#include <optional>
#include <QString>

namespace loungecat {

class HelpDialog : public QDialog {
    Q_OBJECT
public:
    explicit HelpDialog(const std::optional<QString>& initialFilter = std::nullopt, QWidget* parent = nullptr);
    ~HelpDialog() override = default;
};

} // namespace loungecat
