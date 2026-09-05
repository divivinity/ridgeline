#pragma once

#include <QDialog>

struct Antenna;
class TerrainModel;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QStackedWidget;

class SourcePositionDialog : public QDialog {
    Q_OBJECT
public:
    SourcePositionDialog(const TerrainModel& terrain,
                         const Antenna& antenna,
                         QWidget* parent = nullptr);

    float worldX() const { return m_worldX; }
    float worldZ() const { return m_worldZ; }

private slots:
    void validateAndAccept();

private:
    const TerrainModel& m_terrain;
    float m_worldX = 0.0f;
    float m_worldZ = 0.0f;
    QComboBox* m_format = nullptr;
    QStackedWidget* m_stack = nullptr;
    QDoubleSpinBox* m_x = nullptr;
    QDoubleSpinBox* m_z = nullptr;
    QDoubleSpinBox* m_latitude = nullptr;
    QDoubleSpinBox* m_longitude = nullptr;
    QLineEdit* m_mgrs = nullptr;
    QLabel* m_status = nullptr;
};
