// app_activation.mm — Objective-C++ implementation of macOS GUI activation.
//
// When the binary is launched directly from a terminal (not via LaunchServices
// from a .app bundle), the NSApplication run loop is never started properly:
// NSApp exists but is in a pre-launch state, the activation policy is
// "Prohibited" (background-only), and the window is created but never shown
// to the user.  This file drives the NSApplication into its normal
// "Foreground GUI app" state so the GLFW-created NSWindow is actually
// visible and brought to the front.
//
// See app_activation.hpp for the C-linkage declarations.

#ifdef __APPLE__
#import <Cocoa/Cocoa.h>

extern "C" void activate_macos_app_after_init_impl(void) {
    // Ensure NSApplication is initialised.  GLFW calls
    // [NSApplication sharedApplication] in InitWindow, so NSApp exists
    // by the time this runs, but the run loop hasn't started yet.
    if (NSApp == nil) {
        [NSApplication sharedApplication];
    }

    // Step 1: explicitly set the activation policy to "Regular" (a
    // foreground GUI app with a Dock icon and a menu bar).  This is the
    // policy LaunchServices assigns to .app bundles automatically.
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    // Step 2: finish the launch sequence.  Normally this is called by
    // [NSApp run] in -[NSApplicationMain main] — but GLFW manages its
    // own event loop with glfwPollEvents and never calls -[NSApp run],
    // so we have to drive the launch sequence manually.
    [NSApp finishLaunching];

    // Step 3: bring the application to the front.  This is the
    // programmatic equivalent of clicking the Dock icon.
    [NSApp activateIgnoringOtherApps:YES];

    // Step 4: for each on-screen window belonging to this app, force
    // it to the front.  GLFW hides the NSWindow on the Cocoa backend
    // until we tell it otherwise, and orderFront: alone doesn't always
    // cut through the activation stack on a freshly-launched binary.
    for (NSWindow* window in [NSApp windows]) {
        // Make sure the window is on the active space and visible.
        [window setIsVisible:YES];
        // orderFrontRegardless: bypasses the normal focus validation
        // and forces the window to be ordered to the front.
        [window orderFrontRegardless];
        // makeKeyAndOrderFront: also gives it keyboard focus.
        [window makeKeyAndOrderFront:nil];
    }
}

// Pump pending NSEvents from Cocoa's main queue so macOS sees the
// process as responsive (clears the "Application Not Responding"
// badge in the Dock).  GLFW's glfwPollEvents only drains GLFW's own
// event queue; it does NOT drain the Cocoa main run loop, so without
// this the watchdog can time out during heavy first-frame work
// (shader compilation, font loading, etc.) and the badge stays stuck.
extern "C" void pump_cocoa_main_queue_impl(void) {
    NSEvent* event = nil;
    // Drain up to 20 events per call so we never starve the render
    // loop.  Using NSDefaultRunLoopMode means the event goes through
    // the normal AppKit handling (NSWindow mouse-tracking, Cmd+Tab,
    // Dock clicks, etc.).
    for (int i = 0; i < 20; ++i) {
        event = [NSApp nextEventMatchingMask:NSAnyEventMask
                                   untilDate:[NSDate distantPast]
                                      inMode:NSDefaultRunLoopMode
                                     dequeue:YES];
        if (event == nil) break;
        [NSApp sendEvent:event];
    }
}

// ── Menu bar + Quit handler ─────────────────────────────────────────────
//
// Raylib's render loop never calls [NSApp run], so Cocoa never installs
// the default application menu bar.  Without one, the user has no
// visible "Quit" item and Cmd+Q has nothing to dispatch to — the app
// becomes unkillable from the menu and must be force-quit.  Installing
// a real menu bar (even minimal) fixes both: the menu item is visible
// in the menu bar, Cmd+Q matches its key equivalent, and the action
// callback is invoked by AppKit when the user triggers it (because
// sendEvent: dispatches menu key equivalents through the responder
// chain).
//
// We install a single-item menu bar:
//   ▸ [App Name] ▸ Quit [App Name]   (⌘Q)
//
// The Quit action sets a std::atomic<bool> that the C++ render loop
// polls each frame, so the loop can break cleanly and call CloseWindow()
// (which runs the Raylib destructors) instead of exit(0).

#import <atomic>

static std::atomic<bool> g_quit_requested{false};

@interface CTFQuitHandler : NSObject
+ (void)requestQuit:(id)sender;
@end

@implementation CTFQuitHandler
+ (void)requestQuit:(id)sender {
    (void)sender;
    g_quit_requested.store(true);
}
@end

extern "C" void install_macos_menu_impl(void) {
    if (NSApp == nil) return;

    // If a menu bar is already installed (e.g. by a previous call),
    // tear it down so we don't accumulate duplicates on re-init.
    if ([NSApp mainMenu] != nil) {
        [NSApp setMainMenu:nil];
    }

    NSString* appName = [[NSProcessInfo processInfo] processName];
    if (appName == nil || appName.length == 0) {
        appName = @"CTF";
    }

    // Build the menu bar.
    NSMenu* menubar = [[NSMenu alloc] init];

    // Application menu (the bold one with the process name).
    NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
    NSMenu* appMenu = [[NSMenu alloc] init];

    // Quit item — Cmd+Q.
    NSString* quitTitle = [@"Quit " stringByAppendingString:appName];
    NSMenuItem* quitItem = [[NSMenuItem alloc] initWithTitle:quitTitle
                                                      action:@selector(requestQuit:)
                                               keyEquivalent:@"q"];
    [quitItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
    [quitItem setTarget:[CTFQuitHandler class]];
    [appMenu addItem:quitItem];

    [appMenuItem setSubmenu:appMenu];
    [menubar addItem:appMenuItem];

    [NSApp setMainMenu:menubar];
}

extern "C" bool macos_quit_requested_impl(void) {
    return g_quit_requested.load();
}

extern "C" void clear_macos_quit_request_impl(void) {
    g_quit_requested.store(false);
}
#endif
