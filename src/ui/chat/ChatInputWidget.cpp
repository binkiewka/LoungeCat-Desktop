#include "ui/chat/ChatInputWidget.h"
#include <QTextLayout>
#include <QAbstractTextDocumentLayout>
#include <QScopedValueRollback>
#include <QResizeEvent>
#include "ui/theme/IconFactory.h"
#include "util/IrcFormatParser.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QTextCharFormat>
#include <QDateTime>
#include <algorithm>
#include <QFutureWatcher>
#include <QtConcurrent>

namespace loungecat {

SpellCheckHighlighter::SpellCheckHighlighter(SpellChecker* checker, QTextDocument* parent)
    : QSyntaxHighlighter(parent), m_checker(checker) {
    m_pool.setMaxThreadCount(1);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(parent, &QTextDocument::contentsChange, this, [this](int, int removed, int added) {
        if (removed || added) refresh();
    });
    if (checker) connect(checker, &SpellChecker::dictionaryChanged, this, &SpellCheckHighlighter::refresh);
    connect(&m_debounce, &QTimer::timeout, this, [this]() {
        if (!m_checker || !m_checker->isEnabled()) return;
        if (m_pending) { m_debounce.start(); return; }
        m_pending = true;
        const auto revision = document()->revision();
        const auto text = document()->toPlainText();
        const auto dictionary = m_checker->snapshot();
        auto* watcher = new QFutureWatcher<QList<MisspelledWord>>(this);
        connect(watcher, &QFutureWatcher<QList<MisspelledWord>>::finished, this, [this, watcher, revision]() {
            const auto results = watcher->result();
            watcher->deleteLater();
            m_pending = false;
            if (document()->revision() == revision && m_checker && m_checker->isEnabled()) {
                m_results = results;
                rehighlight();
            }
        });
        watcher->setFuture(QtConcurrent::run(&m_pool, [dictionary, text]() { return SpellChecker::checkSnapshot(dictionary, text); }));
    });
}
SpellCheckHighlighter::~SpellCheckHighlighter() { m_pool.clear(); m_pool.waitForDone(); }
void SpellCheckHighlighter::refresh() {
    m_results.clear();
    rehighlight();
    if (m_checker && m_checker->isEnabled()) m_debounce.start();
}

void SpellCheckHighlighter::highlightBlock(const QString& text) {
    if (!m_checker || !m_checker->isEnabled()) {
        return;
    }
    QTextCharFormat errFmt;
    errFmt.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
    errFmt.setUnderlineColor(QColor(0xeb, 0x6f, 0x92));

    const auto offset = currentBlock().position();
    for (const auto& issue : m_results) {
        if (issue.startOffset >= offset && issue.startOffset < offset + text.size())
            setFormat(issue.startOffset - offset, issue.length, errFmt);
    }
}

ChatTextEdit::ChatTextEdit(SpellChecker* spellChecker, QWidget* parent)
    : QPlainTextEdit(parent), m_spellChecker(spellChecker) {
    m_correctionPool.setMaxThreadCount(1);
    setFixedHeight(38);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    document()->setDocumentMargin(4);
    setPlaceholderText(QStringLiteral("Write a message or /command... (Tab to complete, Ctrl+M to fix spelling)"));
    m_highlighter = new SpellCheckHighlighter(m_spellChecker, document());

    connect(this, &QPlainTextEdit::textChanged, this, [this]() {
        const auto text = toPlainText();
        if (text.toUtf8().size() > 64 * 1024) {
            const QSignalBlocker blocker(this);
            setPlainText(m_lastAcceptedText);
            moveCursor(QTextCursor::End);
            setToolTip(QStringLiteral("Draft limit: 64 KiB. The last edit was not accepted."));
            return;
        }
        m_lastAcceptedText = text;
        updateEditorHeight();
    });
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
        this, [this] { updateEditorHeight(); });
    updateEditorHeight();
}

void ChatTextEdit::updateEditorHeight() {
    if (m_updatingHeight) return;
    const QScopedValueRollback<bool> guard(m_updatingHeight, true);
    // QTextDocument lineCount includes soft wraps; blockCount counts only hard
    // paragraph breaks. Keep the existing height budget, then allow scrolling.
    const qint64 lines = std::max(1, document()->lineCount());
    const int chrome = std::max(0, height() - viewport()->height())
        + qCeil(2 * document()->documentMargin());
    const int target = int(std::clamp<qint64>(chrome + lines * fontMetrics().lineSpacing(), 38, 120));
    if (height() != target) setFixedHeight(target);
}

void ChatTextEdit::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    updateEditorHeight();
}

void ChatTextEdit::changeEvent(QEvent* event) {
    QPlainTextEdit::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        updateEditorHeight();
}

void ChatTextEdit::setCompletionContext(
    const QStringList& nicknames,
    const QStringList& channels,
    const QStringList& commands
) {
    m_nicknames = nicknames;
    m_channels = channels;
    m_commands = commands;
}

void ChatTextEdit::setHistory(const QStringList& history) {
    if (m_history == history) return;
    m_history = history;
    m_historyIndex = m_history.size();
}

bool ChatTextEdit::isComposing() const {
    const auto* textLayout = textCursor().block().layout();
    return textLayout && !textLayout->preeditAreaText().isEmpty();
}

void ChatTextEdit::insertControlChar(char16_t code) {
    if (isComposing()) return;
    textCursor().insertText(QString(QChar(code)));
    setFocus();
}

void ChatTextEdit::keyPressEvent(QKeyEvent* event) {
    // Candidate-selection keys belong to the IME until its preedit is committed
    // or cancelled. Never submit the already-committed prefix or replace it
    // with completion/history while an input method still owns composition.
    if (isComposing()) {
        const int key = event->key();
        const bool appKey = key == Qt::Key_Return || key == Qt::Key_Enter
            || key == Qt::Key_Tab || key == Qt::Key_Backtab
            || key == Qt::Key_Up || key == Qt::Key_Down
            || ((event->modifiers() & Qt::ControlModifier)
                && (key == Qt::Key_B || key == Qt::Key_I || key == Qt::Key_U || key == Qt::Key_M));
        if (appKey) event->accept();
        else QPlainTextEdit::keyPressEvent(event);
        return;
    }

    // Enter (without Shift) submits message
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        !(event->modifiers() & Qt::ShiftModifier)) {
        const QString text = toPlainText();
        if (!text.trimmed().isEmpty()) {
            m_tabCompleter.reset();
            Q_EMIT messageSubmitted(text);
        }
        event->accept();
        return;
    }

    // Tab / Shift+Tab completion
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        const bool reverse = (event->key() == Qt::Key_Backtab) || (event->modifiers() & Qt::ShiftModifier);
        const QString curText = toPlainText();
        const int curPos = textCursor().position();
        const TabCompletionResult res = m_tabCompleter.complete(
            curText, curPos, m_nicknames, m_channels, m_commands, reverse
        );
        if (res.text != curText) {
            QTextCursor tc = textCursor();
            tc.beginEditBlock();
            tc.select(QTextCursor::Document);
            tc.insertText(res.text);
            tc.endEditBlock();
            tc.setPosition(res.cursorPosition);
            setTextCursor(tc);
        }
        event->accept();
        return;
    }

    // Formatting & Spellcheck shortcuts
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->key() == Qt::Key_B) {
            insertControlChar(IrcFormatParser::BOLD);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_I) {
            insertControlChar(IrcFormatParser::ITALIC);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_U) {
            insertControlChar(IrcFormatParser::UNDERLINE);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_M && m_spellChecker) {
            if (m_correctionPending) { event->accept(); return; }
            m_correctionPending = true;
            const QString current = toPlainText();
            const auto revision = document()->revision();
            const auto dictionary = m_spellChecker->snapshot();
            auto* watcher = new QFutureWatcher<QString>(this);
            connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, current, revision]() {
                const auto corrected = watcher->result();
                watcher->deleteLater();
                m_correctionPending = false;
                if (document()->revision() == revision && toPlainText() == current && corrected != current) {
                    setPlainText(corrected);
                    auto cursor = textCursor();
                    cursor.movePosition(QTextCursor::End);
                    setTextCursor(cursor);
                }
            });
            watcher->setFuture(QtConcurrent::run(&m_correctionPool, [dictionary, current]() {
                auto corrected = current;
                const auto issues = SpellChecker::checkSnapshot(dictionary, current);
                for (int index = issues.size() - 1; index >= 0; --index)
                    if (!issues[index].suggestions.isEmpty()) corrected.replace(issues[index].startOffset, issues[index].length, issues[index].suggestions.first());
                return corrected;
            }));
            event->accept();
            return;
        }
    }

    // Up / Down input history navigation
    if (event->key() == Qt::Key_Up && !m_history.isEmpty()) {
        QTextCursor tc = textCursor();
        if (!tc.movePosition(QTextCursor::Up)) {
            if (m_historyIndex == m_history.size()) {
                m_draftInput = toPlainText();
            }
            if (m_historyIndex > 0) {
                --m_historyIndex;
                setPlainText(m_history[m_historyIndex]);
                QTextCursor endCur = textCursor();
                endCur.movePosition(QTextCursor::End);
                setTextCursor(endCur);
            }
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_Down && !m_history.isEmpty()) {
        QTextCursor tc = textCursor();
        if (!tc.movePosition(QTextCursor::Down)) {
            if (m_historyIndex < m_history.size()) {
                ++m_historyIndex;
                if (m_historyIndex == m_history.size()) {
                    setPlainText(m_draftInput);
                } else {
                    setPlainText(m_history[m_historyIndex]);
                }
                QTextCursor endCur = textCursor();
                endCur.movePosition(QTextCursor::End);
                setTextCursor(endCur);
            }
            event->accept();
            return;
        }
    }

    m_tabCompleter.reset();
    QPlainTextEdit::keyPressEvent(event);
}

void ChatTextEdit::contextMenuEvent(QContextMenuEvent* event) {
    QMenu* menu = createStandardContextMenu();
    if (m_spellChecker && m_spellChecker->isEnabled()) {
        QTextCursor tc = cursorForPosition(event->pos());
        tc.select(QTextCursor::WordUnderCursor);
        const QString word = tc.selectedText();
        if (!word.isEmpty() && !m_spellChecker->isCorrect(word)) {
            const QStringList suggestions = m_spellChecker->suggest(word);
            if (!suggestions.isEmpty()) {
                QAction* firstStd = menu->actions().isEmpty() ? nullptr : menu->actions().first();
                for (const QString& sug : suggestions) {
                    auto* act = new QAction(QStringLiteral("Replace with \"%1\"").arg(sug), menu);
                    connect(act, &QAction::triggered, this, [this, tc, sug]() mutable {
                        tc.insertText(sug);
                    });
                    menu->insertAction(firstStd, act);
                }
                menu->insertSeparator(firstStd);
            }
        }
    }
    menu->exec(event->globalPos());
    delete menu;
}

ChatInputWidget::ChatInputWidget(SpellChecker* spellChecker, QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("chatInputContainer"));
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 6, 10, 8);
    mainLayout->setSpacing(4);

    // Reply Banner (hidden by default)
    m_replyBanner = new QWidget(this);
    auto* replyLayout = new QHBoxLayout(m_replyBanner);
    replyLayout->setContentsMargins(8, 3, 6, 3);
    m_replyLabel = new QLabel(m_replyBanner);
    m_cancelReplyBtn = new QToolButton(m_replyBanner);
    m_cancelReplyBtn->setFixedSize(28, 28);
    m_cancelReplyBtn->setIconSize(QSize(16, 16));
    m_cancelReplyBtn->setToolTip(QStringLiteral("Cancel Reply"));
    m_cancelReplyBtn->setAccessibleName(QStringLiteral("Cancel Reply"));
    connect(m_cancelReplyBtn, &QToolButton::clicked, this, [this]() {
        setReplyContext(std::nullopt);
    });
    replyLayout->addWidget(m_replyLabel, 1);
    replyLayout->addWidget(m_cancelReplyBtn);
    m_replyBanner->setVisible(false);
    mainLayout->addWidget(m_replyBanner);

    // Keep typing usable when the central pane is narrow. The same widgets
    // move between one and two rows, preserving draft, cursor and focus.
    m_inputLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    m_inputLayout->setSpacing(6);
    auto* editor = new QWidget(this);
    auto* inputRow = new QHBoxLayout(editor);
    inputRow->setContentsMargins(0, 0, 0, 0);
    inputRow->setSpacing(6);

    m_nickBadge = new QLabel(QStringLiteral("<b>nick</b>"), this);
    m_nickBadge->setAlignment(Qt::AlignCenter);
    m_nickBadge->setMinimumHeight(38);
    inputRow->addWidget(m_nickBadge);

    m_textEdit = new ChatTextEdit(spellChecker, this);
    connect(m_textEdit, &QPlainTextEdit::textChanged, this, [this]() {
        const QString text = m_textEdit->toPlainText();
        if (text.trimmed().isEmpty() || (text.startsWith('/') && !text.startsWith(QStringLiteral("//")))) {
            if (m_isCurrentlyTyping) {
                m_isCurrentlyTyping = false;
                m_lastTypingSentMs = 0;
                Q_EMIT typingStateChanged(QStringLiteral("done"));
            }
            return;
        }
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (!m_isCurrentlyTyping || (now - m_lastTypingSentMs >= 3000)) {
            m_isCurrentlyTyping = true;
            m_lastTypingSentMs = now;
            Q_EMIT typingStateChanged(QStringLiteral("active"));
        }
    });
    connect(m_textEdit, &ChatTextEdit::messageSubmitted, this, [this](const QString& text) {
        m_isCurrentlyTyping = false;
        m_lastTypingSentMs = 0;
        const auto reply = m_replyContext;
        Q_EMIT inputSubmitted(text, reply);
    });
    inputRow->addWidget(m_textEdit, 1);

    m_actions = new QWidget(this);
    auto* actions = new QHBoxLayout(m_actions);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(6);
    actions->addStretch();
    auto* boldBtn = new QToolButton(this);
    boldBtn->setText(QStringLiteral("B"));
    boldBtn->setFixedSize(34, 38);
    boldBtn->setStyleSheet(QStringLiteral("font-weight: bold;"));
    boldBtn->setToolTip(QStringLiteral("Bold (Ctrl+B)"));
    boldBtn->setAccessibleName(QStringLiteral("Bold"));
    connect(boldBtn, &QToolButton::clicked, this, [this]() {
        m_textEdit->insertControlChar(IrcFormatParser::BOLD);
    });
    actions->addWidget(boldBtn);

    auto* italicBtn = new QToolButton(this);
    italicBtn->setText(QStringLiteral("I"));
    italicBtn->setFixedSize(34, 38);
    italicBtn->setStyleSheet(QStringLiteral("font-style: italic; font-weight: bold;"));
    italicBtn->setToolTip(QStringLiteral("Italic (Ctrl+I)"));
    italicBtn->setAccessibleName(QStringLiteral("Italic"));
    connect(italicBtn, &QToolButton::clicked, this, [this]() {
        m_textEdit->insertControlChar(IrcFormatParser::ITALIC);
    });
    actions->addWidget(italicBtn);

    auto* underBtn = new QToolButton(this);
    underBtn->setText(QStringLiteral("U"));
    underBtn->setFixedSize(34, 38);
    underBtn->setStyleSheet(QStringLiteral("text-decoration: underline; font-weight: bold;"));
    underBtn->setToolTip(QStringLiteral("Underline (Ctrl+U)"));
    underBtn->setAccessibleName(QStringLiteral("Underline"));
    connect(underBtn, &QToolButton::clicked, this, [this]() {
        m_textEdit->insertControlChar(IrcFormatParser::UNDERLINE);
    });
    actions->addWidget(underBtn);

    m_uploadBtn = new QToolButton(this);
    m_uploadBtn->setFixedSize(38, 38);
    m_uploadBtn->setIconSize(QSize(22, 22));
    m_uploadBtn->setToolTip(QStringLiteral("Upload Image via ImgBB (/uploadimage)"));
    m_uploadBtn->setAccessibleName(QStringLiteral("Upload image"));
    connect(m_uploadBtn, &QToolButton::clicked, this, &ChatInputWidget::uploadImageClicked);
    actions->addWidget(m_uploadBtn);

    m_sendBtn = new QToolButton(this);
    m_sendBtn->setText(QStringLiteral(" Send"));
    m_sendBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_sendBtn->setIconSize(QSize(20, 20));
    m_sendBtn->setMinimumHeight(38);
    m_sendBtn->setObjectName(QStringLiteral("primaryButton"));
    connect(m_sendBtn, &QToolButton::clicked, this, [this]() {
        if (m_textEdit->isComposing()) return;
        const QString text = m_textEdit->toPlainText();
        if (!text.trimmed().isEmpty()) {
            const auto reply = m_replyContext;
            Q_EMIT inputSubmitted(text, reply);
        }
    });
    actions->addWidget(m_sendBtn);

    m_inputLayout->addWidget(editor, 1);
    m_inputLayout->addWidget(m_actions);
    mainLayout->addLayout(m_inputLayout);
    applyTheme(m_theme);
}

void ChatInputWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateInputLayout();
}

void ChatInputWidget::updateInputLayout() {
    if (!m_actions || !m_textEdit) return;
    const int editorWidth = qMax(180, m_textEdit->fontMetrics().horizontalAdvance(QString(20, QLatin1Char('M'))));
    const int needed = layout()->contentsMargins().left() + layout()->contentsMargins().right()
        + m_nickBadge->sizeHint().width() + editorWidth + m_actions->sizeHint().width() + 12;
    m_inputLayout->setDirection(width() < needed ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
}

void ChatInputWidget::applyTheme(const ThemeColors& theme) {
    m_theme = theme;
    m_cancelReplyBtn->setIcon(IconFactory::create(IconType::Close, theme.foreground, 16));
    m_uploadBtn->setIcon(IconFactory::create(IconType::Image, theme.foreground, 22));
    m_sendBtn->setIcon(IconFactory::create(IconType::Send, theme.windowBackground, 20));
    setNicknameBadge(m_currentNickname);
    updateInputLayout();
}

void ChatInputWidget::setNicknameBadge(const QString& nickname) {
    m_currentNickname = nickname;
    m_nickBadge->setText(QStringLiteral("<b>%1</b>").arg(nickname.toHtmlEscaped()));
    m_nickBadge->setStyleSheet(
        QStringLiteral("background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 0px 10px;")
            .arg(m_theme.currentLineBackground.name(), m_theme.accentColor.name(), m_theme.border.name())
    );
}

void ChatInputWidget::setCompletionContext(
    const QStringList& nicknames,
    const QStringList& channels,
    const QStringList& commands
) {
    m_textEdit->setCompletionContext(nicknames, channels, commands);
}

void ChatInputWidget::setHistory(const QStringList& history) {
    m_textEdit->setHistory(history);
}

void ChatInputWidget::setReplyContext(const std::optional<IncomingMessage>& message) {
    m_replyContext = message;
    if (message.has_value()) {
        m_replyLabel->setText(
            QStringLiteral("Replying to <b>%1</b>: %2")
                .arg(message->sender.toHtmlEscaped(), message->content.left(80).toHtmlEscaped())
        );
        m_replyBanner->setVisible(true);
        m_textEdit->setFocus();
    } else {
        m_replyBanner->setVisible(false);
    }
}

void ChatInputWidget::appendMention(const QString& nickname) {
    const QString cur = m_textEdit->toPlainText();
    if (cur.isEmpty()) {
        m_textEdit->setPlainText(nickname + QStringLiteral(": "));
    } else {
        m_textEdit->setPlainText(cur + QStringLiteral(" @") + nickname + QStringLiteral(" "));
    }
    QTextCursor tc = m_textEdit->textCursor();
    tc.movePosition(QTextCursor::End);
    m_textEdit->setTextCursor(tc);
    m_textEdit->setFocus();
}

void ChatInputWidget::focusInput() {
    m_textEdit->setFocus();
}

InputDraft ChatInputWidget::draft() const {
    return {m_textEdit->toPlainText(), m_replyContext, m_textEdit->textCursor().position(), m_textEdit->historyIndex(), m_textEdit->historyDraft()};
}
void ChatInputWidget::restoreDraft(const InputDraft& draft) {
    const QSignalBlocker blocker(m_textEdit);
    m_textEdit->setDraftText(draft.text);
    auto cursor = m_textEdit->textCursor();
    cursor.setPosition(std::clamp(draft.cursor, 0, static_cast<int>(draft.text.size())));
    m_textEdit->setTextCursor(cursor);
    m_textEdit->restoreHistoryIndex(draft.historyIndex);
    m_textEdit->restoreHistoryDraft(draft.historyDraft);
    setReplyContext(draft.reply);
    m_isCurrentlyTyping = false;
    m_lastTypingSentMs = 0;
}
void ChatInputWidget::clearSubmittedDraft(const QString& text) {
    if (m_textEdit->toPlainText() != text) return;
    m_textEdit->clear();
    setReplyContext(std::nullopt);
}

void ChatInputWidget::rehighlightSpellCheck() {
    if (auto* doc = m_textEdit->document()) {
        for (auto* child : doc->children()) {
            if (auto* hl = qobject_cast<SpellCheckHighlighter*>(child)) {
                hl->refresh();
            }
        }
    }
}

} // namespace loungecat
