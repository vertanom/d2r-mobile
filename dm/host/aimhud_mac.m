// Mac App「手柄 → 瞄准指示…」窗口：调手柄瞄准指示（ios/Sources/dm_aim_hud.m）的样子。颜色 + 染色强度、不透明度、圆环粗细、大小，拖动就写自定义目录的 aim_hud.txt，
// 指示半秒内按新值重画（边玩边调）。圆环 / 尖的图：「载入…」把选的 PNG 拷成自定义目录的 aim_ring.png / aim_tip.png（优先于包里原版表情轮盘取来的那两张），
// 「导出当前图…」把正在用的两张原图存到选的文件夹（拿去给别的 AI 改画风用），「用原版图」删掉自定义的两张。
// Mac App "Controller -> Aim Indicator..." window: tunes the look of the controller aim indicator (ios/Sources/dm_aim_hud.m). Color + tint strength, opacity, ring thickness, size; dragging writes aim_hud.txt in the custom directory
// and the indicator redraws with the new values within half a second (tune while playing). Ring / tip images: "Load..." copies the chosen PNG as aim_ring.png / aim_tip.png in the custom directory (winning over the two taken from the original emote wheel in the package),
// "Export current images..." saves the two source images in use to a chosen folder (to restyle them with another AI), "Use original art" deletes the two custom ones.
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "dm.h"

static NSString *T(const char *k) { const char *dm_ui(const char *key); return @(dm_ui(k)); }
static NSString *cpath(NSString *name) { const char *dm_custom_dir(void); const char *d = dm_custom_dir(); return d ? [@(d) stringByAppendingPathComponent:name] : nil; }
static NSString *bpath(NSString *name) { return [[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"aim"] stringByAppendingPathComponent:name]; }
static NSString *in_use(NSString *name) { NSString *c = cpath(name); return c && [NSFileManager.defaultManager fileExistsAtPath:c] ? c : [NSFileManager.defaultManager fileExistsAtPath:bpath(name)] ? bpath(name) : nil; }

// 滑块：最小、最大、缺省（缺省与 dm_aim_hud.m 的 g_look 一致） | sliders: min, max, default (defaults match g_look in dm_aim_hud.m)
static const struct { const char *key, *label; double lo, hi, def; } k_sl[] = {
    { "tint", "dmAimTint", 0, 1, 0 }, { "alpha", "dmAimAlpha", 0.05, 1, 0.85 }, { "thick", "dmAimThick", 0.3, 4, 2 }, { "size", "dmAimSize", 0.5, 2.5, 1 } };
enum { NSL = sizeof k_sl / sizeof *k_sl };

@interface DmAimPanel : NSWindowController <NSWindowDelegate>
@end
@implementation DmAimPanel { NSColorWell *_color; NSSlider *_sl[NSL]; NSTextField *_val[NSL], *_art; }
- (void)save {
    NSColor *c = [_color.color colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    NSMutableString *s = [NSMutableString stringWithString:@"# D2R Mobile: Controller > Aim Indicator… (aim_hud.txt)\n"];
    [s appendFormat:@"color=%02x%02x%02x\n", (int)lround(c.redComponent * 255), (int)lround(c.greenComponent * 255), (int)lround(c.blueComponent * 255)];
    for (int i = 0; i < NSL; i++) { [s appendFormat:@"%s=%.2f\n", k_sl[i].key, _sl[i].doubleValue]; _val[i].stringValue = [NSString stringWithFormat:@"%.2f", _sl[i].doubleValue]; }
    NSString *p = cpath(@"aim_hud.txt"); if (p) [s writeToFile:p atomically:YES encoding:NSUTF8StringEncoding error:nil];
}
- (void)changed:(id)s { (void)s; [self save]; }
- (void)showArt {
    NSString *r = in_use(@"aim_ring.png"), *t = in_use(@"aim_tip.png"); NSString *(^w)(NSString *) = ^NSString *(NSString *p) { return !p ? T("dmAimArtNone") : [p hasPrefix:NSBundle.mainBundle.resourcePath] ? T("dmAimArtGame") : T("dmAimArtCustom"); };
    _art.stringValue = [NSString stringWithFormat:@"%@: %@    %@: %@", T("dmAimRing"), w(r), T("dmAimTip"), w(t)];
}
- (void)load:(NSButton *)b {
    NSString *name = b.tag ? @"aim_tip.png" : @"aim_ring.png", *dst = cpath(name); if (!dst) return;
    NSOpenPanel *o = [NSOpenPanel openPanel]; o.allowedContentTypes = @[UTTypePNG]; o.message = T("dmAimLoadMsg");
    if ([o runModal] != NSModalResponseOK) return;
    [NSFileManager.defaultManager removeItemAtPath:dst error:nil]; NSError *e = nil;
    if (![NSFileManager.defaultManager copyItemAtPath:o.URL.path toPath:dst error:&e]) LOG("【红】瞄准指示窗口：拷 %s 失败：%s", o.URL.path.UTF8String, e.localizedDescription.UTF8String ?: "?");
    else LOG("瞄准指示窗口：载入 %s → %s", o.URL.path.UTF8String, name.UTF8String);
    [self showArt];
}
- (void)export:(id)s {
    (void)s; NSOpenPanel *o = [NSOpenPanel openPanel]; o.canChooseFiles = NO; o.canChooseDirectories = YES; o.canCreateDirectories = YES; o.prompt = T("dmAimExport");
    if ([o runModal] != NSModalResponseOK) return;
    for (NSString *n in @[@"aim_ring.png", @"aim_tip.png"]) { NSString *src = in_use(n), *dst = [o.URL.path stringByAppendingPathComponent:n]; if (!src) continue;
        [NSFileManager.defaultManager removeItemAtPath:dst error:nil]; [NSFileManager.defaultManager copyItemAtPath:src toPath:dst error:nil]; }
    [NSWorkspace.sharedWorkspace openURL:o.URL];
}
- (void)original:(id)s { (void)s; for (NSString *n in @[@"aim_ring.png", @"aim_tip.png"]) { NSString *p = cpath(n); if (p) [NSFileManager.defaultManager removeItemAtPath:p error:nil]; } [self showArt]; }
- (void)defaults:(id)s { (void)s; _color.color = [NSColor colorWithSRGBRed:199 / 255.0 green:179 / 255.0 blue:119 / 255.0 alpha:1]; for (int i = 0; i < NSL; i++) _sl[i].doubleValue = k_sl[i].def; [self save]; }
- (void)folder:(id)s { (void)s; NSString *p = cpath(@""); if (p) [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:p]]; }
- (instancetype)init {
    NSPanel *w = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 520, 360) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
    if (!(self = [super initWithWindow:w])) return nil;
    w.title = [T("dmAimTitle") stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"…."]]; w.delegate = self; w.releasedWhenClosed = NO; w.floatingPanel = YES;
    NSTextField *intro = [NSTextField wrappingLabelWithString:T("dmAimIntro")]; intro.preferredMaxLayoutWidth = 480;
    NSGridView *g = [NSGridView gridViewWithNumberOfColumns:3 rows:0]; g.rowSpacing = 8; g.columnSpacing = 10; g.rowAlignment = NSGridRowAlignmentFirstBaseline; [g columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    _color = [NSColorWell new]; _color.target = self; _color.action = @selector(changed:); [_color.widthAnchor constraintEqualToConstant:60].active = YES;
    [g addRowWithViews:@[[NSTextField labelWithString:T("dmAimColor")], _color, NSGridCell.emptyContentView]];
    for (int i = 0; i < NSL; i++) {
        _sl[i] = [NSSlider sliderWithValue:k_sl[i].def minValue:k_sl[i].lo maxValue:k_sl[i].hi target:self action:@selector(changed:)]; _sl[i].continuous = YES; [_sl[i].widthAnchor constraintEqualToConstant:260].active = YES;
        _val[i] = [NSTextField labelWithString:@""]; _val[i].font = [NSFont monospacedDigitSystemFontOfSize:NSFont.systemFontSize weight:NSFontWeightRegular];
        [g addRowWithViews:@[[NSTextField labelWithString:T(k_sl[i].label)], _sl[i], _val[i]]]; }
    _art = [NSTextField labelWithString:@""]; _art.textColor = NSColor.secondaryLabelColor;
    NSButton *(^btn)(const char *, SEL, NSInteger) = ^NSButton *(const char *t, SEL a, NSInteger tag) { NSButton *b = [NSButton buttonWithTitle:T(t) target:self action:a]; b.tag = tag; return b; };
    NSStackView *art = [NSStackView stackViewWithViews:@[btn("dmAimLoadRing", @selector(load:), 0), btn("dmAimLoadTip", @selector(load:), 1), btn("dmAimExport", @selector(export:), 0), btn("dmAimOriginal", @selector(original:), 0)]];
    NSButton *close = btn("dmAppOK", @selector(close), 0); close.keyEquivalent = @"\r";
    NSStackView *bot = [NSStackView stackViewWithViews:@[btn("dmAimDefaults", @selector(defaults:), 0), btn("dmAimFolder", @selector(folder:), 0), [NSView new], close]];
    NSStackView *all = [NSStackView stackViewWithViews:@[intro, g, _art, art, bot]]; all.orientation = NSUserInterfaceLayoutOrientationVertical; all.alignment = NSLayoutAttributeLeading; all.spacing = 14;
    all.edgeInsets = NSEdgeInsetsMake(18, 20, 18, 20); all.translatesAutoresizingMaskIntoConstraints = NO; [w.contentView addSubview:all];
    [NSLayoutConstraint activateConstraints:@[[all.leadingAnchor constraintEqualToAnchor:w.contentView.leadingAnchor], [all.trailingAnchor constraintEqualToAnchor:w.contentView.trailingAnchor],
        [all.topAnchor constraintEqualToAnchor:w.contentView.topAnchor], [all.bottomAnchor constraintEqualToAnchor:w.contentView.bottomAnchor], [bot.widthAnchor constraintEqualToAnchor:all.widthAnchor constant:-40]]];
    return self;
}
// 打开时按 aim_hud.txt 现状摆（没有的项用缺省） | on opening, lay out from aim_hud.txt as it is now (missing items use the defaults)
- (void)loadFile {
    _color.color = [NSColor colorWithSRGBRed:199 / 255.0 green:179 / 255.0 blue:119 / 255.0 alpha:1]; for (int i = 0; i < NSL; i++) _sl[i].doubleValue = k_sl[i].def;
    NSString *p = cpath(@"aim_hud.txt"), *t = p ? [NSString stringWithContentsOfFile:p encoding:NSUTF8StringEncoding error:nil] : nil;
    for (NSString *line in [t componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet]) {
        NSArray *kv = [line componentsSeparatedByString:@"="]; if (kv.count != 2 || [line hasPrefix:@"#"]) continue;
        if ([kv[0] isEqualToString:@"color"]) { unsigned x = 0; if ([[NSScanner scannerWithString:kv[1]] scanHexInt:&x]) _color.color = [NSColor colorWithSRGBRed:(x >> 16 & 255) / 255.0 green:(x >> 8 & 255) / 255.0 blue:(x & 255) / 255.0 alpha:1]; }
        for (int i = 0; i < NSL; i++) if ([kv[0] isEqualToString:@(k_sl[i].key)]) _sl[i].doubleValue = [kv[1] doubleValue]; }
    for (int i = 0; i < NSL; i++) _val[i].stringValue = [NSString stringWithFormat:@"%.2f", _sl[i].doubleValue];
    [self showArt];
}
- (void)windowWillClose:(NSNotification *)n { (void)n; if (NSColorPanel.sharedColorPanelExists) [NSColorPanel.sharedColorPanel close]; }
@end

void dm_aimhud_show(NSWindow *parent) {
    static DmAimPanel *panel; if (!panel) panel = [DmAimPanel new];
    [panel loadFile];
    if (!panel.window.isVisible) { NSRect pf = parent ? parent.frame : NSScreen.mainScreen.visibleFrame, f = panel.window.frame;
        [panel.window setFrameOrigin:NSMakePoint(NSMaxX(pf) - f.size.width - 20, NSMaxY(pf) - f.size.height - 40)]; }   // 靠右上，不挡画面中间的人物 | top right, so it does not cover the character in the middle
    [panel showWindow:nil]; [panel.window makeKeyAndOrderFront:nil];
}
