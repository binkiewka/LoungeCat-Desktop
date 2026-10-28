#include "ui/theme/ThemeManager.h"
#include "util/PlatformUtils.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace loungecat {

ThemeManager::ThemeManager(const QString& themesDirPath, QObject* parent)
    : QObject(parent),
      m_themesDirPath(themesDirPath.isEmpty()
          ? PlatformUtils::resolveAppDataDirectory().filePath(QStringLiteral("themes"))
          : themesDirPath),
      m_currentTheme(ThemeColors::darkTheme()) {
    QDir dir(m_themesDirPath);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    writeTemplateExampleIfNeeded();
    reloadThemes();
}

void ThemeManager::writeTemplateExampleIfNeeded() const {
    const QString examplePath = QDir(m_themesDirPath).filePath(QStringLiteral("_theme_template.json.example"));
    QFile f(examplePath);
    if (f.exists()) return;
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        ThemeColors example = ThemeColors::darkTheme();
        example.name = QStringLiteral("My Custom Theme");
        f.write(QJsonDocument(example.toJson()).toJson(QJsonDocument::Indented));
        f.close();
    }
}

void ThemeManager::reloadThemes() {
    m_themes.clear();

    // Register built-in themes
    for (const ThemeColors& builtin : {
             ThemeColors::darkTheme(),
             ThemeColors::lightTheme(),
             ThemeColors::highVoltageTheme(),
             ThemeColors::neonNightTheme(),
             ThemeColors::retroWarmthTheme(),
             ThemeColors::stormySeaTheme(),
             ThemeColors::obsidianVeilTheme()
         }) {
        m_themes.insert(builtin.name.toLower(), builtin);
    }

    // Load custom *.json themes (ignoring files starting with '_')
    QDir dir(m_themesDirPath);
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files);
    for (const QFileInfo& fi : files) {
        if (fi.fileName().startsWith('_')) continue;
        QFile file(fi.absoluteFilePath());
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            file.close();
            if (doc.isObject()) {
                ThemeColors custom = ThemeColors::fromJson(doc.object(), fi.completeBaseName());
                m_themes.insert(custom.name.toLower(), custom);
            }
        }
    }
}

QStringList ThemeManager::availableThemeNames() const {
    QStringList names;
    for (const auto& t : m_themes) {
        names.append(t.name);
    }
    return names;
}

ThemeColors ThemeManager::themeByName(const QString& name) const {
    const QString key = name.trimmed().toLower();
    if (m_themes.contains(key)) {
        return m_themes.value(key);
    }
    return ThemeColors::darkTheme();
}

void ThemeManager::setActiveTheme(const QString& name) {
    ThemeColors next = themeByName(name);
    m_currentTheme = next;
    Q_EMIT themeChanged(m_currentTheme);
}

bool ThemeManager::saveCustomTheme(const ThemeColors& theme) {
    QDir dir(m_themesDirPath);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    QString safeFile = theme.name.trimmed();
    safeFile.replace(' ', '_');
    if (safeFile.startsWith('_')) safeFile.remove(0, 1);
    if (safeFile.isEmpty()) safeFile = QStringLiteral("CustomTheme");

    const QString path = QDir(m_themesDirPath).filePath(safeFile + QStringLiteral(".json"));
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    const auto bytes = QJsonDocument(theme.toJson()).toJson(QJsonDocument::Indented);
    if (f.write(bytes) != bytes.size() || !f.commit()) {
        return false;
    }

    reloadThemes();
    setActiveTheme(theme.name);
    return true;
}

bool ThemeManager::deleteCustomTheme(const QString& name) {
    QString safeFile = name.trimmed();
    safeFile.replace(' ', '_');
    const QString path = QDir(m_themesDirPath).filePath(safeFile + QStringLiteral(".json"));
    if (QFile::remove(path)) {
        reloadThemes();
        return true;
    }
    return false;
}

} // namespace loungecat
