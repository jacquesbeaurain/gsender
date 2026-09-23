#pragma once

// Power saving (workspace.powerSaving): unless it is allowed, the display
// is kept from sleeping while the application runs (Electron's
// powerSaveBlocker 'prevent-display-sleep'). Windows only; elsewhere a no-op.

namespace gs::app {

void setDisplaySleepAllowed(bool allowed);

}  // namespace gs::app
