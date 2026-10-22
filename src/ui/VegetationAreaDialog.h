#pragma once
#include "sim/VegetationAreas.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

// Edits one drawn area's name, kind and canopy height.
//
// The bounds are not editable here: they are drawn and reshaped on the map,
// where the operator can see what they cover. The dialog shows what the solver
// makes of them instead - how much ground it actually paints - because that is
// the part the map cannot state precisely.
//
// Construct on the stack and exec(). It must not outlive the call: a lingering
// dialog would be a second QDoubleSpinBox under the main window, and tests that
// look up the source editor's spin box by type would find this one instead.
class VegetationAreaDialog : public QDialog {
    Q_OBJECT
public:
    VegetationAreaDialog(const VegetationArea& area,
                         const QString& effectSummary,
                         float standHeightHintM,
                         QWidget* parent = nullptr);

    // The original bounds with the edited name, kind and height. An emptied
    // name keeps the original; a clearing's height is zero. A stand whose height
    // the operator did not change keeps its stored height exactly, even one
    // outside the spin box's range, so OK on an untouched dialog changes nothing.
    VegetationArea area() const;

private:
    void syncKind();

    VegetationArea m_original;
    QLineEdit* m_name = nullptr;
    QComboBox* m_kind = nullptr;
    QDoubleSpinBox* m_height = nullptr;
    QLabel* m_effect = nullptr;
    // What the height returns to when a clearing is switched back to a stand.
    double m_standHeightM = 15.0;
    // Set only when the operator changes the height, never by syncKind.
    bool m_heightEdited = false;
    bool m_syncing = false;
};
