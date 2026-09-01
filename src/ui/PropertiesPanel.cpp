#include "PropertiesPanel.h"
#include "data/AntennaLibrary.h"
#include "data/AntennaPatternIO.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QFrame>
#include <QSizePolicy>
#include <algorithm>
#include <cmath>

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent) {
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(14, 14, 14, 14);
    outerLayout->setSpacing(10);

    auto* title = new QLabel("EMITTER SETTINGS");
    title->setStyleSheet(
        "color: #5eead4; font-size: 10px; font-weight: 700; letter-spacing: 1px;");
    outerLayout->addWidget(title);

    auto* help = new QLabel(
        "Tune the selected source. Direction, feed loss, polarization and measured patterns are available under Advanced.");
    help->setWordWrap(true);
    help->setStyleSheet("color: #64748b; font-size: 10px;");
    outerLayout->addWidget(help);

    m_noSelLabel = new QLabel("Select an emitter or jammer from the list, or click one in the map.");
    m_noSelLabel->setWordWrap(true);
    m_noSelLabel->setAlignment(Qt::AlignCenter);
    m_noSelLabel->setStyleSheet(
        "color: #64748b; font-size: 11px; padding: 24px 8px;");
    outerLayout->addWidget(m_noSelLabel, 1);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_scrollArea->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { background: #0b1422; width: 8px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #334155; border-radius: 4px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    );

    m_form = new QWidget();
    auto* formLayout = new QVBoxLayout(m_form);
    formLayout->setContentsMargins(0, 4, 4, 4);
    formLayout->setSpacing(8);

    m_freqSpin   = makeSpinBox(1.0, 20000.0, 1.0, " MHz");
    m_powerSpin  = makeSpinBox(0.001, 100000.0, 1.0, " W");
    m_gainSpin   = makeSpinBox(-20.0, 60.0, 0.5, " dBi");
    m_heightSpin = makeSpinBox(0.0, 3000.0, 1.0, " m");
    m_azimuthSpin = makeSpinBox(0.0, 359.9, 1.0, "°");
    m_downtiltSpin = makeSpinBox(-90.0, 90.0, 0.5, "°");
    m_hBeamSpin = makeSpinBox(1.0, 360.0, 1.0, "°");
    m_vBeamSpin = makeSpinBox(1.0, 180.0, 1.0, "°");
    m_frontBackSpin = makeSpinBox(0.0, 80.0, 1.0, " dB");
    m_cableLossSpin = makeSpinBox(0.0, 30.0, 0.1, " dB");

    m_typeCombo = new QComboBox();
    m_polarizationCombo = new QComboBox();
    m_polarizationCombo->addItems({"vertical", "horizontal", "RHCP", "LHCP"});
    m_jammerCheck = new QCheckBox("Jammer");
    m_importPatternButton = new QPushButton("Import CSV pattern…");
    m_patternLabel = new QLabel("Analytic pattern");
    m_patternLabel->setWordWrap(true);
    m_patternLabel->setStyleSheet("color: #64748b; font-size: 9px;");

    m_eirpLabel = new QLabel();
    m_erpLabel = new QLabel();
    const QString powerReadoutStyle =
        "color: #ccfbf1; font-family: monospace; font-size: 10px; font-weight: 600;";
    m_eirpLabel->setStyleSheet(powerReadoutStyle);
    m_erpLabel->setStyleSheet(powerReadoutStyle);
    m_eirpLabel->setWordWrap(true);
    m_erpLabel->setWordWrap(true);
    m_eirpLabel->setToolTip(
        "Boresight effective isotropic radiated power: transmitter power + antenna gain − cable/feed loss.");
    m_erpLabel->setToolTip(
        "Boresight effective radiated power referenced to a half-wave dipole (ERP = EIRP − 2.15 dB).");

    auto* basicForm = new QFormLayout();
    basicForm->setContentsMargins(0, 0, 0, 0);
    basicForm->setSpacing(10);
    basicForm->setLabelAlignment(Qt::AlignRight);
    basicForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    basicForm->addRow("Frequency:", m_freqSpin);
    basicForm->addRow("Power:", m_powerSpin);
    basicForm->addRow("EIRP:", m_eirpLabel);
    basicForm->addRow("ERP:", m_erpLabel);
    basicForm->addRow("Height AGL:", m_heightSpin);
    basicForm->addRow("Role:", m_jammerCheck);
    formLayout->addLayout(basicForm);

    m_advancedToggle = new QToolButton();
    m_advancedToggle->setText("Advanced antenna settings");
    m_advancedToggle->setCheckable(true);
    m_advancedToggle->setArrowType(Qt::RightArrow);
    m_advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_advancedToggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    formLayout->addWidget(m_advancedToggle);

    m_advancedPanel = new QWidget(m_form);
    auto* advancedForm = new QFormLayout(m_advancedPanel);
    advancedForm->setContentsMargins(0, 2, 0, 0);
    advancedForm->setSpacing(10);
    advancedForm->setLabelAlignment(Qt::AlignRight);
    advancedForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    advancedForm->addRow("Type:", m_typeCombo);
    advancedForm->addRow("Gain:", m_gainSpin);
    advancedForm->addRow("Azimuth:", m_azimuthSpin);
    advancedForm->addRow("Downtilt:", m_downtiltSpin);
    advancedForm->addRow("H beamwidth:", m_hBeamSpin);
    advancedForm->addRow("V beamwidth:", m_vBeamSpin);
    advancedForm->addRow("Front/back:", m_frontBackSpin);
    advancedForm->addRow("Cable loss:", m_cableLossSpin);
    advancedForm->addRow("Polarization:", m_polarizationCombo);
    advancedForm->addRow("Measured pattern:", m_importPatternButton);
    advancedForm->addRow("", m_patternLabel);
    m_advancedPanel->hide();
    formLayout->addWidget(m_advancedPanel);
    formLayout->addStretch();

    connect(m_advancedToggle, &QToolButton::toggled, this, [this](bool shown) {
        m_advancedToggle->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
        m_advancedPanel->setVisible(shown);
    });

    m_scrollArea->setWidget(m_form);
    outerLayout->addWidget(m_scrollArea, 1);
    m_scrollArea->hide();

    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PropertiesPanel::onTypeChanged);
    connect(m_freqSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertiesPanel::onValueChanged);
    connect(m_powerSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertiesPanel::onValueChanged);
    connect(m_gainSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertiesPanel::onValueChanged);
    connect(m_heightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertiesPanel::onValueChanged);
    connect(m_azimuthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertiesPanel::onValueChanged);
    connect(m_downtiltSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertiesPanel::onValueChanged);
    connect(m_hBeamSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertiesPanel::onValueChanged);
    connect(m_vBeamSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertiesPanel::onValueChanged);
    connect(m_frontBackSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertiesPanel::onValueChanged);
    connect(m_cableLossSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertiesPanel::onValueChanged);
    connect(m_polarizationCombo, &QComboBox::currentTextChanged,
            this, &PropertiesPanel::onValueChanged);
    connect(m_jammerCheck, &QCheckBox::toggled,
            this, &PropertiesPanel::onValueChanged);
    connect(m_importPatternButton, &QPushButton::clicked,
            this, &PropertiesPanel::onImportPattern);

    updatePowerReadout();
    setMinimumWidth(250);
    setMaximumWidth(380);
    setStyleSheet(
        "QWidget { background: #0b1422; }"
        "QToolButton { color: #94a3b8; border: none; padding: 6px 2px; text-align: left; }"
        "QToolButton:hover { color: #e2e8f0; }"
        "QPushButton { background: #111c2e; border: 1px solid #2a3b52; border-radius: 6px; padding: 5px; }"
    );
}

void PropertiesPanel::setAntennaLibrary(const AntennaLibrary* lib) {
    m_lib = lib;
    if (!lib || lib->types().empty()) return;
    m_loading = true;
    m_typeCombo->clear();
    for (const auto& t : lib->types())
        m_typeCombo->addItem(QString::fromStdString(t.name));
    m_loading = false;
    if (m_scrollArea && m_scrollArea->isVisible()) load(&m_current);
}

void PropertiesPanel::onTypeChanged(int index) {
    if (m_loading || !m_lib || index < 0 ||
        index >= static_cast<int>(m_lib->types().size())) return;
    const AntennaType& type = m_lib->types()[index];
    m_current.antennaType = type.name;
    m_current.gainDbi = type.defaultGainDbi;
    m_current.horizontalBeamwidthDeg = type.beamwidthDeg;
    m_current.azimuthPatternDb.clear();
    m_current.elevationPatternDb.clear();
    m_current.patternSource.clear();

    m_loading = true;
    m_gainSpin->setValue(type.defaultGainDbi);
    m_hBeamSpin->setValue(type.beamwidthDeg);
    m_patternLabel->setText("Analytic pattern");
    m_loading = false;
    updatePowerReadout();
    emit antennaChanged(m_current);
}

void PropertiesPanel::load(const Antenna* antenna) {
    if (!antenna) {
        if (m_scrollArea) m_scrollArea->hide();
        m_noSelLabel->show();
        return;
    }

    m_current = *antenna;
    m_loading = true;
    m_freqSpin->setValue(antenna->frequencyMHz);
    m_powerSpin->setValue(antenna->powerW);
    m_gainSpin->setValue(antenna->gainDbi);
    m_heightSpin->setValue(antenna->position.y);
    m_azimuthSpin->setValue(antenna->azimuthDeg);
    m_downtiltSpin->setValue(antenna->downtiltDeg);
    m_hBeamSpin->setValue(antenna->horizontalBeamwidthDeg);
    m_vBeamSpin->setValue(antenna->verticalBeamwidthDeg);
    m_frontBackSpin->setValue(antenna->frontToBackDb);
    m_cableLossSpin->setValue(antenna->cableLossDb);
    m_jammerCheck->setChecked(antenna->isJammer);

    int polIndex = m_polarizationCombo->findText(
        QString::fromStdString(antenna->polarization), Qt::MatchFixedString);
    if (polIndex < 0) polIndex = 0;
    m_polarizationCombo->setCurrentIndex(polIndex);

    if (m_lib && m_typeCombo->count() > 0) {
        int found = -1;
        for (int i = 0; i < m_typeCombo->count(); ++i) {
            if (m_typeCombo->itemText(i).toStdString() == antenna->antennaType) {
                found = i;
                break;
            }
        }
        m_typeCombo->setCurrentIndex(found >= 0 ? found : 0);
    }

    m_patternLabel->setText(antenna->patternSource.empty()
        ? QStringLiteral("Analytic pattern")
        : QFileInfo(QString::fromStdString(antenna->patternSource)).fileName());
    m_loading = false;
    updatePowerReadout();
    m_noSelLabel->hide();
    m_scrollArea->show();
}

void PropertiesPanel::onValueChanged() {
    if (m_loading) return;
    m_current.frequencyMHz = static_cast<float>(m_freqSpin->value());
    m_current.powerW = static_cast<float>(m_powerSpin->value());
    m_current.gainDbi = static_cast<float>(m_gainSpin->value());
    m_current.position.y = static_cast<float>(m_heightSpin->value());
    m_current.azimuthDeg = static_cast<float>(m_azimuthSpin->value());
    m_current.downtiltDeg = static_cast<float>(m_downtiltSpin->value());
    m_current.horizontalBeamwidthDeg = static_cast<float>(m_hBeamSpin->value());
    m_current.verticalBeamwidthDeg = static_cast<float>(m_vBeamSpin->value());
    m_current.frontToBackDb = static_cast<float>(m_frontBackSpin->value());
    m_current.cableLossDb = static_cast<float>(m_cableLossSpin->value());
    m_current.polarization = m_polarizationCombo->currentText().toStdString();
    m_current.isJammer = m_jammerCheck->isChecked();
    updatePowerReadout();
    emit antennaChanged(m_current);
}

void PropertiesPanel::onImportPattern() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Import antenna pattern", {}, "CSV pattern (*.csv);;All files (*)");
    if (path.isEmpty()) return;
    try {
        AntennaPatternIO::loadCsv(path.toStdString(), m_current);
        m_patternLabel->setText(QFileInfo(path).fileName());
        emit antennaChanged(m_current);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Pattern import failed", e.what());
    }
}

void PropertiesPanel::updatePowerReadout() {
    if (!m_eirpLabel || !m_erpLabel || !m_powerSpin || !m_gainSpin || !m_cableLossSpin)
        return;

    const double powerW = std::max(m_powerSpin->value(), 1.0e-12);
    const double txPowerDbm = 10.0 * std::log10(powerW * 1000.0);
    const double eirpDbm = txPowerDbm + m_gainSpin->value() - m_cableLossSpin->value();
    const double erpDbm = eirpDbm - 2.15;
    const double eirpW = std::pow(10.0, (eirpDbm - 30.0) / 10.0);
    const double erpW = std::pow(10.0, (erpDbm - 30.0) / 10.0);

    m_eirpLabel->setText(QString("%1 dBm  ·  %2 W")
        .arg(eirpDbm, 0, 'f', 1)
        .arg(eirpW, 0, 'f', eirpW < 10.0 ? 2 : 1));
    m_erpLabel->setText(QString("%1 dBm  ·  %2 W")
        .arg(erpDbm, 0, 'f', 1)
        .arg(erpW, 0, 'f', erpW < 10.0 ? 2 : 1));
}

QDoubleSpinBox* PropertiesPanel::makeSpinBox(double min, double max,
                                              double step, const QString& suffix) {
    auto* sb = new QDoubleSpinBox();
    sb->setRange(min, max);
    sb->setSingleStep(step);
    sb->setSuffix(suffix);
    sb->setDecimals((suffix == " MHz" || suffix == " m" || suffix == "°") ? 1 : 3);
    sb->setStyleSheet(
        "QDoubleSpinBox { background: #111c2e; border: 1px solid #2a3b52;"
        " border-radius: 7px; color: #e2e8f0; font-size: 11px; padding: 5px 7px; }"
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {"
        " background: #172338; border: none; }"
    );
    return sb;
}