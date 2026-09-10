#include "PointAnalysisPanel.h"

#include <QFontMetrics>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QStringList>
#include <QList>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

QString formatMetric(float value, const QString& suffix = QStringLiteral(" dB")) {
    return std::isfinite(value)
        ? QString("%1%2").arg(value, 0, 'f', 1).arg(suffix)
        : QStringLiteral("—");
}

QString losName(TerrainModel::LOSStatus status) {
    switch (status) {
        case TerrainModel::LOSStatus::Clear: return QStringLiteral("Clear line of sight");
        case TerrainModel::LOSStatus::Diffracted: return QStringLiteral("Single-edge diffraction");
        case TerrainModel::LOSStatus::Blocked: return QStringLiteral("Terrain blocked");
    }
    return QStringLiteral("Unknown");
}

class ProfileWidget final : public QWidget {
public:
    explicit ProfileWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(190);
        setObjectName("LinkProfilePlot");
    }

    void setAnalysis(const PointAnalysisResult& analysis) {
        m_analysis = analysis;
        update();
    }

    void clear() {
        m_analysis = {};
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#0c1625"));
        const QRectF plot = QRectF(rect()).adjusted(46, 18, -12, -30);
        if (plot.width() <= 0.0 || plot.height() <= 0.0 ||
            m_analysis.profileDistanceM.size() < 2) {
            painter.setPen(QColor("#64748b"));
            painter.drawText(rect(), Qt::AlignCenter, "No serving path at this point");
            return;
        }

        float minimum = std::numeric_limits<float>::infinity();
        float maximum = -std::numeric_limits<float>::infinity();
        for (const float value : m_analysis.terrainElevationM) {
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
        for (const float value : m_analysis.lineOfSightElevationM) {
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
        for (const float value : m_analysis.fresnel60LowerElevationM)
            minimum = std::min(minimum, value);
        const float padding = std::max(5.0f, (maximum - minimum) * 0.12f);
        minimum -= padding;
        maximum += padding;
        const float distance = std::max(m_analysis.profileDistanceM.back(), 1.0f);
        const float elevationSpan = std::max(maximum - minimum, 1.0f);
        auto pointFor = [&](float d, float elevation) {
            return QPointF(plot.left() + d / distance * plot.width(),
                           plot.bottom() - (elevation - minimum) /
                               elevationSpan * plot.height());
        };

        painter.setPen(QPen(QColor(255, 255, 255, 22), 1));
        for (int i = 0; i <= 4; ++i) {
            const qreal y = plot.top() + i * plot.height() / 4.0;
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }

        QPainterPath terrainPath;
        terrainPath.moveTo(plot.left(), plot.bottom());
        for (std::size_t i = 0; i < m_analysis.profileDistanceM.size(); ++i)
            terrainPath.lineTo(pointFor(m_analysis.profileDistanceM[i],
                                        m_analysis.terrainElevationM[i]));
        terrainPath.lineTo(plot.right(), plot.bottom());
        terrainPath.closeSubpath();
        painter.fillPath(terrainPath, QColor(45, 75, 67, 225));
        painter.setPen(QPen(QColor("#86a98f"), 1.5));
        painter.drawPath(terrainPath);

        QPainterPath fresnelPath;
        QPainterPath losPath;
        for (std::size_t i = 0; i < m_analysis.profileDistanceM.size(); ++i) {
            const QPointF fresnelPoint = pointFor(
                m_analysis.profileDistanceM[i],
                m_analysis.fresnel60LowerElevationM[i]);
            const QPointF losPoint = pointFor(
                m_analysis.profileDistanceM[i],
                m_analysis.lineOfSightElevationM[i]);
            if (i == 0) {
                fresnelPath.moveTo(fresnelPoint);
                losPath.moveTo(losPoint);
            } else {
                fresnelPath.lineTo(fresnelPoint);
                losPath.lineTo(losPoint);
            }
        }
        painter.setPen(QPen(QColor("#f59e0b"), 1.2, Qt::DashLine));
        painter.drawPath(fresnelPath);
        painter.setPen(QPen(QColor("#38bdf8"), 1.8));
        painter.drawPath(losPath);

        painter.setPen(QColor("#a8b7ca"));
        QFont axisFont("Segoe UI");
        axisFont.setPixelSize(12);
        painter.setFont(axisFont);
        painter.drawText(QRectF(0, plot.top() - 7, 42, 16),
                         Qt::AlignRight, QString::number(maximum, 'f', 0));
        painter.drawText(QRectF(0, plot.bottom() - 8, 42, 16),
                         Qt::AlignRight, QString::number(minimum, 'f', 0));
        painter.drawText(QRectF(plot.left(), plot.bottom() + 7,
                                plot.width(), 18), Qt::AlignCenter,
                         distance >= 1000.0f
                             ? QString("%1 km path").arg(distance / 1000.0f, 0, 'f', 2)
                             : QString("%1 m path").arg(distance, 0, 'f', 0));
    }

private:
    PointAnalysisResult m_analysis;
};

QLabel* metricValue(const QString& objectName) {
    auto* label = new QLabel("—");
    label->setObjectName(objectName);
    label->setStyleSheet(
        "color: #e2e8f0; font-family: 'Consolas'; font-size: 15px; font-weight: 600;");
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

} // namespace

PointAnalysisPanel::PointAnalysisPanel(QWidget* parent) : QWidget(parent) {
    auto* shell = new QVBoxLayout(this);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("ResultInspectorScrollArea");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget();
    auto* outer = new QVBoxLayout(content);
    outer->setContentsMargins(14, 14, 14, 14);
    outer->setSpacing(10);

    auto* title = new QLabel("Result inspector");
    title->setStyleSheet(
        "color: #f1f5f9; font-size: 16px; font-weight: 600;");
    outer->addWidget(title);
    auto* help = new QLabel(
        "Click any location on a completed coverage map to explain the result.");
    help->setWordWrap(true);
    help->setStyleSheet("color: #a8b7ca; font-size: 12px;");
    outer->addWidget(help);

    m_resultBadge = new QLabel("No result selected");
    m_resultBadge->setObjectName("PointServiceResult");
    m_resultBadge->setAlignment(Qt::AlignCenter);
    outer->addWidget(m_resultBadge);

    m_position = new QLabel("—");
    m_position->setObjectName("PointPosition");
    m_position->setWordWrap(true);
    m_position->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_position->setStyleSheet("color: #b8c8da; font-size: 12px;");
    outer->addWidget(m_position);

    m_serving = new QLabel("—");
    m_serving->setObjectName("PointServingSource");
    m_serving->setWordWrap(true);
    m_serving->setStyleSheet("color: #dbeafe; font-size: 13px; font-weight: 600;");
    outer->addWidget(m_serving);

    auto* metrics = new QGridLayout();
    metrics->setHorizontalSpacing(12);
    metrics->setVerticalSpacing(6);
    m_signal = metricValue("PointSignalMetric");
    m_strongestSignal = metricValue("PointStrongestSignalMetric");
    m_jammer = metricValue("PointJammerMetric");
    m_snr = metricValue("PointSnrMetric");
    m_sinr = metricValue("PointSinrMetric");
    m_margin = metricValue("PointMarginMetric");
    const QStringList names{
        "Desired total", "Strongest path", "Jammer", "SNR", "SINR", "Fade margin"};
    const QList<QLabel*> values{
        m_signal, m_strongestSignal, m_jammer, m_snr, m_sinr, m_margin};
    for (int i = 0; i < names.size(); ++i) {
        auto* name = new QLabel(names[i]);
        name->setStyleSheet("color: #a8b7ca; font-size: 12px;");
        name->setWordWrap(true);
        metrics->addWidget(name, i / 2 * 2, i % 2);
        metrics->addWidget(values[i], i / 2 * 2 + 1, i % 2);
    }
    outer->addLayout(metrics);

    m_path = new QLabel("—");
    m_path->setObjectName("PointPathSummary");
    m_path->setWordWrap(true);
    m_path->setStyleSheet("color: #cbd5e1; font-size: 12px;");
    outer->addWidget(m_path);

    m_profile = new ProfileWidget(this);
    outer->addWidget(m_profile);

    auto* legend = new QLabel(
        "Cyan: direct path   ·   Amber: 60% Fresnel boundary   ·   Green: terrain + effective Earth curvature");
    legend->setWordWrap(true);
    legend->setStyleSheet("color: #a8b7ca; font-size: 12px;");
    outer->addWidget(legend);

    m_explanation = new QLabel("Run coverage, then click the map.");
    m_explanation->setObjectName("PointExplanation");
    m_explanation->setWordWrap(true);
    m_explanation->setStyleSheet(
        "background: #101c2c; border: 1px solid #24364d; border-radius: 8px;"
        " color: #cbd5e1; font-size: 13px; padding: 9px;");
    outer->addWidget(m_explanation);
    outer->addStretch();

    scroll->setWidget(content);
    shell->addWidget(scroll);

    setMinimumWidth(290);
    setMaximumWidth(430);
    setStyleSheet(
        "PointAnalysisPanel, QScrollArea, QScrollArea > QWidget > QWidget { background: #0b1422; }"
        "QScrollBar:vertical { background: #0b1422; width: 8px; }"
        "QScrollBar::handle:vertical { background: #334155; border-radius: 4px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");
    clear();
}

void PointAnalysisPanel::setAnalysis(const PointAnalysisResult& analysis,
                                     const QString& positionText,
                                     const QString& servingSourceName,
                                     const QString& strongestJammerName) {
    if (!analysis.valid) {
        clear();
        return;
    }
    m_resultBadge->setText(analysis.inService ? "IN SERVICE" : "OUT OF SERVICE");
    m_resultBadge->setStyleSheet(analysis.inService
        ? "background: #064e3b; border: 1px solid #10b981; border-radius: 8px; color: #a7f3d0; padding: 7px; font-weight: 750;"
        : "background: #4c1d1d; border: 1px solid #ef4444; border-radius: 8px; color: #fecaca; padding: 7px; font-weight: 750;");
    m_position->setText(positionText);
    m_serving->setText(servingSourceName.isEmpty()
        ? QStringLiteral("No serving emitter")
        : QString("Strongest contributor: %1").arg(servingSourceName));
    m_signal->setText(formatMetric(analysis.desiredDbm, " dBm"));
    m_strongestSignal->setText(formatMetric(analysis.strongestSourceDbm, " dBm"));
    m_jammer->setText(formatMetric(analysis.jammerDbm, " dBm"));
    m_snr->setText(formatMetric(analysis.snrDb));
    m_sinr->setText(formatMetric(analysis.sinrDb));
    m_margin->setText(formatMetric(analysis.fadeMarginDb));
    if (analysis.servingSourceIndex >= 0) {
        m_path->setText(QString("%1 · %2 · Fresnel clearance %3%4")
            .arg(analysis.distanceM >= 1000.0f
                    ? QString("%1 km").arg(analysis.distanceM / 1000.0f, 0, 'f', 2)
                    : QString("%1 m").arg(analysis.distanceM, 0, 'f', 0))
            .arg(losName(analysis.losStatus))
            .arg(QString("%1%").arg(
                analysis.fresnelClearanceRatio * 100.0f, 0, 'f', 0))
            .arg(strongestJammerName.isEmpty()
                    ? QString{} : QString("\nStrongest jammer: %1").arg(strongestJammerName)));
    } else {
        m_path->setText(strongestJammerName.isEmpty()
            ? QStringLiteral("No serving path is available at this location.")
            : QString("No serving path is available.\nStrongest jammer: %1")
                .arg(strongestJammerName));
    }
    auto* profile = static_cast<ProfileWidget*>(m_profile);
    profile->setAnalysis(analysis);
    QStringList explanations;
    for (const auto& item : analysis.explanations)
        explanations.append(QString::fromStdString(item));
    m_explanation->setText(explanations.isEmpty()
        ? QStringLiteral("No additional explanation is available.")
        : QStringLiteral("• ") + explanations.join(QStringLiteral("\n• ")));
}

void PointAnalysisPanel::clear() {
    if (!m_resultBadge) return;
    m_resultBadge->setText("No result selected");
    m_resultBadge->setStyleSheet(
        "background: #111c2e; border: 1px solid #2a3b52; border-radius: 8px; color: #64748b; padding: 7px;");
    m_position->setText("—");
    m_serving->setText("—");
    m_signal->setText("—");
    m_strongestSignal->setText("—");
    m_jammer->setText("—");
    m_snr->setText("—");
    m_sinr->setText("—");
    m_margin->setText("—");
    m_path->setText("—");
    static_cast<ProfileWidget*>(m_profile)->clear();
    m_explanation->setText("Run coverage, then click the map.");
}
