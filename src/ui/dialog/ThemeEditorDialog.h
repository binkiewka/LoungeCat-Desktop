#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include "ui/theme/ThemeManager.h"
#include "data/preferences/PreferencesManager.h"

namespace loungecat {

class ThemeEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ThemeEditorDialog(
        ThemeManager* themeManager,
        PreferencesManager* prefsManager = nullptr,
        QWidget* parent = nullptr
    );
    ~ThemeEditorDialog() override = default;

    QString savedThemeName() const { return m_editingTheme.name; }

private:
    ThemeManager* m_themeManager;
    PreferencesManager* m_prefsManager;
    ThemeColors m_editingTheme;
    QLineEdit* m_nameEdit = nullptr;
    QCheckBox* m_isDarkCheck = nullptr;
};

} // namespace loungecat
