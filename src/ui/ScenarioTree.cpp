#include "ScenarioTree.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

ScenarioTree::ScenarioTree(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* title = new QLabel("Scenario");
    title->setStyleSheet("color: rgba(255,255,255,0.5); font-family: monospace; font-size: 11px; padding: 2px 4px;");
    layout->addWidget(title);

    m_list = new QListWidget();
    m_list->setStyleSheet(
        "QListWidget {"
        "  background: rgba(0,0,0,0.25);"
        "  border: 1px solid rgba(255,255,255,0.08);"
        "  border-radius: 4px;"
        "  color: rgba(255,255,255,0.85);"
        "  font-family: monospace;"
        "  font-size: 12px;"
        "}"
        "QListWidget::item:selected {"
        "  background: rgba(79,195,247,0.25);"
        "  color: #4fc3f7;"
        "}"
        "QListWidget::item:hover {"
        "  background: rgba(255,255,255,0.05);"
        "}"
    );
    connect(m_list, &QListWidget::itemSelectionChanged,
            this, &ScenarioTree::onSelectionChanged);
    layout->addWidget(m_list);

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(4);

    const QString btnStyle =
        "QPushButton {"
        "  background: rgba(255,255,255,0.08);"
        "  border: 1px solid rgba(255,255,255,0.15);"
        "  border-radius: 4px;"
        "  color: rgba(255,255,255,0.75);"
        "  font-family: monospace;"
        "  font-size: 12px;"
        "  padding: 3px 10px;"
        "}"
        "QPushButton:hover { background: rgba(255,255,255,0.15); }";

    m_addBtn = new QPushButton("+ Add");
    m_addBtn->setStyleSheet(btnStyle);
    connect(m_addBtn, &QPushButton::clicked, this, &ScenarioTree::addRequested);
    btnRow->addWidget(m_addBtn);

    m_removeBtn = new QPushButton("− Remove");
    m_removeBtn->setStyleSheet(btnStyle);
    m_removeBtn->setEnabled(false);
    connect(m_removeBtn, &QPushButton::clicked, this, &ScenarioTree::onRemoveClicked);
    btnRow->addWidget(m_removeBtn);

    layout->addLayout(btnRow);

    setMinimumWidth(160);
    setMaximumWidth(260);
    setStyleSheet("background: rgba(15,25,35,0.85);");
}

void ScenarioTree::refresh(const std::vector<Antenna>& antennas) {
    const int prevRow = m_list->currentRow();
    m_list->blockSignals(true);
    m_list->clear();

    int antennaIdx = 1, jammerIdx = 1;
    for (const auto& a : antennas) {
        const QString label = a.isJammer
            ? QString("\U0001F534 Jammer %1").arg(jammerIdx++)
            : QString("\U0001F4E1 Antenna %1").arg(antennaIdx++);
        m_list->addItem(label);
    }

    // Restore selection if still valid.
    if (prevRow >= 0 && prevRow < m_list->count())
        m_list->setCurrentRow(prevRow);

    m_list->blockSignals(false);
    m_removeBtn->setEnabled(m_list->currentRow() >= 0);
}

int ScenarioTree::selectedIndex() const {
    return m_list->currentRow();
}

void ScenarioTree::selectRow(int index) {
    m_list->blockSignals(true);
    m_list->setCurrentRow(index);
    m_list->blockSignals(false);
    m_removeBtn->setEnabled(index >= 0 && index < m_list->count());
}

void ScenarioTree::onSelectionChanged() {
    const int row = m_list->currentRow();
    m_removeBtn->setEnabled(row >= 0);
    emit selectionChanged(row);
}

void ScenarioTree::onRemoveClicked() {
    const int row = m_list->currentRow();
    if (row >= 0) emit removeRequested(row);
}
