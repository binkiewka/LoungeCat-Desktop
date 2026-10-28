#pragma once

#include <QString>
#include <QStringList>
#include <QSet>
#include <QMap>
#include <QMutex>
#include <QObject>
#include <QThreadPool>
#include <QRecursiveMutex>

namespace loungecat {

struct MisspelledWord {
    int startOffset = 0;
    int length = 0;
    QString word;
    QStringList suggestions;
};

struct DictionarySnapshot {
    bool enabled = true;
    QSet<QString> words;
    QSet<QString> slang;
    QMap<QString, QString> typos;
};

class SpellChecker : public QObject {
    Q_OBJECT
public:
    explicit SpellChecker(QObject* parent = nullptr);
    ~SpellChecker() override;

    void setLanguage(const QString& langCode);
    QString language() const { QMutexLocker locker(&m_mutex); return m_language; }

    void setEnabled(bool enabled) { QMutexLocker locker(&m_mutex); m_enabled = enabled; }
    bool isEnabled() const { QMutexLocker locker(&m_mutex); return m_enabled; }
    DictionarySnapshot snapshot() const;
    static QList<MisspelledWord> checkSnapshot(const DictionarySnapshot& snapshot, const QString& text);

    bool isCorrect(const QString& word) const;
    QStringList suggest(const QString& word) const;

    QList<MisspelledWord> checkText(const QString& text) const;
    QString applyAutoCorrections(const QString& text) const;

Q_SIGNALS:
    void dictionaryChanged();

private:
    void loadDictionary(const QString& langCode);

    bool m_enabled = true;
    QString m_language = QStringLiteral("en_US");
    QSet<QString> m_dictionaryWords;
    QSet<QString> m_ircSlangAllowlist;
    QMap<QString, QString> m_commonTypoOverrides;
    mutable QRecursiveMutex m_mutex;
    bool m_loading = false;
    QThreadPool m_pool;
};

} // namespace loungecat
