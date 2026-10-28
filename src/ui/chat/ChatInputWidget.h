#pragma once

#include <QWidget>
#include <QBoxLayout>
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QLabel>
#include <QToolButton>
#include <algorithm>
#include <QPointer>
#include <QTimer>
#include <QThreadPool>
#include <optional>
#include "data/model/IncomingMessage.h"
#include "util/SpellChecker.h"
#include "util/TabCompletion.h"
#include "ui/theme/Theme.h"

namespace loungecat {

struct InputDraft {
    QString text;
    std::optional<IncomingMessage> reply;
    int cursor = 0;
    int historyIndex = -1;
    QString historyDraft = {};
};

class SpellCheckHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit SpellCheckHighlighter(SpellChecker* checker, QTextDocument* parent = nullptr);
    ~SpellCheckHighlighter() override;
    void refresh();

protected:
    void highlightBlock(const QString& text) override;

private:
    QPointer<SpellChecker> m_checker;
    QTimer m_debounce;
    QList<MisspelledWord> m_results;
    bool m_pending = false;
    QThreadPool m_pool;
};

class ChatTextEdit : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit ChatTextEdit(SpellChecker* spellChecker, QWidget* parent = nullptr);
    ~ChatTextEdit() override { m_correctionPool.clear(); m_correctionPool.waitForDone(); }

    void setCompletionContext(const QStringList& nicknames, const QStringList& channels, const QStringList& commands);
    void setHistory(const QStringList& history);
    void insertControlChar(char16_t code);
    bool isComposing() const;
    void setDraftText(const QString& text) { m_lastAcceptedText = text; setPlainText(text); }
    int historyIndex() const { return m_historyIndex; }
    QString historyDraft() const { return m_draftInput; }
    void restoreHistoryDraft(const QString& text) { m_draftInput = text; }
    void restoreHistoryIndex(int index) { m_historyIndex = index < 0 ? m_history.size() : std::clamp(index, 0, static_cast<int>(m_history.size())); }

Q_SIGNALS:
    void messageSubmitted(const QString& text);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void updateEditorHeight();
    bool m_updatingHeight = false;
    QPointer<SpellChecker> m_spellChecker;
    SpellCheckHighlighter* m_highlighter;
    TabCompletion m_tabCompleter;

    QStringList m_nicknames;
    QStringList m_channels;
    QStringList m_commands;
    QStringList m_history;
    int m_historyIndex = -1;
    QString m_draftInput;
    QString m_lastAcceptedText;
    bool m_correctionPending = false;
    QThreadPool m_correctionPool;
};

class ChatInputWidget : public QWidget {
    Q_OBJECT
public:
    explicit ChatInputWidget(SpellChecker* spellChecker, QWidget* parent = nullptr);
    ~ChatInputWidget() override = default;

    void setNicknameBadge(const QString& nickname);
    void setCompletionContext(const QStringList& nicknames, const QStringList& channels, const QStringList& commands);
    void setHistory(const QStringList& history);
    void setReplyContext(const std::optional<IncomingMessage>& message);
    std::optional<IncomingMessage> replyContext() const { return m_replyContext; }
    InputDraft draft() const;
    void restoreDraft(const InputDraft& draft);
    void clearSubmittedDraft(const QString& text);

    void appendMention(const QString& nickname);
    void focusInput();
    void rehighlightSpellCheck();
    void applyTheme(const ThemeColors& theme);

Q_SIGNALS:
    void inputSubmitted(const QString& text, const std::optional<loungecat::IncomingMessage>& replyTo);
    void typingStateChanged(const QString& state);
    void uploadImageClicked();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateInputLayout();
    QBoxLayout* m_inputLayout = nullptr;
    QWidget* m_actions = nullptr;
    std::optional<IncomingMessage> m_replyContext;
    QString m_currentNickname = QStringLiteral("nick");
    ThemeColors m_theme = ThemeColors::darkTheme();
    QWidget* m_replyBanner = nullptr;
    QLabel* m_replyLabel = nullptr;
    QToolButton* m_cancelReplyBtn = nullptr;
    QLabel* m_nickBadge = nullptr;
    ChatTextEdit* m_textEdit = nullptr;
    QToolButton* m_uploadBtn = nullptr;
    QToolButton* m_sendBtn = nullptr;
    SpellCheckHighlighter* m_highlighter = nullptr;
    qint64 m_lastTypingSentMs = 0;
    bool m_isCurrentlyTyping = false;
};

} // namespace loungecat
