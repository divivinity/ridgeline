#pragma once

#include "sim/PointAnalysis.h"

#include <QWidget>

class QLabel;

class PointAnalysisPanel : public QWidget {
    Q_OBJECT
public:
    explicit PointAnalysisPanel(QWidget* parent = nullptr);

    void setAnalysis(const PointAnalysisResult& analysis,
                     const QString& positionText,
                     const QString& servingSourceName,
                     const QString& strongestJammerName);
    void clear();

private:
    QLabel* m_resultBadge = nullptr;
    QLabel* m_position = nullptr;
    QLabel* m_serving = nullptr;
    QLabel* m_signal = nullptr;
    QLabel* m_strongestSignal = nullptr;
    QLabel* m_jammer = nullptr;
    QLabel* m_snr = nullptr;
    QLabel* m_sinr = nullptr;
    QLabel* m_margin = nullptr;
    QLabel* m_path = nullptr;
    QLabel* m_explanation = nullptr;
    QWidget* m_profile = nullptr;
};
