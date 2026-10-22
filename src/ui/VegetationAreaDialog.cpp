#include "VegetationAreaDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

#include <algorithm>

VegetationAreaDialog::VegetationAreaDialog(const VegetationArea& area,
                                           const QString& effectSummary,
                                           float standHeightHintM,
                                           QWidget* parent)
    : QDialog(parent), m_original(area) {
    setObjectName("VegetationAreaDialog");
    setWindowTitle("Vegetation area");

    auto* form = new QFormLayout(this);

    m_name = new QLineEdit(QString::fromStdString(area.name), this);
    m_name->setObjectName("VegetationAreaName");

    m_kind = new QComboBox(this);
    m_kind->setObjectName("VegetationAreaKind");
    m_kind->addItem("Stand", static_cast<int>(VegetationKind::Stand));
    m_kind->addItem("Clearing", static_cast<int>(VegetationKind::Clearing));
    m_kind->setCurrentIndex(m_kind->findData(static_cast<int>(area.kind)));

    const double lowest = 0.5;
    const double highest = VegetationAreas::kMaxCanopyHeightM;
    m_height = new QDoubleSpinBox(this);
    m_height->setObjectName("VegetationAreaHeight");
    m_height->setRange(lowest, highest);
    m_height->setDecimals(1);
    m_height->setSingleStep(0.5);
    m_height->setSuffix(" m");
    m_standHeightM = std::clamp(
        static_cast<double>(area.kind == VegetationKind::Stand ? area.canopyHeightM
                                                               : standHeightHintM),
        lowest, highest);
    m_height->setValue(m_standHeightM);

    auto* explanation = new QLabel(
        "A stand is trees of this height, treated as complete cover. A clearing is "
        "felled ground: it removes canopy, imported or drawn, wherever it reaches.",
        this);
    explanation->setWordWrap(true);

    m_effect = new QLabel(effectSummary, this);
    m_effect->setObjectName("VegetationAreaEffect");
    m_effect->setWordWrap(true);

    form->addRow(explanation);
    form->addRow("Name", m_name);
    form->addRow("Kind", m_kind);
    form->addRow("Canopy height", m_height);
    form->addRow(m_effect);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);

    connect(m_kind, &QComboBox::currentIndexChanged, this, [this] { syncKind(); });
    connect(m_height, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        if (m_syncing || !m_height->isEnabled()) return;
        m_standHeightM = value;
        m_heightEdited = true;
    });
    syncKind();
}

void VegetationAreaDialog::syncKind() {
    const bool clearing =
        m_kind->currentData().toInt() == static_cast<int>(VegetationKind::Clearing);
    m_height->setEnabled(!clearing);
    m_height->setToolTip(clearing ? QStringLiteral("A clearing is bare ground; its height is ignored.")
                                  : QString());
    if (!clearing) {
        m_syncing = true;
        m_height->setValue(m_standHeightM);
        m_syncing = false;
    }
}

VegetationArea VegetationAreaDialog::area() const {
    VegetationArea edited = m_original;
    const QString name = m_name->text().trimmed();
    if (!name.isEmpty()) edited.name = name.toStdString();
    edited.kind = static_cast<VegetationKind>(m_kind->currentData().toInt());
    if (edited.kind == VegetationKind::Clearing)
        edited.canopyHeightM = 0.0f;
    else if (m_original.kind == VegetationKind::Stand && !m_heightEdited)
        edited.canopyHeightM = m_original.canopyHeightM;
    else
        edited.canopyHeightM = static_cast<float>(m_height->value());
    return edited;
}
