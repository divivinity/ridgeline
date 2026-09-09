#include "ScenarioTree.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

ScenarioTree::ScenarioTree(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto* title = new QLabel("Signal sources");
    title->setStyleSheet("color: #f1f5f9; font-size: 16px; font-weight: 600;");
    layout->addWidget(title);
    auto* help = new QLabel("Choose a placement tool, then click the terrain.");
    help->setWordWrap(true);
    help->setStyleSheet("color: #a8b7ca; font-size: 12px;");
    layout->addWidget(help);

    m_list = new QListWidget();
    m_list->setObjectName("SourceList");
    m_list->setAccessibleName("Signal sources");
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setStyleSheet(
        "QListWidget {"
        "  background: #0c1625;"
        "  border: 1px solid #223148;"
        "  border-radius: 9px;"
        "  color: #dbe7f5;"
        "  font-size: 13px;"
        "  padding: 5px;"
        "}"
        "QListWidget::item { padding: 9px 6px; border-radius: 5px; }"
        "QListWidget::item:selected {"
        "  background: #123c43;"
        "  color: #ecfeff;"
        "}"
        "QListWidget::item:hover {"
        "  background: #15243a;"
        "}"
    );
    connect(m_list, &QListWidget::itemSelectionChanged,
            this, &ScenarioTree::onSelectionChanged);
    layout->addWidget(m_list);

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(4);

    const QString btnStyle =
        "QPushButton {"
        "  background: #152238;"
        "  border: 1px solid #2a3b52;"
        "  border-radius: 7px;"
        "  color: #dbe7f5;"
        "  font-size: 12px;"
        "  padding: 7px 9px;"
        "}"
        "QPushButton:hover { background: #1c304b; border-color: #3b526e; }"
        "QPushButton:disabled { color: #475569; background: #101827; }";

    m_addBtn = new QPushButton("+ Emitter");
    m_addBtn->setStyleSheet(btnStyle);
    connect(m_addBtn, &QPushButton::clicked, this, &ScenarioTree::addEmitterRequested);
    btnRow->addWidget(m_addBtn);

    m_addJammerBtn = new QPushButton("+ Jammer");
    m_addJammerBtn->setStyleSheet(btnStyle);
    connect(m_addJammerBtn, &QPushButton::clicked, this, &ScenarioTree::addJammerRequested);
    btnRow->addWidget(m_addJammerBtn);

    m_removeBtn = new QPushButton("− Remove");
    m_removeBtn->setStyleSheet(btnStyle);
    m_removeBtn->setEnabled(false);
    connect(m_removeBtn, &QPushButton::clicked, this, &ScenarioTree::onRemoveClicked);
    btnRow->addWidget(m_removeBtn);

    layout->addLayout(btnRow);

    setMinimumWidth(230);
    setStyleSheet("ScenarioTree { background: #101a29; }");
}

void ScenarioTree::refresh(const std::vector<Antenna>& antennas) {
    const int prevRow = m_list->currentRow();
    m_list->blockSignals(true);
    m_list->clear();

    int antennaIdx = 1, jammerIdx = 1;
    for (const auto& a : antennas) {
        const QString label = a.isJammer
            ? QString("Jammer %1\n%2 MHz · %3 W")
                .arg(jammerIdx++).arg(a.frequencyMHz, 0, 'f', 0).arg(a.powerW, 0, 'f', 0)
            : QString("Emitter %1\n%2 MHz · %3 W")
                .arg(antennaIdx++).arg(a.frequencyMHz, 0, 'f', 0).arg(a.powerW, 0, 'f', 0);
        m_list->addItem(label);
        m_list->item(m_list->count() - 1)->setToolTip(label);
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
