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

// ── Step 3: Pump Cocoa's main queue (keep the process responsive) ─────
// macOS marks an app as "Application Not Responding" in the Dock if it
// does not respond to an Apple event (Cmd+Tab, Dock click, etc.) within
// ~5 s.  GLFW's glfwPollEvents only drains GLFW's own event queue — it
// does NOT touch Cocoa's main run loop.  Calling this once per frame
// from the render loop drains the Cocoa main queue so the watchdog
// sees the process as alive and clears the "Not Responding" badge.
extern "C" void pump_cocoa_main_queue_impl(void);
inline void pump_cocoa_main_queue() {
    pump_cocoa_main_queue_impl();
}

// ── Step 4: Install a real Cocoa menu bar (Quit, Cmd+Q) ──────────────
// Raylib drives its own event loop with glfwPollEvents, so Cocoa never
// installs the default application menu — meaning Cmd+Q has nothing to
// dispatch to and the app can't be quit through normal macOS channels.
// This installs a minimal menu bar with a Quit item (Cmd+Q) whose action
// sets a thread-safe quit flag the render loop polls each frame.  Safe
// to call once after `activate_macos_app_after_init()`.
extern "C" void install_macos_menu_impl(void);
inline void install_macos_menu() { install_macos_menu_impl(); }

// Returns true once after the user picks Quit from the menu or hits
// Cmd+Q.  Resets on `clear_macos_quit_request()` so a single request
// fires only once even if checked from multiple sites.
extern "C" bool macos_quit_requested_impl(void);
inline bool macos_quit_requested() { return macos_quit_requested_impl(); }

extern "C" void clear_macos_quit_request_impl(void);
inline void clear_macos_quit_request() { clear_macos_quit_request_impl(); }

#else
inline void activate_macos_app() {}
inline void activate_macos_app_after_init() {}
inline void pump_cocoa_main_queue() {}
inline void install_macos_menu() {}
inline bool macos_quit_requested() { return false; }
inline void clear_macos_quit_request() {}
#endif
