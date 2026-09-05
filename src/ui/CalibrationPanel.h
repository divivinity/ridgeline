#pragma once

#include <QStringList>
#include <QWidget>

#include <cstddef>
#include <limits>

class QLabel;
class QPushButton;
struct CalibrationReport;

class CalibrationPanel final : public QWidget {
    Q_OBJECT
public:
    enum class State {
        NoMeasurements,
        AwaitingCoverage,
        InsufficientSamples,
        Ready
    };

    // A deliberately small view model keeps this panel independent of the
    // measurement file format and the calibration engine's internal types.
    struct Report {
        std::size_t importedSamples = 0;
        std::size_t usableSamples = 0;
        std::size_t rejectedSamples = 0;

        double meanResidualDb = std::numeric_limits<double>::quiet_NaN();
        double medianResidualDb = std::numeric_limits<double>::quiet_NaN();
        double meanAbsoluteErrorDb = std::numeric_limits<double>::quiet_NaN();
        double meanAbsoluteErrorAfterDb = std::numeric_limits<double>::quiet_NaN();
        double rmseBeforeDb = std::numeric_limits<double>::quiet_NaN();
        double rmseAfterDb = std::numeric_limits<double>::quiet_NaN();
        double recommendedCorrectionDb = std::numeric_limits<double>::quiet_NaN();
        double appliedCorrectionDb = 0.0;

        QStringList warnings;
    };

    explicit CalibrationPanel(QWidget* parent = nullptr);

    void showNoMeasurements();
    void showAwaitingCoverage(const Report& report);
    void showInsufficientSamples(const Report& report);
    void setCalibrationReady(const Report& report);
    void setReport(State state, const Report& report);
    // Convenience adapter for the simulation layer. It automatically selects
    // Ready or InsufficientSamples from CalibrationReport::canApply().
    void setCalibrationReport(const CalibrationReport& report,
                              double appliedCorrectionDb = 0.0);
    void setAppliedCorrection(double correctionDb);

    State state() const { return m_state; }
    const Report& report() const { return m_report; }
    double recommendedCorrectionDb() const { return m_report.recommendedCorrectionDb; }

signals:
    void importMeasurementsRequested();
    void applyRecommendedCorrectionRequested();
    void resetAppliedCorrectionRequested();
    void clearMeasurementsRequested();

private:
    State m_state = State::NoMeasurements;
    Report m_report;

    QLabel* m_statusBadge = nullptr;
    QLabel* m_statusDetail = nullptr;
    QLabel* m_importedCount = nullptr;
    QLabel* m_usableCount = nullptr;
    QLabel* m_rejectedCount = nullptr;
    QLabel* m_meanResidual = nullptr;
    QLabel* m_medianResidual = nullptr;
    QLabel* m_meanAbsoluteError = nullptr;
    QLabel* m_meanAbsoluteErrorAfter = nullptr;
    QLabel* m_rmseBefore = nullptr;
    QLabel* m_rmseAfter = nullptr;
    QLabel* m_recommendedCorrection = nullptr;
    QLabel* m_appliedCorrection = nullptr;
    QLabel* m_warnings = nullptr;
    QPushButton* m_importMeasurements = nullptr;
    QPushButton* m_applyCorrection = nullptr;
    QPushButton* m_resetCorrection = nullptr;
    QPushButton* m_clearMeasurements = nullptr;

    void refreshValues();
    void refreshControls();
    void setStatus(const QString& title, const QString& detail,
                   const QString& background, const QString& border,
                   const QString& foreground);
};
