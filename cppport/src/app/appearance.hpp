#pragma once

// Dark mode (workspace.enableDarkMode): the application in Fusion with a dark
// palette; off, the platform's own style and palette as they were at start.

namespace gs::app {

void applyDarkMode(bool dark);
bool darkModeApplied();

}  // namespace gs::app
