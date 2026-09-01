#include "SourcePositionDialog.h"

#include "geo/Mgrs.h"
#include "sim/TerrainModel.h"
#include "sim/Types.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

SourcePositionDialog::SourcePositionDialog(const TerrainModel& terrain,
                                           const Antenna& antenna,
                                           QWidget* parent)
    : QDialog(parent), m_terrain(terrain),
      m_worldX(antenna.position.x), m_worldZ(antenna.position.z) {
    setWindowTitle("Edit source position");
    setModal(true);
    setMinimumWidth(520);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(12);

    auto* title = new QLabel(antenna.isJammer
        ? "Move selected jammer" : "Move selected emitter");
    title->setObjectName("DialogTitle");
    layout->addWidget(title);
    auto* help = new QLabel(
        "Enter an exact position. Ridgeline preserves antenna height above ground "
        "and recalculates terrain elevation at the destination.");
    help->setWordWrap(true);
    help->setObjectName("DialogHelp");
    layout->addWidget(help);

    auto* formatForm = new QFormLayout();
    m_format = new QComboBox();
    m_format->setObjectName("SourcePositionFormatCombo");
    m_format->addItem("Map coordinates (X / Z)");
    if (terrain.hasGeoReference()) {
        m_format->addItem("WGS84 latitude / longitude");
        m_format->addItem("MGRS grid reference");
    }
    formatForm->addRow("Coordinate format", m_format);
    layout->addLayout(formatForm);

    m_stack = new QStackedWidget();
    m_stack->setObjectName("SourcePositionStack");

    auto* localPage = new QWidget();
    auto* localForm = new QFormLayout(localPage);
    localForm->setContentsMargins(0, 0, 0, 0);
    m_x = new QDoubleSpinBox();
    m_x->setObjectName("SourceWorldXSpin");
    m_x->setRange(0.0, terrain.worldSize().x);
    m_x->setDecimals(2);
    m_x->setSuffix(" m");
    m_x->setValue(antenna.position.x);
    m_z = new QDoubleSpinBox();
    m_z->setObjectName("SourceWorldZSpin");
    m_z->setRange(0.0, terrain.worldSize().z);
    m_z->setDecimals(2);
    m_z->setSuffix(" m");
    m_z->setValue(antenna.position.z);
    localForm->addRow("East / X", m_x);
    localForm->addRow("North / Z", m_z);
    m_stack->addWidget(localPage);

    if (terrain.hasGeoReference()) {
        const auto current = terrain.worldToLatLon(antenna.position.x, antenna.position.z);
        auto* wgsPage = new QWidget();
        auto* wgsForm = new QFormLayout(wgsPage);
        wgsForm->setContentsMargins(0, 0, 0, 0);
        m_latitude = new QDoubleSpinBox();
        m_latitude->setObjectName("SourceLatitudeSpin");
        m_latitude->setRange(-85.0, 85.0);
        m_latitude->setDecimals(7);
        m_latitude->setValue(current.latDeg);
        m_longitude = new QDoubleSpinBox();
        m_longitude->setObjectName("SourceLongitudeSpin");
        m_longitude->setRange(-180.0, 180.0);
        m_longitude->setDecimals(7);
        m_longitude->setValue(current.lonDeg);
        wgsForm->addRow("Latitude", m_latitude);
        wgsForm->addRow("Longitude", m_longitude);
        m_stack->addWidget(wgsPage);

        auto* mgrsPage = new QWidget();
        auto* mgrsForm = new QFormLayout(mgrsPage);
        mgrsForm->setContentsMargins(0, 0, 0, 0);
        m_mgrs = new QLineEdit();
        m_mgrs->setObjectName("SourceMgrsEdit");
        m_mgrs->setPlaceholderText("Example: 18S UJ 23394 07395");
        std::string mgrsText;
        std::string error;
        if (wgs84ToMgrs(current.latDeg, current.lonDeg, 5, mgrsText, error))
            m_mgrs->setText(QString::fromStdString(mgrsText));
        mgrsForm->addRow("MGRS", m_mgrs);
        m_stack->addWidget(mgrsPage);
        m_format->setCurrentIndex(1);
        m_stack->setCurrentIndex(1);
    }
    layout->addWidget(m_stack);

    const float ground = terrain.heightAtWorld(antenna.position.x, antenna.position.z);
    const float agl = std::max(0.0f, antenna.position.y - ground);
    m_status = new QLabel(QString("Current ground %1 m MSL · antenna %2 m AGL")
        .arg(ground, 0, 'f', 1).arg(agl, 0, 'f', 1));
    m_status->setObjectName("PositionStatus");
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText("Move source");
    buttons->button(QDialogButtonBox::Ok)->setObjectName("MoveSourceButton");
    layout->addWidget(buttons);

    connect(m_format, QOverload<int>::of(&QComboBox::currentIndexChanged),
            m_stack, &QStackedWidget::setCurrentIndex);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &SourcePositionDialog::validateAndAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setStyleSheet(R"(
        QDialog { background: #0b1220; color: #e8eef7; }
        QLabel#DialogTitle { color: #f8fafc; font-size: 20px; font-weight: 650; }
        QLabel#DialogHelp, QLabel#PositionStatus { color: #94a3b8; font-size: 11px; }
        QLineEdit, QDoubleSpinBox, QComboBox {
            min-height: 34px; background: #111c2e; border: 1px solid #26364d;
            border-radius: 7px; padding: 2px 10px; color: #e8eef7;
        }
        QLineEdit:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #2dd4bf; }
        QPushButton { min-height: 34px; padding: 0 16px; border-radius: 7px;
            border: 1px solid #2a3b52; background: #152238; color: #dbe7f5; }
        QPushButton#MoveSourceButton { background: #0f766e; border-color: #14b8a6;
            color: white; font-weight: 650; }
    )");
}

void SourcePositionDialog::validateAndAccept() {
    float x = 0.0f;
    float z = 0.0f;
    if (m_format->currentIndex() == 0) {
        x = static_cast<float>(m_x->value());
        z = static_cast<float>(m_z->value());
    } else {
        double latitude = 0.0;
        double longitude = 0.0;
        if (m_format->currentIndex() == 1) {
            latitude = m_latitude->value();
            longitude = m_longitude->value();
        } else {
            MgrsCoordinate coordinate;
            std::string error;
            if (!mgrsToWgs84(m_mgrs->text().toStdString(), coordinate, error)) {
                m_status->setStyleSheet("color: #fca5a5;");
                m_status->setText(QString::fromStdString(error));
                return;
            }
            latitude = coordinate.latitudeDeg;
            longitude = coordinate.longitudeDeg;
        }
        const glm::vec2 world = m_terrain.latLonToWorld(latitude, longitude);
        x = world.x;
        z = world.y;
    }

    const glm::vec3 size = m_terrain.worldSize();
    if (x < 0.0f || z < 0.0f || x > size.x || z > size.z) {
        m_status->setStyleSheet("color: #fca5a5;");
        m_status->setText("That coordinate is outside the loaded analysis area.");
        return;
    }
    m_worldX = x;
    m_worldZ = z;
    accept();
}
