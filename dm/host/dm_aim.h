// 手柄瞄准按技能形态分三类（纯函数，hle_private_aim.c 用，宿主单测 tests/t_aim.c 核）：
//   位移（跳跃 / 跳跃攻击 / 冲锋 / 传送 / 旋风 / 龙翔）= 施放函数号 srvdofunc 27 / 52 / 67 / 76 / 77 / 78：左摇杆瞄，自由瞄准时不选怪；
//   飞弹类（普通攻击、近战挥砍、飞弹、原地施法）= 手柄落点距离 ≤ 2.0 且不按等级算：右摇杆微调方向（原有的锥内选怪）；
//   落点类（暴风雪 / 陷阱 / 召唤）= 其余有距离的：右摇杆的角度给方向、推的幅度给距离。
// Controller aiming splits skills into three shapes (pure functions, used by hle_private_aim.c, checked by the host unit test tests/t_aim.c):
//   movement (Leap / Leap Attack / Charge / Teleport / Whirlwind / Dragon Flight) = cast function srvdofunc 27 / 52 / 67 / 76 / 77 / 78: aimed with the left stick, picks no monster under free aim;
//   skillshot (normal attack, melee swings, missiles, cast in place) = controller landing distance ≤ 2.0 and not computed by level: the right stick fine-tunes the direction (the existing cone target pick);
//   ground zone (Blizzard / traps / summons) = the rest that have a distance: the right stick's angle gives the direction, how far it is pushed gives the distance.
#pragma once
#include <stdint.h>
#include <stdio.h>
enum { DM_AIM_NONE, DM_AIM_SHOT, DM_AIM_MOVE, DM_AIM_ZONE };
static inline int dm_aim_is_move_fn(int fn) { return fn == 27 || fn == 52 || fn == 67 || fn == 76 || fn == 77 || fn == 78; }
static inline int dm_aim_class(int fn, float dist, int scaled) {
    if (dm_aim_is_move_fn(fn)) return DM_AIM_MOVE;
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
// 这一下施放要不要不选怪：位移类缺省一直不选（move_always=1，Victor 2026-11-04：跳跃按住左摇杆跳满射程、不推就原地跳，其余位移技能也这样）；
// 其余类别只在自由瞄准开着时（free_on）。两种情况都还要过技能表（skill_<号>=0 可以单独关掉）
// Whether this cast picks no monster: movement skills by default always (move_always=1, Victor 2026-11-04: Leap with the left stick held jumps the full range, without it jumps in place, and the other movement skills should do the same);
// other classes only while free aim is on (free_on). Both cases still go through the skill list (skill_<id>=0 turns a single one off)
static inline int dm_aim_ignore_target(int cls, int skill, int free_on, int (*get)(const char *key, int def)) {
    if (!free_on && !(cls == DM_AIM_MOVE && get("move_always", 1))) return 0;
    return dm_aim_free_listed(cls, skill, get);
}
// 摇杆推的幅度（0–1）→ 距离比例：死区 0.25 以内 = 0，0.95 以上 = 1，中间线性 | Stick push amount (0-1) -> distance fraction: within the 0.25 dead zone = 0, above 0.95 = 1, linear between
static inline float dm_aim_frac(float mag) { float f = (mag - 0.25f) / 0.70f; return f < 0 ? 0 : f > 1 ? 1 : f; }
