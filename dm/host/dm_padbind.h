// 实体手柄键位重映射：每颗实体键 → 一组 Switch 键（可以一颗不发、发一颗、或发「ZL + 技能格键」这种组合）。纯函数，前端 dm_gamepad.m 用，宿主单测 tests/t_padbind.c 核。
//   没有配置 = 原样（每颗键发它自己），与没有这一层时一字不差。
//   按下那一刻记下这颗键要发什么（latch），松开前一直发这一组：中途改了表 / 开了面板，松开时放掉的仍是按下时发的那组，不会卡键。
//   各颗键的输出按位或：L3 → ZL 按着、L2 → ZL+X 按了又松，ZL 仍由 L3 撑着。
//   分段（stage_ms > 0）：一组里同时有 ZL 和别的键时，先发 ZL，过 stage_ms 再发别的键；松开时先放别的键，ZL 再撑 stage_ms（游戏按住 ZL 才认第二套技能，见 dm_freetp_zr_free 的 zl 参数）。
//   菜单里（menu = 1）新按下的键原样发：储藏箱 / 编年史用 L / R / ZL / ZR 翻页签，重映射只管局内。
//   组合（MINUS+UP=MAPSIZE）：按着修饰键（− ）再按触发键（↑）发组合的输出。当修饰键用的键按下后先等 DM_PADBIND_CHORD_MS：这段时间里按了触发键 = 组合，修饰键自己什么都不发；
//   过了还没组合 = 照常按住，开始发它自己的输出（按住连放技能照旧，只晚这几十毫秒）；按住途中再按触发键，组合照样成立，修饰键自己的输出当场放掉。
//   等的时间里就松开 = 单按，补发它自己的输出 DM_PADBIND_TAP_MS 毫秒。修饰键最多的那个组合优先。
//   键盘（Mac App）：同一张表另有一栏「键盘键 → 输出」（KEY_G=RS），输出跟手柄一样是 Switch 键或宿主动作；没写的键按缺省（dm_padbind_key_def，即帮助页的键位对照）。
//   键盘一颗键一个输出，没有组合和分段；WASD（移动）、回车、Esc 不进表，前端照旧。
//   输出除了 Switch 键还可以是宿主动作（DM_PADACT_*，高位）：按下那一刻记进 st->fired，前端取走执行，不发给游戏；st->acts = 此刻按着的宿主动作（地图平移按住一直走）。
// Physical controller remapping: each physical button -> a set of Switch buttons (none, one, or a combination such as "ZL + a skill-slot button"). Pure functions, used by the front end dm_gamepad.m, checked by the host unit test tests/t_padbind.c.
//   No configuration = unchanged (each button sends itself), byte for byte the same as without this layer.
//   What a button sends is recorded the moment it goes down (latch) and sent until release: if the table changes or a panel opens in between, the release still drops what was sent on press, so nothing sticks.
//   Outputs of all buttons are ORed: with L3 -> ZL held, pressing and releasing L2 -> ZL+X leaves ZL held by L3.
//   Staging (stage_ms > 0): when a set has ZL plus other buttons, ZL goes first and the others follow after stage_ms; on release the others drop first and ZL stays stage_ms longer (the game only takes the second skill set while ZL is held, see the zl parameter of dm_freetp_zr_free).
//   In menus (menu = 1) newly pressed buttons are sent unchanged: the stash / Chronicle switch tabs with L / R / ZL / ZR; remapping only applies in game.
//   Combos (MINUS+UP=MAPSIZE): holding the modifier (−) and pressing the trigger (↑) sends the combo's output. A button used as a modifier first waits DM_PADBIND_CHORD_MS: a trigger pressed within it = the combo, and the modifier sends nothing itself;
//   no combo by then = an ordinary hold, it starts sending its own output (holding to keep casting still works, only these few tens of ms later); pressing a trigger later while holding still forms the combo and drops the modifier's own output at once.
//   Released within the wait = a tap, its own output is sent for DM_PADBIND_TAP_MS milliseconds. The combo with the most modifiers wins.
//   Keyboard (Mac App): the same table has a second column "keyboard key -> output" (KEY_G=RS), outputs are Switch buttons or host actions like the controller's; keys not written use the default (dm_padbind_key_def, the Help page's key reference).
//   One output per keyboard key, no combos and no staging; WASD (movement), Return and Esc are not in the table and the front end keeps handling them.
//   Besides Switch buttons an output can hold host actions (DM_PADACT_*, high bits): they are recorded in st->fired the moment they go down, the front end takes and runs them, and they are not sent to the game; st->acts = host actions held right now (map panning keeps moving while held).
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "dm_input.h"

enum { DM_PADBIND_N = 16, DM_PADBIND_COMBOS = 32, DM_PADBIND_TAP_MS = 80, DM_PADBIND_CHORD_MS = 120 };   // 实体键 = 原版键位位 0–15（A B X Y LS RS L R ZL ZR + − ← ↑ → ↓）；组合最多 32 条；修饰键单按补发的毫秒；修饰键等组合的毫秒 | Physical buttons = original button bits 0-15; at most 32 combos; milliseconds a lone modifier tap is sent; milliseconds a modifier waits for a combo
// 宿主动作：输出的位 48 起（实体手柄只有 0–15，前端自用位不进表的输出） | Host actions: output bits from 48 (the physical controller only has 0-15; the front end's own bits never appear in table outputs)
enum { DM_PADBIND_ACT0 = 48, DM_PADBIND_NACT = 13 };
#define DM_PADACT_MAPSIZE (1ull << 48)      // 地图：全屏 ↔ 小地图（dm_ui_map_full） | map: fullscreen <-> minimap (dm_ui_map_full)
#define DM_PADACT_TOWNPORTAL (1ull << 49)   // 免费回城（dm_tp_request） | free town portal (dm_tp_request)
#define DM_PADACT_MAPPOS (1ull << 50)       // 小地图放左 ↔ 放右（dm_ui_map_pos_toggle） | minimap left <-> right (dm_ui_map_pos_toggle)
#define DM_PADACT_MAPZOOMIN (1ull << 51)    // 地图放大一步（dm_ui_map_zoom） | map zoom in one step (dm_ui_map_zoom)
#define DM_PADACT_MAPZOOMOUT (1ull << 52)   // 地图缩小一步 | map zoom out one step
#define DM_PADACT_MAPLEFT (1ull << 53)      // 地图平移（按住一直走，dm_ui_map_pan）：左 / 右 / 上 / 下 | map panning (keeps moving while held, dm_ui_map_pan): left / right / up / down
#define DM_PADACT_MAPRIGHT (1ull << 54)
#define DM_PADACT_MAPUP (1ull << 55)
#define DM_PADACT_MAPDOWN (1ull << 56)
#define DM_PADACT_MAPCENTER (1ull << 57)    // 平移归零（dm_ui_map_pan_reset） | reset panning (dm_ui_map_pan_reset)
#define DM_PADACT_FREEAIM (1ull << 58)      // 按住 = 自由瞄准（PC 的 Shift：表里的技能不选怪，朝左摇杆方向放，hle_private_aim.c） | held = free aim (the PC's Shift: listed skills pick no monster and go the left stick's way, hle_private_aim.c)
#define DM_PADACT_FREEAIMTOGGLE (1ull << 59)   // 自由瞄准开 ↔ 关 | free aim on <-> off
#define DM_PADACT_RELEASECAST (1ull << 60)   // 松开才放开 ↔ 关（位移 / 落点类按住瞄、松开放，hle_private_aim.c） | cast on release on <-> off (movement / ground zone: hold to aim, release to cast, hle_private_aim.c)
#define DM_PADACT_ALL (((1ull << DM_PADBIND_NACT) - 1) << DM_PADBIND_ACT0)
typedef struct { uint64_t mods; int btn; uint64_t out; } dm_padbind_combo;   // 按着 mods 的全部再按 btn → out | holding all of mods and pressing btn -> out
// 键盘键：名字（KEY_ 之后那段）与 Mac 虚拟键码。只许往后加（文件里按名字存，不按序号） | Keyboard keys: name (the part after KEY_) and Mac virtual key code. Append only (files store names, not indices)
typedef struct { const char *name; unsigned short code; } dm_padbind_key;
static const dm_padbind_key dm_padbind_keys[] = {
    {"B", 11}, {"C", 8}, {"E", 14}, {"F", 3}, {"G", 5}, {"H", 4}, {"I", 34}, {"J", 38}, {"K", 40}, {"L", 37}, {"M", 46}, {"N", 45}, {"O", 31}, {"P", 35}, {"Q", 12}, {"R", 15}, {"T", 17}, {"U", 32}, {"V", 9}, {"X", 7}, {"Y", 16}, {"Z", 6},
    {"1", 18}, {"2", 19}, {"3", 20}, {"4", 21}, {"5", 23}, {"6", 22}, {"7", 26}, {"8", 28}, {"9", 25}, {"0", 29},
    {"F1", 122}, {"F2", 120}, {"F3", 99}, {"F4", 118}, {"F5", 96}, {"F6", 97}, {"F7", 98}, {"F8", 100}, {"F9", 101}, {"F10", 109}, {"F11", 103}, {"F12", 111},
    {"SPACE", 49}, {"TAB", 48}, {"EQUAL", 24}, {"MINUS", 27}, {"LBRACKET", 33}, {"RBRACKET", 30}, {"SEMICOLON", 41}, {"QUOTE", 39}, {"COMMA", 43}, {"PERIOD", 47}, {"SLASH", 44}, {"GRAVE", 50}, {"BACKSLASH", 42},
    {"LEFT", 123}, {"UP", 126}, {"RIGHT", 124}, {"DOWN", 125}, {"PADPLUS", 69},
};
enum { DM_PADBIND_NKEY = sizeof dm_padbind_keys / sizeof *dm_padbind_keys };
static inline int dm_padbind_key_index(unsigned short code) { for (int i = 0; i < DM_PADBIND_NKEY; i++) if (dm_padbind_keys[i].code == code) return i; return -1; }
// 键盘缺省（与帮助页「键盘与手柄按键对照」一致）：J K U I → A B X Y，F G → 按摇杆，Q E Z C → L R ZL ZR，Tab → −，= 与小键盘 + → ＋，空格 → A，方向键 → 十字键
// Keyboard defaults (as on the Help page "Keyboard and Controller Buttons"): J K U I -> A B X Y, F G -> stick presses, Q E Z C -> L R ZL ZR, Tab -> −, = and keypad + -> +, Space -> A, arrow keys -> D-pad
static inline uint64_t dm_padbind_key_def(int i) {
    switch (dm_padbind_keys[i].code) {
        case 38: case 49: return DM_BTN_A; case 40: return DM_BTN_B; case 32: return DM_BTN_X; case 34: return DM_BTN_Y;
        case 3: return DM_BTN_LS; case 5: return DM_BTN_RS; case 12: return DM_BTN_L; case 14: return DM_BTN_R; case 6: return DM_BTN_ZL; case 8: return DM_BTN_ZR;
        case 48: return DM_BTN_MINUS; case 24: case 69: return DM_BTN_PLUS;
        case 123: return DM_BTN_LEFT; case 126: return DM_BTN_UP; case 124: return DM_BTN_RIGHT; case 125: return DM_BTN_DOWN;
    }
    return 0;
}
typedef struct { uint64_t out[DM_PADBIND_N]; int stage_ms; int ncombo; dm_padbind_combo combo[DM_PADBIND_COMBOS]; uint64_t kout[DM_PADBIND_NKEY]; } dm_padbind_table;   // kout：键盘各键的输出 | kout: each keyboard key's output   // stage_ms：分段毫秒（0 = 不分段） | stage_ms: staging milliseconds (0 = no staging)
// pend = 当修饰键按着、还在等组合；used = 按着期间组过组合（松开前不再发）；tap = 修饰键单按、正在补发；fired = 这一步新按下的宿主动作（前端取走后清零）；acts = 此刻按着的宿主动作
// pend = held as a modifier, still waiting for a combo; used = formed a combo while held (sends nothing until released); tap = lone modifier tap being sent; fired = host actions that went down in this step (the front end clears it after taking them); acts = host actions held right now
typedef struct { uint64_t held, ignore, latch[DM_PADBIND_N], t_down[DM_PADBIND_N], t_up[DM_PADBIND_N], tail, pend, used, tap, fired, acts; } dm_padbind_state;

static const char *const dm_padbind_names[DM_PADBIND_N] = { "A", "B", "X", "Y", "LS", "RS", "L", "R", "ZL", "ZR", "PLUS", "MINUS", "LEFT", "UP", "RIGHT", "DOWN" };
static const char *const dm_padbind_act_names[DM_PADBIND_NACT] = { "MAPSIZE", "TOWNPORTAL", "MAPPOS", "MAPZOOMIN", "MAPZOOMOUT", "MAPLEFT", "MAPRIGHT", "MAPUP", "MAPDOWN", "MAPCENTER", "FREEAIM", "FREEAIMTOGGLE", "RELEASECAST" };

static inline void dm_padbind_identity(dm_padbind_table *t) { memset(t, 0, sizeof *t); for (int i = 0; i < DM_PADBIND_N; i++) t->out[i] = 1ull << i; for (int i = 0; i < DM_PADBIND_NKEY; i++) t->kout[i] = dm_padbind_key_def(i); }
static inline int dm_padbind_is_identity(const dm_padbind_table *t) { for (int i = 0; i < DM_PADBIND_N; i++) if (t->out[i] != 1ull << i) return 0; return !t->ncombo; }   // 只看手柄（键盘那栏不影响手柄这层） | controller only (the keyboard column does not affect this layer)
// 设一条组合：同一组 mods + btn 的旧条目换掉；out 为 ~0 = 删掉这条。满了返回 0 | Set a combo: an old entry with the same mods + btn is replaced; out ~0 = delete it. Returns 0 when full
static inline int dm_padbind_set_combo(dm_padbind_table *t, uint64_t mods, int btn, uint64_t out) {
    for (int k = 0; k < t->ncombo; k++) if (t->combo[k].mods == mods && t->combo[k].btn == btn) {
        if (out == ~0ull) { t->combo[k] = t->combo[--t->ncombo]; } else t->combo[k].out = out; return 1; }
    if (out == ~0ull) return 1;
    if (t->ncombo >= DM_PADBIND_COMBOS) return 0;
    t->combo[t->ncombo++] = (dm_padbind_combo){ mods, btn, out }; return 1;
}

static inline int dm_padbind_name(const char *s, size_t n) {   // 键名 → 位；认不出 = -1 | Button name -> bit; unknown = -1
    for (int i = 0; i < DM_PADBIND_N; i++) if (strlen(dm_padbind_names[i]) == n && !strncasecmp(s, dm_padbind_names[i], n)) return i;
    return -1;
}
static inline uint64_t dm_padbind_out_token(const char *s, size_t n) {   // 输出一项：键或宿主动作 → 位；认不出 = 0 | One output token: a button or a host action -> bit; unknown = 0
    int b = dm_padbind_name(s, n); if (b >= 0) return 1ull << b;
    for (int a = 0; a < DM_PADBIND_NACT; a++) if (strlen(dm_padbind_act_names[a]) == n && !strncasecmp(s, dm_padbind_act_names[a], n)) return 1ull << (DM_PADBIND_ACT0 + a);
    return 0;
}
// 解析「LS=ZL;ZL=ZL+X;RS=NONE;MINUS+UP=MAPSIZE;STAGE=40」：分号或换行分条，等号左边一颗实体键（或 + 连起来的组合：最后一颗是触发键，前面的是修饰键），
// 右边用 + 连起来的 Switch 键 / 宿主动作，或 NONE；STAGE = 分段毫秒（0–500）；# 开头的行是注释。没提到的键原样。
// 返回 1 = 全部认得；有认不出的条目返回 0，表保持原样（不半套生效）。
// Parses "LS=ZL;ZL=ZL+X;RS=NONE;MINUS+UP=MAPSIZE;STAGE=40": entries split by semicolons or newlines, one physical button left of '=' (or a combo joined by '+': the last button is the trigger, the ones before are modifiers),
// Switch buttons / host actions joined by '+' or NONE on the right; STAGE = staging milliseconds (0-500); lines starting with # are comments. Buttons not mentioned stay unchanged.
// Returns 1 = everything recognized; any unknown entry returns 0 and leaves the table unchanged (never half applied).
static inline int dm_padbind_parse(const char *s, dm_padbind_table *t) {
    dm_padbind_table r; dm_padbind_identity(&r);
    while (s && *s) {
        const char *e = s; while (*e && *e != ';' && *e != '\n') e++;
        const char *eq = memchr(s, '=', (size_t)(e - s));
        const char *a = s; while (a < e && (*a == ' ' || *a == '\t' || *a == '\r')) a++;
        if (a < e && *a != '#') {
            if (!eq) return 0;
            const char *ae = eq; while (ae > a && (ae[-1] == ' ' || ae[-1] == '\t')) ae--;
            if (ae - a == 5 && !strncasecmp(a, "STAGE", 5)) {
                char *end; long v = strtol(eq + 1, &end, 10); while (end < e && (*end == ' ' || *end == '\t' || *end == '\r')) end++;
                if (end == eq + 1 || end != e || v < 0 || v > 500) return 0;
                r.stage_ms = (int)v; s = *e ? e + 1 : e; continue;
            }
            int key = -1;
            if (ae - a > 4 && !strncasecmp(a, "KEY_", 4)) {   // 键盘键：KEY_<名> | keyboard key: KEY_<name>
                for (int i = 0; i < DM_PADBIND_NKEY; i++) if (strlen(dm_padbind_keys[i].name) == (size_t)(ae - a - 4) && !strncasecmp(a + 4, dm_padbind_keys[i].name, (size_t)(ae - a - 4))) key = i;
                if (key < 0) return 0;
            }
            uint64_t mods = 0; int src = -1;
            if (key < 0) for (const char *p = a;;) {   // 左边：一颗键，或「修饰键+…+触发键」 | left side: one button, or "modifier+...+trigger"
                while (p < ae && (*p == ' ' || *p == '\t')) p++;
                const char *q = p; while (q < ae && *q != '+') q++;
                const char *qe = q; while (qe > p && (qe[-1] == ' ' || qe[-1] == '\t')) qe--;
                int b = dm_padbind_name(p, (size_t)(qe - p)); if (b < 0) return 0;
                if (src >= 0) { if (mods & 1ull << src) return 0; mods |= 1ull << src; }
                src = b; if (q >= ae) break; p = q + 1;
            }
            if (key < 0 && (mods & 1ull << src)) return 0;
            uint64_t m = 0; const char *p = eq + 1;
            for (;;) {
                while (p < e && (*p == ' ' || *p == '\t')) p++;
                const char *q = p; while (q < e && *q != '+') q++;
                const char *qe = q; while (qe > p && (qe[-1] == ' ' || qe[-1] == '\t' || qe[-1] == '\r')) qe--;
                if (qe - p == 4 && !strncasecmp(p, "NONE", 4)) { if (m) return 0; }
                else { uint64_t b = dm_padbind_out_token(p, (size_t)(qe - p)); if (!b) return 0; m |= b; }
                if (q >= e) break; p = q + 1;
            }
            if (key >= 0) r.kout[key] = m; else if (!mods) r.out[src] = m; else if (!dm_padbind_set_combo(&r, mods, src, m)) return 0;
        }
        s = *e ? e + 1 : e;
    }
    *t = r; return 1;
}

// 写成 dm_padbind_parse 认的文本：STAGE 一行，之后只写改过的键（一行一条），再写组合，最后写改过的键盘键。返回写出的长度；放不下返回 0。
// Formats the text dm_padbind_parse reads: one STAGE line, then only the changed buttons (one per line), then the combos, then the changed keyboard keys. Returns the length written; 0 if it does not fit.
static inline int dm_padbind_put(char *buf, size_t cap, size_t *n, const char *s) { size_t l = strlen(s); if (*n + l + 1 > cap) return 0; memcpy(buf + *n, s, l + 1); *n += l; return 1; }
static inline int dm_padbind_put_out(char *buf, size_t cap, size_t *n, uint64_t m) {   // 输出：键按键序，再宿主动作；空 = NONE | output: buttons in bit order, then host actions; empty = NONE
    if (!m) return dm_padbind_put(buf, cap, n, "NONE");
    int first = 1;
    for (int b = 0; b < DM_PADBIND_N; b++) if (m & 1ull << b) { if ((!first && !dm_padbind_put(buf, cap, n, "+")) || !dm_padbind_put(buf, cap, n, dm_padbind_names[b])) return 0; first = 0; }
    for (int a = 0; a < DM_PADBIND_NACT; a++) if (m & 1ull << (DM_PADBIND_ACT0 + a)) { if ((!first && !dm_padbind_put(buf, cap, n, "+")) || !dm_padbind_put(buf, cap, n, dm_padbind_act_names[a])) return 0; first = 0; }
    return 1;
}
static inline size_t dm_padbind_format(const dm_padbind_table *t, char *buf, size_t cap) {
    size_t n = 0; char st[32]; snprintf(st, sizeof st, "STAGE=%d\n", t->stage_ms); if (!cap) return 0; buf[0] = 0;
    if (!dm_padbind_put(buf, cap, &n, st)) return 0;
    for (int i = 0; i < DM_PADBIND_N; i++) {
        if (t->out[i] == 1ull << i) continue;
        if (!dm_padbind_put(buf, cap, &n, dm_padbind_names[i]) || !dm_padbind_put(buf, cap, &n, "=") || !dm_padbind_put_out(buf, cap, &n, t->out[i]) || !dm_padbind_put(buf, cap, &n, "\n")) return 0;
    }
    for (int k = 0; k < t->ncombo; k++) {
        const dm_padbind_combo *c = &t->combo[k];
        for (int b = 0; b < DM_PADBIND_N; b++) if (c->mods & 1ull << b) { if (!dm_padbind_put(buf, cap, &n, dm_padbind_names[b]) || !dm_padbind_put(buf, cap, &n, "+")) return 0; }
        if (!dm_padbind_put(buf, cap, &n, dm_padbind_names[c->btn]) || !dm_padbind_put(buf, cap, &n, "=") || !dm_padbind_put_out(buf, cap, &n, c->out) || !dm_padbind_put(buf, cap, &n, "\n")) return 0;
    }
    for (int i = 0; i < DM_PADBIND_NKEY; i++) {
        if (t->kout[i] == dm_padbind_key_def(i)) continue;
        if (!dm_padbind_put(buf, cap, &n, "KEY_") || !dm_padbind_put(buf, cap, &n, dm_padbind_keys[i].name) || !dm_padbind_put(buf, cap, &n, "=") || !dm_padbind_put_out(buf, cap, &n, t->kout[i]) || !dm_padbind_put(buf, cap, &n, "\n")) return 0;
    }
    return n;
}
// 改表时调用：此刻按着的键全部放掉，并且松开前不再算数（要重新按一次才按新表发） | Call when the table changes: everything held now is released and ignored until it is let go (it sends by the new table on the next press)
static inline void dm_padbind_reset(dm_padbind_state *st, uint64_t phys) { memset(st, 0, sizeof *st); st->ignore = phys & ((1ull << DM_PADBIND_N) - 1); }

// 一步：phys = 实体键此刻按着的位，now_ms = 单调毫秒。返回发给游戏的键（宿主动作不在里面，按下那一刻记进 st->fired）。*wake_ms = 还要在多少毫秒后再调一次（分段 / 尾巴 / 单按补发到期），没有 = -1。
// One step: phys = physical buttons held now, now_ms = monotonic milliseconds. Returns the buttons sent to the game (host actions are not in it; they are recorded in st->fired when they go down). *wake_ms = call again after this many milliseconds (a stage, tail or tap expires), none = -1.
static inline uint64_t dm_padbind_step(const dm_padbind_table *t, dm_padbind_state *st, uint64_t phys, uint64_t now_ms, int menu, int stage_ms, int *wake_ms) {
    uint64_t mask = (1ull << DM_PADBIND_N) - 1, extra = phys & ~mask, out = 0, mods = 0; int wake = -1;
    phys &= mask; st->ignore &= phys;
    if (!menu) for (int k = 0; k < t->ncombo; k++) mods |= t->combo[k].mods;
    st->acts = 0;
#define DM_PADBIND_WAKE(w_) do { int w__ = (w_); if (wake < 0 || w__ < wake) wake = w__; } while (0)
    for (int i = 0; i < DM_PADBIND_N; i++) {
        uint64_t bit = 1ull << i; int now_on = (phys & bit) && !(st->ignore & bit), was_on = (st->held & bit) != 0;
        if (now_on && !was_on) {
            int best = -1, bestn = 0;
            if (!menu) for (int k = 0; k < t->ncombo; k++) { const dm_padbind_combo *c = &t->combo[k]; int nm = __builtin_popcountll(c->mods);
                if (c->btn == i && !(c->mods & ~st->held) && nm > bestn) { best = k; bestn = nm; } }
            if (menu) st->latch[i] = bit;
            else if (best >= 0) {   // 组合成立：修饰键自己的输出当场放掉（还在等的就不再发） | combo formed: the modifiers' own outputs are dropped at once (those still waiting never send)
                const dm_padbind_combo *c = &t->combo[best]; st->latch[i] = c->out; st->used |= c->mods; st->pend &= ~c->mods;
                for (int j = 0; j < DM_PADBIND_N; j++) if (c->mods & 1ull << j) st->latch[j] = 0; }
            else if (mods & bit) { st->latch[i] = 0; st->pend |= bit; }
            else st->latch[i] = t->out[i];
            st->t_down[i] = now_ms; st->held |= bit; st->tap &= ~bit; st->fired |= st->latch[i] & DM_PADACT_ALL;
        }
        if (!now_on && was_on) {
            st->held &= ~bit; st->t_up[i] = now_ms;
            if (st->pend & bit) {   // 修饰键在等的时间里松开：单按，补发它自己的输出 | modifier released within the wait: a tap, send its own output
                st->pend &= ~bit; st->latch[i] = t->out[i]; st->tap |= bit; st->fired |= st->latch[i] & DM_PADACT_ALL;
            } else if (stage_ms > 0 && (st->latch[i] & DM_BTN_ZL) && (st->latch[i] & mask & ~(uint64_t)DM_BTN_ZL)) st->tail |= bit;
            st->used &= ~bit;
        }
        if ((st->pend & bit) && (st->held & bit)) {   // 修饰键等够了还没组合：照常按住，从这一刻起发它自己的输出 | the modifier waited long enough with no combo: an ordinary hold, its own output starts now
            uint64_t age = now_ms - st->t_down[i];
            if (age >= DM_PADBIND_CHORD_MS) { st->pend &= ~bit; st->latch[i] = t->out[i]; st->t_down[i] = now_ms; st->fired |= st->latch[i] & DM_PADACT_ALL; }
            else DM_PADBIND_WAKE(DM_PADBIND_CHORD_MS - (int)age);
        }
    }
    // 第二遍才算输出：同一步里后面的键组成组合时，前面修饰键的输出也不漏发 | Outputs in a second pass: when a later button forms a combo in the same step, the earlier modifier's output does not leak out
    for (int i = 0; i < DM_PADBIND_N; i++) {
        uint64_t bit = 1ull << i;
        if ((st->held & bit) && !(st->pend & bit)) st->acts |= st->latch[i] & DM_PADACT_ALL;
        uint64_t l = st->latch[i] & mask, mod = l & DM_BTN_ZL, rest = l & ~(uint64_t)DM_BTN_ZL;
        int staged = stage_ms > 0 && mod && rest;
        if (st->held & bit) {
            if (!staged) out |= l;
            else { uint64_t age = now_ms - st->t_down[i]; out |= mod; if (age >= (uint64_t)stage_ms) out |= rest; else DM_PADBIND_WAKE(stage_ms - (int)age); }
        } else if (st->tap & bit) {   // 单按补发：分段时先 ZL，到点再全发 TAP 毫秒 | tap: with staging ZL first, then everything for TAP ms
            uint64_t age = now_ms - st->t_up[i], pre = staged ? (uint64_t)stage_ms : 0;
            if (age < pre) { out |= mod; DM_PADBIND_WAKE((int)(pre - age)); }
            else if (age < pre + DM_PADBIND_TAP_MS) { out |= l; DM_PADBIND_WAKE((int)(pre + DM_PADBIND_TAP_MS - age)); }
            else st->tap &= ~bit;
        } else if (st->tail & bit) {
            uint64_t age = now_ms - st->t_up[i];
            if (age < (uint64_t)stage_ms) { out |= mod; DM_PADBIND_WAKE(stage_ms - (int)age); } else st->tail &= ~bit;
        }
    }
#undef DM_PADBIND_WAKE
    if (wake_ms) *wake_ms = wake;
    return out | extra;
}
