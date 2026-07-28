#pragma once

// macOS helpers to make a terminal-launched binary display a GUI window.
//
// macOS treats command-line tools as background processes: they have no
// WindowServer connection, so GLFW/Raylib can create an NSWindow but it
// will never become visible.  Two steps are needed:
//
//   1. TransformProcessType (before InitWindow)
//        Changes the process type from background to foreground so that
//        the process CAN connect to the WindowServer.
//
//   2. [NSApp activateIgnoringOtherApps:] (after InitWindow)
//        Brings the application to front and makes the NSWindow the key
//        window.  Without this call the window is created but never
//        ordered forward even though the process has the right type.
//
// Both steps are safe when the binary is launched through `open ctf.app`
// — they become no-ops.  Keep calling them unconditionally so the direct
// binary launch path (debugging, CI) also works.

#ifdef __APPLE__
#include <cstdint>

// ── Step 1: TransformProcessType ──────────────────────────────────────
// TransformProcessType lives in ApplicationServices.framework, not the
// Cocoa umbrella pulled in by Raylib.  Link `-framework ApplicationServices`
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

// ── Step 2: Activate the application (bring windows to front) ─────────
// Must be called AFTER InitWindow() because NSApp (the shared NSApplication
// instance) is created by GLFW during InitWindow and does not exist before.
// Implemented in app_activation.mm using real Objective-C syntax so the
// msgSend ABI is correct on ARM64 (Apple Silicon).
extern "C" void activate_macos_app_after_init_impl(void);
inline void activate_macos_app_after_init() {
    activate_macos_app_after_init_impl();
}

#else
inline void activate_macos_app() {}
inline void activate_macos_app_after_init() {}
#endif
