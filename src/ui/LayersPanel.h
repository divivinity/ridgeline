#pragma once
#include <QWidget>

class LayerStack;
class QListWidget;

// One row per map layer: visibility, name, opacity, and drag-to-reorder.
// Replaces the fixed overlay checkboxes and the Layers menu.
//
// The panel owns no layer state. It writes to the LayerStack and rebuilds
// itself from the stack when anything else changes it, so the toolbar, a loaded
// scenario and this panel can never disagree about what is on screen.
class LayersPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayersPanel(QWidget* parent = nullptr);

    // Not owned; the stack outlives the panel.
    void setStack(LayerStack* stack);
    void refresh();

private:
    void rebuildRows();
    void applyRowOrder();

    LayerStack* m_stack = nullptr;
    QListWidget* m_list = nullptr;
    // Guards against reacting to changes this panel is itself making.
    bool m_updating = false;
};
