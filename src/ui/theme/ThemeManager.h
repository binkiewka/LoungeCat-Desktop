#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include "ui/theme/Theme.h"

namespace loungecat {

class ThemeManager : public QObject {
    Q_OBJECT
public:
    explicit ThemeManager(const QString& themesDirPath = {}, QObject* parent = nullptr);
    ~ThemeManager() override = default;

    void reloadThemes();
    QStringList availableThemeNames() const;
    const ThemeColors& currentTheme() const { return m_currentTheme; }
    ThemeColors themeByName(const QString& name) const;

    void setActiveTheme(const QString& name);
    bool saveCustomTheme(const ThemeColors& theme);
    bool deleteCustomTheme(const QString& name);

    QString themesDirPath() const { return m_themesDirPath; }

Q_SIGNALS:
    void themeChanged(const loungecat::ThemeColors& theme);

private:
    void writeTemplateExampleIfNeeded() const;

    QString m_themesDirPath;
    QMap<QString, ThemeColors> m_themes;
    ThemeColors m_currentTheme;
};

} // namespace loungecat
