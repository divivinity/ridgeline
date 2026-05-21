#include "HeatmapLegend.h"
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFontMetrics>

// Mirror the shader's heatColor() ramp:
//   t=0.00  dead      (#CC1A1A)
//   t=0.33  marginal  (#E6B200)
//   t=0.66  good      (#1ACD33)
//   t=1.00  strong    (#0099FF)
// We render it top→bottom strong→dead so "better = up" matches intuition.

static constexpr float kMinDbm = -120.0f;
static constexpr float kMaxDbm =  -40.0f;

HeatmapLegend::HeatmapLegend(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(72, 210);
}

void HeatmapLegend::reposition(const QSize& parentSize) {
    const int margin = 12;
    move(parentSize.width() - width() - margin, margin);
}

void HeatmapLegend::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // --- background pill ---
    QRectF bg(0, 0, width(), height());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(15, 20, 30, 185));
    QPainterPath pill;
    pill.addRoundedRect(bg, 8, 8);
    p.fillPath(pill, QColor(15, 20, 30, 185));

    // --- title ---
    QFont titleFont = font();
    titleFont.setPixelSize(11);
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.setPen(QColor(210, 220, 230));
    p.drawText(QRect(0, 6, width(), 16), Qt::AlignHCenter, "Signal");

    // --- gradient bar (top = strong, bottom = dead) ---
    const QRect bar(10, 26, 20, 150);
    QLinearGradient grad(bar.topLeft(), bar.bottomLeft());
    // top = strong (cyan/blue), bottom = dead (red) — same as shader
    grad.setColorAt(0.00, QColor(  0, 153, 255));   // strong
    grad.setColorAt(0.34, QColor( 26, 205,  51));   // good
    grad.setColorAt(0.67, QColor(230, 178,   0));   // marginal
    grad.setColorAt(1.00, QColor(204,  26,  26));   // dead
    p.fillRect(bar, grad);

    // thin border around bar
    p.setPen(QColor(80, 100, 120));
    p.setBrush(Qt::NoBrush);
    p.drawRect(bar);

    // --- tick labels ---
    QFont labFont = font();
    labFont.setPixelSize(10);
    p.setFont(labFont);
    p.setPen(QColor(200, 215, 230));

    // Four ticks at t = 0.0, 0.33, 0.66, 1.0
    struct Tick { float t; QString label; };
    const Tick ticks[] = {
        { 0.00f, "-40" },
        { 0.33f, "-67" },
        { 0.66f, "-93" },
        { 1.00f, "-120" },
    };
    const int barTop    = bar.top();
    const int barBottom = bar.bottom();
    for (const auto& tk : ticks) {
        const int y = barTop + static_cast<int>(tk.t * (barBottom - barTop));
        // tick line
        p.setPen(QColor(120, 140, 160));
        p.drawLine(bar.right() + 1, y, bar.right() + 4, y);
        // label
        p.setPen(QColor(200, 215, 230));
        p.drawText(QRect(bar.right() + 5, y - 7, 30, 14),
                   Qt::AlignVCenter | Qt::AlignLeft, tk.label);
    }

    // --- unit label ---
    QFont unitFont = font();
    unitFont.setPixelSize(9);
    p.setFont(unitFont);
    p.setPen(QColor(140, 160, 180));
    p.drawText(QRect(0, bar.bottom() + 6, width(), 14), Qt::AlignHCenter, "dBm");
}
