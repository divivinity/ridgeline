#include "MapFurniture.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <cmath>

namespace {
constexpr int kMaxBarPixels = 180;
const QColor kPanel(7, 14, 25, 205);
const QColor kEdge(148, 163, 184, 60);
const QColor kInk(219, 234, 254);
}

// ---------------------------------------------------------------------------
// MapScaleBar
// ---------------------------------------------------------------------------

MapScaleBar::MapScaleBar(QWidget* parent) : QWidget(parent) {
    setObjectName("MapScaleBar");
    setAccessibleName("Map scale");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(kMaxBarPixels + 26, 44);
    hide();   // nothing to say until a camera exists
}

void MapScaleBar::setMetresPerPixel(double metresPerPixel) {
    if (m_metresPerPixel == metresPerPixel) return;
    m_metresPerPixel = metresPerPixel;
    recompute();
}

void MapScaleBar::recompute() {
    m_bar = MapMeasure::chooseScaleBar(m_metresPerPixel, kMaxBarPixels);
    setVisible(m_bar.valid());
    setToolTip(m_bar.valid()
        ? QStringLiteral("Scale at the centre of the view: %1").arg(m_bar.label)
        : QString());
    update();
}

void MapScaleBar::paintEvent(QPaintEvent*) {
    if (!m_bar.valid()) return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.setPen(QPen(kEdge, 1));
    painter.setBrush(kPanel);
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);

    const int barWidth = static_cast<int>(std::lround(m_bar.pixels));
    const int left = 13;
    const int baseline = height() - 14;

    // A plain bar with end ticks reads unambiguously at a glance, which a
    // chequered bar does not at these sizes.
    painter.setPen(QPen(kInk, 2));
    painter.drawLine(left, baseline, left + barWidth, baseline);
    painter.drawLine(left, baseline - 5, left, baseline + 1);
    painter.drawLine(left + barWidth, baseline - 5, left + barWidth, baseline + 1);

    QFont font = painter.font();
    font.setPixelSize(12);
    painter.setFont(font);
    painter.setPen(kInk);
    painter.drawText(QRect(left, 6, width() - left - 6, baseline - 10),
                     Qt::AlignLeft | Qt::AlignVCenter, m_bar.label);
}

// ---------------------------------------------------------------------------
// NorthIndicator
// ---------------------------------------------------------------------------

NorthIndicator::NorthIndicator(QWidget* parent) : QWidget(parent) {
    setObjectName("NorthIndicator");
    setAccessibleName("North indicator");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(52, 52);
}

double NorthIndicator::northScreenAngleDeg() const {
    return std::fmod(std::fmod(180.0 - m_azimuthDeg, 360.0) + 360.0, 360.0);
}

void NorthIndicator::setCameraAzimuth(double azimuthDeg) {
    if (m_azimuthDeg == azimuthDeg) return;
    m_azimuthDeg = azimuthDeg;
    // The camera faces the opposite bearing to where it is parked.
    setToolTip(QStringLiteral("Looking %1")
                   .arg(MapMeasure::compassPoint(azimuthDeg + 180.0)));
    update();
}

void NorthIndicator::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.setPen(QPen(kEdge, 1));
    painter.setBrush(kPanel);
    painter.drawEllipse(rect().adjusted(1, 1, -1, -1));

    painter.translate(width() / 2.0, height() / 2.0);
    painter.rotate(northScreenAngleDeg());

    QPolygonF needle;
    needle << QPointF(0, -16) << QPointF(6, 6) << QPointF(0, 2) << QPointF(-6, 6);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(94, 234, 212));
    painter.drawPolygon(needle);

    QFont font = painter.font();
    font.setPixelSize(11);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(kInk);
    painter.drawText(QRectF(-8, -26, 16, 12), Qt::AlignCenter, "N");
}
