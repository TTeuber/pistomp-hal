// settings_menu_mac.mm — native Cocoa backing for sim_menu.h (macOS simulator).
//
// Turns the app's plain-C++ sim_menu::Model into a real Mac Settings… experience:
// an app-menu item (⌘,) that opens an NSWindow of NSPopUpButtons for input /
// output device, sample rate, and buffer size. Each popup applies its choice
// live through Model::apply.
//
// Why this composes with SDL: by the time install() runs (the app calls it after
// lvgl_display::init brings SDL up), SDL has already created the NSApplication
// and its main menu. We just append to [NSApp mainMenu]. SDL's run loop pumps
// Cocoa events via nextEventMatchingMask every frame, so this auxiliary window's
// controls and the menu are serviced on the main thread alongside the LVGL UI —
// no separate event loop needed.

#import <Cocoa/Cocoa.h>

#include "sim_menu.h"

// ObjC controller: owns a heap copy of the Model and the lazily-built window.
@interface PSSettingsController : NSObject
@property(nonatomic, strong) NSWindow*      window;
@property(nonatomic, strong) NSPopUpButton* inputPopup;
@property(nonatomic, strong) NSPopUpButton* outputPopup;
@property(nonatomic, strong) NSPopUpButton* ratePopup;
@property(nonatomic, strong) NSPopUpButton* bufferPopup;
@end

@implementation PSSettingsController {
    sim_menu::Model _model;
}

- (instancetype)initWithModel:(const sim_menu::Model&)model {
    if ((self = [super init])) _model = model;
    return self;
}

// One label + popup row. `topY` is the row's top, measured from the window top;
// Cocoa is bottom-left origin so we flip against the content height.
- (NSPopUpButton*)addRow:(NSString*)label atTop:(CGFloat)topY in:(NSView*)content {
    const CGFloat H = content.frame.size.height;
    const CGFloat rowH = 26, labelW = 110, popupW = 230, pad = 20;
    const CGFloat y = H - topY - rowH;

    NSTextField* lbl = [[NSTextField alloc] initWithFrame:
        NSMakeRect(pad, y, labelW, rowH)];
    lbl.stringValue = label;
    lbl.editable = NO; lbl.bordered = NO; lbl.bezeled = NO;
    lbl.drawsBackground = NO;
    lbl.alignment = NSTextAlignmentRight;
    [content addSubview:lbl];

    NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:
        NSMakeRect(pad + labelW + 10, y - 2, popupW, rowH) pullsDown:NO];
    popup.target = self;
    popup.action = @selector(selectionChanged:);
    [content addSubview:popup];
    return popup;
}

- (void)buildWindow {
    const CGFloat W = 400, H = 200;
    NSRect frame = NSMakeRect(0, 0, W, H);
    self.window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self.window.title = @"Audio Settings";
    self.window.releasedWhenClosed = NO;   // we keep it; reopen reuses it
    [self.window center];

    NSView* content = self.window.contentView;
    self.inputPopup  = [self addRow:@"Input:"        atTop:24  in:content];
    self.outputPopup = [self addRow:@"Output:"       atTop:62  in:content];
    self.ratePopup   = [self addRow:@"Sample Rate:"  atTop:100 in:content];
    self.bufferPopup = [self addRow:@"Buffer Size:"  atTop:138 in:content];
}

// (Re)load every popup from the Model's live getters and select current values.
- (void)reload {
    [self fillStringPopup:self.inputPopup
                    items:(_model.inputs ? _model.inputs() : std::vector<std::string>{})
                  current:(_model.currentInput ? _model.currentInput() : std::string{})];
    [self fillStringPopup:self.outputPopup
                    items:(_model.outputs ? _model.outputs() : std::vector<std::string>{})
                  current:(_model.currentOutput ? _model.currentOutput() : std::string{})];
    [self fillIntPopup:self.ratePopup
                 items:(_model.rates ? _model.rates() : std::vector<int>{})
               current:(_model.currentRate ? _model.currentRate() : 0)];
    [self fillIntPopup:self.bufferPopup
                 items:(_model.buffers ? _model.buffers() : std::vector<int>{})
               current:(_model.currentBuffer ? _model.currentBuffer() : 0)];
}

- (void)fillStringPopup:(NSPopUpButton*)popup
                  items:(const std::vector<std::string>&)items
                current:(const std::string&)current {
    [popup removeAllItems];
    for (const auto& s : items)
        [popup addItemWithTitle:[NSString stringWithUTF8String:s.c_str()]];
    if (!current.empty())
        [popup selectItemWithTitle:[NSString stringWithUTF8String:current.c_str()]];
}

- (void)fillIntPopup:(NSPopUpButton*)popup
               items:(const std::vector<int>&)items
             current:(int)current {
    [popup removeAllItems];
    for (int v : items)
        [popup addItemWithTitle:[NSString stringWithFormat:@"%d", v]];
    if (current > 0)
        [popup selectItemWithTitle:[NSString stringWithFormat:@"%d", current]];
}

// Menu item target: build once, refresh, show.
- (void)openSettings:(id)sender {
    if (!self.window) [self buildWindow];
    [self reload];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

// Any popup changed: read all four and apply live. On failure, alert and revert
// the display to whatever the app actually has now.
- (void)selectionChanged:(id)sender {
    std::string in  = std::string(self.inputPopup.titleOfSelectedItem.UTF8String  ?: "");
    std::string out = std::string(self.outputPopup.titleOfSelectedItem.UTF8String ?: "");
    int rate   = self.ratePopup.titleOfSelectedItem.intValue;
    int buffer = self.bufferPopup.titleOfSelectedItem.intValue;

    bool ok = _model.apply ? _model.apply(in, out, rate, buffer) : false;
    if (!ok) {
        NSAlert* a = [[NSAlert alloc] init];
        a.messageText = @"Couldn't switch audio device";
        a.informativeText = @"The selected device or format couldn't be opened. "
                            @"Reverting to the previous setting.";
        [a addButtonWithTitle:@"OK"];
        [a runModal];
        [self reload];   // resync popups to the app's real (unchanged) state
    }
}
@end

namespace sim_menu {

void install(const Model& model) {
    @autoreleasepool {
        if (NSApp == nil) return;   // no Cocoa app (shouldn't happen post-SDL_Init)

        // Retained for the process lifetime; the menu/window hold no strong ref
        // back to it otherwise.
        static PSSettingsController* controller = nil;
        controller = [[PSSettingsController alloc] initWithModel:model];

        // The app menu is the first submenu of the main menu (the one named
        // after the app). AppKit/SDL already place the standard Settings… item
        // there (⌘,), but disabled — it has no target/action. Repurpose THAT
        // item rather than inserting our own, so we don't end up with a
        // duplicate; on macOS 13+ the item may read "Preferences…" internally
        // even though it shows as "Settings…", so match either.
        NSMenu* mainMenu = [NSApp mainMenu];
        if (mainMenu.numberOfItems == 0) return;
        NSMenu* appMenu = [[mainMenu itemAtIndex:0] submenu];
        if (appMenu == nil) return;

        NSMenuItem* settings = nil;
        for (NSMenuItem* it in appMenu.itemArray) {
            if ([it.title hasPrefix:@"Settings"] || [it.title hasPrefix:@"Preferences"]) {
                settings = it;
                break;
            }
        }
        if (settings) {
            // Wire the existing standard item. Its target responding to the
            // action is what flips it from grayed to enabled (autoenablesItems).
            settings.target = controller;
            settings.action = @selector(openSettings:);
            if (settings.keyEquivalent.length == 0) settings.keyEquivalent = @",";
        } else {
            // No standard item present — add one in the Mac-standard spot.
            NSMenuItem* item = [[NSMenuItem alloc]
                initWithTitle:@"Settings…"
                       action:@selector(openSettings:)
                keyEquivalent:@","];
            item.target = controller;
            [appMenu insertItem:item atIndex:1];
            [appMenu insertItem:[NSMenuItem separatorItem] atIndex:2];
        }
    }
}

} // namespace sim_menu
