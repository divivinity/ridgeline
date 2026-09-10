#include "CalibrationPanel.h"

#include "sim/MeasurementCalibration.h"

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QVBoxLayout>

#include <cmath>

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

QLabel* sectionLabel(const QString& text) {
    auto* label = new QLabel(text);
    label->setStyleSheet(
        "color: #a9b9cd; font-size: 12px; font-weight: 700;");
    return label;
}

QFrame* divider() {
    auto* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: #203149; background: #203149; max-height: 1px;");
    return line;
}

QString formatCount(std::size_t value) {
    return QLocale().toString(static_cast<qulonglong>(value));
}

QString formatDb(double value, bool signedValue = false) {
    if (!std::isfinite(value)) return QStringLiteral("\u2014");
    if (std::abs(value) < 0.005) value = 0.0;
    const QString prefix = signedValue && value > 0.0 ? QStringLiteral("+") : QString();
    return QStringLiteral("%1%2 dB").arg(prefix).arg(value, 0, 'f', 2);
}

} // namespace

CalibrationPanel::CalibrationPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("CalibrationPanel");

    auto* shell = new QVBoxLayout(this);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("CalibrationScrollArea");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget();
    auto* outer = new QVBoxLayout(content);
    outer->setContentsMargins(14, 14, 14, 14);
    outer->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("Measurement calibration"));
    title->setWordWrap(true);
    title->setStyleSheet(
        "color: #e8f1fa; font-size: 16px; font-weight: 700;");
    outer->addWidget(title);

    auto* help = new QLabel(QStringLiteral(
        "Compare field measurements with the current coverage result, then apply a recommended model correction."));
    help->setWordWrap(true);
    help->setStyleSheet("color: #a9b9cd; font-size: 13px;");
    outer->addWidget(help);

    m_statusBadge = new QLabel();
    m_statusBadge->setObjectName("CalibrationStatus");
    m_statusBadge->setAlignment(Qt::AlignCenter);
    m_statusBadge->setWordWrap(true);
    outer->addWidget(m_statusBadge);

    m_statusDetail = new QLabel();
    m_statusDetail->setObjectName("CalibrationDetail");
    m_statusDetail->setWordWrap(true);
    m_statusDetail->setStyleSheet("color: #a9b9cd; font-size: 13px;");
    outer->addWidget(m_statusDetail);

    outer->addWidget(sectionLabel(QStringLiteral("MEASUREMENT MATCHING")));

    auto* countGrid = new QGridLayout();
    countGrid->setHorizontalSpacing(14);
    countGrid->setVerticalSpacing(5);
    m_importedCount = valueLabel("CalibrationImportedCount", QStringLiteral("0"));
    m_usableCount = valueLabel("CalibrationUsableCount", QStringLiteral("0"));
    m_rejectedCount = valueLabel("CalibrationRejectedCount", QStringLiteral("0"));
    countGrid->addWidget(captionLabel(QStringLiteral("Imported")), 0, 0);
    countGrid->addWidget(captionLabel(QStringLiteral("Paired / usable")), 0, 1);
    countGrid->addWidget(captionLabel(QStringLiteral("Rejected")), 0, 2);
    countGrid->addWidget(m_importedCount, 1, 0);
    countGrid->addWidget(m_usableCount, 1, 1);
    countGrid->addWidget(m_rejectedCount, 1, 2);
    countGrid->setColumnStretch(0, 1);
    countGrid->setColumnStretch(1, 1);
    countGrid->setColumnStretch(2, 1);
    outer->addLayout(countGrid);

    outer->addWidget(divider());
    outer->addWidget(sectionLabel(QStringLiteral("MODEL ERROR")));

    auto* residualGrid = new QGridLayout();
    residualGrid->setHorizontalSpacing(14);
    residualGrid->setVerticalSpacing(5);
    m_meanResidual = valueLabel("CalibrationMeanResidual");
    m_medianResidual = valueLabel("CalibrationMedianResidual");
    m_meanAbsoluteError = valueLabel("CalibrationMaeBefore");
    m_meanAbsoluteErrorAfter = valueLabel("CalibrationMaeAfter");
    residualGrid->addWidget(captionLabel(QStringLiteral("Mean residual")), 0, 0);
    residualGrid->addWidget(captionLabel(QStringLiteral("Median residual")), 0, 1);
    residualGrid->addWidget(captionLabel(QStringLiteral("MAE before")), 0, 2);
    residualGrid->addWidget(m_meanResidual, 1, 0);
    residualGrid->addWidget(m_medianResidual, 1, 1);
    residualGrid->addWidget(m_meanAbsoluteError, 1, 2);
    residualGrid->addWidget(captionLabel(QStringLiteral("MAE after correction")), 2, 0, 1, 2);
    residualGrid->addWidget(m_meanAbsoluteErrorAfter, 3, 0, 1, 2);
    residualGrid->setColumnStretch(0, 1);
    residualGrid->setColumnStretch(1, 1);
    residualGrid->setColumnStretch(2, 1);
    outer->addLayout(residualGrid);

    auto* residualHelp = new QLabel(QStringLiteral(
        "Residual is measured minus predicted signal. Positive values mean the model predicts too little signal."));
    residualHelp->setWordWrap(true);
    residualHelp->setStyleSheet("color: #a9b9cd; font-size: 12px;");
    outer->addWidget(residualHelp);

    outer->addWidget(divider());
    outer->addWidget(sectionLabel(QStringLiteral("CORRECTION QUALITY")));

    auto* qualityGrid = new QGridLayout();
    qualityGrid->setHorizontalSpacing(14);
    qualityGrid->setVerticalSpacing(5);
    m_rmseBefore = valueLabel("CalibrationRmseBefore");
    m_rmseAfter = valueLabel("CalibrationRmseAfter");
    m_recommendedCorrection = valueLabel("CalibrationRecommendedCorrection");
    m_recommendedCorrection->setStyleSheet(
        "color: #6ee7b7; font-family: Consolas, monospace; font-size: 14px; font-weight: 700;");
    qualityGrid->addWidget(captionLabel(QStringLiteral("RMSE before")), 0, 0);
    qualityGrid->addWidget(captionLabel(QStringLiteral("RMSE after")), 0, 1);
    qualityGrid->addWidget(captionLabel(QStringLiteral("Recommended")), 0, 2);
    qualityGrid->addWidget(m_rmseBefore, 1, 0);
    qualityGrid->addWidget(m_rmseAfter, 1, 1);
    qualityGrid->addWidget(m_recommendedCorrection, 1, 2);
    qualityGrid->setColumnStretch(0, 1);
    qualityGrid->setColumnStretch(1, 1);
    qualityGrid->setColumnStretch(2, 1);
    outer->addLayout(qualityGrid);

    auto* appliedGrid = new QGridLayout();
    appliedGrid->setContentsMargins(0, 2, 0, 0);
    appliedGrid->setColumnStretch(0, 1);
    m_appliedCorrection = valueLabel("CalibrationAppliedCorrection", QStringLiteral("0.00 dB"));
    appliedGrid->addWidget(captionLabel(QStringLiteral("Currently applied correction")), 0, 0);
    appliedGrid->addWidget(m_appliedCorrection, 1, 0);
    outer->addLayout(appliedGrid);

    m_warnings = new QLabel();
    m_warnings->setObjectName("CalibrationWarnings");
    m_warnings->setTextFormat(Qt::PlainText);
    m_warnings->setWordWrap(true);
    m_warnings->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_warnings->setStyleSheet(
        "background: #2b2111; border: 1px solid #854d0e; border-radius: 6px; "
        "color: #fde68a; padding: 8px; font-size: 12px;");
    outer->addWidget(m_warnings);

    m_importMeasurements = new QPushButton(QStringLiteral("Import measurements\u2026"));
    m_importMeasurements->setObjectName("CalibrationImportButton");
    m_importMeasurements->setToolTip(QStringLiteral(
        "Import measurement locations and observed signal levels from a CSV file."));
    outer->addWidget(m_importMeasurements);

    m_applyCorrection = new QPushButton(QStringLiteral("Apply recommendation"));
    m_applyCorrection->setObjectName("CalibrationApplyCorrectionButton");
    m_applyCorrection->setToolTip(QStringLiteral(
        "Apply the recommended signal correction to future coverage calculations."));
    outer->addWidget(m_applyCorrection);

    m_resetCorrection = new QPushButton(QStringLiteral("Reset applied correction"));
    m_resetCorrection->setObjectName("CalibrationResetCorrectionButton");
    outer->addWidget(m_resetCorrection);

    m_clearMeasurements = new QPushButton(QStringLiteral("Clear measurements"));
    m_clearMeasurements->setObjectName("CalibrationClearButton");
    m_clearMeasurements->setToolTip(QStringLiteral(
        "Remove imported measurements without changing the applied correction."));
    outer->addWidget(m_clearMeasurements);
    outer->addStretch();

    connect(m_importMeasurements, &QPushButton::clicked,
            this, &CalibrationPanel::importMeasurementsRequested);
    connect(m_applyCorrection, &QPushButton::clicked,
            this, &CalibrationPanel::applyRecommendedCorrectionRequested);
    connect(m_resetCorrection, &QPushButton::clicked,
            this, &CalibrationPanel::resetAppliedCorrectionRequested);
    connect(m_clearMeasurements, &QPushButton::clicked,
            this, &CalibrationPanel::clearMeasurementsRequested);

    scroll->setWidget(content);
    shell->addWidget(scroll);

    setMinimumWidth(300);
    setMaximumWidth(460);
    setStyleSheet(
        "CalibrationPanel, QScrollArea, QScrollArea > QWidget > QWidget { background: #0b1422; }"
        "QScrollBar:vertical { background: #0b1422; width: 8px; }"
        "QScrollBar::handle:vertical { background: #334155; border-radius: 4px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QPushButton { background: #132237; border: 1px solid #334a65; border-radius: 6px; color: #cbd5e1; padding: 9px; font-size: 13px; }"
        "QPushButton:hover { background: #1a2c45; border-color: #4f6987; }"
        "QPushButton:pressed { background: #0d1929; }"
        "QPushButton:disabled { background: #0e1928; border-color: #243449; color: #52637a; }"
        "QPushButton#CalibrationApplyCorrectionButton { background: #0f766e; border-color: #2dd4bf; color: #ecfeff; font-weight: 650; }"
        "QPushButton#CalibrationApplyCorrectionButton:disabled { background: #12332f; border-color: #24524d; color: #5f7e7a; }");

    showNoMeasurements();
}

void CalibrationPanel::showNoMeasurements() {
    const double appliedCorrection = m_report.appliedCorrectionDb;
    m_state = State::NoMeasurements;
    m_report = Report{};
    m_report.appliedCorrectionDb = appliedCorrection;
    setStatus(QStringLiteral("NO MEASUREMENTS"),
              QStringLiteral("Import field measurements to compare them with a coverage result."),
              QStringLiteral("#111c2e"), QStringLiteral("#2a3b52"),
              QStringLiteral("#94a3b8"));
    refreshValues();
    refreshControls();
}

void CalibrationPanel::showAwaitingCoverage(const Report& report) {
    m_state = State::AwaitingCoverage;
    m_report = report;
    setStatus(QStringLiteral("COVERAGE REQUIRED"),
              QStringLiteral("Measurements are loaded. Run coverage for this area to pair predicted and observed values."),
              QStringLiteral("#2b2111"), QStringLiteral("#d97706"),
              QStringLiteral("#fde68a"));
    refreshValues();
    refreshControls();
}

void CalibrationPanel::showInsufficientSamples(const Report& report) {
    m_state = State::InsufficientSamples;
    m_report = report;
    setStatus(QStringLiteral("MORE MATCHES NEEDED"),
              QStringLiteral("At least 3 finite measurement and prediction pairs are required before a correction can be applied."),
              QStringLiteral("#2b2111"), QStringLiteral("#d97706"),
              QStringLiteral("#fde68a"));
    refreshValues();
    refreshControls();
}

void CalibrationPanel::setCalibrationReady(const Report& report) {
    if (report.usableSamples < 3 || !std::isfinite(report.recommendedCorrectionDb)) {
        showInsufficientSamples(report);
        return;
    }

    m_state = State::Ready;
    m_report = report;
    setStatus(QStringLiteral("CALIBRATION READY"),
              QStringLiteral("Review the error reduction, then apply the recommended correction if it fits the field data."),
              QStringLiteral("#12312f"), QStringLiteral("#10b981"),
              QStringLiteral("#a7f3d0"));
    refreshValues();
    refreshControls();
}

void CalibrationPanel::setReport(State state, const Report& report) {
    switch (state) {
    case State::NoMeasurements:
        m_report.appliedCorrectionDb = report.appliedCorrectionDb;
        showNoMeasurements();
        break;
    case State::AwaitingCoverage:
        showAwaitingCoverage(report);
        break;
    case State::InsufficientSamples:
        showInsufficientSamples(report);
        break;
    case State::Ready:
        setCalibrationReady(report);
        break;
    }
}

void CalibrationPanel::setCalibrationReport(const CalibrationReport& report,
                                            double appliedCorrectionDb) {
    Report view;
    view.importedSamples = report.imported;
    view.usableSamples = report.paired;
    view.rejectedSamples = report.rejected;
    view.meanResidualDb = report.meanBiasDb;
    view.medianResidualDb = report.medianBiasDb;
    view.meanAbsoluteErrorDb = report.maeDb;
    view.meanAbsoluteErrorAfterDb = report.maeAfterBiasDb;
    view.rmseBeforeDb = report.rmseBeforeDb;
    view.rmseAfterDb = report.rmseAfterBiasDb;
    view.recommendedCorrectionDb = report.meanBiasDb;
    view.appliedCorrectionDb = appliedCorrectionDb;
    for (const std::string& warning : report.warnings)
        view.warnings.push_back(QString::fromStdString(warning));

    if (report.canApply()) setCalibrationReady(view);
    else showInsufficientSamples(view);
}

void CalibrationPanel::setAppliedCorrection(double correctionDb) {
    m_report.appliedCorrectionDb = correctionDb;
    m_appliedCorrection->setText(formatDb(correctionDb, true));
    refreshControls();
}

void CalibrationPanel::refreshValues() {
    m_importedCount->setText(formatCount(m_report.importedSamples));
    m_usableCount->setText(formatCount(m_report.usableSamples));
    m_rejectedCount->setText(formatCount(m_report.rejectedSamples));
    m_meanResidual->setText(formatDb(m_report.meanResidualDb, true));
    m_medianResidual->setText(formatDb(m_report.medianResidualDb, true));
    m_meanAbsoluteError->setText(formatDb(m_report.meanAbsoluteErrorDb));
    m_meanAbsoluteErrorAfter->setText(formatDb(m_report.meanAbsoluteErrorAfterDb));
    m_rmseBefore->setText(formatDb(m_report.rmseBeforeDb));
    m_rmseAfter->setText(formatDb(m_report.rmseAfterDb));
    m_recommendedCorrection->setText(formatDb(m_report.recommendedCorrectionDb, true));
    m_appliedCorrection->setText(formatDb(m_report.appliedCorrectionDb, true));

    m_warnings->setVisible(!m_report.warnings.isEmpty());
    m_warnings->setText(m_report.warnings.isEmpty()
        ? QString()
        : QStringLiteral("\u26a0  %1").arg(m_report.warnings.join(QStringLiteral("\n\u2022  "))));
}

void CalibrationPanel::refreshControls() {
    const bool recommendationReady = m_state == State::Ready
        && m_report.usableSamples >= 3
        && std::isfinite(m_report.recommendedCorrectionDb);
    const bool correctionApplied = std::isfinite(m_report.appliedCorrectionDb)
        && std::abs(m_report.appliedCorrectionDb) >= 0.005;

    m_applyCorrection->setEnabled(recommendationReady);
    m_resetCorrection->setEnabled(correctionApplied);
    m_clearMeasurements->setEnabled(m_state != State::NoMeasurements);
}

void CalibrationPanel::setStatus(const QString& title, const QString& detail,
                                 const QString& background, const QString& border,
                                 const QString& foreground) {
    m_statusBadge->setText(title);
    m_statusBadge->setStyleSheet(QStringLiteral(
        "background: %1; border: 1px solid %2; border-radius: 8px; color: %3; padding: 7px; font-size: 12px; font-weight: 750;")
        .arg(background, border, foreground));
    m_statusDetail->setText(detail);
}
