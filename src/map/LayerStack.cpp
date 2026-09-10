#include "map/LayerStack.h"
#include <algorithm>

LayerStack::LayerStack() {
    m_layers = {
        {"satellite",  "Satellite",        LayerKind::BaseRaster,     true,  0.86f},
        {"usgs-topo",  "USGS topographic", LayerKind::BaseRaster,     false, 1.0f},
        {"hillshade",  "Hillshade",        LayerKind::TerrainDerived, false, 0.6f},
        {"coverage",   "Coverage",         LayerKind::Analysis,       true,  1.0f},
        {"comparison", "Scenario compare", LayerKind::Analysis,       false, 1.0f},
        {"los",        "Line of sight",    LayerKind::Analysis,       false, 1.0f},
        {"pattern",    "Antenna pattern",  LayerKind::Analysis,       false, 1.0f},
        {"jamming",    "Jamming range",    LayerKind::Analysis,       false, 1.0f},
        {"fresnel",    "Fresnel zone",     LayerKind::Analysis,       false, 1.0f},
    };
}

const MapLayer* LayerStack::find(const QString& id) const {
    const auto it = std::find_if(m_layers.begin(), m_layers.end(),
        [&id](const MapLayer& l) { return l.id == id; });
    return it == m_layers.end() ? nullptr : &*it;
}

MapLayer* LayerStack::mutableFind(const QString& id) {
    const auto it = std::find_if(m_layers.begin(), m_layers.end(),
        [&id](const MapLayer& l) { return l.id == id; });
    return it == m_layers.end() ? nullptr : &*it;
}

void LayerStack::notify() {
    // Copy first: a listener may register another while reacting.
    const auto listeners = m_listeners;
    for (const auto& listener : listeners)
        if (listener) listener();
}

void LayerStack::setVisible(const QString& id, bool visible) {
    MapLayer* layer = mutableFind(id);
    if (!layer || layer->visible == visible) return;
    layer->visible = visible;
    notify();
}

void LayerStack::setOpacity(const QString& id, float opacity) {
    MapLayer* layer = mutableFind(id);
    if (!layer) return;
    const float clamped = std::clamp(opacity, 0.0f, 1.0f);
    if (layer->opacity == clamped) return;
    layer->opacity = clamped;
    notify();
}

void LayerStack::move(const QString& id, std::size_t toIndex) {
    const auto it = std::find_if(m_layers.begin(), m_layers.end(),
        [&id](const MapLayer& l) { return l.id == id; });
    if (it == m_layers.end()) return;
    const MapLayer layer = *it;
    m_layers.erase(it);
    const std::size_t target = std::min(toIndex, m_layers.size());
    m_layers.insert(m_layers.begin() + static_cast<std::ptrdiff_t>(target), layer);
    notify();
}

std::vector<MapLayer> LayerStack::visibleOfKind(LayerKind kind) const {
    std::vector<MapLayer> result;
    for (const MapLayer& layer : m_layers)
        if (layer.kind == kind && layer.visible) result.push_back(layer);
    return result;
}

std::vector<LayerStack::State> LayerStack::state() const {
    std::vector<State> out;
    out.reserve(m_layers.size());
    for (const MapLayer& layer : m_layers)
        out.push_back({layer.id, layer.visible, layer.opacity});
    return out;
}

void LayerStack::applyState(const std::vector<State>& state) {
    if (state.empty()) return;
    std::vector<MapLayer> ordered;
    ordered.reserve(m_layers.size());
    for (const State& entry : state) {
        MapLayer* layer = mutableFind(entry.id);
        if (!layer) continue;   // unknown id: ignore, never insert
        layer->visible = entry.visible;
        layer->opacity = std::clamp(entry.opacity, 0.0f, 1.0f);
        ordered.push_back(*layer);
    }
    for (const MapLayer& layer : m_layers) {
        const bool placed = std::any_of(ordered.begin(), ordered.end(),
            [&layer](const MapLayer& l) { return l.id == layer.id; });
        if (!placed) ordered.push_back(layer);
    }
    m_layers = std::move(ordered);
    notify();
}
