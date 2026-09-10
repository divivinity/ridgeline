#include "ScenarioComparisonPanel.h"

#include "sim/ScenarioComparison.h"

#include <QCheckBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QVBoxLayout>

#include <cmath>
#include <cstddef>

namespace {

QLabel* valueLabel(const char* objectName, const QString& initial = QStringLiteral("\u2014")) {
    auto* label = new QLabel(initial);
    label->setObjectName(objectName);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    label->setStyleSheet(
        "color: #e2e8f0; font-family: Consolas, monospace; font-size: 14px; font-weight: 650;");
    return label;
}

QLabel* captionLabel(const QString& text) {
    auto* label = new QLabel(text);
    label->setWordWrap(true);
    label->setStyleSheet("color: #a9b9cd; font-size: 12px;");
    return label;
}

QString formatPercent(double value) {
    return std::isfinite(value)
        ? QStringLiteral("%1%").arg(value, 0, 'f', 1)
        : QStringLiteral("\u2014");
}

QString formatSignedPercentagePoints(double value) {
    if (!std::isfinite(value)) return QStringLiteral("\u2014");
    if (std::abs(value) < 0.05) value = 0.0;
    return QStringLiteral("%1%2 pp").arg(value > 0.0 ? QStringLiteral("+") : QString())
        .arg(value, 0, 'f', 1);
}

QString formatDelta(double value, std::size_t samples) {
    if (!std::isfinite(value) || samples == 0) return QStringLiteral("\u2014");
    if (std::abs(value) < 0.05) value = 0.0;
    return QStringLiteral("%1%2 dB").arg(value > 0.0 ? QStringLiteral("+") : QString())
        .arg(value, 0, 'f', 1);
}

QString formatCells(std::size_t count, std::size_t total) {
    const QString countText = QLocale().toString(static_cast<qulonglong>(count));
    if (total == 0) return countText;
    const double percent = 100.0 * static_cast<double>(count) / static_cast<double>(total);
    return QStringLiteral("%1  \u00b7  %2%").arg(countText).arg(percent, 0, 'f', 1);
}

QFrame* divider() {
    auto* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: #203149; background: #203149; max-height: 1px;");
    return line;
}

} // namespace

ScenarioComparisonPanel::ScenarioComparisonPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("ScenarioComparisonPanel");

    auto* shell = new QVBoxLayout(this);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("ScenarioComparisonScrollArea");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget();
    auto* outer = new QVBoxLayout(content);
    outer->setContentsMargins(14, 14, 14, 14);
    outer->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("Scenario comparison"));
    title->setStyleSheet(
        "color: #e8f1fa; font-size: 16px; font-weight: 700;");
    outer->addWidget(title);

    auto* help = new QLabel(QStringLiteral(
        "Save a completed coverage result, change the scenario, then run coverage again to see what improved or regressed."));
    help->setWordWrap(true);
    help->setStyleSheet("color: #a9b9cd; font-size: 13px;");
    outer->addWidget(help);

    m_statusBadge = new QLabel();
    m_statusBadge->setObjectName("ScenarioComparisonStatus");
    m_statusBadge->setAlignment(Qt::AlignCenter);
    m_statusBadge->setWordWrap(true);
    outer->addWidget(m_statusBadge);

    m_statusDetail = new QLabel();
    m_statusDetail->setObjectName("ScenarioComparisonDetail");
    m_statusDetail->setWordWrap(true);
    m_statusDetail->setStyleSheet("color: #a9b9cd; font-size: 13px;");
    outer->addWidget(m_statusDetail);

    auto* serviceTitle = new QLabel(QStringLiteral("SERVICE COVERAGE"));
    serviceTitle->setStyleSheet(
        "color: #a9b9cd; font-size: 12px; font-weight: 700;");
    outer->addWidget(serviceTitle);

    auto* serviceGrid = new QGridLayout();
    serviceGrid->setHorizontalSpacing(14);
    serviceGrid->setVerticalSpacing(5);
    m_baselineService = valueLabel("ScenarioBaselineService");
    m_currentService = valueLabel("ScenarioCurrentService");
    m_netChange = valueLabel("ScenarioNetChange");
    serviceGrid->addWidget(captionLabel(QStringLiteral("Baseline")), 0, 0);
    serviceGrid->addWidget(captionLabel(QStringLiteral("Current")), 0, 1);
    serviceGrid->addWidget(captionLabel(QStringLiteral("Net change")), 0, 2);
    serviceGrid->addWidget(m_baselineService, 1, 0);
    serviceGrid->addWidget(m_currentService, 1, 1);
    serviceGrid->addWidget(m_netChange, 1, 2);
    serviceGrid->setColumnStretch(0, 1);
    serviceGrid->setColumnStretch(1, 1);
    serviceGrid->setColumnStretch(2, 1);
    outer->addLayout(serviceGrid);

    outer->addWidget(divider());

    auto* transitionsTitle = new QLabel(QStringLiteral("CELL TRANSITIONS"));
    transitionsTitle->setStyleSheet(
        "color: #a9b9cd; font-size: 12px; font-weight: 700;");
    outer->addWidget(transitionsTitle);

    auto* transitionGrid = new QGridLayout();
    transitionGrid->setHorizontalSpacing(12);
    transitionGrid->setVerticalSpacing(6);
    m_gained = valueLabel("ScenarioGainedCells");
    m_gained->setStyleSheet(
        "color: #6ee7b7; font-family: Consolas, monospace; font-size: 14px; font-weight: 650;");
    m_lost = valueLabel("ScenarioLostCells");
    m_lost->setStyleSheet(
        "color: #fca5a5; font-family: Consolas, monospace; font-size: 14px; font-weight: 650;");
    m_retained = valueLabel("ScenarioRetainedCells");
    m_unchanged = valueLabel("ScenarioUnchangedCells");
    const QString transitionNames[] = {
        QStringLiteral("Gained service"), QStringLiteral("Lost service"),
        QStringLiteral("Retained service"), QStringLiteral("No service in either")};
    QLabel* transitionValues[] = {m_gained, m_lost, m_retained, m_unchanged};
    for (int i = 0; i < 4; ++i) {
        const int row = (i / 2) * 2;
        const int column = i % 2;
        transitionGrid->addWidget(captionLabel(transitionNames[i]), row, column);
        transitionGrid->addWidget(transitionValues[i], row + 1, column);
    }
    transitionGrid->setColumnStretch(0, 1);
    transitionGrid->setColumnStretch(1, 1);
    outer->addLayout(transitionGrid);

    outer->addWidget(divider());

    auto* metricsTitle = new QLabel(QStringLiteral("MEAN RF CHANGE"));
    metricsTitle->setStyleSheet(
        "color: #a9b9cd; font-size: 12px; font-weight: 700;");
    outer->addWidget(metricsTitle);

    auto* metricGrid = new QGridLayout();
    metricGrid->setHorizontalSpacing(14);
    metricGrid->setVerticalSpacing(5);
    m_desiredDelta = valueLabel("ScenarioDesiredDelta");
    m_sinrDelta = valueLabel("ScenarioSinrDelta");
    m_fadeDelta = valueLabel("ScenarioFadeMarginDelta");
    metricGrid->addWidget(captionLabel(QStringLiteral("Desired")), 0, 0);
    metricGrid->addWidget(captionLabel(QStringLiteral("SINR")), 0, 1);
    metricGrid->addWidget(captionLabel(QStringLiteral("Fade margin")), 0, 2);
    metricGrid->addWidget(m_desiredDelta, 1, 0);
    metricGrid->addWidget(m_sinrDelta, 1, 1);
    metricGrid->addWidget(m_fadeDelta, 1, 2);
    metricGrid->setColumnStretch(0, 1);
    metricGrid->setColumnStretch(1, 1);
    metricGrid->setColumnStretch(2, 1);
    outer->addLayout(metricGrid);

    auto* meanHelp = new QLabel(QStringLiteral(
        "Positive values mean the current result is stronger. Means use cells with valid values in both runs."));
    meanHelp->setWordWrap(true);
    meanHelp->setStyleSheet("color: #a9b9cd; font-size: 12px;");
    outer->addWidget(meanHelp);

    m_showChanges = new QCheckBox(QStringLiteral("Show changes on map"));
    m_showChanges->setObjectName("ScenarioShowChangesCheck");
    m_showChanges->setToolTip(QStringLiteral(
        "Highlight gained service in green and lost service in red."));
    outer->addWidget(m_showChanges);

    m_setBaseline = new QPushButton(QStringLiteral("Use current as baseline"));
    m_setBaseline->setObjectName("ScenarioSetBaselineButton");
    outer->addWidget(m_setBaseline);

    m_replaceBaseline = new QPushButton(QStringLiteral("Replace baseline"));
    m_replaceBaseline->setToolTip(QStringLiteral("Replace the saved baseline with the current coverage result."));
    m_replaceBaseline->setObjectName("ScenarioReplaceBaselineButton");
    outer->addWidget(m_replaceBaseline);

    m_clearComparison = new QPushButton(QStringLiteral("Clear baseline"));
    m_clearComparison->setObjectName("ScenarioClearComparisonButton");
    m_clearComparison->setToolTip(QStringLiteral(
        "Remove the saved baseline and its comparison."));
    outer->addWidget(m_clearComparison);
    outer->addStretch();

    connect(m_setBaseline, &QPushButton::clicked,
            this, &ScenarioComparisonPanel::setBaselineRequested);
    connect(m_replaceBaseline, &QPushButton::clicked,
            this, &ScenarioComparisonPanel::replaceBaselineRequested);
    connect(m_clearComparison, &QPushButton::clicked,
            this, &ScenarioComparisonPanel::clearComparisonRequested);
    connect(m_showChanges, &QCheckBox::toggled,
            this, &ScenarioComparisonPanel::showChangesOnMapChanged);

    scroll->setWidget(content);
    shell->addWidget(scroll);

    setMinimumWidth(300);
    setMaximumWidth(460);
    setStyleSheet(
        "ScenarioComparisonPanel, QScrollArea, QScrollArea > QWidget > QWidget { background: #0b1422; }"
        "QScrollBar:vertical { background: #0b1422; width: 8px; }"
        "QScrollBar::handle:vertical { background: #334155; border-radius: 4px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QCheckBox { color: #cbd5e1; font-size: 13px; spacing: 8px; }"
        "QCheckBox:disabled { color: #52637a; }"
        "QCheckBox::indicator { width: 15px; height: 15px; }"
        "QCheckBox::indicator:unchecked { background: #101c2c; border: 1px solid #3b4d65; border-radius: 3px; }"
        "QCheckBox::indicator:checked { background: #0f766e; border: 1px solid #5eead4; border-radius: 3px; }"
        "QPushButton { background: #132237; border: 1px solid #334a65; border-radius: 6px; color: #cbd5e1; padding: 9px; font-size: 13px; }"
        "QPushButton:hover { background: #1a2c45; border-color: #4f6987; }"
        "QPushButton:pressed { background: #0d1929; }"
        "QPushButton:disabled { background: #0e1928; border-color: #243449; color: #52637a; }"
        "QPushButton#ScenarioSetBaselineButton { background: #0f766e; border-color: #2dd4bf; color: #ecfeff; font-weight: 650; }"
        "QPushButton#ScenarioSetBaselineButton:disabled { background: #12332f; border-color: #24524d; color: #5f7e7a; }");

    showNoBaseline(false);
}

void ScenarioComparisonPanel::showNoBaseline(bool currentResultAvailable) {
    m_state = State::NoBaseline;
    m_currentResultAvailable = currentResultAvailable;
    resetValues();
    setStatus(QStringLiteral("NO BASELINE"),
              currentResultAvailable
                  ? QStringLiteral("The current coverage result is ready to save as your baseline.")
                  : QStringLiteral("Run coverage first, then save that result as the baseline."),
              QStringLiteral("#111c2e"), QStringLiteral("#2a3b52"),
              QStringLiteral("#94a3b8"));
    refreshControls();
}

void ScenarioComparisonPanel::showBaselineAwaitingCurrent(bool currentResultAvailable) {
    m_state = State::AwaitingCurrentResult;
    m_currentResultAvailable = currentResultAvailable;
    resetValues();
    setStatus(QStringLiteral("BASELINE SAVED"),
              currentResultAvailable
                  ? QStringLiteral("Change the scenario and run coverage to compare it with the saved baseline.")
                  : QStringLiteral("Run coverage after your changes to calculate the comparison."),
              QStringLiteral("#12312f"), QStringLiteral("#2dd4bf"),
              QStringLiteral("#99f6e4"));
    refreshControls();
}

void ScenarioComparisonPanel::setComparison(const ScenarioComparisonResult& comparison) {
    m_currentResultAvailable = true;
    if (!comparison.compatible) {
        m_state = State::Incompatible;
        resetValues();
        setStatus(QStringLiteral("CANNOT COMPARE"),
                  comparison.reason.empty()
                      ? QStringLiteral("The two results use different map grids. Run both with the same location and quality.")
                      : QString::fromStdString(comparison.reason),
                  QStringLiteral("#431d22"), QStringLiteral("#ef4444"),
                  QStringLiteral("#fecaca"));
        refreshControls();
        return;
    }

    m_state = State::ComparisonReady;
    setStatus(QStringLiteral("COMPARISON READY"),
              QStringLiteral("Green marks newly served cells; red marks cells that lost service."),
              QStringLiteral("#12312f"), QStringLiteral("#10b981"),
              QStringLiteral("#a7f3d0"));

    m_baselineService->setText(formatPercent(comparison.baselineServicePercent));
    m_currentService->setText(formatPercent(comparison.currentServicePercent));
    const double net = comparison.currentServicePercent - comparison.baselineServicePercent;
    m_netChange->setText(formatSignedPercentagePoints(net));
    m_netChange->setStyleSheet(QStringLiteral(
        "color: %1; font-family: Consolas, monospace; font-size: 14px; font-weight: 700;")
        .arg(net > 0.05 ? QStringLiteral("#6ee7b7")
                       : net < -0.05 ? QStringLiteral("#fca5a5")
                                     : QStringLiteral("#e2e8f0")));

    m_gained->setText(formatCells(comparison.gained, comparison.totalCells));
    m_lost->setText(formatCells(comparison.lost, comparison.totalCells));
    m_retained->setText(formatCells(comparison.retained, comparison.totalCells));
    m_unchanged->setText(formatCells(comparison.unchanged, comparison.totalCells));
    m_desiredDelta->setText(formatDelta(comparison.meanDesiredDeltaDb,
                                        comparison.desiredDeltaSamples));
    m_sinrDelta->setText(formatDelta(comparison.meanSinrDeltaDb,
                                     comparison.sinrDeltaSamples));
    m_fadeDelta->setText(formatDelta(comparison.meanFadeMarginDeltaDb,
                                     comparison.fadeMarginDeltaSamples));
    refreshControls();
}

void ScenarioComparisonPanel::setCurrentResultAvailable(bool available) {
    m_currentResultAvailable = available;
    refreshControls();
}

bool ScenarioComparisonPanel::changesVisible() const {
    return m_showChanges && m_showChanges->isChecked();
}

void ScenarioComparisonPanel::setChangesVisible(bool visible) {
    if (!m_showChanges) return;
    m_showChanges->setChecked(visible && m_state == State::ComparisonReady);
}

void ScenarioComparisonPanel::resetValues() {
    const QString empty = QStringLiteral("\u2014");
    m_baselineService->setText(empty);
    m_currentService->setText(empty);
    m_netChange->setText(empty);
    m_netChange->setStyleSheet(
        "color: #e2e8f0; font-family: Consolas, monospace; font-size: 14px; font-weight: 650;");
    m_gained->setText(empty);
    m_lost->setText(empty);
    m_retained->setText(empty);
    m_unchanged->setText(empty);
    m_desiredDelta->setText(empty);
    m_sinrDelta->setText(empty);
    m_fadeDelta->setText(empty);
}

void ScenarioComparisonPanel::refreshControls() {
    const bool hasBaseline = m_state != State::NoBaseline;
    const bool comparisonReady = m_state == State::ComparisonReady;

    m_setBaseline->setVisible(!hasBaseline);
    m_setBaseline->setEnabled(!hasBaseline && m_currentResultAvailable);
    m_replaceBaseline->setVisible(hasBaseline);
    m_replaceBaseline->setEnabled(hasBaseline && m_currentResultAvailable);
    m_clearComparison->setEnabled(hasBaseline);
    m_showChanges->setEnabled(comparisonReady);
    if (!comparisonReady && m_showChanges->isChecked())
        m_showChanges->setChecked(false);
}

void ScenarioComparisonPanel::setStatus(const QString& title, const QString& detail,
                                        const QString& background, const QString& border,
                                        const QString& foreground) {
    m_statusBadge->setText(title);
    m_statusBadge->setStyleSheet(QStringLiteral(
        "background: %1; border: 1px solid %2; border-radius: 8px; color: %3; padding: 7px; font-size: 12px; font-weight: 750;")
        .arg(background, border, foreground));
    m_statusDetail->setText(detail);
}
