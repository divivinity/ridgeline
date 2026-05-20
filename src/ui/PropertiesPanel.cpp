#include "PropertiesPanel.h"
#include "data/AntennaLibrary.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QGroupBox>

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent) {
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(4, 4, 4, 4);
    outerLayout->setSpacing(4);

    auto* title = new QLabel("Properties");
    title->setStyleSheet("color: rgba(255,255,255,0.5); font-family: monospace; font-size: 11px; padding: 2px 4px;");
    outerLayout->addWidget(title);

    // "Nothing selected" placeholder
    m_noSelLabel = new QLabel("No antenna selected.");
    m_noSelLabel->setAlignment(Qt::AlignCenter);
    m_noSelLabel->setStyleSheet("color: rgba(255,255,255,0.3); font-family: monospace; font-size: 11px;");
    outerLayout->addWidget(m_noSelLabel);

    // Actual form (hidden when nothing is selected)
    m_form = new QWidget();
    auto* form = new QFormLayout(m_form);
    form->setContentsMargins(4, 4, 4, 4);
    form->setSpacing(6);
    form->setLabelAlignment(Qt::AlignRight);

    const QString labelStyle = "color: rgba(255,255,255,0.6); font-family: monospace; font-size: 11px;";

    m_freqSpin   = makeSpinBox(1.0,      6000.0,   1.0,  " MHz");
    m_powerSpin  = makeSpinBox(0.001, 100000.0,    1.0,  " W");
    m_gainSpin   = makeSpinBox(0.0,      40.0,     0.5,  " dBi");
    m_heightSpin = makeSpinBox(0.0,    3000.0,     1.0,  " m");

    auto* freqLabel   = new QLabel("Frequency:");  freqLabel->setStyleSheet(labelStyle);
    auto* powerLabel  = new QLabel("Power:");      powerLabel->setStyleSheet(labelStyle);
    auto* gainLabel   = new QLabel("Gain:");       gainLabel->setStyleSheet(labelStyle);
    auto* heightLabel = new QLabel("Height AGL:"); heightLabel->setStyleSheet(labelStyle);
    auto* jammerLabel = new QLabel("Is Jammer:");  jammerLabel->setStyleSheet(labelStyle);

    m_typeCombo = new QComboBox();
    m_typeCombo->setStyleSheet(
        "QComboBox {"
        "  background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.15);"
        "  border-radius: 3px;"
        "  color: rgba(255,255,255,0.85);"
        "  font-family: monospace; font-size: 12px;"
        "  padding: 2px 6px;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background: #1a2a3a;"
        "  color: rgba(255,255,255,0.85);"
        "  selection-background-color: rgba(79,195,247,0.3);"
        "}"
    );
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PropertiesPanel::onTypeChanged);
    form->addRow("Type", m_typeCombo);

    form->addRow(freqLabel,   m_freqSpin);
    form->addRow(powerLabel,  m_powerSpin);
    form->addRow(gainLabel,   m_gainSpin);
    form->addRow(heightLabel, m_heightSpin);

    m_jammerCheck = new QCheckBox();
    m_jammerCheck->setStyleSheet(
        "QCheckBox::indicator { width: 14px; height: 14px; }"
        "QCheckBox::indicator:checked { background: #ef5350; border: 1px solid #ef5350; border-radius: 2px; }"
        "QCheckBox::indicator:unchecked { background: transparent; border: 1px solid rgba(255,255,255,0.3); border-radius: 2px; }"
    );
    form->addRow(jammerLabel, m_jammerCheck);

    outerLayout->addWidget(m_form);
    outerLayout->addStretch();

    m_form->hide();

    // Wire all controls to onValueChanged
    connect(m_freqSpin,    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertiesPanel::onValueChanged);
    connect(m_powerSpin,   QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertiesPanel::onValueChanged);
    connect(m_gainSpin,    QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertiesPanel::onValueChanged);
    connect(m_heightSpin,  QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &PropertiesPanel::onValueChanged);
    connect(m_jammerCheck, &QCheckBox::toggled,
            this, &PropertiesPanel::onValueChanged);

    setMinimumWidth(180);
    setMaximumWidth(280);
    setStyleSheet("background: rgba(15,25,35,0.85);");
}

void PropertiesPanel::setAntennaLibrary(const AntennaLibrary* lib) {
    m_lib = lib;
    if (!lib || lib->types().empty()) return;

    m_loading = true;
    m_typeCombo->clear();
    for (const auto& t : lib->types())
        m_typeCombo->addItem(QString::fromStdString(t.name));
    m_loading = false;

    // If an antenna is already loaded, re-sync the combo to its type.
    if (m_form->isVisible()) {
        const std::string& typeName = m_current.antennaType;
        bool found = false;
        for (int i = 0; i < m_typeCombo->count(); ++i) {
            if (m_typeCombo->itemText(i).toStdString() == typeName) {
                m_loading = true;
                m_typeCombo->setCurrentIndex(i);
                m_loading = false;
                found = true;
                break;
            }
        }
        if (!found && m_typeCombo->count() > 0) {
            m_loading = true;
            m_typeCombo->setCurrentIndex(0);
            m_loading = false;
            m_current.antennaType = m_typeCombo->itemText(0).toStdString();
        }
    }
}

void PropertiesPanel::onTypeChanged(int index) {
    if (m_loading || !m_lib || index < 0) return;
    const auto& types = m_lib->types();
    if (index >= static_cast<int>(types.size())) return;

    const AntennaType& t = types[index];
    m_current.antennaType = t.name;

    // Apply the type's default gain when the type changes.
    m_loading = true;
    m_gainSpin->setValue(static_cast<double>(t.defaultGainDbi));
    m_loading = false;
    m_current.gainDbi = t.defaultGainDbi;

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
    m_freqSpin->setValue(static_cast<double>(antenna->frequencyMHz));
    m_powerSpin->setValue(static_cast<double>(antenna->powerW));
    m_gainSpin->setValue(static_cast<double>(antenna->gainDbi));
    m_heightSpin->setValue(static_cast<double>(antenna->position.y));
    m_jammerCheck->setChecked(antenna->isJammer);
    // Sync type combo
    if (m_lib && m_typeCombo->count() > 0) {
        const std::string& typeName = antenna->antennaType;
        bool found = false;
        for (int i = 0; i < m_typeCombo->count(); ++i) {
            if (m_typeCombo->itemText(i).toStdString() == typeName) {
                m_typeCombo->setCurrentIndex(i);
                found = true;
                break;
            }
        }
        if (!found && m_typeCombo->count() > 0) {
            m_typeCombo->setCurrentIndex(0);
            m_current.antennaType = m_typeCombo->itemText(0).toStdString();
        }
    }
    m_loading = false;
    m_noSelLabel->hide();
    m_form->show();
}

void PropertiesPanel::onValueChanged() {
    if (m_loading) return;
    m_current.frequencyMHz = static_cast<float>(m_freqSpin->value());
    m_current.powerW       = static_cast<float>(m_powerSpin->value());
    m_current.gainDbi      = static_cast<float>(m_gainSpin->value());
    m_current.position.y   = static_cast<float>(m_heightSpin->value());
    m_current.isJammer     = m_jammerCheck->isChecked();
    emit antennaChanged(m_current);
}

QDoubleSpinBox* PropertiesPanel::makeSpinBox(double min, double max,
                                              double step, const QString& suffix) {
    auto* sb = new QDoubleSpinBox();
    sb->setRange(min, max);
    sb->setSingleStep(step);
    sb->setSuffix(suffix);
    sb->setDecimals(suffix == " MHz" || suffix == " m" ? 1 : 3);
    sb->setStyleSheet(
        "QDoubleSpinBox {"
        "  background: rgba(0,0,0,0.3);"
        "  border: 1px solid rgba(255,255,255,0.12);"
        "  border-radius: 3px;"
        "  color: rgba(255,255,255,0.85);"
        "  font-family: monospace;"
        "  font-size: 11px;"
        "  padding: 2px 4px;"
        "}"
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {"
        "  background: rgba(255,255,255,0.05);"
        "  border: none;"
        "}"
    );
    return sb;
}
