// 手柄接入（原版 Switch 本来就是手柄操作，恢复手柄支持；复用原版手柄交互，不自己设计）。
// iOS GameController（MFi / Xbox / PS / Switch Pro）的扩展手柄 → 原版手柄输入（宿主 dm_pad_set，与触屏输入在游戏取手柄时合并）。
// 四个面键按键帽上印的字母对应：提示条写哪个字母，就按手柄上印着那个字母的键（提示条画的是 Switch 键帽）。
// Xbox 手柄（GCXboxGamepad / 产品类别含 Xbox）自动换成 PC / Xbox 版的键位：按物理位置，下确认、右返回、上 = 放进堆叠仓库、左 = Y 功能，
//   提示条换成原版的 Xbox 键帽（dm_padmap.h dm_padmap_face_xbox、dm_padmode.c 键帽样式）。哪只手柄有明确输入就按哪只的类型换键帽，设置里不用开关。
//   其余手柄：系统给的 buttonA/B/X/Y 直接发游戏的 A/B/X/Y，不再按型号细分：系统对任天堂手柄按键帽字母给（印 A 的右键 = buttonA），Xbox 字母与位置一致，
//   没字母的（PS 等）按菱形位置给（下方键 = buttonA = 确认）。规则在 host/dm_padmap.h（纯函数，宿主单测 t_padmap 核）；连接时记一行厂商名与产品类别。
//   PlayStation 手柄（产品类别 DualSense / DualShock 4）键位照此，提示条自动换成原版 PS 键帽（下 ✕ = A 确认、右 ○ = B、左 □ = X、上 △ = Y）。
//   （先前一律按 Switch 的位置排——右键确认——Xbox 手柄玩家照提示条「A 选择」按印着 A 的下方键，实际发的是 B，选角页选不了人。）
//   （之后按 GCExtendedGamepad.h 的菱形约定认类型再翻译：任天堂手柄被换了两次，A/B、X/Y 全反——公开 日志。）玩家在系统设置里改过键位的，系统已先换好再给我们。
//   肩键 L/R、扳机 ZL/ZR、摇杆按下 LS/RS、十字键、菜单键→＋、选项键→－。
//   选项键（buttonOptions）有的手柄没有，那时减号没有来源：不拿 Home 键顶（头文件：系统不吃才轮到 App，靠不住），留空。Xbox 的视图键、PS 的分享 / 创建键、Switch Pro 的 − 都是选项键。
// 摇杆照原值换成 ±32767（GameController 与原版都是上为正）。
//
// 谁接管（谁有明确输入谁接管，不因漂移来回切）：
//   · 手柄「明确输入」= 按下任何一颗键，或把摇杆从中位推出来。摇杆带迟滞：推过 25% 才算推出，回到 15% 以内才算回中——停在阈值附近的漂移不会一会儿算一会儿不算。
//   · 手柄有明确输入 → 手柄模式（布局换原版手柄版）；碰屏幕 → 触屏模式（前端调 dm_gamepad_touched）。
//   · 碰过屏幕之后，摇杆要先回中、再推出来才重新接管：一直偏着的摇杆（漂移大的手柄）不会把布局又切回手柄版。按键随时接管。
//   · 触屏模式下手柄摇杆的值不交给游戏（手柄连着放在一边、摇杆有点漂移，不该盖掉触屏摇杆）；手柄模式下没推出来的摇杆给 0、推出来的给原值。
//   · 游戏取手柄处（hle_hid.c fill）：触屏摇杆有值就用触屏的，否则用手柄的。
// 多个手柄：每个手柄各存各的键和摇杆，交给游戏的是合起来的——键取或、每根摇杆取推得最远的那个。断开一个只拿掉它那一份，别的手柄按着的键不丢。
// 失活 / 进后台 / 来电（前端 front_ios_focus_event → dm_gamepad_reset）：所有手柄的键和摇杆清零。回调只在值变化时来，按着键切走、在后台松开，回来不会补一次「松开」。
#import <GameController/GameController.h>
#include "dm_input.h"
#include "dm_padmap.h"
#include "dm_padbind.h"
#include <sys/stat.h>
#include "hle.h"

static void (*g_on_mode)(int pad);
static int g_pad_mode, g_need_center;
void dm_padmode_request(int pad);   // 宿主 dm_padmode.c：布局换成原版手柄版 / 触屏版 | Host dm_padmode.c: switch the layouts to the original controller version / the touch version
static void set_mode(int pad) { if (g_pad_mode == pad) return; g_pad_mode = pad; dm_padmode_request(pad); LOG("手柄：%s", pad ? "手柄有输入 → 手柄模式" : "碰了屏幕 / 手柄断开 → 触屏模式"); if (g_on_mode) g_on_mode(pad); }
int dm_gamepad_mode(void) { return g_pad_mode; }

// 各手柄各存各的（槽 0、1 留给触点脚本的两个虚拟手柄，其余给实体手柄）
// Each controller is stored separately (slots 0 and 1 are reserved for the touch script's two virtual controllers, the rest are for physical controllers)
enum { NSLOT = 6 };
static struct { const void *who; uint64_t b; int32_t ax[4]; int glyph; } g_s[NSLOT];
static int g_glyph = DM_GLYPH_PAD;   // 最近一次明确输入来自哪种手柄 → 键帽样式 | Which kind of controller the latest clear input came from -> glyph style
static int slot_of(const void *who, int make) { int fr = -1; for (int i = 0; i < NSLOT; i++) { if (g_s[i].who == who) return i; if (fr < 0 && !g_s[i].who) fr = i; }
    if (make && fr >= 0) { memset(&g_s[fr], 0, sizeof g_s[fr]); g_s[fr].who = who; return fr; } return -1; }

static int32_t axis(float v) { if (v > 1) v = 1; if (v < -1) v = -1; return (int32_t)lroundf(v * DM_STICK_MAX); }
static int stick_on(int was, int32_t x, int32_t y) { double m = hypot(x, y) / DM_STICK_MAX; return was ? m >= 0.15 : m >= 0.25; }   // 迟滞：推过 25% 算推出，回到 15% 内算回中 | Hysteresis: past 25% counts as pushed out, back within 15% counts as centered
// 实体键 → 游戏键的重映射表（dm_padbind.h），文件 pad_remap.txt 与 sw_custom.txt 同目录；没有文件 = 原样。Mac 与 iOS 都按它走；
// 表只在 Mac 的键位窗口（host/padbind_mac.m）里编，iPhone 上这份由 iCloud 从 Mac 带过来（cloud_ios.m dm_cloud_setting_pull），键盘那栏 iOS 不用。
// 文件改了（时间戳变）在下一次手柄输入时重读，不用重启；读不懂的文件整份不生效、沿用上一份，日志说原因。
// Physical -> game button remap table (dm_padbind.h), file pad_remap.txt next to sw_custom.txt; no file = unchanged. Both the Mac and iOS follow it;
// the table is edited only in the Mac's controller buttons window (host/padbind_mac.m), and on the iPhone this copy is brought over from the Mac by iCloud (cloud_ios.m dm_cloud_setting_pull); iOS does not use the keyboard column.
// When the file changes (timestamp) it is re-read on the next controller input, no restart needed; a file that does not parse is ignored as a whole, the previous table stays, and the log says why.
static dm_padbind_table g_bind; static dm_padbind_state g_bind_st; static int g_bind_init; static uint64_t g_bind_next;
static void bind_reload(uint64_t held) {
    static struct timespec seen; uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1000000, next = g_bind_next;
    if (!g_bind_init) { dm_padbind_identity(&g_bind); g_bind_init = 1; } else if (now < next) return;
    g_bind_next = now + 500;
    const char *dm_custom_dir(void); const char *dir = dm_custom_dir(); if (!dir) return;
    char p[1100]; snprintf(p, sizeof p, "%s/pad_remap.txt", dir); struct stat sb;
    if (stat(p, &sb)) { if (seen.tv_sec || seen.tv_nsec) { memset(&seen, 0, sizeof seen); dm_padbind_identity(&g_bind); dm_padbind_reset(&g_bind_st, held); LOG("手柄键位：pad_remap.txt 没了 → 原样"); } return; }
    if (sb.st_mtimespec.tv_sec == seen.tv_sec && sb.st_mtimespec.tv_nsec == seen.tv_nsec) return;
    seen = sb.st_mtimespec;
    NSString *txt = [NSString stringWithContentsOfFile:@(p) encoding:NSUTF8StringEncoding error:nil]; dm_padbind_table t;
    if (!txt || !dm_padbind_parse(txt.UTF8String, &t)) { LOG("【红】手柄键位：%s 读不懂，沿用上一份", p); return; }
    g_bind = t; dm_padbind_reset(&g_bind_st, held);
    LOG("手柄键位：读入 %s（%s，分段 %d 毫秒）", p, dm_padbind_is_identity(&t) ? "原样" : "有改动", t.stage_ms);
}
static void apply(void);
// 地图平移按住一直走：每 30 毫秒挪 12 格（Victor 2026-11-04 再快一倍），松开（acts 里没有平移了）就停。方向实测（2026-11-03）：卷动量为正时地图往右 / 往下走，所以「地图往左」给负值 | Map panning keeps moving while held: 12 cells every 30 ms (Victor 2026-11-04: twice as fast again), stops once released (no panning left in acts). Direction measured (2026-11-03): a positive scroll moves the map right / down, so "map left" is negative
static int g_pan_running; static uint64_t g_key_acts, g_key_fired;   // 键盘按着的宿主动作 / 键盘新按下、还没执行的宿主动作（dm_gamepad_key） | host actions held on the keyboard / ones newly pressed on the keyboard and not yet run (dm_gamepad_key)
static void pan_tick(void) {
    uint64_t a = g_bind_st.acts | g_key_acts; int dx = !!(a & DM_PADACT_MAPRIGHT) - !!(a & DM_PADACT_MAPLEFT), dy = !!(a & DM_PADACT_MAPDOWN) - !!(a & DM_PADACT_MAPUP);
    if (!dx && !dy) { g_pan_running = 0; return; }
    void dm_ui_map_pan(int dx, int dy); dm_ui_map_pan(dx * 12, dy * 12);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{ pan_tick(); });
}
// 键位窗口（host/padbind_mac.m）点了「应用」/ iCloud 换进了新的一份（cloud_ios.m）：马上重读文件、按新表放掉按着的键，不等下一次手柄输入
// The controller buttons window (host/padbind_mac.m) pressed Apply / iCloud swapped in a new copy (cloud_ios.m): re-read the file now and release held buttons by the new table, without waiting for the next controller input
void dm_gamepad_remap_reload(void) { dispatch_async(dispatch_get_main_queue(), ^{ g_bind_next = 0; apply(); }); }
// 键位窗口「设定…」：等手柄上按一颗键或一个组合，交给 cb(修饰键位, 触发键位 0–15)，不交给游戏；cb = NULL 取消。
// 组合 = 按着修饰键再按触发键：全部松开时才定，最后按下的那颗是触发键、之前按着的是修饰键；单按一颗 = 没有修饰键。等的时候所有键都不进游戏；开始等时已经按着的键不算。只在主线程调。
// Controller buttons window "Set…": wait for one button or a combo on the controller and hand it to cb(modifier bits, trigger bit 0-15) instead of the game; cb = NULL cancels.
// Combo = hold the modifiers then press the trigger: decided when everything is released, the last button pressed is the trigger and those held before it are the modifiers; a single press = no modifiers. While waiting no button reaches the game; buttons already held when waiting starts do not count. Main thread only.
static void (^g_capture)(uint64_t mods, int bit); static uint64_t g_raw_b, g_cap_acc; static int g_cap_last;
void dm_gamepad_capture(void (^cb)(uint64_t mods, int bit)) { g_capture = cb; g_cap_acc = 0; g_cap_last = -1; }
// 局内没开面板才按表；菜单 / 面板里原样（储藏箱 / 编年史用 L / R / ZL / ZR 翻页签）。Mac 的 dm_front_navigation 恒为 1（app_mac.m），用触摸层自己的判断；
// iOS 用 dm_front_navigation（同下面 ZR 那段判游戏态）
// The table applies only in a run with no panel open; in menus / panels everything is unchanged (the stash / Chronicle switch tabs with L / R / ZL / ZR). On the Mac dm_front_navigation is always 1 (app_mac.m), so the touch layer's own check is used;
// iOS uses dm_front_navigation (the same game state check as the ZR block below)
#if TARGET_OS_OSX
static int bind_menu(void) { int dm_touch_host_world_active(void), dm_uiscreen_panel(void); return !dm_touch_host_world_active() || dm_uiscreen_panel(); }
#else
static int bind_menu(void) { int dm_front_navigation(void), dm_uiscreen_panel(void); return dm_front_navigation() || dm_uiscreen_panel(); }
#endif
#if TARGET_OS_OSX
// Mac 键盘（front_mac.m on_key 调，主线程）：键盘键 idx（dm_padbind_keys）按下 / 松开，按表的键盘那栏换成输出。按下那一刻定下发什么（同手柄的 latch），
// 菜单里按缺省。宿主动作交给 apply 跟手柄的一起执行（按住型的 FREEAIM、地图平移也认键盘）。返回此刻键盘按着的全部 Switch 键
// Mac keyboard (called by front_mac.m on_key, main thread): keyboard key idx (dm_padbind_keys) down / up, turned into its output by the table's keyboard column. What it sends is fixed the moment it goes down (like the controller's latch),
// and menus use the default. Host actions are handed to apply and run together with the controller's (the held FREEAIM and map panning also take the keyboard). Returns every Switch button the keyboard holds right now
static uint64_t g_key_latch[DM_PADBIND_NKEY]; static uint8_t g_key_down[DM_PADBIND_NKEY];
uint64_t dm_gamepad_key(int idx, int down) {
    bind_reload(g_raw_b);
    if (idx >= 0 && idx < DM_PADBIND_NKEY) {
        if (down && !g_key_down[idx]) { uint64_t o = bind_menu() ? dm_padbind_key_def(idx) : g_bind.kout[idx]; g_key_latch[idx] = o; g_key_down[idx] = 1; g_key_fired |= o & DM_PADACT_ALL;
            if (o & DM_PADACT_ALL) LOG("键盘键位：KEY_%s → 宿主动作 0x%llx", dm_padbind_keys[idx].name, (unsigned long long)(o & DM_PADACT_ALL)); }
        else if (!down) g_key_down[idx] = 0; }
    uint64_t b = 0, a = 0; for (int i = 0; i < DM_PADBIND_NKEY; i++) if (g_key_down[i]) { b |= g_key_latch[i] & ((1ull << DM_PADBIND_N) - 1); a |= g_key_latch[i] & DM_PADACT_ALL; }
    uint64_t was = g_key_acts; g_key_acts = a; if (g_key_fired || a != was) apply();
    return b;
}
#endif
// 把各手柄合起来、定模式、交给宿主。任何一份变了都重算一遍
// Merge all controllers, decide the mode and hand the result to the host. Recompute whenever any one of them changes
static void apply(void) {
    uint64_t b = 0; int32_t ax[4] = {0}; double best[2] = {0};
    for (int i = 0; i < NSLOT; i++) { if (!g_s[i].who) continue; b |= g_s[i].b;
        for (int k = 0; k < 2; k++) { double m = hypot(g_s[i].ax[2 * k], g_s[i].ax[2 * k + 1]); if (m > best[k]) { best[k] = m; ax[2 * k] = g_s[i].ax[2 * k]; ax[2 * k + 1] = g_s[i].ax[2 * k + 1]; } } }
    static int on[2]; static uint64_t prev_b;
    on[0] = stick_on(on[0], ax[0], ax[1]); on[1] = stick_on(on[1], ax[2], ax[3]);
    if (!on[0] && !on[1]) g_need_center = 0;   // 碰过屏幕后摇杆回过中了：之后再推出来算数 | The stick has returned to center since the screen was touched: pushing it out again from now on counts
    if ((b & ~prev_b) || ((on[0] || on[1]) && !g_need_center)) { dm_padmode_request_glyph(g_glyph); set_mode(1); }   // 先报键帽样式再要手柄版：同一次切换里就换成这只手柄的键帽（Mac App 上也是从键盘键帽换回手柄键帽） | Report the glyph style before asking for the controller layout, so the same switch already uses this controller's glyphs (on the Mac App this is also the switch from keyboard glyphs back to controller glyphs)
    prev_b = b;
    if (!g_pad_mode || !on[0]) ax[0] = ax[1] = 0; if (!g_pad_mode || !on[1]) ax[2] = ax[3] = 0;   // 触屏模式不交摇杆；手柄模式下没推出来的给 0 | Sticks are not delivered in touch mode; in controller mode a stick that is not pushed out gives 0
    // 重映射在免费回城之前：那一段看的是游戏要收到的 ZL / ZR。菜单里新按下的键原样（翻页签）。
    // Mac 的 dm_front_navigation 恒为 1（app_mac.m），局内 / 面板用触摸层自己的判断 dm_touch_host_world_active（局内、没开面板、没暂停）
    // Remapping happens before the free town portal: that block looks at the ZL / ZR the game will receive. In menus newly pressed buttons stay unchanged (tab switching).
    // On the Mac dm_front_navigation is always 1 (app_mac.m), so in-run / panel state comes from the touch layer's own dm_touch_host_world_active (in a run, no panel, not paused)
    { int wake = -1, menu = bind_menu(); static int last_menu = -1; bind_reload(b);
      // 等着设定：每一下都把按着的键记为「松开前不算」，所以等的时候都不进游戏；全松开时定下按过的组合 | Waiting for Set: every call marks the held buttons "ignored until released", so nothing reaches the game while waiting; the combo is decided once everything is released
      { uint64_t raw = b & ((1ull << DM_PADBIND_N) - 1), fresh = raw & ~g_raw_b; g_raw_b = raw;
        if (g_capture) { dm_padbind_reset(&g_bind_st, raw);
          for (int i = 0; i < DM_PADBIND_N; i++) if (fresh & 1ull << i) { g_cap_last = i; g_cap_acc = raw & ~(1ull << i); }   // 最后按下的是触发键，那一刻还按着的是修饰键 | the last press is the trigger; whatever is still held at that moment is the modifiers
          if (!raw && g_cap_last >= 0) { void (^cb)(uint64_t, int) = g_capture; uint64_t mods = g_cap_acc; int bit = g_cap_last; g_capture = nil;
            LOG("手柄键位：设定抓到 %s（修饰键位 0x%llx）", dm_padbind_names[bit], (unsigned long long)mods); dispatch_async(dispatch_get_main_queue(), ^{ cb(mods, bit); }); } } }
      if (menu != last_menu && !dm_padbind_is_identity(&g_bind)) { static int n; if (n++ < 20) LOG("手柄键位：%s", menu ? "菜单 / 面板 → 新按的键原样" : "局内 → 按表重映射"); } last_menu = menu;
      b = dm_padbind_step(&g_bind, &g_bind_st, b, clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1000000, menu, g_bind.stage_ms, &wake);
      // 表里的宿主动作：按下那一刻执行一次，不发给游戏 | Host actions from the table: run once at the moment they go down, never sent to the game
      g_bind_st.fired |= g_key_fired; g_key_fired = 0;   // 键盘新按下的宿主动作一起执行 | host actions newly pressed on the keyboard run together
      uint64_t f_free = g_bind_st.fired, acts = g_bind_st.acts | g_key_acts;
      if (g_bind_st.fired) { uint64_t f = g_bind_st.fired; g_bind_st.fired = 0; void dm_ui_map_full(int on), dm_tp_request(void); int dm_ui_map_shown_full(void);
        if (f & DM_PADACT_MAPSIZE) { int on = !dm_ui_map_shown_full(); dm_ui_map_full(on); LOG("手柄键位：动作 MAPSIZE → %s", on ? "全屏地图" : "小地图"); }
        if (f & DM_PADACT_TOWNPORTAL) { dm_tp_request(); LOG("手柄键位：动作 TOWNPORTAL → 免费回城"); }
        if (f & DM_PADACT_WEAPONSWAP) { void dm_weaponswap_request(void); dm_weaponswap_request(); LOG("手柄键位：动作 WEAPONSWAP → 换武器"); }
        { void dm_ui_map_pos_toggle(void), dm_ui_map_zoom(int d);
          if (f & DM_PADACT_MAPPOS) dm_ui_map_pos_toggle();
          if (f & DM_PADACT_MAPZOOMIN) dm_ui_map_zoom(1);
          if (f & DM_PADACT_MAPZOOMOUT) dm_ui_map_zoom(-1);
          if (f & DM_PADACT_MAPCENTER) { void dm_ui_map_pan_reset(void); dm_ui_map_pan_reset(); } } }
      { static int last_hold; int hold = (acts & DM_PADACT_FREEAIM) != 0, tog = (f_free & DM_PADACT_FREEAIMTOGGLE) != 0;   // 自由瞄准：按住型跟 acts，切换型按下一次切一次（hle_private_aim.c） | free aim: the hold kind follows acts, the toggle kind flips once per press (hle_private_aim.c)
        if (hold != last_hold || tog) { void dm_aim_free_set(int, int); dm_aim_free_set(hold, tog); if (hold != last_hold) LOG("手柄键位：动作 FREEAIM → %s", hold ? "按住" : "松开"); last_hold = hold; }
        if (f_free & DM_PADACT_RELEASECAST) { void dm_aim_release_toggle(void); dm_aim_release_toggle(); } }
      if (!g_pan_running && (acts & (DM_PADACT_MAPLEFT | DM_PADACT_MAPRIGHT | DM_PADACT_MAPUP | DM_PADACT_MAPDOWN))) { g_pan_running = 1; pan_tick(); }
      if (wake >= 0) dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(wake + 1) * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{ apply(); }); }
    // ZR：原版六个技能格之一。这一格空着（没绑技能）时按它 = 免费回城（手柄玩家也免费回城，与触屏回城钮一致）；绑了技能就整颗交还原版（不再整颗占用）。
    // 想把回城放在别的键上：原版选技能界面「综合技能」里有回城技能可绑（宿主进局时挂上的，dm_freetp.c），那条走原版自己的施放，不经这里。
    // 只在游戏态接管：面板开着时 ZR 照常交给原版（储藏箱 / 编年史用 ZL / ZR 翻页签）；按下时在游戏态接管的那一下，到松开前都不交给游戏
    // ZR: one of the six original skill slots. While this slot is empty (no skill bound), pressing it = free town portal (controller players get
    // the free town portal too, consistent with the touch town portal button); once a skill is bound the whole button is handed back to the
    // original game (we no longer claim the whole button).
    // To put town portal on another key: the original skill selection screen has a town portal skill under "General Skills" that can be bound
    // (attached by the host on joining a run, dm_freetp.c); that one goes through the original game's own cast and does not pass through here.
    // Taken over only in game state: with a panel open ZR goes to the original game as usual (the stash / Chronicle use ZL / ZR to switch tabs);
    // a press that was taken over in game state is not handed to the game until it is released
    { static uint64_t prev; static int mine; void dm_tp_request(void); int dm_front_navigation(void), dm_freetp_zr_free(int zl);
      if ((b & DM_BTN_ZR) && !(prev & DM_BTN_ZR)) { mine = !dm_front_navigation() && dm_freetp_zr_free((b & DM_BTN_ZL) != 0); if (mine) { dm_tp_request(); LOG("手柄：ZR（这一格空着）→ 免费回城"); } }
      prev = b; if (mine) b &= ~(uint64_t)DM_BTN_ZR; if (!(b & DM_BTN_ZR) && !(prev & DM_BTN_ZR)) mine = 0; }
    dm_pad_set(b, ax[0], ax[1], ax[2], ax[3]);
}
static void put(const void *who, uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry, int glyph) {
    int i = slot_of(who, 1); if (i < 0) return;
    if ((b & ~g_s[i].b) || hypot(lx, ly) >= 0.25 * DM_STICK_MAX || hypot(rx, ry) >= 0.25 * DM_STICK_MAX) g_glyph = glyph;   // 这只手柄有明确输入：键帽跟它的类型 | This controller gave clear input: the glyphs follow its type
    g_s[i].b = b; g_s[i].ax[0] = lx; g_s[i].ax[1] = ly; g_s[i].ax[2] = rx; g_s[i].ax[3] = ry; g_s[i].glyph = glyph; apply();
}
static void drop(const void *who) {   // 这个手柄没了：只拿掉它那一份；一个手柄都不剩才回触屏模式 | This controller is gone: remove only its share; return to touch mode only when no controller is left
    int i = slot_of(who, 0); if (i < 0) return; memset(&g_s[i], 0, sizeof g_s[i]); apply();
    for (int k = 0; k < NSLOT; k++) if (g_s[k].who) return;
    set_mode(0);
}
void dm_gamepad_touched(void) { if (g_pad_mode) { g_need_center = 1; set_mode(0); apply(); } }   // 前端：碰了屏幕。摇杆此刻偏着的话要先回中才重新接管 | Front end: the screen was touched. If a stick is deflected right now it must return to center before taking over again
void dm_gamepad_reset(void) {   // 前端：失活 / 进后台 / 来电 | Front end: resign active / enter background / incoming call
    int any = 0; for (int i = 0; i < NSLOT; i++) if (g_s[i].who) { any |= g_s[i].b != 0 || g_s[i].ax[0] || g_s[i].ax[1] || g_s[i].ax[2] || g_s[i].ax[3]; g_s[i].b = 0; memset(g_s[i].ax, 0, sizeof g_s[i].ax); }
#if TARGET_OS_OSX
    for (int i = 0; i < DM_PADBIND_NKEY; i++) any |= g_key_down[i];
    memset(g_key_down, 0, sizeof g_key_down); g_key_acts = 0; g_key_fired = 0;   // 键盘按着的也放掉（front_mac.m 失焦时调） | release what the keyboard holds too (front_mac.m calls this on focus loss)
#endif
    if (any) { LOG("手柄：失活，手柄的键和摇杆清零"); apply(); }
}
// 测试（触点脚本 pad / pad2 / padoff / padoff2 步）：没有实体手柄时走同一条路，只少了 GameController 回调那一层。n = 1 或 2：两个虚拟手柄
// Test (touch script pad / pad2 / padoff / padoff2 steps): without a physical controller the same path is taken, minus only the GameController callback
// layer. n = 1 or 2: two virtual controllers
void dm_gamepad_inject_n(int n, uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) { put((const void *)(intptr_t)(n == 2 ? 2 : 1), b, lx, ly, rx, ry, DM_GLYPH_PAD); }
void dm_gamepad_drop_n(int n) { LOG("手柄：断开（虚拟手柄 %d）", n == 2 ? 2 : 1); drop((const void *)(intptr_t)(n == 2 ? 2 : 1)); }
void dm_gamepad_inject(uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) { dm_gamepad_inject_n(1, b, lx, ly, rx, ry); }
static int is_xbox(GCController *c) { return [c.extendedGamepad isKindOfClass:[GCXboxGamepad class]] || [c.productCategory ?: @"" containsString:@"Xbox"]; }
static int is_ps(GCController *c) { NSString *k = c.productCategory; return [k isEqualToString:GCProductCategoryDualSense] || [k isEqualToString:GCProductCategoryDualShock4]; }
static int glyph_of(GCController *c) { return is_xbox(c) ? DM_GLYPH_XBOX : is_ps(c) ? DM_GLYPH_PS : DM_GLYPH_PAD; }   // 这只手柄用哪套键帽
static void publish(GCController *c, GCExtendedGamepad *g) {
    uint64_t b = 0; int xb = is_xbox(c);
    if (xb) b |= dm_padmap_face_xbox(g.buttonA.pressed, g.buttonB.pressed, g.buttonX.pressed, g.buttonY.pressed, 0);   // Xbox 手柄：按物理位置（dm_padmap.h） | Xbox controller: by physical position (dm_padmap.h)
    else b |= dm_padmap_face(g.buttonA.pressed, g.buttonB.pressed, g.buttonX.pressed, g.buttonY.pressed);   // 其余：系统给哪个字母发哪个字母（dm_padmap.h） | Others: send whichever letter the system reports (dm_padmap.h)
    if (g.leftShoulder.pressed) b |= DM_BTN_L; if (g.rightShoulder.pressed) b |= DM_BTN_R; if (g.leftTrigger.pressed) b |= DM_BTN_ZL; if (g.rightTrigger.pressed) b |= DM_BTN_ZR;
    if (g.leftThumbstickButton.pressed) b |= DM_BTN_LS; if (g.rightThumbstickButton.pressed) b |= DM_BTN_RS;
    if (g.buttonMenu.pressed) b |= DM_BTN_PLUS; if (g.buttonOptions.pressed) b |= DM_BTN_MINUS;
    if (g.dpad.left.pressed) b |= DM_BTN_LEFT; if (g.dpad.up.pressed) b |= DM_BTN_UP; if (g.dpad.right.pressed) b |= DM_BTN_RIGHT; if (g.dpad.down.pressed) b |= DM_BTN_DOWN;
    put((__bridge const void *)c, b, axis(g.leftThumbstick.xAxis.value), axis(g.leftThumbstick.yAxis.value), axis(g.rightThumbstick.xAxis.value), axis(g.rightThumbstick.yAxis.value), glyph_of(c));
}
static void attach(GCController *c) {
    GCExtendedGamepad *g = c.extendedGamepad; if (!g) { LOG("手柄：%s 不是扩展手柄，不接", c.vendorName.UTF8String ?: "?"); return; }
    LOG("手柄：连上 %s，类别 %s → %s", c.vendorName.UTF8String ?: "?", c.productCategory.UTF8String ?: "?", is_xbox(c) ? "Xbox 手柄：面键按位置排（下 A 右 B 上 X 左 Y）、Xbox 键帽" : is_ps(c) ? "面键 buttonA/B/X/Y 直接发 A/B/X/Y、PlayStation 键帽" : "面键 buttonA/B/X/Y 直接发 A/B/X/Y");
    __weak GCController *wc = c;
    g.valueChangedHandler = ^(GCExtendedGamepad *gp, GCControllerElement *el) { (void)el; GCController *sc = wc; if (sc) publish(sc, gp); };
}
void dm_gamepad_start(void (*on_mode)(int pad)) {
    g_on_mode = on_mode;
#if TARGET_OS_OSX
    // Mac：窗口没焦点时手柄照样进游戏（系统缺省只给前台 App）。defaults write <包标识> PadInBackground -bool NO 关掉
    // Mac: the gamepad keeps reaching the game while its window is not focused (by default the system only feeds the frontmost app). defaults write <bundle id> PadInBackground -bool NO turns it off
    { NSUserDefaults *d = NSUserDefaults.standardUserDefaults; BOOL bg = [d objectForKey:@"PadInBackground"] ? [d boolForKey:@"PadInBackground"] : YES;
      GCController.shouldMonitorBackgroundEvents = bg; LOG("手柄：后台输入%s", bg ? "开" : "关（PadInBackground=NO）"); }
#endif
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    [nc addObserverForName:GCControllerDidConnectNotification object:nil queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification *n) { attach(n.object); }];
    [nc addObserverForName:GCControllerDidDisconnectNotification object:nil queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification *n) {
        LOG("手柄：断开 %s", ((GCController *)n.object).vendorName.UTF8String ?: "?"); drop((__bridge const void *)n.object); }];
    for (GCController *c in GCController.controllers) attach(c);
}
