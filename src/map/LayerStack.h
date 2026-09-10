#pragma once
#include <QString>
#include <functional>
#include <vector>

enum class LayerKind { BaseRaster, TerrainDerived, Analysis };

struct MapLayer {
    QString id;
    QString displayName;
    LayerKind kind = LayerKind::Analysis;
    bool visible = false;
    float opacity = 1.0f;
};

// Ordered map layers, bottom first. Paint order is stack order. Deliberately
// free of Qt Widgets and OpenGL so it can be tested without a display.
class LayerStack {
public:
    struct State {
        QString id;
        bool visible = false;
        float opacity = 1.0f;
    };

    LayerStack();

    std::size_t size() const { return m_layers.size(); }
    const MapLayer& at(std::size_t index) const { return m_layers[index]; }
    const MapLayer* find(const QString& id) const;

    void setVisible(const QString& id, bool visible);
    void setOpacity(const QString& id, float opacity);
    void move(const QString& id, std::size_t toIndex);

    // Visible layers of one kind, in stack order.
    std::vector<MapLayer> visibleOfKind(LayerKind kind) const;

    std::vector<State> state() const;
    // Ordering follows the supplied list; ids absent from it keep their
    // relative order after those present. Unknown ids are ignored rather than
    // inserted, so a scenario written by a later version stays loadable.
    void applyState(const std::vector<State>& state);

    void setOnChanged(std::function<void()> callback) {
        m_onChanged = std::move(callback);
    }

private:
    MapLayer* mutableFind(const QString& id);
    void notify();

    std::vector<MapLayer> m_layers;
    std::function<void()> m_onChanged;
};
