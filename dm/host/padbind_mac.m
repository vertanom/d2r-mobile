// Mac App「手柄 → 手柄按键…」窗口：游戏的每个动作一行（技能格、第二套技能、腰带、地图、菜单……加宿主动作：地图大小、免费回城），旁边写着现在哪些实体键 / 组合触发它；
// 点「设定…」再在手柄上按一颗键或一个组合（按着修饰键再按触发键），它就改发这个动作（dm_gamepad_capture）。外加第二套技能的分段毫秒。
// 「应用」把表写成 pad_remap.txt（dm_padbind_format，与 sw_custom.txt 同目录），再叫 dm_gamepad.m 马上重读：不用重启，按着的键按新表放掉。
// 一颗实体键（或一个组合）只发一个动作：把它设给新动作，它就离开旧动作。文件里手写了列表里没有的输出时，多一行「自定：…」把它原样保住，不会因为开了一次窗口就被改掉。
// Mac App "Controller -> Controller Buttons..." window: one row per game action (skill slots, 2nd skill set, belt, map, menu... plus host actions: map size, free town portal) showing which physical buttons / combos trigger it now;
// click "Set..." then press a button or a combo on the controller (hold the modifiers, then press the trigger) and it sends this action from then on (dm_gamepad_capture). Plus the staging milliseconds for the 2nd skill set.
// Apply writes the table as pad_remap.txt (dm_padbind_format, next to sw_custom.txt) and tells dm_gamepad.m to re-read it at once: no restart, held buttons are released by the new table.
// A physical button (or combo) sends one action: setting it to a new action takes it off the old one. When the file holds a hand-written output that is not in the list, an extra "Custom: ..." row keeps it as is, so opening the window once never changes it.
// 每行两栏：「手柄」与「键盘」（Victor 2026-11-05：不常用的命令放键盘，手柄只留对局要紧的）。键盘那栏点「设定…」再按一个键盘键（这个窗口自己收键，不进游戏），一个键一个动作，存成 KEY_<名>=…（dm_padbind.h）
// Each row has two columns, "Controller" and "Keyboard" (Victor 2026-11-05: rarely needed commands go on the keyboard, the controller keeps what matters in a fight). In the keyboard column click "Set..." then press a key (this window takes the key itself, it never reaches the game); one key, one action, stored as KEY_<name>=... (dm_padbind.h)
#import <Cocoa/Cocoa.h>
#include "dm_padbind.h"
#include "dm.h"

static NSString *T(const char *k) { const char *dm_ui(const char *key); return @(dm_ui(k)); }
// 实体键的名字：Switch 名，括号里是 PlayStation / 通用叫法；+ / − 用通用叫法 Start / Select（Victor 2026-11-03），括号里是 Switch 名（系统给的 buttonA/B/X/Y 对 PS 手柄按菱形位置：下 ×、右 ○、左 □、上 △，见 dm_padmap.h）
// Physical button names: the Switch name, with the PlayStation / common name in brackets; + / − use the common names Start / Select (Victor 2026-11-03) with the Switch name in brackets (for PS controllers the system's buttonA/B/X/Y follow the diamond: bottom ×, right ○, left □, top △, see dm_padmap.h)
static const char *const k_name[DM_PADBIND_N] = { "A (×)", "B (○)", "X (□)", "Y (△)", "LS (L3)", "RS (R3)", "L (L1)", "R (R1)", "ZL (L2)", "ZR (R2)", "Start (+)", "Select (−)", "✛ ←", "✛ ↑", "✛ →", "✛ ↓" };
static const int k_slots[] = { 0, 1, 2, 3, 7, 9 };   // 技能格键：A B X Y R ZR（ZL 是换套键） | Skill-slot buttons: A B X Y R ZR (ZL switches the set)

static NSString *mask_name(uint64_t m) {
    NSMutableArray *a = [NSMutableArray array]; for (int b = 0; b < DM_PADBIND_N; b++) if (m & 1ull << b) [a addObject:@(k_name[b])];
    for (int k = 0; k < DM_PADBIND_NACT; k++) if (m & 1ull << (DM_PADBIND_ACT0 + k)) [a addObject:@(dm_padbind_act_names[k])];
    return a.count ? [a componentsJoinedByString:@" + "] : T("dmPadNothing");
}
// 表里的占位符是 C 的 %s：直接换成文字，不走 stringWithFormat（%s 配 NSString 会崩，配 C 串又不按 UTF-8 解，× ○ 会乱码）
// The table's placeholders are C %s: substitute the text directly instead of stringWithFormat (%s with an NSString crashes, and with a C string it is not decoded as UTF-8, garbling × ○)
static NSString *fill1(const char *key, NSString *arg) { return [T(key) stringByReplacingOccurrencesOfString:@"%s" withString:arg]; }
static NSString *remap_path(void) { const char *dm_custom_dir(void); const char *d = dm_custom_dir(); return d ? [@(d) stringByAppendingPathComponent:@"pad_remap.txt"] : nil; }
// 动作的名字：技能格键叫「技能格 X」，ZL 叫「换第二套技能（按住）」，十字键是腰带四列（左 1、上 2、下 3、右 4，见 dm_touch_controls.c），− 地图、+ 菜单，宿主动作有自己的名字；L / LS / RS 原版没有固定用途，就是键名
// Action names: skill-slot buttons are "Skill slot X", ZL is "2nd skill set (hold)", the D-pad is the four belt columns (left 1, up 2, down 3, right 4, see dm_touch_controls.c), − map, + menu, host actions have their own names; L / LS / RS have no fixed use in the original, so they keep the button name
static const int k_belt[4] = { 12, 13, 15, 14 };
static NSString *func_name(uint64_t m) {
    if (m == DM_BTN_RS) return fill1("dmPadFnEmote", @(k_name[5]));   // 原版按右摇杆 = 表情轮盘 | the original's right stick press = emote wheel
    for (int k = 0; k < 4; k++) if (m == 1ull << k_belt[k]) return [NSString stringWithFormat:@"%@  %@", fill1("dmPadFnBelt", [NSString stringWithFormat:@"%d", k + 1]), @(k_name[k_belt[k]])];
    if (m == DM_BTN_MINUS) return [NSString stringWithFormat:@"%@  %@", T("dmPadFnMap"), @(k_name[11])];
    if (m == DM_BTN_PLUS) return [NSString stringWithFormat:@"%@  %@", T("dmPadFnMenu"), @(k_name[10])];
    if (m == DM_PADACT_MAPSIZE) return T("dmPadFnMapSize");
    if (m == DM_PADACT_TOWNPORTAL) return T("dmPadFnPortal");
    if (m == DM_PADACT_MAPPOS) return T("dmPadFnMapPos");
    if (m == DM_PADACT_MAPZOOMIN) return T("dmPadFnZoomIn");
    if (m == DM_PADACT_MAPZOOMOUT) return T("dmPadFnZoomOut");
    if (m == DM_PADACT_MAPLEFT) return fill1("dmPadFnPan", @"←");
    if (m == DM_PADACT_MAPRIGHT) return fill1("dmPadFnPan", @"→");
    if (m == DM_PADACT_MAPUP) return fill1("dmPadFnPan", @"↑");
    if (m == DM_PADACT_MAPDOWN) return fill1("dmPadFnPan", @"↓");
    if (m == DM_PADACT_MAPCENTER) return T("dmPadFnPanReset");
    if (m == DM_PADACT_FREEAIM) return T("dmPadFnFreeAim");
    if (m == DM_PADACT_FREEAIMTOGGLE) return T("dmPadFnFreeAimToggle");
    if (m == DM_PADACT_RELEASECAST) return T("dmPadFnReleaseCast");
    for (unsigned k = 0; k < sizeof k_slots / sizeof *k_slots; k++) { uint64_t s = 1ull << k_slots[k];
        if (m == s) return fill1("dmPadFnSkill", @(k_name[k_slots[k]]));
        if (m == (DM_BTN_ZL | s)) return fill1("dmPadSet2", @(k_name[k_slots[k]])); }
    if (m == DM_BTN_ZL) return fill1("dmPadFnZL", @(k_name[8]));
    if (m && !(m & (m - 1))) return @(k_name[__builtin_ctzll(m)]);
    return fill1("dmPadCustom", mask_name(m));
}
void dm_gamepad_capture(void (^cb)(uint64_t mods, int bit));
// 键盘键给人看的名字：字母数字照写，其余用符号 | Keyboard key names for people: letters and digits as is, symbols for the rest
static NSString *key_name(int i) {
    static const char *const sym[][2] = { {"SPACE", "␣ Space"}, {"TAB", "⇥ Tab"}, {"EQUAL", "="}, {"MINUS", "-"}, {"LBRACKET", "["}, {"RBRACKET", "]"}, {"SEMICOLON", ";"}, {"QUOTE", "'"}, {"COMMA", ","}, {"PERIOD", "."},
        {"SLASH", "/"}, {"GRAVE", "`"}, {"BACKSLASH", "\\"}, {"LEFT", "←"}, {"UP", "↑"}, {"RIGHT", "→"}, {"DOWN", "↓"}, {"PADPLUS", "Num +"} };
    for (unsigned k = 0; k < sizeof sym / sizeof *sym; k++) if (!strcmp(sym[k][0], dm_padbind_keys[i].name)) return @(sym[k][1]);
    return @(dm_padbind_keys[i].name);
}
static NSString *combo_name(uint64_t mods, int btn) {   // 「− + ✛ ↑」：修饰键按键序，触发键最后 | "− + ✛ ↑": modifiers in bit order, trigger last
    NSMutableArray *a = [NSMutableArray array]; for (int b = 0; b < DM_PADBIND_N; b++) if (mods & 1ull << b) [a addObject:@(k_name[b])];
    [a addObject:@(k_name[btn])]; return [a componentsJoinedByString:@" + "];
}

@interface DmPadPanel : NSWindowController <NSWindowDelegate>
@property NSMutableArray<NSNumber *> *funcs;            // 每行的功能（发给游戏的键位） | each row's function (buttons sent to the game)
@property NSMutableArray<NSTextField *> *bound, *kbound;   // 每行：现在触发它的实体键 / 键盘键 | each row: the physical buttons / keyboard keys triggering it now
@property NSMutableArray<NSButton *> *sets, *clears, *ksets, *kclears;
@property NSGridView *grid;   // 一张表两半：左半（列 0–6）技能 13 行，右半（列 8–14）其余动作；每半：名字、手柄（键 / 设定 / 清除）、键盘（键 / 设定 / 清除）；第 0 行是表头；分两张表时矮的那张会被拉高、行间冒空隙 | one grid, two halves: left (columns 0-3) the 13 skill rows, right (columns 5-8) the other actions; with two grids the shorter one got stretched, opening gaps between rows
@property NSTextField *stage, *unused;
@property NSInteger listening;                          // 正在等手柄键的那一行，-1 = 没在等 | the row waiting for a controller button, -1 = not waiting
@property NSInteger klistening;                         // 正在等键盘键的那一行，-1 = 没在等 | the row waiting for a keyboard key, -1 = not waiting
@property id keyMon;                                    // 等键盘键时的本地键盘监听 | local key monitor while waiting for a keyboard key
@property NSTextField *note;                            // 设定的结果 / 提示（满了等） | result of a Set / notices (table full, etc.)
@end
@implementation DmPadPanel { dm_padbind_table _t; }
- (void)addRow:(uint64_t)m {
    NSInteger r = (NSInteger)self.funcs.count; [self.funcs addObject:@(m)];
    NSTextField *l = [NSTextField labelWithString:func_name(m)]; l.alignment = NSTextAlignmentRight;
    NSTextField *b = [NSTextField labelWithString:@""]; [b.widthAnchor constraintGreaterThanOrEqualToConstant:130].active = YES;
    NSTextField *kb = [NSTextField labelWithString:@""]; [kb.widthAnchor constraintGreaterThanOrEqualToConstant:60].active = YES;
    NSButton *set = [NSButton buttonWithTitle:T("dmPadBind") target:self action:@selector(set:)]; set.tag = r;
    NSButton *clr = [NSButton buttonWithTitle:T("dmPadClear") target:self action:@selector(clear:)]; clr.tag = r;
    NSButton *kset = [NSButton buttonWithTitle:T("dmPadBind") target:self action:@selector(kset:)]; kset.tag = r;
    NSButton *kclr = [NSButton buttonWithTitle:T("dmPadClear") target:self action:@selector(kclear:)]; kclr.tag = r;
    // 32 行：小号控件，整窗放得进笔记本屏幕 | 24 rows: small controls so the window fits a laptop screen
    NSFont *f = [NSFont systemFontOfSize:NSFont.smallSystemFontSize]; l.font = f; b.font = kb.font = [NSFont boldSystemFontOfSize:NSFont.smallSystemFontSize];
    for (NSButton *x in @[set, clr, kset, kclr]) { x.controlSize = NSControlSizeSmall; x.font = f; }
    [self.bound addObject:b]; [self.sets addObject:set]; [self.clears addObject:clr]; [self.kbound addObject:kb]; [self.ksets addObject:kset]; [self.kclears addObject:kclr];
    NSInteger row = 1 + (r < 13 ? r : r - 13), col = r < 13 ? 0 : 8;
    [self growTo:row];
    NSArray *v = @[l, b, set, clr, kb, kset, kclr]; for (NSInteger c = 0; c < 7; c++) [self.grid cellAtColumnIndex:col + c rowIndex:row].contentView = v[(NSUInteger)c];
}
- (void)growTo:(NSInteger)row { NSView *e = NSGridCell.emptyContentView; NSMutableArray *a = [NSMutableArray array]; for (int c = 0; c < 15; c++) [a addObject:e];
    while (self.grid.numberOfRows <= row) [self.grid addRowWithViews:a]; }
// 按 _t 重摆：每行列出发这个功能的实体键；文件里有列表外的组合就补一行「自定」；没发任何东西的实体键列在底下
// Re-lay out from _t: each row lists the physical buttons sending its function; a combination from the file that is not listed gets an extra "Custom" row; physical buttons sending nothing are listed below
- (void)refresh {
    for (int p = 0; p < DM_PADBIND_N; p++) if (_t.out[p] && ![self.funcs containsObject:@(_t.out[p])]) [self addRow:_t.out[p]];
    for (int k = 0; k < _t.ncombo; k++) if (_t.combo[k].out && ![self.funcs containsObject:@(_t.combo[k].out)]) [self addRow:_t.combo[k].out];
    for (int k = 0; k < DM_PADBIND_NKEY; k++) if (_t.kout[k] && ![self.funcs containsObject:@(_t.kout[k])]) [self addRow:_t.kout[k]];
    int busy = self.listening >= 0 || self.klistening >= 0;
    for (NSUInteger r = 0; r < self.funcs.count; r++) {
        uint64_t m = self.funcs[r].unsignedLongLongValue; NSMutableArray *a = [NSMutableArray array];
        for (int p = 0; p < DM_PADBIND_N; p++) if (_t.out[p] == m) [a addObject:@(k_name[p])];
        for (int k = 0; k < _t.ncombo; k++) if (_t.combo[k].out == m) [a addObject:combo_name(_t.combo[k].mods, _t.combo[k].btn)];
        NSTextField *b = self.bound[r]; int on = self.listening == (NSInteger)r;
        b.stringValue = on ? T("dmPadListen") : a.count ? [a componentsJoinedByString:@", "] : @"—";
        b.textColor = on ? NSColor.controlAccentColor : a.count ? NSColor.labelColor : NSColor.tertiaryLabelColor;
        self.sets[r].enabled = !busy || on; self.sets[r].title = on ? T("dmPadCancel") : T("dmPadBind");
        self.clears[r].enabled = !busy && a.count;
        NSMutableArray *ka = [NSMutableArray array]; for (int k = 0; k < DM_PADBIND_NKEY; k++) if (_t.kout[k] == m) [ka addObject:key_name(k)];
        NSTextField *kb = self.kbound[r]; int kon = self.klistening == (NSInteger)r;
        kb.stringValue = kon ? T("dmPadKeyListen") : ka.count ? [ka componentsJoinedByString:@", "] : @"—";
        kb.textColor = kon ? NSColor.controlAccentColor : ka.count ? NSColor.labelColor : NSColor.tertiaryLabelColor;
        self.ksets[r].enabled = !busy || kon; self.ksets[r].title = kon ? T("dmPadCancel") : T("dmPadBind");
        self.kclears[r].enabled = !busy && ka.count;
    }
    NSMutableArray *u = [NSMutableArray array]; for (int p = 0; p < DM_PADBIND_N; p++) if (!_t.out[p]) [u addObject:@(k_name[p])];
    self.unused.stringValue = u.count ? fill1("dmPadUnused", [u componentsJoinedByString:@", "]) : @""; self.unused.hidden = !u.count;
    [self fit];
}
// 窗口高度跟内容走（左上角不动）：提示行出现 / 消失时，多出来的高度不再被摊到表格的某几行里（行与行之间会冒出大空隙）
// The window height follows the content (top-left corner fixed): when a notice line appears / disappears, spare height is no longer spread into some grid rows (big gaps between rows)
- (void)fit {
    NSWindow *w = self.window; [w.contentView layoutSubtreeIfNeeded]; NSSize want = w.contentView.fittingSize; NSRect f = w.frame;
    NSRect nf = [w frameRectForContentRect:NSMakeRect(0, 0, want.width, want.height)]; nf.origin = NSMakePoint(f.origin.x, NSMaxY(f) - nf.size.height);
    if (!NSEqualRects(nf, f)) [w setFrame:nf display:YES];
}
- (void)stopListening { if (self.listening >= 0) { dm_gamepad_capture(nil); self.listening = -1; }
    if (self.keyMon) { [NSEvent removeMonitor:self.keyMon]; self.keyMon = nil; } self.klistening = -1; }
- (void)set:(NSButton *)s {
    if (self.listening == s.tag) { [self stopListening]; [self refresh]; return; }
    [self stopListening]; self.listening = s.tag; [self refresh];
    __weak DmPadPanel *me = self; NSInteger row = s.tag;
    dm_gamepad_capture(^(uint64_t mods, int bit) { DmPadPanel *p = me; if (!p || p.listening != row) return;
        p.listening = -1; [p take:bit mods:mods row:row]; });
}
// 单颗键：这颗键改发这个动作；组合：同一组合的旧条目换掉。组合用到的修饰键单按时照旧发自己的动作（松开时补发） | Single button: it now sends this action; combo: the old entry for the same combo is replaced. A modifier used by combos still sends its own action when tapped alone (sent on release)
- (void)take:(int)bit mods:(uint64_t)mods row:(NSInteger)row {
    uint64_t m = self.funcs[(NSUInteger)row].unsignedLongLongValue; NSString *what = mods ? combo_name(mods, bit) : @(k_name[bit]);
    // 撑第二套技能的键（发 ZL 的）不能当修饰键：当了修饰键按下就不发，按住它再按技能键会放第一套 | a button holding the 2nd skill set (one that sends ZL) cannot be a modifier: a modifier sends nothing on press, so holding it and pressing a skill button would cast the 1st set
    for (int p = 0; p < DM_PADBIND_N; p++) if ((mods & 1ull << p) && _t.out[p] == DM_BTN_ZL) {
        self.note.stringValue = fill1("dmPadNoZLMod", @(k_name[p])); self.note.hidden = NO; LOG("键位窗口：%s 撑第二套技能，不能当修饰键，没设", combo_name(mods, bit).UTF8String); [self refresh]; return; }
    if (!mods) _t.out[bit] = m;
    else if (!dm_padbind_set_combo(&_t, mods, bit, m)) { self.note.stringValue = T("dmPadFull"); self.note.hidden = NO; [self refresh]; return; }
    self.note.stringValue = mods ? fill1("dmPadModNote", mask_name(mods)) : @""; self.note.hidden = !mods;
    LOG("键位窗口：%s → %s", what.UTF8String, func_name(m).UTF8String); [self refresh];
}
// 键盘那栏「设定…」：这个窗口收下一个按下的键（不进游戏）。Esc = 取消；表外的键（WASD、回车）给提示，不设。一个键只发一个动作：设给新动作就离开旧动作
// Keyboard column "Set...": this window takes the next key pressed (it never reaches the game). Esc = cancel; keys outside the table (WASD, Return) get a notice and are not set. A key sends one action: setting it to a new action takes it off the old one
- (void)kset:(NSButton *)s {
    if (self.klistening == s.tag) { [self stopListening]; [self refresh]; return; }
    [self stopListening]; self.klistening = s.tag; [self refresh];
    __weak DmPadPanel *me = self; NSInteger row = s.tag;
    self.keyMon = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown handler:^NSEvent *(NSEvent *e) { DmPadPanel *p = me;
        if (!p || p.klistening != row || e.window != p.window) return e;
        if (e.isARepeat) return nil;
        unsigned short c = e.keyCode; int k = dm_padbind_key_index(c); [p stopListening];
        if (c != 53) { if (k < 0) { p.note.stringValue = T("dmPadKeyBad"); p.note.hidden = NO; }
            else { uint64_t m = p.funcs[(NSUInteger)row].unsignedLongLongValue; [p setKey:k out:m]; p.note.hidden = YES; LOG("键位窗口：键盘 %s → %s", dm_padbind_keys[k].name, func_name(m).UTF8String); } }
        [p refresh]; return nil; }];
}
- (void)setKey:(int)k out:(uint64_t)m { _t.kout[k] = m; }
- (void)kclear:(NSButton *)s {
    uint64_t m = self.funcs[(NSUInteger)s.tag].unsignedLongLongValue; for (int k = 0; k < DM_PADBIND_NKEY; k++) if (_t.kout[k] == m) _t.kout[k] = 0;
    [self refresh];
}
- (void)clear:(NSButton *)s {
    uint64_t m = self.funcs[(NSUInteger)s.tag].unsignedLongLongValue; for (int p = 0; p < DM_PADBIND_N; p++) if (_t.out[p] == m) _t.out[p] = 0;
    for (int k = _t.ncombo - 1; k >= 0; k--) if (_t.combo[k].out == m) dm_padbind_set_combo(&_t, _t.combo[k].mods, _t.combo[k].btn, ~0ull);
    [self refresh];
}
- (instancetype)init {
    NSPanel *w = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 1400, 640) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
    if (!(self = [super initWithWindow:w])) return nil;
    w.title = [T("dmPadTitle") stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"…."]]; w.delegate = self; w.releasedWhenClosed = NO; w.floatingPanel = YES;
    NSTextField *intro = [NSTextField wrappingLabelWithString:T("dmPadIntro")]; intro.preferredMaxLayoutWidth = 1340;
    self.funcs = [NSMutableArray array]; self.bound = [NSMutableArray array]; self.sets = [NSMutableArray array]; self.clears = [NSMutableArray array]; self.listening = -1;
    self.kbound = [NSMutableArray array]; self.ksets = [NSMutableArray array]; self.kclears = [NSMutableArray array]; self.klistening = -1;
    self.grid = [NSGridView gridViewWithNumberOfColumns:15 rows:0]; self.grid.rowSpacing = 4; self.grid.columnSpacing = 8; self.grid.rowAlignment = NSGridRowAlignmentFirstBaseline;
    [self.grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing; [self.grid columnAtIndex:8].xPlacement = NSGridCellPlacementTrailing; [self.grid columnAtIndex:7].width = 18;
    [self.grid columnAtIndex:4].leadingPadding = 10; [self.grid columnAtIndex:12].leadingPadding = 10;   // 键盘那栏与手柄那栏隔开一点 | a little gap between the keyboard and controller columns
    [self growTo:0];
    for (NSInteger c = 0; c < 2; c++) { NSTextField *hp = [NSTextField labelWithString:T("dmPadColPad")], *hk = [NSTextField labelWithString:T("dmPadColKey")];   // 表头 | header
        for (NSTextField *h in @[hp, hk]) { h.font = [NSFont boldSystemFontOfSize:NSFont.smallSystemFontSize]; h.textColor = NSColor.secondaryLabelColor; }
        [self.grid cellAtColumnIndex:c * 8 + 1 rowIndex:0].contentView = hp; [self.grid cellAtColumnIndex:c * 8 + 4 rowIndex:0].contentView = hk; }
    // 行序：六个技能格、第二套的六个、换套键、腰带四列、地图、地图大小 / 位置 / 缩放 / 平移、免费回城、自由瞄准（按住 / 开关）、菜单，然后其余各键 | Row order: the six skill slots, the six of the 2nd set, the set switch, the four belt columns, map, map size / position / zoom / panning, free town portal, free aim (hold / toggle), menu, then the remaining buttons
    for (unsigned k = 0; k < sizeof k_slots / sizeof *k_slots; k++) [self addRow:1ull << k_slots[k]];
    for (unsigned k = 0; k < sizeof k_slots / sizeof *k_slots; k++) [self addRow:DM_BTN_ZL | 1ull << k_slots[k]];
    [self addRow:DM_BTN_ZL];
    for (int k = 0; k < 4; k++) [self addRow:1ull << k_belt[k]];
    [self addRow:DM_BTN_MINUS]; [self addRow:DM_PADACT_MAPSIZE]; [self addRow:DM_PADACT_MAPPOS]; [self addRow:DM_PADACT_MAPZOOMIN]; [self addRow:DM_PADACT_MAPZOOMOUT];
    [self addRow:DM_PADACT_MAPLEFT]; [self addRow:DM_PADACT_MAPRIGHT]; [self addRow:DM_PADACT_MAPUP]; [self addRow:DM_PADACT_MAPDOWN]; [self addRow:DM_PADACT_MAPCENTER];
    [self addRow:DM_PADACT_TOWNPORTAL]; [self addRow:DM_PADACT_FREEAIM]; [self addRow:DM_PADACT_FREEAIMTOGGLE]; [self addRow:DM_PADACT_RELEASECAST]; [self addRow:DM_BTN_PLUS];
    for (int b = 0; b < DM_PADBIND_N; b++) if (![self.funcs containsObject:@(1ull << b)]) [self addRow:1ull << b];
    self.unused = [NSTextField wrappingLabelWithString:@""]; self.unused.textColor = NSColor.secondaryLabelColor; self.unused.preferredMaxLayoutWidth = 1340;
    self.note = [NSTextField wrappingLabelWithString:@""]; self.note.textColor = NSColor.secondaryLabelColor; self.note.preferredMaxLayoutWidth = 1340; self.note.hidden = YES;
    NSNumberFormatter *nf = [NSNumberFormatter new]; nf.minimum = @0; nf.maximum = @500; nf.allowsFloats = NO;
    self.stage = [NSTextField textFieldWithString:@""]; self.stage.formatter = nf; [self.stage.widthAnchor constraintEqualToConstant:60].active = YES;
    NSStepper *st = [NSStepper new]; st.minValue = 0; st.maxValue = 500; st.increment = 10; st.valueWraps = NO;
    [st bind:NSValueBinding toObject:self.stage withKeyPath:@"integerValue" options:nil]; [self.stage bind:NSValueBinding toObject:st withKeyPath:@"integerValue" options:nil];
    NSStackView *delay = [NSStackView stackViewWithViews:@[[NSTextField labelWithString:T("dmPadDelay")], self.stage, st]];
    NSButton *def = [NSButton buttonWithTitle:T("dmPadDefaults") target:self action:@selector(defaults:)];
    NSButton *cancel = [NSButton buttonWithTitle:T("dmPadCancel") target:self action:@selector(cancel:)]; cancel.keyEquivalent = @"\e";
    NSButton *ok = [NSButton buttonWithTitle:T("dmPadApply") target:self action:@selector(apply:)]; ok.keyEquivalent = @"\r";
    NSStackView *btns = [NSStackView stackViewWithViews:@[def, [NSView new], cancel, ok]]; [btns setHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSStackView *all = [NSStackView stackViewWithViews:@[intro, self.grid, self.note, self.unused, delay, btns]]; all.orientation = NSUserInterfaceLayoutOrientationVertical; all.alignment = NSLayoutAttributeLeading; all.spacing = 14;
    all.edgeInsets = NSEdgeInsetsMake(18, 20, 18, 20); all.translatesAutoresizingMaskIntoConstraints = NO;
    [w.contentView addSubview:all];
    [NSLayoutConstraint activateConstraints:@[[all.leadingAnchor constraintEqualToAnchor:w.contentView.leadingAnchor], [all.trailingAnchor constraintEqualToAnchor:w.contentView.trailingAnchor],
        [all.topAnchor constraintEqualToAnchor:w.contentView.topAnchor], [all.bottomAnchor constraintEqualToAnchor:w.contentView.bottomAnchor], [btns.widthAnchor constraintEqualToAnchor:all.widthAnchor constant:-40]]];
    return self;
}
// 打开时按文件现状摆：没有文件 / 读不懂 = 全部原样，分段给 50 毫秒（实测：ZL 与技能格键同一下发出时游戏亮第二套、放的却是第一套）
// On opening, lay out from the file as it is now: no file / unreadable = all original, staging 50 ms (measured: with ZL and the skill-slot button sent together the game highlights the 2nd set but casts the 1st)
- (void)load {
    [self stopListening]; self.note.hidden = YES;
    dm_padbind_identity(&_t); _t.stage_ms = 50; NSString *p = remap_path();
    NSString *txt = p ? [NSString stringWithContentsOfFile:p encoding:NSUTF8StringEncoding error:nil] : nil;
    if (txt && !dm_padbind_parse(txt.UTF8String, &_t)) { dm_padbind_identity(&_t); _t.stage_ms = 50; LOG("【红】键位窗口：%s 读不懂，窗口按全部原样摆", p.UTF8String); }
    self.stage.integerValue = _t.stage_ms; [self refresh];
}
- (void)defaults:(id)s { (void)s; [self stopListening]; dm_padbind_identity(&_t); self.note.hidden = YES; [self refresh]; }
// Esc：在等手柄键时只取消等待，否则关窗 | Esc: while waiting for a controller button it only cancels the wait, otherwise closes the window
- (void)cancel:(id)s { (void)s; if (self.listening >= 0) { [self stopListening]; [self refresh]; return; } [self.window close]; }
- (void)windowWillClose:(NSNotification *)n { (void)n; [self stopListening]; }
- (void)apply:(id)s {
    (void)s; [self stopListening]; [self.window makeFirstResponder:nil];   // 让正在编辑的毫秒数先落定 | commit the milliseconds being edited first
    dm_padbind_table t = _t; long v = self.stage.integerValue; t.stage_ms = (int)(v < 0 ? 0 : v > 500 ? 500 : v);
    char buf[4096]; size_t n = dm_padbind_format(&t, buf, sizeof buf); NSString *p = remap_path(); NSError *e = nil;
    NSString *txt = n ? [@"# D2R Mobile: Controller > Controller Buttons… (pad_remap.txt)\n" stringByAppendingString:@(buf)] : nil;
    if (!p || !txt || ![txt writeToFile:p atomically:YES encoding:NSUTF8StringEncoding error:&e]) {
        NSAlert *a = [NSAlert new]; a.messageText = T("dmPadSaveFail"); a.informativeText = e.localizedDescription ?: (p ?: @""); [a addButtonWithTitle:T("dmAppOK")]; [a runModal];
        LOG("【红】键位窗口：写 %s 失败：%s", p.UTF8String ?: "（没有目录）", e.localizedDescription.UTF8String ?: "?"); return; }
    LOG("键位窗口：应用 → %s", p.UTF8String);
    void dm_gamepad_remap_reload(void); dm_gamepad_remap_reload();
    [self.window close];
}
@end

void dm_padbind_show(NSWindow *parent) {
    static DmPadPanel *panel; if (!panel) panel = [DmPadPanel new];
    [panel load];
    if (!panel.window.isVisible) { NSRect pf = parent ? parent.frame : NSScreen.mainScreen.visibleFrame, f = panel.window.frame;
        [panel.window setFrameOrigin:NSMakePoint(NSMidX(pf) - f.size.width / 2, NSMidY(pf) - f.size.height / 2)]; }
    [panel showWindow:nil]; [panel.window makeKeyAndOrderFront:nil];
}
