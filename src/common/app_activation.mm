// app_activation.mm — Objective-C++ implementation of macOS GUI activation.
//
// This file uses real Objective-C syntax (not runtime objc_msgSend casts)
// so the calls are ABI-safe on ARM64 (Apple Silicon).  It is compiled as
// Objective-C++ (.mm) so it can #import <Cocoa/Cocoa.h> and call
// [NSApp activateIgnoringOtherApps:YES] directly.
//
// See app_activation.hpp for the C-linkage declarations.

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>

extern "C" void activate_macos_app_after_init_impl(void) {
    // [NSApp activateIgnoringOtherApps:YES]
    // NSApp is a global variable set by the Application Kit.
    if (NSApp) {
        [NSApp activateIgnoringOtherApps:YES];
    }
}
#endif
