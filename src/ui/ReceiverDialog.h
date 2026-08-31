#pragma once

#include <QDialog>
#include "sim/Types.h"

class QDoubleSpinBox;
class QComboBox;

class ReceiverDialog : public QDialog {
    Q_OBJECT
public:
    explicit ReceiverDialog(const ReceiverSettings& settings,
                            QWidget* parent = nullptr);
    ReceiverSettings settings() const;

private:
    QDoubleSpinBox* m_sensitivity = nullptr;
    QDoubleSpinBox* m_bandwidthKhz = nullptr;
    QDoubleSpinBox* m_noiseFigure = nullptr;
    QDoubleSpinBox* m_requiredSnr = nullptr;
    QDoubleSpinBox* m_fadeMargin = nullptr;
    QComboBox* m_polarization = nullptr;
};
