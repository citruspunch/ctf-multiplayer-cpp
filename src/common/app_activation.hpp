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
#include <objc/message.h>
#include <objc/runtime.h>

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
// Uses the Objective-C runtime directly (no .mm file needed).
inline void activate_macos_app_after_init() {
    // [NSApp activateIgnoringOtherApps:YES]
    // Note: objc_getClass returns Class (objc_class*) which is NOT the
    // same type as id (objc_object*) on the modern runtime, so we need
    // a double-cast via void* to bypass the type-system check.
    SEL sharedAppSel = sel_registerName("sharedApplication");
    id (*sendId)(id, SEL) = (id (*)(id, SEL))objc_msgSend;
    id app = sendId((id)(void*)objc_getClass("NSApplication"), sharedAppSel);
    if (app) {
        SEL activateSel = sel_registerName("activateIgnoringOtherApps:");
        void (*sendVoid)(id, SEL, BOOL) =
            (void (*)(id, SEL, BOOL))objc_msgSend;
        sendVoid(app, activateSel, YES);
    }
}

#else
inline void activate_macos_app() {}
inline void activate_macos_app_after_init() {}
#endif
