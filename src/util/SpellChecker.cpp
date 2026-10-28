#include "util/SpellChecker.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QMutexLocker>
#include <algorithm>
#include <QFutureWatcher>
#include <QThread>
#include <QtConcurrent>

namespace loungecat {

SpellChecker::SpellChecker(QObject* parent) : QObject(parent) {
    m_pool.setMaxThreadCount(1);
    m_ircSlangAllowlist = {
        QStringLiteral("lol"), QStringLiteral("brb"), QStringLiteral("afk"),
        QStringLiteral("lmao"), QStringLiteral("rofl"), QStringLiteral("imo"),
        QStringLiteral("imho"), QStringLiteral("tbh"), QStringLiteral("ngl"),
        QStringLiteral("irc"), QStringLiteral("nick"), QStringLiteral("chan"),
        QStringLiteral("loungecat"), QStringLiteral("whois"), QStringLiteral("nickserv"),
        QStringLiteral("chanserv"), QStringLiteral("sasl"), QStringLiteral("ssl"),
        QStringLiteral("tls"), QStringLiteral("btw"), QStringLiteral("idk"),
        QStringLiteral("ikr"), QStringLiteral("omg"), QStringLiteral("wtf"),
        QStringLiteral("np"), QStringLiteral("ty"), QStringLiteral("thx"),
        QStringLiteral("pls"), QStringLiteral("plz")
    };

    m_commonTypoOverrides = {
        {QStringLiteral("wut"), QStringLiteral("what")},
        {QStringLiteral("cn"), QStringLiteral("can")},
        {QStringLiteral("ar"), QStringLiteral("are")},
        {QStringLiteral("yu"), QStringLiteral("you")},
        {QStringLiteral("ur"), QStringLiteral("your")},
        {QStringLiteral("teh"), QStringLiteral("the")},
        {QStringLiteral("recieve"), QStringLiteral("receive")},
        {QStringLiteral("seperate"), QStringLiteral("separate")},
        {QStringLiteral("definately"), QStringLiteral("definitely")},
        {QStringLiteral("occured"), QStringLiteral("occurred")},
        {QStringLiteral("untill"), QStringLiteral("until")},
        {QStringLiteral("alot"), QStringLiteral("a lot")},
        {QStringLiteral("dont"), QStringLiteral("don't")},
        {QStringLiteral("cant"), QStringLiteral("can't")},
        {QStringLiteral("wont"), QStringLiteral("won't")},
        {QStringLiteral("im"), QStringLiteral("I'm")}
    };

    loadDictionary(m_language);
}

void SpellChecker::setLanguage(const QString& langCode) {
    Q_ASSERT(QThread::currentThread() == thread());
    QMutexLocker locker(&m_mutex);
    if (m_language == langCode && !m_dictionaryWords.isEmpty()) {
        return;
    }
    m_language = langCode;
    loadDictionary(langCode);
}

SpellChecker::~SpellChecker() { m_pool.clear(); m_pool.waitForDone(); }
DictionarySnapshot SpellChecker::snapshot() const {
    QMutexLocker locker(&m_mutex);
    return {m_enabled, m_dictionaryWords, m_ircSlangAllowlist, m_commonTypoOverrides};
}
void SpellChecker::loadDictionary(const QString& language) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_loading) return;
    m_loading = true;
    auto* watcher = new QFutureWatcher<QSet<QString>>(this);
    connect(watcher, &QFutureWatcher<QSet<QString>>::finished, this, [this, watcher, language]() {
        const auto words = watcher->result();
        watcher->deleteLater();
        QMutexLocker locker(&m_mutex);
        m_loading = false;
        if (m_language == language) { m_dictionaryWords = words; Q_EMIT dictionaryChanged(); }
        else loadDictionary(m_language);
    });
    watcher->setFuture(QtConcurrent::run(&m_pool, [language]() {
        QString code = language;
        if (code == QStringLiteral("en")) code = QStringLiteral("en_US");
        if (code == QStringLiteral("de")) code = QStringLiteral("de_DE");
        if (code == QStringLiteral("nl")) code = QStringLiteral("nl_NL");
        if (code == QStringLiteral("pl")) code = QStringLiteral("pl_PL");
        QSet<QString> words;
        const QStringList paths{QStringLiteral("/usr/share/hunspell/%1.dic").arg(code),
            QStringLiteral("/usr/share/myspell/dicts/%1.dic").arg(code), QStringLiteral("/usr/share/dict/words")};
        for (const auto& path : paths) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) continue;
            QTextStream stream(&file);
            while (!stream.atEnd() && words.size() < 150000) {
                const auto line = stream.readLine(256).section('/', 0, 0).trimmed();
                if (!line.isEmpty() && !line[0].isDigit()) words.insert(line.toLower());
            }
            if (words.size() > 100) break;
        }
        return words;
    }));
}

static bool wordIsCorrect(const DictionarySnapshot& snapshot, const QString& word) {
    if (!snapshot.enabled) return true;
    const QString lower = word.trimmed().toLower();
    if (lower.length() <= 1 || lower.length() > 64) return true;

    // Ignore numbers, URLs, channels, mentions
    for (QChar c : lower) {
        if (c.isDigit()) return true;
    }

    if (snapshot.typos.contains(lower)) {
        return false;
    }
    if (snapshot.slang.contains(lower)) {
        return true;
    }
    if (snapshot.words.isEmpty()) {
        return true;
    }
    return snapshot.words.contains(lower);
}

static QStringList suggestWords(const DictionarySnapshot& snapshot, const QString& word) {
    const QString lower = word.trimmed().toLower();
    QStringList suggestions;
    if (lower.size() > 64) return suggestions;

    if (snapshot.typos.contains(lower)) {
        suggestions.append(snapshot.typos.value(lower));
    }

    if (snapshot.words.isEmpty() || suggestions.size() >= 5) {
        return suggestions;
    }

    // 1-edit-distance lookup in loaded dictionary (deletion, transposition, substitution, insertion)
    const QString alphabet = QStringLiteral("abcdefghijklmnopqrstuvwxyz");

    auto checkCandidate = [&](const QString& cand) {
        if (snapshot.words.contains(cand) && !suggestions.contains(cand)) {
            suggestions.append(cand);
        }
    };

    // Transpositions
    for (int i = 0; i + 1 < lower.length() && suggestions.size() < 5; ++i) {
        QString cand = lower;
        std::swap(cand[i], cand[i + 1]);
        checkCandidate(cand);
    }
    // Deletions
    for (int i = 0; i < lower.length() && suggestions.size() < 5; ++i) {
        QString cand = lower.left(i) + lower.mid(i + 1);
        checkCandidate(cand);
    }
    // Substitutions
    for (int i = 0; i < lower.length() && suggestions.size() < 5; ++i) {
        QString cand = lower;
        for (QChar ch : alphabet) {
            cand[i] = ch;
            checkCandidate(cand);
            if (suggestions.size() >= 5) break;
        }
    }
    // Insertions
    for (int i = 0; i <= lower.length() && suggestions.size() < 5; ++i) {
        for (QChar ch : alphabet) {
            QString cand = lower.left(i) + ch + lower.mid(i);
            checkCandidate(cand);
            if (suggestions.size() >= 5) break;
        }
    }

    return suggestions;
}

QList<MisspelledWord> SpellChecker::checkSnapshot(const DictionarySnapshot& snapshot, const QString& text) {
    QList<MisspelledWord> results;
    if (!snapshot.enabled || text.isEmpty() || text.startsWith('/')) {
        return results;
    }

    const QString bounded = text.left(8192);
    // Correction works on prose, not fragments of URLs or IRC identifiers.
    // Checking only the character before a word misses URL schemes/queries
    // and the later words in names such as #general-teh or @alice-teh.
    static const QRegularExpression identifierRe(
        QStringLiteral(R"(\b[A-Za-z][A-Za-z0-9+.-]*://[^\s]*|\bwww\.[^\s]+|(?<![\p{L}\p{N}_])[@#&][^\s]+)"),
        QRegularExpression::CaseInsensitiveOption);
    QList<QPair<qsizetype, qsizetype>> protectedRanges;
    auto identifiers = identifierRe.globalMatch(bounded);
    while (identifiers.hasNext()) {
        const auto identifier = identifiers.next();
        protectedRanges.append({identifier.capturedStart(), identifier.capturedEnd()});
    }
    qsizetype protectedIndex = 0;
    static const QRegularExpression wordRe(QStringLiteral(R"(\b[A-Za-z']+\b)"));
    auto it = wordRe.globalMatch(bounded);
    while (it.hasNext() && results.size() < 64) {
        const auto match = it.next();
        const int start = match.capturedStart(0);
        while (protectedIndex < protectedRanges.size() && protectedRanges[protectedIndex].second <= start)
            ++protectedIndex;
        if (protectedIndex < protectedRanges.size() &&
            protectedRanges[protectedIndex].first < match.capturedEnd()) continue;
        // Skip words preceded by @, #, or part of a URL
        if (start > 0 && (text[start - 1] == '@' || text[start - 1] == '#' ||
                          text[start - 1] == '/' || text[start - 1] == ':' || text[start - 1] == '.')) {
            continue;
        }
        const QString w = match.captured(0);
        if (!wordIsCorrect(snapshot, w)) {
            MisspelledWord mw;
            mw.startOffset = start;
            mw.length = match.capturedLength(0);
            mw.word = w;
            mw.suggestions = suggestWords(snapshot, w);
            results.append(mw);
        }
    }

    return results;
}

bool SpellChecker::isCorrect(const QString& word) const { return wordIsCorrect(snapshot(), word); }
QStringList SpellChecker::suggest(const QString& word) const { return suggestWords(snapshot(), word); }
QList<MisspelledWord> SpellChecker::checkText(const QString& text) const { return checkSnapshot(snapshot(), text); }

QString SpellChecker::applyAutoCorrections(const QString& text) const {
    const QList<MisspelledWord> issues = checkText(text);
    if (issues.isEmpty()) {
        return text;
    }

    QString updated = text;
    // Apply from right to left to preserve offsets
    for (int i = issues.size() - 1; i >= 0; --i) {
        const auto& issue = issues[i];
        if (!issue.suggestions.isEmpty()) {
            updated.replace(issue.startOffset, issue.length, issue.suggestions.first());
        }
    }
    return updated;
}

} // namespace loungecat
