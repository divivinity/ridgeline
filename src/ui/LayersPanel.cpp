#include "LayersPanel.h"
#include "map/LayerStack.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSlider>
#include <QSignalBlocker>
#include <QTimer>
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
        m_stack->addListener([this] {
            // A change made elsewhere can arrive while one of this panel's own
            // rows is mid-signal, and rebuilding would delete the widget that
            // is emitting. Updating in place is safe; only a reorder needs new
            // rows, and that is deferred out of the signal.
            if (m_updating) return;
            if (orderMatchesStack()) syncRows();
            else QTimer::singleShot(0, this, [this] { rebuildRows(); });
        });
    }
    rebuildRows();
}

void LayersPanel::refresh() {
    if (orderMatchesStack()) syncRows();
    else rebuildRows();
}

bool LayersPanel::orderMatchesStack() const {
    if (!m_stack || !m_list) return false;
    if (static_cast<std::size_t>(m_list->count()) != m_stack->size()) return false;
    // The list is top-down, the stack bottom-first.
    for (int row = 0; row < m_list->count(); ++row) {
        const std::size_t index = m_stack->size() - 1 - static_cast<std::size_t>(row);
        if (m_list->item(row)->data(Qt::UserRole).toString() != m_stack->at(index).id)
            return false;
    }
    return true;
}

void LayersPanel::syncRows() {
    if (!m_stack || !m_list) return;
    m_updating = true;
    for (int row = 0; row < m_list->count(); ++row) {
        const QString id = m_list->item(row)->data(Qt::UserRole).toString();
        const MapLayer* layer = m_stack->find(id);
        if (!layer) continue;
        const int percent = static_cast<int>(layer->opacity * 100.0f + 0.5f);
        m_list->item(row)->setData(Qt::AccessibleTextRole, layer->displayName);
        m_list->item(row)->setData(Qt::AccessibleDescriptionRole,
            QString("%1; opacity %2%")
                .arg(layer->visible ? "Shown" : "Hidden").arg(percent));
        if (auto* box = findChild<QCheckBox*>("LayerVisible" + id)) {
            const QSignalBlocker blocker(box);
            box->setChecked(layer->visible);
        }
        if (auto* slider = findChild<QSlider*>("LayerOpacity" + id)) {
            const QSignalBlocker blocker(slider);
            slider->setValue(percent);
            slider->setEnabled(layer->visible);
            slider->setToolTip(QString("%1 opacity: %2%")
                                   .arg(layer->displayName).arg(percent));
        }
    }
    m_updating = false;
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
        connect(visible, &QCheckBox::toggled, this, [this, id, opacityFor = row](bool on) {
            if (!m_stack) return;
            m_updating = true;
            m_stack->setVisible(id, on);
            m_updating = false;
            // Only this row's opacity control is affected, so update it in
            // place rather than rebuilding the list from inside this signal.
            if (auto* slider = opacityFor->findChild<QSlider*>("LayerOpacity" + id))
                slider->setEnabled(on);
            syncRows();
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
            syncRows();
        });
        rowLayout->addWidget(opacity);

        auto* item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, id);
        item->setSizeHint(row->sizeHint());
        m_list->setItemWidget(item, row);
    }
    m_updating = false;
    syncRows();
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
    QTimer::singleShot(0, this, [this] { rebuildRows(); });
}
