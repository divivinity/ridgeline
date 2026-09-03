#include "ReceiverDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QDoubleSpinBox* spin(double min, double max, double step, const QString& suffix) {
    auto* box = new QDoubleSpinBox();
    box->setRange(min, max);
    box->setSingleStep(step);
    box->setSuffix(suffix);
    box->setDecimals(2);
    return box;
}
}

ReceiverDialog::ReceiverDialog(const ReceiverSettings& settings, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Receiver and service criteria");
    setMinimumWidth(390);

    auto* outer = new QVBoxLayout(this);
    auto* intro = new QLabel(
        "These values control the thermal-noise floor, service contour, SINR, and fade-margin calculations.");
    intro->setWordWrap(true);
    outer->addWidget(intro);

    auto* form = new QFormLayout();
    m_heightAgl = spin(0.0, 1000.0, 0.5, " m AGL");
    m_heightAgl->setObjectName("ReceiverHeightAglSpin");
    m_sensitivity = spin(-170.0, -20.0, 1.0, " dBm");
    m_bandwidthKhz = spin(0.001, 100000.0, 1.0, " kHz");
    m_noiseFigure = spin(0.0, 40.0, 0.5, " dB");
    m_requiredSnr = spin(-20.0, 60.0, 1.0, " dB");
    m_fadeMargin = spin(0.0, 60.0, 1.0, " dB");
    m_polarization = new QComboBox();
    m_polarization->addItems({"vertical", "horizontal", "RHCP", "LHCP"});

    m_heightAgl->setValue(settings.receiverHeightAglM);
    m_sensitivity->setValue(settings.sensitivityDbm);
    m_bandwidthKhz->setValue(settings.bandwidthHz / 1000.0f);
    m_noiseFigure->setValue(settings.noiseFigureDb);
    m_requiredSnr->setValue(settings.requiredSnrDb);
    m_fadeMargin->setValue(settings.fadeMarginTargetDb);
    int pol = m_polarization->findText(QString::fromStdString(settings.polarization), Qt::MatchFixedString);
    m_polarization->setCurrentIndex(pol >= 0 ? pol : 0);

    form->addRow("Receiver height:", m_heightAgl);
    form->addRow("Sensitivity:", m_sensitivity);
    form->addRow("Bandwidth:", m_bandwidthKhz);
    form->addRow("Noise figure:", m_noiseFigure);
    form->addRow("Required SINR:", m_requiredSnr);
    form->addRow("Fade margin target:", m_fadeMargin);
    form->addRow("Receive polarization:", m_polarization);
    outer->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);
}

ReceiverSettings ReceiverDialog::settings() const {
    ReceiverSettings result;
    result.receiverHeightAglM = static_cast<float>(m_heightAgl->value());
    result.sensitivityDbm = static_cast<float>(m_sensitivity->value());
    result.bandwidthHz = static_cast<float>(m_bandwidthKhz->value() * 1000.0);
    result.noiseFigureDb = static_cast<float>(m_noiseFigure->value());
    result.requiredSnrDb = static_cast<float>(m_requiredSnr->value());
    result.fadeMarginTargetDb = static_cast<float>(m_fadeMargin->value());
    result.polarization = m_polarization->currentText().toStdString();
    return result;
}
