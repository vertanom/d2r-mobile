// 手柄瞄准按技能形态分三类（纯函数，hle_private_aim.c 用，宿主单测 tests/t_aim.c 核）：
//   位移（跳跃 / 跳跃攻击 / 冲锋 / 传送 / 旋风 / 龙翔 / 刃之跃迁）= 施放函数号 srvdofunc 27 / 52 / 67 / 76 / 77 / 78，外加技能号 390：左摇杆瞄，自由瞄准时不选怪；
//   飞弹类（普通攻击、近战挥砍、飞弹、原地施法）= 手柄落点距离 ≤ 2.0 且不按等级算：右摇杆微调方向（原有的锥内选怪）；
//   落点类（暴风雪 / 陷阱 / 召唤）= 其余有距离的：右摇杆的角度给方向、推的幅度给距离。
// Controller aiming splits skills into three shapes (pure functions, used by hle_private_aim.c, checked by the host unit test tests/t_aim.c):
//   movement (Leap / Leap Attack / Charge / Teleport / Whirlwind / Dragon Flight / Blade Warp) = cast function srvdofunc 27 / 52 / 67 / 76 / 77 / 78, plus skill id 390: aimed with the left stick, picks no monster under free aim;
//   skillshot (normal attack, melee swings, missiles, cast in place) = controller landing distance ≤ 2.0 and not computed by level: the right stick fine-tunes the direction (the existing cone target pick);
//   ground zone (Blizzard / traps / summons) = the rest that have a distance: the right stick's angle gives the direction, how far it is pushed gives the distance.
#pragma once
#include <stdint.h>
#include <stdio.h>
enum { DM_AIM_NONE, DM_AIM_SHOT, DM_AIM_MOVE, DM_AIM_ZONE };
static inline int dm_aim_is_move_fn(int fn) { return fn == 27 || fn == 52 || fn == 67 || fn == 76 || fn == 77 || fn == 78; }
// 按技能号补进位移类的：刃之跃迁 Blade Warp（术士，技能号 390）——施放函数 166 与镜刃 Mirrored Blades（392）共用，不能按函数号收（skills.txt 1.0.34 实读）
// Added to movement by skill id: Blade Warp (Warlock, skill id 390) -- its cast function 166 is shared with Mirrored Blades (392), so it cannot be taken by function number (read from skills.txt 1.0.34)
static inline int dm_aim_is_move_skill(int sk) { return sk == 390; }
static inline int dm_aim_class(int sk, int fn, float dist, int scaled) {
    if (dm_aim_is_move_fn(fn) || dm_aim_is_move_skill(sk)) return DM_AIM_MOVE;
    if (!(dist > 0.0f)) return DM_AIM_NONE;
    return dist <= 2.0f && !scaled ? DM_AIM_SHOT : DM_AIM_ZONE;
}
// 自由瞄准对这个技能生不生效：skill_<号> 单独写了就按它，否则按类别开关（缺省只开位移类）。get = 读设置的键（没有 = def）
// Whether free aim applies to this skill: an explicit skill_<id> wins, otherwise the class switch (by default only movement is on). get = reads a settings key (missing = def)
static inline int dm_aim_free_listed(int cls, int skill, int (*get)(const char *key, int def)) {
    char k[24]; snprintf(k, sizeof k, "skill_%d", skill); int v = get(k, -1); if (v >= 0) return v != 0;
    if (cls == DM_AIM_MOVE) return get("move", 1) != 0;
    if (cls == DM_AIM_ZONE) return get("zone", 0) != 0;
    if (cls == DM_AIM_SHOT) return get("shot", 0) != 0;
    return 0;
}
// 这一下施放要不要不选怪。先定「平常」：位移类缺省一直不选（move_always=1，Victor 2026-11-04：跳跃按住左摇杆跳满射程、不推就原地跳，其余位移技能也这样），
// 自由瞄准开关（toggle_on）开着时表里的其余类别也不选。按住 FREEAIM（hold）= 反过来（Victor 2026-11-05）：平常不选怪的这时选怪（刃之跃迁按住就跳到怪身上），平常选怪的这时不选。
// 只管技能表里的（skill_<号>=0 单独关掉的一个、不在表里的类别：按住也不变）
// Whether this cast picks no monster. First the "usual": movement skills by default always pick none (move_always=1, Victor 2026-11-04: Leap with the left stick held jumps the full range, without it jumps in place, and the other movement skills should do the same),
// and while the free aim switch (toggle_on) is on the other listed classes pick none too. Holding FREEAIM (hold) = the inverse (Victor 2026-11-05): what usually picks no monster picks one now (Blade Warp held warps onto a monster), what usually picks one picks none.
// Only for listed skills (one turned off by skill_<id>=0, or a class not in the list: holding changes nothing)
static inline int dm_aim_ignore_target(int cls, int skill, int toggle_on, int hold, int (*get)(const char *key, int def)) {
    if (!dm_aim_free_listed(cls, skill, get)) return 0;
    int usual = toggle_on || (cls == DM_AIM_MOVE && get("move_always", 1));
    return usual != (hold != 0);
}
// 瞄准指示（前端画，dm_aim_hud.m）：kind 位 1 = 人物脚下的地面圈（d 非零时带朝 d 的尖），位 2 = 落点圈（在玩家 + l 处，地上半径 lr 格）。
// f = 玩家脚底（画面归一化 0–1，同 dm_map_goal_view.sx / sy）；d = 世界格方向的单位向量（0 = 没方向）；l = 落点相对玩家的格差。前端用 dm_aim_cells_to_px 按自己画面的高换算
// Aim indicator (drawn by the front end, dm_aim_hud.m): kind bit 1 = the ground ring under the character (with a tip toward d when d is non-zero), bit 2 = the landing circle (at player + l, radius lr cells on the ground).
// f = the player's feet (picture-normalized 0-1, like dm_map_goal_view.sx / sy); d = unit vector in world cell directions (0 = no direction); l = the landing point's cell offset from the player. The front end converts with dm_aim_cells_to_px by its own picture height
typedef struct { int kind; float fx, fy, dx, dy, lx, ly, lr; } dm_aim_view;
int dm_aim_view_get(dm_aim_view *v);   // 任意线程（前端主线程）取最新一份；返回 kind | any thread (the front end's main thread) takes the latest one; returns kind
// 世界格差 (dx, dy) → 画面像素差：一格沿 x 是右 16、下 8 经典像素，沿 y 是左 16、下 8；每经典像素 k = 画面高 × 0.00206 像素（dm_world.c 点地面走同一套，实测）
// World cell difference (dx, dy) -> picture pixel difference: one cell along x is 16 right and 8 down in classic pixels, along y 16 left and 8 down; each classic pixel is k = picture height × 0.00206 pixels (the same as dm_world.c click-to-walk, measured)
static inline void dm_aim_cells_to_px(float dx, float dy, float H, float *px, float *py) { float k = H * 0.00206f; *px = (dx - dy) * 16 * k; *py = (dx + dy) * 8 * k; }
// 摇杆推的幅度（0–1）→ 距离比例：死区 0.25 以内 = 0，0.95 以上 = 1，中间线性 | Stick push amount (0-1) -> distance fraction: within the 0.25 dead zone = 0, above 0.95 = 1, linear between
static inline float dm_aim_frac(float mag) { float f = (mag - 0.25f) / 0.70f; return f < 0 ? 0 : f > 1 ? 1 : f; }
