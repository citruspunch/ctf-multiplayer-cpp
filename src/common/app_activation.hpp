#pragma once

// macOS helper: promote a terminal-launched process to a foreground GUI
// application.  When you run a binary directly from the terminal (i.e.
// without going through `open ctf.app`), macOS treats it as a background
// tool: it has no connection to the WindowServer, so GLFW/Raylib can
// create a window but it never becomes visible.  Calling
// `TransformProcessType` flips the process into foreground-application
// mode, which is what fixes the window.
//
// This function MUST be called BEFORE `InitWindow()` so that the
// process is already a foreground GUI app when the NSWindow is created.
//
// When the binary is launched through `open ctf.app`, the bundle path
// already gives the process foreground status, so this becomes a no-op
// in practice — but calling it unconditionally is safe and keeps the
// direct-binary launch path working (debugging, CI, headless smoke
// tests, etc.).

#ifdef __APPLE__
#include <cstdint>

// Carbon HIToolbox declarations.  TransformProcessType lives in
// ApplicationServices.framework, NOT in the Cocoa umbrella pulled in
// by Raylib, so the executable must link `-framework ApplicationServices`
// (see `src/common/CMakeLists.txt`).
extern "C" {

struct ProcessSerialNumber {
    std::uint32_t highLongOfPSN;
    std::uint32_t lowLongOfPSN;
};
typedef struct ProcessSerialNumber ProcessSerialNumber;
typedef std::int32_t OSStatus;

OSStatus GetCurrentProcess(ProcessSerialNumber* psn);
OSStatus TransformProcessType(const ProcessSerialNumber* psn,
                              std::uint32_t type);

}  // extern "C"

constexpr std::uint32_t kProcessTransformToForegroundApplication = 1;

inline void activate_macos_app() {
    ProcessSerialNumber psn = {0, 0};
    if (GetCurrentProcess(&psn) == 0) {
        TransformProcessType(&psn,
                             kProcessTransformToForegroundApplication);
    }
}

#else
inline void activate_macos_app() {}
#endif
