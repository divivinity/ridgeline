#include "PropertiesPanel.h"
#include "data/AntennaLibrary.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QGroupBox>
#include <QToolButton>

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent) {
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(14, 14, 14, 14);
    outerLayout->setSpacing(10);

    auto* title = new QLabel("EMITTER SETTINGS");
    title->setStyleSheet("color: #5eead4; font-size: 10px; font-weight: 700; letter-spacing: 1px;");
    outerLayout->addWidget(title);
    auto* help = new QLabel("Tune the selected source. Advanced antenna details are optional.");
    help->setWordWrap(true);
    help->setStyleSheet("color: #64748b; font-size: 10px;");
    outerLayout->addWidget(help);

    // "Nothing selected" placeholder
    m_noSelLabel = new QLabel("Select an emitter or jammer from the list.");
    m_noSelLabel->setWordWrap(true);
    m_noSelLabel->setAlignment(Qt::AlignCenter);
    m_noSelLabel->setStyleSheet("color: #64748b; font-size: 11px; padding: 24px 8px;");
    outerLayout->addWidget(m_noSelLabel);

    // Actual form (hidden when nothing is selected)
    m_form = new QWidget();
    auto* form = new QFormLayout(m_form);
    form->setContentsMargins(0, 6, 0, 0);
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignRight);

    const QString labelStyle = "color: #94a3b8; font-size: 11px;";

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

    m_jammerCheck = new QCheckBox("Jammer");
    m_jammerCheck->setStyleSheet(
        "QCheckBox::indicator { width: 14px; height: 14px; }"
        "QCheckBox::indicator:checked { background: #ef5350; border: 1px solid #ef5350; border-radius: 2px; }"
        "QCheckBox::indicator:unchecked { background: transparent; border: 1px solid rgba(255,255,255,0.3); border-radius: 2px; }"
    );
    jammerLabel->setText("Role:");
    form->addRow(jammerLabel, m_jammerCheck);

    auto* advanced = new QToolButton();
    advanced->setText("Advanced antenna settings");
    advanced->setCheckable(true);
    advanced->setArrowType(Qt::RightArrow);
    advanced->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    form->addRow(advanced);
    form->setRowVisible(m_typeCombo, false);
    form->setRowVisible(m_gainSpin, false);
    connect(advanced, &QToolButton::toggled, this,
            [form, advanced, this](bool shown) {
        advanced->setArrowType(shown ? Qt::DownArrow : Qt::RightArrow);
        form->setRowVisible(m_typeCombo, shown);
        form->setRowVisible(m_gainSpin, shown);
    });

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

    setMinimumWidth(230);
    setMaximumWidth(330);
    setStyleSheet(
        "QWidget { background: #0b1422; }"
        "QToolButton { color: #94a3b8; border: none; padding: 6px 2px; }"
        "QToolButton:hover { color: #e2e8f0; }"
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
        "  background: #111c2e;"
        "  border: 1px solid #2a3b52;"
        "  border-radius: 7px;"
        "  color: #e2e8f0;"
        "  font-size: 11px;"
        "  padding: 5px 7px;"
        "}"
        "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {"
        "  background: #172338;"
        "  border: none;"
        "}"
    );
    return sb;
}
