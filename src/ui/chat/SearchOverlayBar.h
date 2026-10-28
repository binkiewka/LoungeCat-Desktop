#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QLabel>
#include <QToolButton>
#include "ui/theme/Theme.h"

namespace loungecat {

class SearchOverlayBar : public QWidget {
    Q_OBJECT
public:
    explicit SearchOverlayBar(QWidget* parent = nullptr);
    ~SearchOverlayBar() override = default;

    QString query() const;
    void focusSearchInput();
    void setMatchStatus(int currentMatchOneBased, int totalMatches);
    void applyTheme(const ThemeColors& theme);

Q_SIGNALS:
    void queryChanged(const QString& query);
    void nextMatchRequested();
    void previousMatchRequested();
    void closed();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_statusLabel = nullptr;
    QToolButton* m_prevBtn = nullptr;
    QToolButton* m_nextBtn = nullptr;
    QToolButton* m_closeBtn = nullptr;
};

} // namespace loungecat
