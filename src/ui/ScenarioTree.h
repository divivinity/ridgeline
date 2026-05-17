#pragma once
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <vector>
#include "sim/Types.h"

// Left-dock panel that lists all antennas in the scene.
// MainWindow calls refresh() whenever the antenna list changes.
// Signals propagate user actions back to MainWindow.
class ScenarioTree : public QWidget {
    Q_OBJECT
public:
    explicit ScenarioTree(QWidget* parent = nullptr);

    // Rebuild the list from the current antenna vector.
    // Tries to preserve the selected row; clears selection if list shrinks.
    void refresh(const std::vector<Antenna>& antennas);

    // Returns the currently selected antenna index, or -1 if none.
    int selectedIndex() const;

signals:
    void selectionChanged(int index);   // -1 = deselected
    void addRequested();                // user clicked Add
    void removeRequested(int index);    // user clicked Remove; index is valid

private:
    QListWidget* m_list      = nullptr;
    QPushButton* m_addBtn    = nullptr;
    QPushButton* m_removeBtn = nullptr;

    void onSelectionChanged();
    void onRemoveClicked();
};
