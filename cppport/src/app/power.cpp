#include "power.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace gs::app {

void setDisplaySleepAllowed(bool allowed) {
#ifdef _WIN32
    // Held for the thread's life (the UI thread lives as long as the app).
    SetThreadExecutionState(allowed ? ES_CONTINUOUS : ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
#else
    (void)allowed;
#endif
}

}  // namespace gs::app
