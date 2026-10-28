#pragma once

#include <QFrame>
#include <QLabel>
#include <QToolButton>
#include "data/model/UrlPreview.h"
#include "ui/theme/Theme.h"

namespace loungecat {

class UrlPreviewCardWidget : public QFrame {
    Q_OBJECT
public:
    explicit UrlPreviewCardWidget(const UrlPreview& preview, const ThemeColors& theme, QWidget* parent = nullptr);
    ~UrlPreviewCardWidget() override = default;

private:
    UrlPreview m_preview;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_descLabel = nullptr;
    QLabel* m_siteLabel = nullptr;
};

} // namespace loungecat
