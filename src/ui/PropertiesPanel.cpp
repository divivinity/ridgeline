#include "PropertiesPanel.h"
#include "data/AntennaLibrary.h"
#include "data/AntennaPatternIO.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QToolButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent) {
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(14, 14, 14, 14);
    outerLayout->setSpacing(10);

    auto* title = new QLabel("EMITTER SETTINGS");
    title->setStyleSheet("color: #5eead4; font-size: 10px; font-weight: 700; letter-spacing: 1px;");
    outerLayout->addWidget(title);
    auto* help = new QLabel("Tune the selected source. Direction, feed loss, polarization and measured patterns are available under Advanced.");
    help->setWordWrap(true);
    help->setStyleSheet("color: #64748b; font-size: 10px;");
    outerLayout->addWidget(help);

    m_noSelLabel = new QLabel("Select an emitter or jammer from the list.");
    m_noSelLabel->setWordWrap(true);
    m_noSelLabel->setAlignment(Qt::AlignCenter);
    m_noSelLabel->setStyleSheet("color: #64748b; font-size: 11px; padding: 24px 8px;");
    outerLayout->addWidget(m_noSelLabel);

    m_form = new QWidget();
    auto* form = new QFormLayout(m_form);
    form->setContentsMargins(0, 6, 0, 0);
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignRight);

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

    form->addRow("Type:", m_typeCombo);
    form->addRow("Frequency:", m_freqSpin);
    form->addRow("Power:", m_powerSpin);
    form->addRow("Gain:", m_gainSpin);
    form->addRow("Height AGL:", m_heightSpin);
    form->addRow("Role:", m_jammerCheck);

    auto* advanced = new QToolButton();
    advanced->setText("Advanced antenna settings");
    advanced->setCheckable(true);
    advanced->setArrowType(Qt::RightArrow);
    advanced->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    form->addRow(advanced);

    form->addRow("Azimuth:", m_azimuthSpin);
    form->addRow("Downtilt:", m_downtiltSpin);
    form->addRow("H beamwidth:", m_hBeamSpin);
    form->addRow("V beamwidth:", m_vBeamSpin);
    form->addRow("Front/back:", m_frontBackSpin);
    form->addRow("Cable loss:", m_cableLossSpin);
    form->addRow("Polarization:", m_polarizationCombo);
    form->addRow("Measured pattern:", m_importPatternButton);
    form->addRow("", m_patternLabel);

    const QList<QWidget*> advancedWidgets = {
        m_typeCombo, m_gainSpin, m_azimuthSpin, m_downtiltSpin,
        m_hBeamSpin, m_vBeamSpin, m_frontBackSpin, m_cableLossSpin,
        m_polarizationCombo, m_importPatternButton, m_patternLabel
    };
    for (QWidget* widget : advancedWidgets) form->setRowVisible(widget, false);
    connect(advanced, &QToolButton::toggled, this,
            [form, advanced, advancedWidgets](bool shown) {
        advanced->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
        for (QWidget* widget : advancedWidgets) form->setRowVisible(widget, shown);
    });

    outerLayout->addWidget(m_form);
    outerLayout->addStretch();
    m_form->hide();

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

    setMinimumWidth(250);
    setMaximumWidth(380);
    setStyleSheet(
        "QWidget { background: #0b1422; }"
        "QToolButton { color: #94a3b8; border: none; padding: 6px 2px; }"
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
    if (m_form->isVisible()) load(&m_current);
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
    emit antennaChanged(m_current);
}

void PropertiesPanel::load(const Antenna* antenna) {
    if (!antenna) {
        m_form->hide();
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
    m_noSelLabel->hide();
    m_form->show();
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
