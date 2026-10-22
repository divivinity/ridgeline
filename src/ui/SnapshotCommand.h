#pragma once
#include <QDateTime>
#include <QString>
#include <QUndoCommand>

#include <functional>
#include <utility>

// An undo step that stores a whole editable state before and after, plus the
// selection that went with each.
//
// Whole-state snapshots rather than per-field deltas: a list of sources or of
// vegetation areas is small, and restoring the exact prior list cannot drift
// the way replaying inverse operations can after an index shifts.
//
// Header-only and free of MainWindow, so its merge rules and the order in which
// mixed histories undo are tested without a display.
template <typename State>
class SnapshotCommand final : public QUndoCommand {
public:
    using Apply = std::function<void(const State&, int)>;

    SnapshotCommand(State before, int selectedBefore,
                    State after, int selectedAfter,
                    QString description, int mergeId, Apply apply)
        : QUndoCommand(std::move(description)),
          m_before(std::move(before)),
          m_after(std::move(after)),
          m_selectedBefore(selectedBefore),
          m_selectedAfter(selectedAfter),
          m_mergeId(mergeId),
          m_timestampMs(QDateTime::currentMSecsSinceEpoch()),
          m_apply(std::move(apply)) {}

    void undo() override { m_apply(m_before, m_selectedBefore); }
    void redo() override { m_apply(m_after, m_selectedAfter); }
    int id() const override { return m_mergeId; }

    // Merges only with another snapshot of the same state type, the same
    // non-negative id, within 900 ms - a run of spin-box steps becomes one undo.
    // The type check means a vegetation edit can never fold into a source edit
    // that happens to share an id.
    bool mergeWith(const QUndoCommand* other) override {
        const auto* newer = dynamic_cast<const SnapshotCommand<State>*>(other);
        if (!newer || m_mergeId < 0 || newer->m_mergeId != m_mergeId ||
            newer->m_timestampMs - m_timestampMs > 900) {
            return false;
        }
        m_after = newer->m_after;
        m_selectedAfter = newer->m_selectedAfter;
        m_timestampMs = newer->m_timestampMs;
        return true;
    }

private:
    State m_before;
    State m_after;
    int m_selectedBefore = -1;
    int m_selectedAfter = -1;
    int m_mergeId = -1;
    qint64 m_timestampMs = 0;
    Apply m_apply;
};
