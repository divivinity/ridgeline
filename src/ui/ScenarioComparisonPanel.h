#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;

struct ScenarioComparisonResult;

class ScenarioComparisonPanel final : public QWidget {
    Q_OBJECT
public:
    enum class State {
        NoBaseline,
        AwaitingCurrentResult,
        ComparisonReady,
        Incompatible
    };

    explicit ScenarioComparisonPanel(QWidget* parent = nullptr);

    // Use these methods as the comparison workflow advances. The availability
    // flag controls whether the current coverage result can be captured as a
    // new/replacement baseline.
    void showNoBaseline(bool currentResultAvailable = false);
    void showBaselineAwaitingCurrent(bool currentResultAvailable = false);
    void setComparison(const ScenarioComparisonResult& comparison);
    void setCurrentResultAvailable(bool available);

    State state() const { return m_state; }
    bool changesVisible() const;
    void setChangesVisible(bool visible);

signals:
    void setBaselineRequested();
    void replaceBaselineRequested();
    void clearComparisonRequested();
    void showChangesOnMapChanged(bool visible);

private:
    State m_state = State::NoBaseline;
    bool m_currentResultAvailable = false;

    QLabel* m_statusBadge = nullptr;
    QLabel* m_statusDetail = nullptr;
    QLabel* m_baselineService = nullptr;
    QLabel* m_currentService = nullptr;
    QLabel* m_netChange = nullptr;
    QLabel* m_gained = nullptr;
    QLabel* m_lost = nullptr;
    QLabel* m_retained = nullptr;
    QLabel* m_unchanged = nullptr;
    QLabel* m_desiredDelta = nullptr;
    QLabel* m_sinrDelta = nullptr;
    QLabel* m_fadeDelta = nullptr;
    QPushButton* m_setBaseline = nullptr;
    QPushButton* m_replaceBaseline = nullptr;
    QPushButton* m_clearComparison = nullptr;
    QCheckBox* m_showChanges = nullptr;

    void resetValues();
    void refreshControls();
    void setStatus(const QString& title, const QString& detail,
                   const QString& background, const QString& border,
                   const QString& foreground);
};

