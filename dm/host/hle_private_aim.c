// skill-aim-v1-private：仅原技能ground消费者的方向输入适配，非世界/目标/资格setter。
// skill-aim-v1-private: direction input adaptation for the original skill ground consumer only, not a world/target/eligibility setter.
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
// 右摇杆（屏幕方向，x 右正、y 上正）→ 世界格方向的单位向量。按等距投影严格反算：屏幕 x ∝ dx − dy、屏幕上方 ∝ −(dx + dy) / 2，
// 所以 dx = (x − 2y) / 2、dy = (−x − 2y) / 2。拖哪个屏幕角度就朝哪个屏幕角度放（真机读数：原先借用游戏「移动摇杆 → 世界」的旋转常数，
// 摇杆 45° 走的是沿格子边 = 屏幕 26.6°，斜向各差 18.4°）
// Right stick (screen direction, x positive right, y positive up) → unit vector of the world cell direction. Strictly inverted from the isometric projection: screen x ∝ dx − dy, screen up ∝ −(dx + dy) / 2,
// so dx = (x − 2y) / 2, dy = (−x − 2y) / 2. Whatever screen angle is dragged is the screen angle cast toward (real device reading: formerly the game's "move stick → world" rotation constant was borrowed,
// and a 45° stick went along the cell edge = 26.6° on screen, each diagonal off by 18.4°)
int dm_private_aim_vector(int32_t rx,int32_t ry,float out[2]) {
#pragma clang fp contract(off)
    if(!rx && !ry)return 0;
    if(rx>32767)rx=32767;if(rx<-32767)rx=-32767;
    if(ry>32767)ry=32767;if(ry<-32767)ry=-32767;
    float x=(float)rx/32767.0f,y=(float)ry/32767.0f;
    float dx=(x-2.0f*y)*0.5f,dy=(-x-2.0f*y)*0.5f,n=sqrtf(dx*dx+dy*dy);
    if(!(n>0.0f) || !isfinite(n))return 0;
    out[0]=dx/n;out[1]=dy/n;
    return 1;
}
#ifndef DM_PRIVATE_AIM_VECTOR_ONLY
#include "dm.h"
#include "dm_input.h"
#include "dm_aim.h"
#include "dm_custom.h"
#include "dm_uiscreen.h"
#include <mach/mach.h>
static int read_live(uint64_t a,void *out,size_t n) {
    vm_size_t copied=0;
    return a && a<=UINT64_MAX-n && vm_read_overwrite(mach_task_self(),a,n,(vm_address_t)out,&copied)==KERN_SUCCESS && copied==n;
}
// ---- 按住拖动时由手指方向选目标（缺省启用，半角 AIM_CONE_DEG；测试开关 DM_AIM_CONE=<半角度数> 可调，=0 关掉回到原生选目标） ----
// 原生每帧按「距离 + 与移动摇杆方向 / 朝向的夹角」选怪（main+0x2f4320 / 0x2f8f90，±90°、20–22 格），右摇杆不参与，所以拖技能钮不改变打谁。
// 这里在算施放方向的同一刻（本适配，早于游戏取目标）：手指方向的锥内有怪 → 把它写进「指定目标槽」（管理器 +0xd10，点击入口 0x4acec4 优先于自动选的）；
// 锥内没怪 → 把当前技能手柄记录的 alwaysIgnoreTarget（记录 +0x18，旋风 / 传送原版就置位）临时置 1，游戏就按方向对位置放。右摇杆归零（点按不拖）时两处都恢复，原生不变。
// 每次施放（按下沿与按住重放）都重新选：锁着的那只死了 / 走出锥，下一次施放就换锥内夹角最小的另一只，锥内没有了就对位置放。
// ---- Target selection by finger direction during hold-and-drag (enabled by default, half angle AIM_CONE_DEG; adjustable with the test switch DM_AIM_CONE=<half angle in degrees>, =0 turns it off and returns to native target selection) ----
// Natively a monster is selected every frame by "distance + angle to the move stick direction / facing" (main+0x2f4320 / 0x2f8f90, ±90°, 20–22 cells), the right stick takes no part, so dragging a skill button does not change who gets hit.
// Here, at the same moment the cast direction is computed (this adaptation, earlier than the game fetching the target): a monster inside the finger direction cone → write it into the "designated target slot" (manager +0xd10, the click entry 0x4acec4 takes priority over the auto-selected one);
// no monster in the cone → temporarily set alwaysIgnoreTarget of the current skill's controller record (record +0x18, already set for Whirlwind / Teleport in the original game) to 1, and the game casts at the position by direction. When the right stick returns to zero (tap without drag) both places are restored, native behavior unchanged.
// Every cast (press edge and hold recast) selects again: if the locked one died / walked out of the cone, the next cast switches to another one with the smallest angle in the cone, and casts at the position if none is left in the cone.
#define AIM_CONE_DEG 30.0f
#define AIM_CONE_MAX_DIST 22.0f
static uint64_t g_cone_rec, g_cone_slot_mgr; static uint8_t g_cone_rec_was; static _Atomic int g_cone_set, g_cone_keep;   // keep = 位移类缺省不选怪设的，右摇杆归零时也不恢复 | keep = set by the movement default, not restored when the right stick returns to zero
static float cone_cos(void) { static float c = -2; if (c < -1.5f) { const char *e = getenv("DM_AIM_CONE"); float d = e ? (float)atof(e) : AIM_CONE_DEG; c = d > 0 ? cosf(d * 3.14159265f / 180.0f) : 2.0f; } return c; }   // 2 = 关掉 | 2 = off
static int cone_unit_xy(uint64_t u, float *x, float *y) { uint64_t pp = 0; uint16_t a = 0, c = 0; if (!read_live(u + 0x38, &pp, 8) || !pp || !read_live(pp + 2, &a, 2) || !read_live(pp + 6, &c, 2)) return 0; *x = (float)a + 0.5f; *y = (float)c + 0.5f; return 1; }   // 会走动的单位（玩家 / 怪）：路径结构 +2 / +6 是整数格（+0 / +4 是 16.16 定点的低半） | units that walk (players / monsters): path struct +2 / +6 are integer cells (+0 / +4 are the low half of the 16.16 fixed point)
static void cone_restore(void) {
    if (g_cone_rec) { *(uint8_t *)(g_cone_rec + 0x18) = g_cone_rec_was; g_cone_rec = 0; }
    if (g_cone_slot_mgr) { *(uint64_t *)(g_cone_slot_mgr + 0xd10) = 0; g_cone_slot_mgr = 0; }
    atomic_store(&g_cone_set, 0); atomic_store(&g_cone_keep, 0);
}
// 取手柄处每帧调（hle_hid.c）：右摇杆归零了就把动过的两处恢复（拖完之后没有再施放时，不能把指定目标留在槽里）
// Called every frame at the controller read (hle_hid.c): once the right stick returns to zero, restore the two places that were changed (when there is no further cast after the drag ends, the designated target must not be left in the slot)
// ---- 技能形态（dm_aim.h 三类）：手柄技能记录 + 技能表的施放函数号。施放适配与「松开才放」都用 ----
// ---- Skill shape (the three classes of dm_aim.h): the controller skill record + the skill table's cast function number. Used by both the cast adapter and "cast on release" ----
typedef uint64_t (*Mgr)(void); typedef uint64_t (*Rec)(uint64_t,int32_t); typedef float (*Range)(uint64_t,uint64_t,int);
static int free_get(const char *key, int def);   // aim_free.txt（见下） | aim_free.txt (see below)
static const module_t *aim_main(void) { static const module_t *m; if (!m) for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) m = &g_mod[i]; return m; }
// pl = 玩家单位（+0x1bd 版本号挑技能表）。取不到施放函数号 = NONE（不放远、不进自由瞄准，与原先「取不到记录的一律不放远」一致） | pl = the player unit (+0x1bd version picks the skill table). Cast function not readable = NONE (not pushed far, no free aim, consistent with the former "anything whose record cannot be fetched is not pushed far")
static int skill_shape(const module_t *m, uint64_t pl, int32_t sk, uint64_t *rec_out, float *dist_out, uint8_t *scaled_out, uint16_t *fn_out) {
    uint64_t mg = ((Mgr)(m->base + 0x2acecc))(), rec = sk >= 0 ? ((Rec)(m->base + 0x2b4bc8))(mg, sk) : 0; float dist = 0; uint8_t scaled = 1, ver = 0; uint16_t fn = 0; int cls = DM_AIM_NONE;
    if (rec && read_live(rec + 8, &dist, 4) && read_live(rec + 0x10, &scaled, 1) && pl && read_live(pl + 0x1bd, &ver, 1) && ver >= 1 && ver <= 3) {
        uint64_t tbl = 0, base = 0, cnt = 0;
        if (read_live((uint64_t)m->base + 0x2d64a18 + (uint64_t)ver * 16, &tbl, 8) && read_live(tbl + 0x11e8, &base, 8) && read_live(tbl + 0x11f0, &cnt, 8) && (uint64_t)sk < cnt &&
            read_live(base + (uint64_t)sk * 0x2ec + 0x4e, &fn, 2)) cls = dm_aim_class(sk, fn, dist, scaled); }
    if (rec_out) *rec_out = rec; if (dist_out) *dist_out = dist; if (scaled_out) *scaled_out = scaled; if (fn_out) *fn_out = fn; return cls;
}
// 实体键（A B X Y R ZR）现在绑的技能号：面 → 格表 main+0x2d13cbc（6 × {逻辑键, 普通格, 替代格 = 按着 ZL}）、格 → 技能表 main+0x2d5f0c0（每格 28 字节，武器第二组 +16 格；那格空着看第一组），读法同 dm_padmode.c / dm_freetp.c。没有 = −1
// The skill id bound right now to a physical button (A B X Y R ZR): face -> slot table main+0x2d13cbc (6 × {logical key, normal slot, alternate slot = ZL held}), slot -> skill table main+0x2d5f0c0 (28 bytes per slot, the second weapon set +16 slots; an empty one falls back to the first set), read like dm_padmode.c / dm_freetp.c. None = -1
static int32_t button_skill(const module_t *m, uint64_t bit, int zl) {
    static const struct { uint64_t b; uint32_t lg; } face[6] = { { DM_BTN_A, 0x2000 }, { DM_BTN_B, 0x1000 }, { DM_BTN_X, 0x8000 }, { DM_BTN_Y, 0x4000 }, { DM_BTN_R, 0x200 }, { DM_BTN_ZR, 0x800 } };
    uint32_t want = 0; for (int f = 0; f < 6; f++) if (face[f].b == bit) want = face[f].lg; if (!want) return -1;
    int dm_swapskill_wset(void); int bank = dm_swapskill_wset() ? 16 : 0; const uint8_t *b = (const uint8_t *)m->base;
    for (int i = 0; i < 6; i++) { uint32_t lg, sl[2], id = 0xffffffffu; memcpy(&lg, b + 0x2d13cbc + 12 * i, 4); memcpy(sl, b + 0x2d13cbc + 12 * i + 4, 8); if (lg != want || sl[zl != 0] >= 12) continue;
        memcpy(&id, b + 0x2d5f0c0 + (uint64_t)(bank + sl[zl != 0]) * 28, 4); if (id == 0xffffffffu && bank) memcpy(&id, b + 0x2d5f0c0 + (uint64_t)sl[zl != 0] * 28, 4);
        return id == 0xffffffffu ? -1 : (int32_t)id; }
    return -1;
}
static float g_rg_seen[1024];   // 每个技能施放时游戏算出的射程（0x2b5c70，按等级）：「松开才放」预览落点用；没放过 = 手柄记录的缺省距离 | the range the game computed for each skill at cast time (0x2b5c70, by level): used by the "cast on release" landing preview; never cast = the controller record's default distance
static void range_seen(int32_t sk, float rg) { if (sk >= 0 && sk < 1024 && rg > 0.0f && isfinite(rg)) g_rg_seen[sk] = rg; }
// ---- 松开才放（Victor 2026-11-04：按住时预览跟着人走、碍事；想按住瞄好、松开才出）----
// 开着时，实体手柄按下的技能键若绑的是位移 / 落点类：按住期间不交给游戏（人也停下，左摇杆只用来瞄），松开那一刻替玩家按 3 次取手柄长的一下，游戏按那时的方向放；
// 飞弹类与其余照原样按下就放（瞬发不需要预览）。开关：aim_free.txt 的 release_cast（缺省 0）定开机时的值，手柄键位的宿主动作 RELEASECAST 切换。面板 / 局外 / 触屏布局下不拦，拦着时离开局内就当没按
// ---- Cast on release (Victor 2026-11-04: while held the preview moves with the character and gets in the way; he wants to hold to aim and cast on release) ----
// While on, a skill button pressed on the physical controller whose skill is movement / ground zone: not handed to the game while held (the character stops too; the left stick only aims), and at the moment of release it is pressed for the player for 3 controller reads, so the game casts with the direction of that moment;
// skillshots and the rest still cast on press as before (instant casts need no preview). Switch: release_cast in aim_free.txt (default 0) sets the starting value, the controller remap host action RELEASECAST toggles it. Not intercepted in panels / outside a run / in the touch layout; a hold in progress that leaves the run counts as never pressed
static _Atomic int g_rel = -1;
static int release_on(void) { int v = atomic_load(&g_rel); if (v < 0) { v = free_get("release_cast", 0) != 0; atomic_store(&g_rel, v); } return v; }
void dm_aim_release_toggle(void) { int on = !release_on(); atomic_store(&g_rel, on); LOG("松开才放：切换 → %s", on ? "开（位移 / 落点类按住瞄、松开放）" : "关（按下就放）"); }
static struct { uint64_t bit; int zl, cls, inject; int32_t sk, lx, ly; } g_hold;
static _Atomic int g_left_ovr; static _Atomic uint64_t g_left_ovr_v;
static void hold_clear(void) { memset(&g_hold, 0, sizeof g_hold); atomic_store(&g_left_ovr, 0); }
static void hold_left(int32_t lx, int32_t ly) { g_hold.lx = lx; g_hold.ly = ly; atomic_store(&g_left_ovr_v, (uint32_t)lx | (uint64_t)(uint32_t)ly << 32); atomic_store(&g_left_ovr, 1); }
// 施放时取左摇杆：按住瞄的时候游戏收到的是 0（人不走），用按住期间玩家推的那份 | The left stick at cast time: while holding to aim the game receives 0 (the character does not walk), so use what the player pushed during the hold
static void aim_left(int32_t *lx, int32_t *ly) { if (atomic_load(&g_left_ovr)) { uint64_t v = atomic_load(&g_left_ovr_v); *lx = (int32_t)(uint32_t)v; *ly = (int32_t)(uint32_t)(v >> 32); } else dm_input_sampled_left(lx, ly); }
int dm_padmode_current(void);
// 取手柄处（hle_hid.c fill，游戏主线程）：pad = 实体手柄此刻按着的键 | At the controller read (hle_hid.c fill, game main thread): pad = physical controller buttons held right now
uint64_t dm_aim_release_filter(uint64_t buttons, uint64_t pad, int32_t *lx, int32_t *ly) {
    static uint64_t prev; uint64_t fresh = pad & ~prev; prev = pad;
    const module_t *m = aim_main(); int world = m && dm_uiscreen_ingame() && !dm_uiscreen_panel() && dm_padmode_current();
    if (g_hold.inject) { buttons |= g_hold.bit | (g_hold.zl ? DM_BTN_ZL : 0); *lx = *ly = 0; if (--g_hold.inject == 0) hold_clear(); return buttons; }
    if (g_hold.bit) {
        if (!world) { LOG("松开才放：离开局内 / 开了面板，按着的技能 %d 不放", g_hold.sk); hold_clear(); return buttons; }
        if (pad & g_hold.bit) { buttons &= ~g_hold.bit; hold_left(*lx, *ly); *lx = *ly = 0; return buttons; }
        g_hold.inject = 3; buttons |= g_hold.bit | (g_hold.zl ? DM_BTN_ZL : 0); *lx = *ly = 0;
        { static int lg; if (lg++ < 40) LOG("松开才放：松开 → 放技能 %d（类别 %d，左摇杆 %d,%d）", g_hold.sk, g_hold.cls, g_hold.lx, g_hold.ly); }
        return buttons; }
    if (!world || !release_on() || !(fresh & (DM_BTN_A | DM_BTN_B | DM_BTN_X | DM_BTN_Y | DM_BTN_R | DM_BTN_ZR))) return buttons;
    uint64_t pl = ((uint64_t (*)(uint32_t))(m->base + 0x45f9d0))(*(uint32_t *)(m->base + 0x2d20e4c)); int zl = (pad & DM_BTN_ZL) != 0;
    for (int b = 0; b < 16; b++) { uint64_t bit = 1ull << b; if (!(fresh & bit & (DM_BTN_A | DM_BTN_B | DM_BTN_X | DM_BTN_Y | DM_BTN_R | DM_BTN_ZR))) continue;
        int32_t sk = button_skill(m, bit, zl); int cls = sk >= 0 ? skill_shape(m, pl, sk, NULL, NULL, NULL, NULL) : DM_AIM_NONE;
        if (cls != DM_AIM_MOVE && cls != DM_AIM_ZONE) continue;
        g_hold.bit = bit; g_hold.zl = zl; g_hold.cls = cls; g_hold.sk = sk; buttons &= ~bit; hold_left(*lx, *ly); *lx = *ly = 0;
        { static int lg; if (lg++ < 40) LOG("松开才放：按住 %s%s = 技能 %d（类别 %d）→ 先不放，瞄", zl ? "ZL+" : "", b == 0 ? "A" : b == 1 ? "B" : b == 2 ? "X" : b == 3 ? "Y" : b == 7 ? "R" : "ZR", sk, cls); }
        break; }
    return buttons;
}
// ---- 瞄准指示（dm_aim.h dm_aim_view，前端 dm_aim_hud.m 画）：只在手柄版布局、镜头实验关着时 ----
// 人物脚下的地面圈 + 朝瞄准方向的尖（实体手柄右摇杆推着时，或「松开才放」按住瞄的时候）；按住瞄时再加落点圈：位移类 = 左摇杆方向 × 射程（不推 = 原地），落点类 = 右摇杆方向 × 推的比例 × 射程。
// 按下就放的施放不画（瞬发不需要预览）。每帧在取手柄处换成画面坐标发布（脚底取游戏自己的投影，dm_world.c）
// ---- Aim indicator (dm_aim.h dm_aim_view, drawn by the front end dm_aim_hud.m): only in the controller layout and with the camera experiment off ----
// A ground ring under the character + a tip toward the aim direction (while the physical controller right stick is pushed, or while holding to aim under "cast on release"); while holding to aim also a landing circle: movement = left stick direction × range (not pushed = in place), ground zone = right stick direction × push fraction × range.
// Casts on press are not drawn (instant casts need no preview). Converted to picture coordinates and published every frame at the controller read (the feet come from the game's own projection, dm_world.c)
static dm_aim_view g_av; static _Atomic uint32_t g_av_seq;
int dm_cam_on(void); int dm_world_player_screen(float *fx, float *fy, int *W, int *H);
static void aim_view_pump(void) {
    dm_aim_view v; memset(&v, 0, sizeof v); float fx, fy; int W, H; const module_t *m = aim_main();
    if (m && dm_padmode_current() && !dm_cam_on() && dm_world_player_screen(&fx, &fy, &W, &H)) {
        float d[2]; int32_t rx = 0, ry = 0; dm_input_sampled_right(&rx, &ry); int rpad = dm_input_sampled_right_pad();
        if (g_hold.bit && !g_hold.inject) {
            uint64_t pl = ((uint64_t (*)(uint32_t))(m->base + 0x45f9d0))(*(uint32_t *)(m->base + 0x2d20e4c)); float dist = 0; skill_shape(m, pl, g_hold.sk, NULL, &dist, NULL, NULL);
            float rg = g_hold.sk >= 0 && g_hold.sk < 1024 && g_rg_seen[g_hold.sk] > 0 ? g_rg_seen[g_hold.sk] : dist; v.kind = 3;
            if (g_hold.cls == DM_AIM_MOVE) { v.lr = 1.0f; if (dm_private_aim_vector(g_hold.lx, g_hold.ly, d)) { v.dx = d[0]; v.dy = d[1]; v.lx = d[0] * rg; v.ly = d[1] * rg; } }
            else { v.lr = 2.5f; if (dm_private_aim_vector(rx, ry, d)) { float f = rpad ? dm_aim_frac(sqrtf((float)rx * rx + (float)ry * ry) / 32767.0f) : 1.0f; v.dx = d[0]; v.dy = d[1]; v.lx = d[0] * f * rg; v.ly = d[1] * f * rg; } else v.kind = 1; } }
        else if (rpad && dm_private_aim_vector(rx, ry, d)) { v.kind = 1; v.dx = d[0]; v.dy = d[1]; }
        if (v.kind) { v.fx = fx / W; v.fy = fy / H; } }
    atomic_fetch_add(&g_av_seq, 1); g_av = v; atomic_fetch_add(&g_av_seq, 1);
}
int dm_aim_view_get(dm_aim_view *v) {
    for (int t = 0; t < 4; t++) { uint32_t a = atomic_load(&g_av_seq); if (a & 1) continue; *v = g_av; if (atomic_load(&g_av_seq) == a) return v->kind; }
    memset(v, 0, sizeof *v); return 0;
}
void dm_aim_cone_pump(void) {
    aim_view_pump();
    if (!atomic_load(&g_cone_set)) return; int32_t rx = 0, ry = 0; dm_input_sampled_right(&rx, &ry);
    if (!(getenv("DM_TEST_AIM") || getenv("DM_TEST_AIM_AT")) && !rx && !ry && !dm_aim_free_on() && !atomic_load(&g_cone_keep)) { cone_restore(); return; }   // 自由瞄准开着时不恢复：右摇杆本来就不动 | not restored while free aim is on: the right stick is not moved then anyway
}
// ---- 自由瞄准（PC 按住 Shift 放技能 = 不选怪、朝方向放）：手柄键位的宿主动作 FREEAIM（按住）/ FREEAIMTOGGLE（开关），dm_gamepad.m 调 dm_aim_free_set ----
// 按住是「反过来」（dm_aim_ignore_target）：位移类平常就不选怪，按住时改回选怪。
// 生效时表里的技能（aim_free.txt，缺省 = 位移类，见 dm_aim.h）：不走锥内选怪，指定目标槽清零、alwaysIgnoreTarget 置 1（同上面「锥内没怪」那条已实测的路），方向取左摇杆。
// 测试开关 DM_TEST_FREEAIM=1 = 一直开着（按键脚本没有宿主动作）
// ---- Free aim (the PC's hold-Shift cast = pick no monster, cast by direction): the controller remap host actions FREEAIM (held) / FREEAIMTOGGLE (on/off), dm_gamepad.m calls dm_aim_free_set ----
// Holding is "the inverse" (dm_aim_ignore_target): movement skills usually pick no monster, and pick one again while it is held.
// While in effect the listed skills (aim_free.txt, default = movement, see dm_aim.h) skip the cone pick, clear the designated target slot and set alwaysIgnoreTarget to 1 (the same path as "no monster in the cone" above, already measured), with the direction taken from the left stick.
// Test switch DM_TEST_FREEAIM=1 = always on (the key script has no host actions)
static _Atomic int g_free_hold, g_free_toggle;
void dm_aim_free_set(int hold, int toggle_edge) {
    atomic_store(&g_free_hold, hold != 0);
    if (toggle_edge) { int on = !atomic_fetch_xor(&g_free_toggle, 1); LOG("自由瞄准：切换 → %s", on ? "开" : "关"); }
}
static int free_toggle_on(void) { static int t = -1; if (t < 0) t = getenv("DM_TEST_FREEAIM") != NULL; return t || atomic_load(&g_free_toggle); }
int dm_aim_free_on(void) { return free_toggle_on() || atomic_load(&g_free_hold); }
// aim_free.txt（自定义目录，键=值，# 注释）：move / zone / shot = 类别开关，skill_<号> = 单个技能。整份缓存、2 秒重读一次（按住连放每拍都要查）
// aim_free.txt (custom directory, key=value, # comments): move / zone / shot = class switches, skill_<id> = a single skill. Cached whole, re-read every 2 seconds (a held recast asks every tick)
static char g_free_txt[2048]; static uint64_t g_free_txt_ns;
static int free_get(const char *key, int def) {
    uint64_t t = now_ns(); if (!g_free_txt_ns || t - g_free_txt_ns > 2000000000ull) { g_free_txt_ns = t; g_free_txt[0] = 0; const char *d = dm_custom_dir(); char p[1100];
        if (d) { snprintf(p, sizeof p, "%s/aim_free.txt", d); FILE *f = fopen(p, "r"); if (f) { size_t n = fread(g_free_txt, 1, sizeof g_free_txt - 1, f); g_free_txt[n] = 0; fclose(f); } } }
    size_t kn = strlen(key); int v = def;
    for (const char *l = g_free_txt; *l; ) { const char *e = strchr(l, '\n'); if (!e) e = l + strlen(l);
        while (l < e && (*l == ' ' || *l == '\t')) l++;
        if (*l != '#' && (size_t)(e - l) > kn && !strncmp(l, key, kn)) { const char *q = l + kn; while (q < e && (*q == ' ' || *q == '\t')) q++; if (q < e && *q == '=') v = atoi(q + 1); }
        l = *e ? e + 1 : e; }
    return v;
}
// 测试开关 DM_TEST_AIM_FRAC=<0–1>：位移 / 落点类的方向乘这个比例，看落点是否跟着缩短（离线验证「方向不归一化、距离 = 方向 × 射程」对服务端也成立） | Test switch DM_TEST_AIM_FRAC=<0-1>: the direction of movement / ground zone skills is multiplied by this fraction, to see whether the landing point shortens with it (checks that "direction is not normalized, distance = direction × range" also holds on the server)
static float test_frac(void) { static float f = -1; if (f < 0) { const char *e = getenv("DM_TEST_AIM_FRAC"); f = e ? (float)atof(e) : 1.0f; if (!(f >= 0 && f <= 1)) f = 1.0f; } return f; }
// 在客户端单位表里找手指方向锥内的怪：类别 1（main+0x2f7b7c）、可选中（0x464a70）、22 格内、夹角余弦 ≥ 门槛；取夹角最小的。out_n = 22 格内的怪总数（日志用）
// 测试：DM_TEST_AIM_DROP=<n>——同一只被锁满 n 次施放后当它死了（从候选里剔掉），看下一次施放换谁。真死亡走的是同一个剔除（模式 0 / 12、不可选中）
// Find a monster inside the finger direction cone in the client unit table: category 1 (main+0x2f7b7c), selectable (0x464a70), within 22 cells, cosine of the angle ≥ threshold; take the one with the smallest angle. out_n = total number of monsters within 22 cells (for logging)
// Test: DM_TEST_AIM_DROP=<n> -- after the same one has been locked for n casts, treat it as dead (remove it from the candidates) and see who the next cast switches to. Real death goes through the same removal (mode 0 / 12, not selectable)
static uint32_t g_cone_dropped[8]; static int g_cone_ndrop;
static uint64_t cone_pick(const module_t *m, uint64_t pl, const float pos[2], const float vec[2], float cmin, int *out_n, float *out_cos) {
    uint64_t best = 0; float bc = -2; int n = 0;
    for (int k = 0; k < 128; k++) for (uint64_t u = *(uint64_t *)(m->base + 0x2d603a0 + 1 * 0x400 + k * 8); u; u = *(uint64_t *)(u + 0x158)) {
        float x, y; uint32_t mode = *(uint32_t *)(u + 0xc); if (mode == 0 || mode == 12 || !cone_unit_xy(u, &x, &y)) continue;
        if (((int (*)(int, uint64_t, uint64_t))(m->base + 0x2f7b7c))(0x165, pl, u) != 1 || !(((int (*)(uint64_t, int, int, int, int))(m->base + 0x464a70))(u, 0, 0, -1, -1) & 1)) continue;
        { int dr = 0; for (int i = 0; i < g_cone_ndrop; i++) if (g_cone_dropped[i] == *(uint32_t *)(u + 8)) dr = 1; if (dr) continue; }
        float dx = x - pos[0], dy = y - pos[1], d = sqrtf(dx * dx + dy * dy); if (d < 0.01f || d > AIM_CONE_MAX_DIST) continue; n++;
        float c = (dx * vec[0] + dy * vec[1]) / d; if (c >= cmin && c > bc) { bc = c; best = u; } }
    *out_n = n; *out_cos = bc; return best;
}
// 测试：DM_TEST_AIM_AT=m0 / m1 / gap——右摇杆方向取「朝最近 / 第二近的怪」/「背离所有怪的最大空档」（怪的位置每局不同，脚本里写不死角度）。返回 1 = 已填 rx / ry
// Test: DM_TEST_AIM_AT=m0 / m1 / gap -- the right stick direction is taken as "toward the nearest / second-nearest monster" / "the largest gap facing away from all monsters" (monster positions differ every run, an angle cannot be hard-coded in the script). Returns 1 = rx / ry filled
static int cone_test_dir(const module_t *m, uint64_t pl, const float pos[2], int32_t *rx, int32_t *ry) {
    const char *e = getenv("DM_TEST_AIM_AT"); if (!e) return 0; float ang[64], dist[64]; uint32_t ids[64]; int n = 0;
    static int latched; static float la; if (latched) { *rx = (int32_t)(32767.0f * cosf(la)); *ry = (int32_t)(32767.0f * sinf(la)); return 1; }   // 带 L 后缀（m0L / m1L）：第一次算出的方向锁住不再跟怪，模拟手指不动 | with the L suffix (m0L / m1L): the direction computed the first time is locked and no longer follows the monster, simulating a finger that does not move
    for (int k = 0; k < 128 && n < 64; k++) for (uint64_t u = *(uint64_t *)(m->base + 0x2d603a0 + 1 * 0x400 + k * 8); u && n < 64; u = *(uint64_t *)(u + 0x158)) {
        float x, y; uint32_t mode = *(uint32_t *)(u + 0xc); if (mode == 0 || mode == 12 || !cone_unit_xy(u, &x, &y)) continue;
        if (((int (*)(int, uint64_t, uint64_t))(m->base + 0x2f7b7c))(0x165, pl, u) != 1) continue;
        float dx = x - pos[0], dy = y - pos[1], d = sqrtf(dx * dx + dy * dy); if (d < 0.01f || d > AIM_CONE_MAX_DIST) continue;
        ang[n] = atan2f(-(dx + dy) * 0.5f, dx - dy); dist[n] = d; ids[n] = *(uint32_t *)(u + 8); n++; }   // 屏幕角：x ∝ dx − dy，上 ∝ −(dx + dy) / 2 | screen angle: x ∝ dx − dy, up ∝ −(dx + dy) / 2
    float a = 0;
    if (!strncmp(e, "gap", 3)) { if (n) { for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (ang[j] < ang[i]) { float t = ang[i]; ang[i] = ang[j]; ang[j] = t; }
            float bg = -1; for (int i = 0; i < n; i++) { float g = (i + 1 < n ? ang[i + 1] : ang[0] + 6.2831853f) - ang[i]; if (g > bg) { bg = g; a = ang[i] + g * 0.5f; } }
            { static int lg; if (lg++ < 6) LOG("瞄准测试方向 gap：22 格内怪 %d 只，朝最大空档中间（屏幕角 %.0f°，空档宽 %.0f°）", n, a * 57.29578f, bg * 57.29578f); } } }
    else { int want = e[1] == '1', bi = -1, b2 = -1; for (int i = 0; i < n; i++) { if (bi < 0 || dist[i] < dist[bi]) { b2 = bi; bi = i; } else if (b2 < 0 || dist[i] < dist[b2]) b2 = i; }
        int pick = want ? b2 : bi; if (pick < 0) return 0; a = ang[pick];
        { static int lg; if (lg++ < 6) LOG("瞄准测试方向 %s：22 格内怪 %d 只，朝 id %u（距 %.1f 格，屏幕角 %.0f°）；最近 id %u 距 %.1f", e, n, ids[pick], dist[pick], a * 57.29578f, ids[bi], dist[bi]); } }
    if (strchr(e, 'L')) { latched = 1; la = a; }
    *rx = (int32_t)(32767.0f * cosf(a)); *ry = (int32_t)(32767.0f * sinf(a)); return 1;
}
void svc_private_skill_aim(uint64_t *regs) {
    const module_t *m=NULL;for(int i=0;i<g_nmod;i++)if(!strcmp(g_mod[i].name,"main"))m=&g_mod[i];
    if(!m)die("skill-aim缺main");
    typedef void (*Original)(uint64_t,uint64_t,float *,float *);
    float *position=(float *)regs[2],*direction=(float *)regs[3];
    ((Original)(m->base+0x2b4ef8))(regs[0],regs[1],position,direction); // 先保原位置/零aim和所有原回退。 | first preserve the original position/zero aim and all original fallbacks.
    uint64_t caller=0;uint32_t type=99,id=0,current=0;
    int scope=regs[29]<=UINT64_MAX-8 && read_live(regs[29]+8,&caller,8) &&
        // 两个调用点都算「朝哪放」：0x2b5b80 = 技能键按下沿（0x2b5b7c 调 0x2fae98）；0x2b3298 = 按住期间每拍重放（0x2b3294 调 0x2fae98，后加：
        // 只认按下沿时，按住拖动的重放回退到角色朝向，连发的位置包坐标不变）。第三个调用点 0x2b352c（光标瞄准型技能取初始点）没纳入，没有现场验。
        // 产物里的 aim.tsv 只记了按下沿那一个（离线规则脚本没动：改它会让所有产物判过期重出，而这里的作用域是宿主运行时判的）
        // Both call sites compute "which way to cast": 0x2b5b80 = skill button press edge (0x2b5b7c calls 0x2fae98); 0x2b3298 = per-tick recast while held (0x2b3294 calls 0x2fae98, added later:
        // when only the press edge was handled, the recast of a hold-and-drag fell back to the character's facing and the coordinates of the repeated position packets did not change). The third call site 0x2b352c (cursor-aimed skills fetching the initial point) is not included, there was no test case to verify it.
        // aim.tsv in the build output only records the press-edge one (the offline rule script was not touched: changing it would make all build outputs be judged stale and rebuilt, while the scope here is judged at host runtime)
        (caller==(uint64_t)m->base+0x2b5b80 || caller==(uint64_t)m->base+0x2b3298) &&
        read_live(regs[1],&type,4) && !type && read_live(regs[1]+8,&id,4) &&
        read_live((uint64_t)m->base+0x2d60380,&current,4) && id==current;
    if(!scope)return;
    int32_t rx=0,ry=0;dm_input_sampled_right(&rx,&ry);
    { const char *t=getenv("DM_TEST_AIM");if(t)sscanf(t,"%d,%d",&rx,&ry); }   // 测试：顶替右摇杆采样（宿主按键脚本没有右摇杆伪键） | test: stands in for right stick sampling (the host key script has no right stick pseudo-keys)
    { float p0[2]={position[0],position[1]}; cone_test_dir(m,regs[1],p0,&rx,&ry); }
    float vec[2],original[2]={direction[0],direction[1]};int active=dm_private_aim_vector(rx,ry,vec),fr=dm_aim_free_on();
    // 每次施放都进来取类别：位移类缺省不选怪，不拖也要改（dm_aim_ignore_target）；其余类别不拖、没开自由瞄准 = 原生
    // Every cast comes in to get the class: movement skills pick no monster by default, even without a drag (dm_aim_ignore_target); other classes without a drag and without free aim = native
    {
        // 目标点 = 位置 + 方向 × 射程（游戏在 0x2fac70 里乘，射程取自 0x2b5c70），再取整到格。近战类射程只有 2–4 格，取整后方向只剩 8 个档（斜向吸到格子边）；
        // 落点距离无所谓的技能把方向放大，让目标点落到 24 格外（目标格相对角色真实位置的取整误差 ≤1.7°；按整数格差量、不计角色站位小数时 ≤3°——16 格时真机按格差量到正上 / 正下各偏 5°）。实测（圣骑普通攻击，目标点 12 格外按住 4 发）：人原地挥砍、不走动。射程更大的技能落点有意义，不动
        // Target point = position + direction × range (the game multiplies in 0x2fac70, range comes from 0x2b5c70), then rounded to a cell. Melee-type range is only 2–4 cells, after rounding only 8 direction steps remain (diagonals snap to cell edges);
        // for skills whose landing distance does not matter the direction is scaled up so the target point lands beyond 24 cells (rounding error of the target cell relative to the character's real position ≤1.7°; measured by integer cell difference, ignoring the fractional part of the character's position, ≤3° -- at 16 cells the real device measured by cell difference 5° off for straight up / straight down each). Measured (Paladin normal attack, target point beyond 12 cells, held for 4 casts): the character swings in place and does not walk. For skills with a larger range the landing point is meaningful, untouched
        // 哪些技能放远，按游戏自己的手柄技能配置判（包内 controllerskillsettings.json，载入后是手柄技能记录，main+0x2b4bc8(管理器, 技能号) 取）：
        // 记录 +8 = defaultTargetDistance（设计师给这个技能填的手柄落点距离，射程函数 0x2b5c70 返回的就是它），+0x10 = 「按技能等级算」标志。
        // 缺省 2.0 = 没填 = 落点距离无所谓（普通攻击、近战挥砍、飞弹、原地施法）→ 放远取方向；填了距离的（冲锋 12、旋风 10、跳跃 16.6、传送 23、
        // 暴风雪 / 陷阱 12–15、召唤 4–6）落点有意义 → 原生 方向 × 距离，不动。唯一「距离 2.0 却会位移」的是龙翔，用施放函数号排除：
        // 技能表记录（步长 0x2ec，[版本表 + 0x11e8]）+0x4e = srvdofunc，27 / 52 / 67 / 76 / 77 / 78 是位移类（传送 / 龙翔 / 冲锋 / 旋风 / 跳跃 / 跳跃攻击）。
        // 取不到记录的一律不放远。当前技能号 = 管理器（0x2fae98 的 x23）里这个玩家槽（x25）的记录 +0xd4c（0x2faf50–0x2faf54 读的同一处）
        // Which skills get pushed far is judged by the game's own controller skill configuration (controllerskillsettings.json in the package, loaded as controller skill records, fetched with main+0x2b4bc8(manager, skill id)):
        // record +8 = defaultTargetDistance (the controller landing distance the designer filled in for this skill, it is what the range function 0x2b5c70 returns), +0x10 = the "computed by skill level" flag.
        // Default 2.0 = not filled in = landing distance does not matter (normal attack, melee swings, missiles, cast in place) → push far and take the direction; with a distance filled in (Charge 12, Whirlwind 10, Leap 16.6, Teleport 23,
        // Blizzard / traps 12–15, summons 4–6) the landing point is meaningful → native direction × distance, untouched. The only one with "distance 2.0 but still displaces" is Dragon Flight, excluded by its cast function number:
        // skill table record (stride 0x2ec, [version table + 0x11e8]) +0x4e = srvdofunc, 27 / 52 / 67 / 76 / 77 / 78 are the displacement kind (Teleport / Dragon Flight / Charge / Whirlwind / Leap / Leap Attack).
        // Anything whose record cannot be fetched is not pushed far. Current skill id = record +0xd4c of this player slot (x25) in the manager (x23 of 0x2fae98) (the same place 0x2faf50–0x2faf54 read)
        int32_t sk=-1;read_live(regs[23]+(uint64_t)(uint32_t)regs[25]*0xb8+0xd4c,&sk,4);
        uint64_t mg=((Mgr)(m->base+0x2acecc))(),rec=sk>=0?((Rec)(m->base+0x2b4bc8))(mg,sk):0;   // 记录也给锥内选怪 / 自由瞄准置 alwaysIgnoreTarget 用 | the record is also used by the cone pick / free aim to set alwaysIgnoreTarget
        float dist=0.0f;uint8_t scaled=1;uint16_t fn=0;int cls=skill_shape(m,regs[1],sk,NULL,&dist,&scaled,&fn);   // 类别（dm_aim.h，skill_shape 见上） | class (dm_aim.h, skill_shape above)
        int far=cls==DM_AIM_SHOT;
        if(rec && dm_aim_ignore_target(cls,sk,free_toggle_on(),atomic_load(&g_free_hold),free_get)) {   // 自由瞄准：左摇杆方向（松着 = 原方向），不选怪 | free aim: the left stick's direction (released = the original direction), no monster picked
            int32_t lx=0,ly=0;aim_left(&lx,&ly);float lv[2],f=test_frac();int lon=dm_private_aim_vector(lx,ly,lv);
            if(lon){direction[0]=lv[0]*f;direction[1]=lv[1]*f;} else {direction[0]*=f;direction[1]*=f;}
            cone_restore(); *(uint64_t *)(mg+0xd10)=0; g_cone_slot_mgr=mg; g_cone_rec=rec; g_cone_rec_was=*(uint8_t *)(rec+0x18); *(uint8_t *)(rec+0x18)=1; atomic_store(&g_cone_set,1); atomic_store(&g_cone_keep,!fr);
            range_seen(sk,((Range)(m->base+0x2b5c70))(mg,regs[1],0));
            { static int lg; if(lg++<40)LOG("自由瞄准：技能号 %d 类别 %d 施放函数 %u → 不选怪，方向 %s（%.2f, %.2f）比例 %.2f",sk,cls,fn,lon?"左摇杆":"原方向",direction[0],direction[1],f); }
        } else if(!active) { if(atomic_load(&g_cone_set))cone_restore(); }   // 不拖 = 原生 | no drag = native
        else {
            // 落点类 + 实体手柄右摇杆：角度给方向、推的幅度给距离（dm_aim_frac：推到底 = 射程），不进锥内选怪（锁怪会把落点拉到怪身上）；触屏拖动照旧
            // Ground zone + physical controller right stick: the angle gives the direction and the push amount the distance (dm_aim_frac: fully pushed = the range), no cone pick (locking a monster would pull the landing point onto it); touch drags unchanged
            int zone=cls==DM_AIM_ZONE && dm_input_sampled_right_pad() && rec; float zf=zone?dm_aim_frac(sqrtf((float)rx*rx+(float)ry*ry)/32767.0f):1.0f;
            float rg=((Range)(m->base+0x2b5c70))(mg,regs[1],0),k=(far && rg>0.0f && isfinite(rg))?24.0f/rg:zone?zf:(cls==DM_AIM_MOVE||cls==DM_AIM_ZONE)?test_frac():1.0f;
            { static int lg; if(getenv("DM_SKILL_AIM_LOG") && lg++<12)LOG("瞄准适配：技能号 %d 手柄落点距离 %.2f 施放函数 %u 类别 %d 射程 %.2f 放大 %.1f",sk,dist,fn,cls,rg,k); }
            direction[0]=vec[0]*k;direction[1]=vec[1]*k;
            range_seen(sk,rg);
            if(zone) { cone_restore(); *(uint64_t *)(mg+0xd10)=0; g_cone_slot_mgr=mg; g_cone_rec=rec; g_cone_rec_was=*(uint8_t *)(rec+0x18); *(uint8_t *)(rec+0x18)=1; atomic_store(&g_cone_set,1);
                { static int lg; if(lg++<40)LOG("落点瞄准：技能号 %d 施放函数 %u → 推 %.0f%% 射程 %.1f 格，不选怪",sk,fn,zf*100.0f,rg); } }
            else if(cone_cos()<=1.0f) {   // 手指方向选目标 | target selection by finger direction
                cone_restore(); int nmon=0; float bc=0; float p0[2]={position[0],position[1]};
                uint64_t tgt=cone_pick(m,regs[1],p0,vec,cone_cos(),&nmon,&bc);
                if(tgt){ *(uint64_t *)(mg+0xd10)=tgt; g_cone_slot_mgr=mg; }
                else if(rec){ g_cone_rec=rec; g_cone_rec_was=*(uint8_t *)(rec+0x18); *(uint8_t *)(rec+0x18)=1; }
                { static int dn=-1; static uint32_t lid, cnt; if(dn<0){ const char *e=getenv("DM_TEST_AIM_DROP"); dn=e?atoi(e):0; } if(dn>0&&tgt){ uint32_t id=*(uint32_t *)(tgt+8); cnt=id==lid?cnt+1:1; lid=id; if(cnt>=(uint32_t)dn&&g_cone_ndrop<8){ g_cone_dropped[g_cone_ndrop++]=id; LOG("瞄准测试：id %u 被锁满 %d 次，之后当它死了",id,dn); } } }
                atomic_store(&g_cone_set,1);
                { static int lg; static uint32_t last=0xfffffffe; uint32_t now=tgt?*(uint32_t *)(tgt+8):0xffffffff; if(now!=last && lg++<60){ last=now; uint32_t tc=0,ti=0; if(tgt){ tc=*(uint32_t *)(tgt+4); ti=*(uint32_t *)(tgt+8); }
                    LOG("瞄准选目标：22 格内怪 %d 只；手指方向锥内 %s（类号 %u id %u，夹角 %.1f°）→ %s",nmon,tgt?"有":"无",tc,ti,tgt?acosf(bc>1?1:bc)*57.29578f:0.0f,tgt?"锁它":"对位置放"); } }
            }
        }
    }
    const char *path=getenv("DM_SKILL_AIM_LOG");if(path){static FILE *f;static unsigned n;if(!f)f=fopen(path,"w");
        if(f && n++<128){fprintf(f,"{\"event\":\"skill_ground_adapter\",\"time_ns\":%llu,\"caller\":\"%#llx\",\"unit_id\":%u,\"rx\":%d,\"ry\":%d,\"active\":%d,\"position\":[%.9g,%.9g],\"original_direction\":[%.9g,%.9g],\"direction\":[%.9g,%.9g]}\n",now_ns(),caller-(uint64_t)m->base,id,rx,ry,active,position[0],position[1],original[0],original[1],direction[0],direction[1]);fflush(f);}}
}
#endif
