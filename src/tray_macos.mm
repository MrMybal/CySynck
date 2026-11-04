#include "desktop_support.hpp"
#import <AppKit/AppKit.h>

@interface CySynckTrayTarget : NSObject
@property(copy) void (^handler)(NSInteger);
- (void)activate:(id)sender;
@end
@implementation CySynckTrayTarget
- (void)activate:(id)sender {
    if (self.handler)
        self.handler([sender tag]);
}
@end

namespace cysynck::desktop {
static NSString *native(const std::string &text) {
    return [[NSString alloc] initWithBytes:text.data()
                                    length:text.size()
                                  encoding:NSUTF8StringEncoding];
}
struct Tray::Impl {
    NSStatusItem *item = nil;
    CySynckTrayTarget *target = nil;
    NSMenu *menu = nil;
    NSMenuItem *application_quit = nil;
    std::deque<TrayAction> actions;
    TrayLabels last;
    explicit Impl(const TrayLabels &labels) {
        item = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
        if (!item)
            return;
        if (@available(macOS 11.0, *)) {
            item.button.image = [NSImage imageWithSystemSymbolName:@"arrow.triangle.2.circlepath"
                                          accessibilityDescription:@"CySynck"];
            [item.button.image setTemplate:YES];
        }
        if (!item.button.image)
            item.button.title = @"CS";
        target = [[CySynckTrayTarget alloc] init];
        Impl *owner = this;
        target.handler = ^(NSInteger command) {
          if (command >= 1 && command <= 4 && owner->actions.size() < 32)
              owner->actions.push_back(static_cast<TrayAction>(command));
        };
        auto application_menu =
            NSApp.mainMenu.numberOfItems > 0 ? [NSApp.mainMenu itemAtIndex:0].submenu : nil;
        for (NSMenuItem *entry in application_menu.itemArray) {
            if (entry.action == @selector(terminate:)) {
                application_quit = entry;
                entry.target = target;
                entry.action = @selector(activate:);
                entry.tag = static_cast<NSInteger>(TrayAction::quit);
            }
        }
        menu = [[NSMenu alloc] initWithTitle:@"CySynck"];
        menu.autoenablesItems = NO;
        for (NSInteger i = 1; i <= 4; ++i) {
            if (i == 4)
                [menu addItem:[NSMenuItem separatorItem]];
            auto entry = [[NSMenuItem alloc] initWithTitle:@""
                                                    action:@selector(activate:)
                                             keyEquivalent:i == 4 ? @"q" : @""];
            entry.target = target;
            entry.tag = i;
            [menu addItem:entry];
        }
        item.menu = menu;
        update(labels);
    }
    void update(const TrayLabels &labels) {
        if (!item)
            return;
        if (last == labels)
            return;
        last = labels;
        std::array<std::string, 4> values{labels.show, labels.open_web, labels.backup_all,
                                          labels.quit};
        for (NSInteger i = 1; i <= 4; ++i)
            [menu itemWithTag:i].title = native(values[i - 1]);
        item.button.toolTip = native(labels.tooltip);
    }
    ~Impl() {
        if (application_quit) {
            application_quit.target = nil;
            application_quit.action = @selector(terminate:);
            application_quit.tag = 0;
        }
        target.handler = nil;
        if (item)
            [[NSStatusBar systemStatusBar] removeStatusItem:item];
    }
};
Tray::Tray(const TrayLabels &labels) : impl_(std::make_unique<Impl>(labels)) {}
Tray::~Tray() = default;
bool Tray::available() const { return impl_->item != nil && impl_->item.visible; }
std::string Tray::reason() const { return "The macOS status bar is unavailable"; }
void Tray::update(const TrayLabels &labels) { impl_->update(labels); }
void Tray::pump() {}
bool Tray::next(TrayAction &action) {
    if (impl_->actions.empty())
        return false;
    action = impl_->actions.front();
    impl_->actions.pop_front();
    return true;
}
} // namespace cysynck::desktop
