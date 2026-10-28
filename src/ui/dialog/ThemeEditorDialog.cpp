#include "ui/dialog/ThemeEditorDialog.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QSet>
#include <QMessageBox>

namespace loungecat {

namespace {

bool isBuiltinThemeName(const QString& name) {
    static const QSet<QString> builtins = {
        QStringLiteral("dark"),
        QStringLiteral("light"),
        QStringLiteral("high voltage"),
        QStringLiteral("neon night"),
        QStringLiteral("retro warmth"),
        QStringLiteral("stormy sea"),
        QStringLiteral("obsidian veil")
    };
    return builtins.contains(name.trimmed().toLower());
}

QString buttonStyleForColor(const QColor& c) {
    const double lum = 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
    const QString textColor = lum > 150.0 ? QStringLiteral("#111111") : QStringLiteral("#ffffff");
    return QStringLiteral(
        "background-color: %1; color: %2; font-family: monospace; font-weight: 600; "
        "border: 1px solid rgba(128,128,128,0.4); border-radius: 5px; padding: 5px 10px;"
    ).arg(c.name(), textColor);
}

} // namespace

ThemeEditorDialog::ThemeEditorDialog(
    ThemeManager* themeManager,
    PreferencesManager* prefsManager,
    QWidget* parent
)
    : QDialog(parent),
      m_themeManager(themeManager),
      m_prefsManager(prefsManager),
      m_editingTheme(themeManager ? themeManager->currentTheme() : ThemeColors::darkTheme()) {
    setWindowTitle(QStringLiteral("Custom Theme Editor"));
    resize(460, 560);

    auto* mainLayout = new QVBoxLayout(this);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* container = new QWidget(scroll);
    auto* form = new QFormLayout(container);
    form->setSpacing(10);

    const QString initialName = isBuiltinThemeName(m_editingTheme.name)
        ? (m_editingTheme.name + QStringLiteral(" Custom"))
        : m_editingTheme.name;
    m_nameEdit = new QLineEdit(initialName, container);
    form->addRow(QStringLiteral("Theme Name:"), m_nameEdit);

    m_isDarkCheck = new QCheckBox(QStringLiteral("Dark Theme Palette"), container);
    m_isDarkCheck->setChecked(m_editingTheme.isDark);
    form->addRow(QString(), m_isDarkCheck);

    auto addColorButton = [&](const QString& label, QColor* targetColor) {
        auto* btn = new QPushButton(targetColor->name(), container);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(buttonStyleForColor(*targetColor));
        connect(btn, &QPushButton::clicked, this, [this, btn, targetColor, label]() {
            const QColor picked = QColorDialog::getColor(*targetColor, this, label, QColorDialog::ShowAlphaChannel);
            if (picked.isValid()) {
                *targetColor = picked;
                btn->setText(picked.name());
                btn->setStyleSheet(buttonStyleForColor(picked));
            }
        });
        form->addRow(label, btn);
    };

    addColorButton(QStringLiteral("Window Background"), &m_editingTheme.windowBackground);
    addColorButton(QStringLiteral("Chat Background"), &m_editingTheme.chatBackground);
    addColorButton(QStringLiteral("Sidebar Background"), &m_editingTheme.sidebarBackground);
    addColorButton(QStringLiteral("Input Background"), &m_editingTheme.inputBackground);
    addColorButton(QStringLiteral("Current Line / Surface"), &m_editingTheme.currentLineBackground);
    addColorButton(QStringLiteral("Selection Background"), &m_editingTheme.selectionBackground);
    addColorButton(QStringLiteral("Foreground Text"), &m_editingTheme.foreground);
    addColorButton(QStringLiteral("Secondary / Muted Text"), &m_editingTheme.comment);
    addColorButton(QStringLiteral("Accent Color"), &m_editingTheme.accentColor);
    addColorButton(QStringLiteral("Timestamp Color"), &m_editingTheme.timestampColor);
    addColorButton(QStringLiteral("System Message Color"), &m_editingTheme.systemMessageColor);
    addColorButton(QStringLiteral("Action Message Color"), &m_editingTheme.actionMessageColor);
    addColorButton(QStringLiteral("Unread Badge"), &m_editingTheme.unreadBadge);
    addColorButton(QStringLiteral("Border Color"), &m_editingTheme.border);

    scroll->setWidget(container);
    mainLayout->addWidget(scroll, 1);

    auto* btnBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(btnBox, &QDialogButtonBox::accepted, this, [this]() {
        QString newName = m_nameEdit->text().trimmed();
        if (newName.isEmpty()) {
            newName = QStringLiteral("Custom");
        }
        m_editingTheme.name = newName;
        m_editingTheme.isDark = m_isDarkCheck->isChecked();
        if (m_themeManager) {
            if (!m_themeManager->saveCustomTheme(m_editingTheme)) {
                QMessageBox::warning(this, QStringLiteral("Theme not saved"), QStringLiteral("Cannot save this theme. The existing file was preserved."));
                return;
            }
        }
        if (m_prefsManager) {
            if (!m_prefsManager->update([&](UserPreferences& p) {
                p.theme = m_editingTheme.name;
            })) return;
        }
        accept();
    });
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(btnBox);
}

} // namespace loungecat
