#include "ui/theme/IconFactory.h"
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QIconEngine>
#include <QFile>

namespace loungecat {

namespace {

class SvgIconEngine : public QIconEngine {
public:
    explicit SvgIconEngine(const QByteArray& svgData)
        : m_svgData(svgData) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        Q_UNUSED(mode)
        Q_UNUSED(state)
        painter->setRenderHint(QPainter::Antialiasing, true);
        QSvgRenderer renderer(m_svgData);
        renderer.render(painter, rect);
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        Q_UNUSED(mode)
        Q_UNUSED(state)
        // QIconEngine receives the requested raster size. QIcon applies the
        // screen DPR itself; doubling here makes icons exceed their layout slot.
        if (size.isEmpty()) return {};
        QPixmap px(size);
        px.fill(Qt::transparent);
        QPainter p(&px);
        p.setRenderHint(QPainter::Antialiasing, true);
        QSvgRenderer renderer(m_svgData);
        renderer.render(&p, QRectF(QPointF(0, 0), QSizeF(size)));
        return px;
    }

    QIconEngine* clone() const override {
        return new SvgIconEngine(m_svgData);
    }

private:
    QByteArray m_svgData;
};

QString rawSvgTemplate(IconType type) {
    switch (type) {
        case IconType::Plus:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<circle cx=\"12\" cy=\"12\" r=\"9\"/>"
                "<line x1=\"12\" y1=\"8\" x2=\"12\" y2=\"16\"/>"
                "<line x1=\"8\" y1=\"12\" x2=\"16\" y2=\"12\"/>"
                "</svg>"
            );
        case IconType::Grid:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<rect x=\"3\" y=\"3\" width=\"7\" height=\"7\" rx=\"1.5\"/>"
                "<rect x=\"14\" y=\"3\" width=\"7\" height=\"7\" rx=\"1.5\"/>"
                "<rect x=\"3\" y=\"14\" width=\"7\" height=\"7\" rx=\"1.5\"/>"
                "<rect x=\"14\" y=\"14\" width=\"7\" height=\"7\" rx=\"1.5\"/>"
                "</svg>"
            );
        case IconType::Settings:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<circle cx=\"12\" cy=\"12\" r=\"3.2\"/>"
                "<path d=\"M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z\"/>"
                "</svg>"
            );
        case IconType::Backup:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<ellipse cx=\"12\" cy=\"5\" rx=\"8\" ry=\"3\"/>"
                "<path d=\"M20 12c0 1.66-3.58 3-8 3s-8-1.34-8-3\"/>"
                "<path d=\"M4 5v14c0 1.66 3.58 3 8 3s8-1.34 8-3V5\"/>"
                "</svg>"
            );
        case IconType::Help:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<circle cx=\"12\" cy=\"12\" r=\"9\"/>"
                "<path d=\"M9.1 9a3 3 0 0 1 5.8 1c0 2-3 2.5-3 4\"/>"
                "<circle cx=\"12\" cy=\"17\" r=\"0.8\" fill=\"currentColor\"/>"
                "</svg>"
            );
        case IconType::ChevronLeft:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<polyline points=\"15 18 9 12 15 6\"/>"
                "</svg>"
            );
        case IconType::ChevronRight:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<polyline points=\"9 18 15 12 9 6\"/>"
                "</svg>"
            );
        case IconType::ChevronUp:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<polyline points=\"18 15 12 9 6 15\"/>"
                "</svg>"
            );
        case IconType::ChevronDown:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<polyline points=\"6 9 12 15 18 9\"/>"
                "</svg>"
            );
        case IconType::Search:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<circle cx=\"10.5\" cy=\"10.5\" r=\"7\"/>"
                "<line x1=\"15.8\" y1=\"15.8\" x2=\"21\" y2=\"21\"/>"
                "</svg>"
            );
        case IconType::Download:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<path d=\"M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2z\"/>"
                "<polyline points=\"17 21 17 13 7 13 7 21\"/>"
                "<polyline points=\"7 3 7 8 15 8\"/>"
                "</svg>"
            );
        case IconType::Trash:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<polyline points=\"3 6 5 6 21 6\"/>"
                "<path d=\"M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2\"/>"
                "<line x1=\"10\" y1=\"11\" x2=\"10\" y2=\"17\"/>"
                "<line x1=\"14\" y1=\"11\" x2=\"14\" y2=\"17\"/>"
                "</svg>"
            );
        case IconType::Users:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<path d=\"M16 21v-2a4 4 0 0 0-4-4H5a4 4 0 0 0-4 4v2\"/>"
                "<circle cx=\"8.5\" cy=\"7\" r=\"4\"/>"
                "<path d=\"M23 21v-2a4 4 0 0 0-3-3.87\"/>"
                "<path d=\"M16 3.13a4 4 0 0 1 0 7.75\"/>"
                "</svg>"
            );
        case IconType::Image:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"3\"/>"
                "<circle cx=\"8.5\" cy=\"8.5\" r=\"1.8\" fill=\"currentColor\"/>"
                "<polyline points=\"21 15 16 10 5 21\"/>"
                "</svg>"
            );
        case IconType::Send:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<line x1=\"22\" y1=\"2\" x2=\"11\" y2=\"13\"/>"
                "<polygon points=\"22 2 15 22 11 13 2 9 22 2\"/>"
                "</svg>"
            );
        case IconType::Close:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.5\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<line x1=\"18\" y1=\"6\" x2=\"6\" y2=\"18\"/>"
                "<line x1=\"6\" y1=\"6\" x2=\"18\" y2=\"18\"/>"
                "</svg>"
            );
        case IconType::ChannelHash:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<line x1=\"4\" y1=\"9\" x2=\"20\" y2=\"9\"/>"
                "<line x1=\"4\" y1=\"15\" x2=\"20\" y2=\"15\"/>"
                "<line x1=\"10\" y1=\"3\" x2=\"8\" y2=\"21\"/>"
                "<line x1=\"16\" y1=\"3\" x2=\"14\" y2=\"21\"/>"
                "</svg>"
            );
        case IconType::ServerPulse:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<rect x=\"2\" y=\"3\" width=\"20\" height=\"7\" rx=\"2\"/>"
                "<rect x=\"2\" y=\"14\" width=\"20\" height=\"7\" rx=\"2\"/>"
                "<circle cx=\"6\" cy=\"6.5\" r=\"1.2\" fill=\"currentColor\"/>"
                "<circle cx=\"6\" cy=\"17.5\" r=\"1.2\" fill=\"currentColor\"/>"
                "<line x1=\"18\" y1=\"6.5\" x2=\"14\" y2=\"6.5\"/>"
                "<line x1=\"18\" y1=\"17.5\" x2=\"14\" y2=\"17.5\"/>"
                "</svg>"
            );
        case IconType::DirectMessage:
            return QStringLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
                "<path d=\"M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z\"/>"
                "</svg>"
            );
    }
    return QString();
}

} // namespace

QIcon IconFactory::createStatusDot(const QColor& color, bool hollow, int size) {
    const int effectiveSize = qMax(12, size);
    // Draw at 2x HiDPI
    QPixmap px(effectiveSize * 2, effectiveSize * 2);
    px.setDevicePixelRatio(2.0);
    px.fill(Qt::transparent);

    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal s = static_cast<qreal>(effectiveSize);
    const qreal pad = 2.0;
    const QRectF inner(pad, pad, s - 2 * pad, s - 2 * pad);

    if (hollow) {
        QPen pen(color, 2.0);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(inner);
    } else {
        // Outer subtle translucent glow ring
        QColor glow = color;
        glow.setAlpha(55);
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(QRectF(0.5, 0.5, s - 1.0, s - 1.0));

        // Inner solid vibrant dot
        p.setBrush(color);
        p.drawEllipse(inner);
    }
    return QIcon(px);
}

QIcon IconFactory::create(IconType type, const QColor& color, int size) {
    Q_UNUSED(size)
    QString svgStr = rawSvgTemplate(type);
    svgStr.replace(QStringLiteral("currentColor"), color.name());
    const QByteArray svgBytes = svgStr.toUtf8();
    return QIcon(new SvgIconEngine(svgBytes));
}

} // namespace loungecat
