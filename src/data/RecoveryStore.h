#pragma once
#include <QDateTime>
#include <QString>
#include <string>

struct ScenarioDoc;

// Crash and close-without-saving protection.
//
// A scenario that has never been saved has no file to fall back on, so an
// unsaved workspace is only as durable as the process holding it. This keeps a
// copy outside the process and hands it back on the next start.
//
// The copy is written through ScenarioIO, so it is the same format and inherits
// the same atomic replacement: an interrupted autosave cannot leave a truncated
// recovery file that then destroys the work it was meant to protect.
namespace RecoveryStore {

// Under the application's data location, so it survives a crash but is not
// mistaken for a document the user chose to keep. Honours RIDGELINE_STATE_DIR
// so tests never touch the real profile.
QString path();

// True when a recovery copy is waiting. Only meaningful at startup: during a
// session the running window is the authority, not the file.
bool exists();

// Wall-clock of the waiting copy, for telling the user how old it is. Invalid
// when nothing is waiting.
QDateTime savedAt();

// Returns false rather than throwing: autosave runs on a timer behind the
// user's work and must never interrupt it with a dialog.
bool write(const ScenarioDoc& doc);

// Throws like any other scenario read, since the caller is acting on an
// explicit "recover" choice and a failure needs to be visible.
ScenarioDoc read();

// After a successful save, an explicit discard, or a clean exit. A stale copy
// would offer to restore work the user already dealt with.
void clear();

} // namespace RecoveryStore
