#include "LayersPanel.h"
#include "map/LayerStack.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSlider>
#include <QVBoxLayout>

LayersPanel::LayersPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 14, 14, 14);
    outer->setSpacing(10);

    auto* title = new QLabel("Map layers");
    title->setStyleSheet("color: #f1f5f9; font-size: 16px; font-weight: 600;");
    outer->addWidget(title);

    auto* help = new QLabel("Drag to reorder. Layers higher in the list draw on top.");
    help->setWordWrap(true);
    help->setStyleSheet("color: #a8b7ca; font-size: 12px;");
    outer->addWidget(help);

    m_list = new QListWidget(this);
    m_list->setObjectName("LayerList");
    m_list->setAccessibleName("Map layers");
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setDragDropMode(QAbstractItemView::InternalMove);
    m_list->setStyleSheet(
        "QListWidget { background: #0c1625; border: 1px solid #223148;"
        " border-radius: 9px; color: #dbe7f5; font-size: 13px; padding: 5px; }"
        "QListWidget::item { padding: 2px; border-radius: 5px; }"
        "QListWidget::item:selected { background: #123c43; color: #ecfeff; }");
    outer->addWidget(m_list, 1);

    // The list model reports the drop after the move, so read the widget order
    // back rather than trying to predict the destination index.
    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this,
            [this] { applyRowOrder(); });

    setStyleSheet("LayersPanel { background: #101a29; }");
    setMinimumWidth(280);
}

void LayersPanel::setStack(LayerStack* stack) {
    m_stack = stack;
    if (m_stack) {
        m_stack->setOnChanged([this] {
            if (!m_updating) refresh();
        });
    }
    rebuildRows();
}

void LayersPanel::refresh() {
    rebuildRows();
}

void LayersPanel::rebuildRows() {
    if (!m_list) return;
    m_updating = true;
    m_list->clear();
    if (!m_stack) {
        m_updating = false;
        return;
    }

    // The stack is bottom-first; the list reads top-down, so show it reversed.
    for (std::size_t i = m_stack->size(); i-- > 0;) {
        const MapLayer& layer = m_stack->at(i);
        const QString id = layer.id;

        auto* row = new QWidget();
        row->setObjectName("LayerRow" + id);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(6, 4, 6, 4);
        rowLayout->setSpacing(8);

        auto* visible = new QCheckBox(layer.displayName);
        visible->setObjectName("LayerVisible" + id);
        visible->setAccessibleName(layer.displayName + " visible");
        visible->setChecked(layer.visible);
        visible->setToolTip("Show or hide " + layer.displayName);
        connect(visible, &QCheckBox::toggled, this, [this, id](bool on) {
            if (!m_stack) return;
            m_updating = true;
            m_stack->setVisible(id, on);
            m_updating = false;
            refresh();
        });
        rowLayout->addWidget(visible, 1);

        auto* opacity = new QSlider(Qt::Horizontal);
        opacity->setObjectName("LayerOpacity" + id);
        opacity->setAccessibleName(layer.displayName + " opacity percent");
        opacity->setRange(0, 100);
        opacity->setValue(static_cast<int>(layer.opacity * 100.0f + 0.5f));
        opacity->setFixedWidth(90);
        opacity->setEnabled(layer.visible);
        opacity->setToolTip(QString("%1 opacity: %2%")
                                .arg(layer.displayName)
                                .arg(opacity->value()));
        connect(opacity, &QSlider::valueChanged, this, [this, id, opacity, layer](int value) {
            opacity->setToolTip(QString("%1 opacity: %2%").arg(layer.displayName).arg(value));
            if (!m_stack) return;
            // Do not rebuild here: replacing the slider mid-drag would drop the
            // grab. The stack is the source of truth either way.
            m_updating = true;
            m_stack->setOpacity(id, static_cast<float>(value) / 100.0f);
            m_updating = false;
        });
        rowLayout->addWidget(opacity);

        auto* item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, id);
        item->setSizeHint(row->sizeHint());
        m_list->setItemWidget(item, row);
    }
    m_updating = false;
}

void LayersPanel::applyRowOrder() {
    if (!m_stack || !m_list || m_updating) return;
    m_updating = true;
    // The list is top-down and the stack is bottom-first, so the last row in the
    // list becomes index 0. Placing each in turn leaves the stack in list order.
    const int rows = m_list->count();
    for (int row = rows - 1, target = 0; row >= 0; --row, ++target) {
        const QString id = m_list->item(row)->data(Qt::UserRole).toString();
        m_stack->move(id, static_cast<std::size_t>(target));
    }
    m_updating = false;
    refresh();
}
