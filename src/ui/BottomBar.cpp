#include "BottomBar.h"
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFont>
#include <algorithm>  // std::clamp

BottomBar::BottomBar(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(90);
    setMaximumHeight(150);
}

void BottomBar::setAntenna(const Antenna& antenna) {
    try {
        m_spectrum = sweepSpectrum(
            antenna.powerW, antenna.gainDbi,
            1000.0f,         // 1 km reference distance
            30.0f, 3000.0f,  // 30 MHz – 3 GHz sweep
            120);            // 120 sample points
        m_hasAntenna = true;
    } catch (const std::exception&) {
        m_spectrum.clear();
        m_hasAntenna = false;
    }
    update();
}

void BottomBar::clearAntenna() {
    m_spectrum.clear();
    m_hasAntenna = false;
    update();
}

void BottomBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(10, 18, 28));

    const int marginL = 46, marginR = 10, marginT = 8, marginB = 22;
    const QRect plot(marginL, marginT,
                     width()  - marginL - marginR,
                     height() - marginT - marginB);

    if (!m_hasAntenna || m_spectrum.empty()) {
        p.setPen(QColor(255, 255, 255, 60));
        p.setFont(QFont("monospace", 10));
        p.drawText(rect(), Qt::AlignCenter, "Select an antenna to view spectrum");
        return;
    }

    // Guard against zero-width or zero-height plot
    if (plot.width() <= 0 || plot.height() <= 0) return;

    // Guard against degenerate spectrum (fewer than 2 points)
    if (m_spectrum.size() < 2) return;

    // Horizontal grid lines
    p.setPen(QPen(QColor(255, 255, 255, 18), 1));
    for (int i = 0; i <= 4; ++i) {
        int y = plot.top() + i * plot.height() / 4;
        p.drawLine(plot.left(), y, plot.right(), y);
    }

    // Y-axis dBm labels
    p.setPen(QColor(255, 255, 255, 80));
    p.setFont(QFont("monospace", 8));
    for (int i = 0; i <= 4; ++i) {
        float dbm = kMaxDbm - i * (kMaxDbm - kMinDbm) / 4.0f;
        int   y   = plot.top() + i * plot.height() / 4;
        p.drawText(0, y - 7, marginL - 4, 14,
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(static_cast<int>(dbm)));
    }

    // X-axis frequency labels
    const float fStart = m_spectrum.front().freqMHz;
    const float fEnd   = m_spectrum.back().freqMHz;
    for (int i = 0; i <= 4; ++i) {
        float fMHz = fStart + i * (fEnd - fStart) / 4.0f;
        int   x    = plot.left() + i * plot.width() / 4;
        QString label = (fMHz >= 1000.0f)
            ? QString::number(fMHz / 1000.0f, 'f', 1) + " GHz"
            : QString::number(static_cast<int>(fMHz))  + " MHz";
        p.drawText(x - 30, plot.bottom() + 4, 60, 16, Qt::AlignCenter, label);
    }

    // Coordinate mappers
    auto toX = [&](float fMHz) -> float {
        return plot.left() + (fMHz - fStart) / (fEnd - fStart) * plot.width();
    };
    auto toY = [&](float dbm) -> float {
        float t = (dbm - kMinDbm) / (kMaxDbm - kMinDbm); // 0=bottom, 1=top
        return static_cast<float>(plot.bottom()) - t * plot.height();
    };

    // Filled area under curve
    QPolygonF poly;
    poly << QPointF(toX(fStart), static_cast<float>(plot.bottom()));
    for (const auto& pt : m_spectrum) {
        float y = toY(std::clamp(pt.powerDbm, kMinDbm, kMaxDbm));
        poly << QPointF(toX(pt.freqMHz), y);
    }
    poly << QPointF(toX(fEnd), static_cast<float>(plot.bottom()));

    QLinearGradient grad(0, plot.top(), 0, plot.bottom());
    grad.setColorAt(0.0, QColor(79, 195, 247, 70));
    grad.setColorAt(1.0, QColor(79, 195, 247,  8));
    p.setBrush(grad);
    p.setPen(Qt::NoPen);
    p.drawPolygon(poly);

    // Spectrum line on top of fill
    QPainterPath path;
    bool first = true;
    for (const auto& pt : m_spectrum) {
        float x = toX(pt.freqMHz);
        float y = toY(std::clamp(pt.powerDbm, kMinDbm, kMaxDbm));
        if (first) { path.moveTo(x, y); first = false; }
        else          path.lineTo(x, y);
    }
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(79, 195, 247, 210), 1.5f));
    p.drawPath(path);

    // "dBm" rotated axis label
    p.save();
    p.setPen(QColor(255, 255, 255, 55));
    p.setFont(QFont("monospace", 8));
    p.translate(8, plot.center().y());
    p.rotate(-90);
    p.drawText(-20, -5, 40, 12, Qt::AlignCenter, "dBm");
    p.restore();
}
