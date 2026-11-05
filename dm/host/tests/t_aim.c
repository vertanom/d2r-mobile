// 手柄瞄准按技能形态分类（dm_aim.h）：位移 / 飞弹 / 落点三类、自由瞄准的技能表（类别开关与单个技能）、摇杆幅度 → 距离比例。
// Controller aiming by skill shape (dm_aim.h): the three classes movement / skillshot / ground zone, the free aim skill list (class switches and single skills), stick push -> distance fraction.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../dm_aim.h"
static int checks, fails;
#define CHECK(c, ...) do { checks++; if (c) printf("  ✓ "); else { fails++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static const char *g_txt;   // 假的 aim_free.txt：键=值，一行一条 | a fake aim_free.txt: key=value, one per line
static int get(const char *key, int def) {
    size_t kn = strlen(key); for (const char *l = g_txt; l && *l; ) { const char *e = strchr(l, '\n'); if (!e) e = l + strlen(l);
        if ((size_t)(e - l) > kn && !strncmp(l, key, kn) && l[kn] == '=') return atoi(l + kn + 1); l = *e ? e + 1 : e; }
    return def;
}
int main(void) {
    // 分类 | Classes
    CHECK(dm_aim_class(77, 16.6f, 1) == DM_AIM_MOVE, "跳跃（施放函数 77，16.6 格、按等级）= 位移");
    CHECK(dm_aim_class(52, 2.0f, 0) == DM_AIM_MOVE, "龙翔（距离 2.0 却会位移）= 位移，不当飞弹");
    CHECK(dm_aim_class(1, 2.0f, 0) == DM_AIM_SHOT, "普通攻击（2.0、不按等级）= 飞弹类");
    CHECK(dm_aim_class(5, 12.0f, 0) == DM_AIM_ZONE && dm_aim_class(5, 2.0f, 1) == DM_AIM_ZONE, "填了距离 / 按等级算 = 落点类");
    CHECK(dm_aim_class(5, 0.0f, 0) == DM_AIM_NONE, "没有距离 = 不分类（不动）");
    // 自由瞄准的技能表 | The free aim skill list
    g_txt = "";
    CHECK(dm_aim_free_listed(DM_AIM_MOVE, 132, get) && !dm_aim_free_listed(DM_AIM_ZONE, 59, get) && !dm_aim_free_listed(DM_AIM_SHOT, 0, get) && !dm_aim_free_listed(DM_AIM_NONE, 7, get), "缺省：只有位移类");
    g_txt = "move=0\nzone=1\nskill_132=1\nskill_59=0\n";
    CHECK(dm_aim_free_listed(DM_AIM_MOVE, 132, get) && !dm_aim_free_listed(DM_AIM_MOVE, 54, get), "skill_132=1 赢过 move=0");
    CHECK(!dm_aim_free_listed(DM_AIM_ZONE, 59, get) && dm_aim_free_listed(DM_AIM_ZONE, 56, get), "zone=1 开落点类，skill_59=0 单独关掉");
    g_txt = "skill_13=1\n";
    CHECK(!dm_aim_free_listed(DM_AIM_SHOT, 1, get), "skill_1 不会误认成 skill_13");
    // 不选怪：位移类缺省一直，其余只在自由瞄准开着时 | No target pick: movement always by default, the rest only while free aim is on
    g_txt = "";
    CHECK(dm_aim_ignore_target(DM_AIM_MOVE, 132, 0, get), "位移类没开自由瞄准也不选怪（缺省 move_always=1）");
    CHECK(!dm_aim_ignore_target(DM_AIM_SHOT, 0, 0, get) && !dm_aim_ignore_target(DM_AIM_ZONE, 59, 0, get), "其余类别没开自由瞄准 = 原生");
    g_txt = "move_always=0\n";
    CHECK(!dm_aim_ignore_target(DM_AIM_MOVE, 132, 0, get) && dm_aim_ignore_target(DM_AIM_MOVE, 132, 1, get), "move_always=0：位移类回到只在自由瞄准时");
    g_txt = "skill_143=0\n";
    CHECK(!dm_aim_ignore_target(DM_AIM_MOVE, 143, 0, get) && dm_aim_ignore_target(DM_AIM_MOVE, 132, 0, get), "skill_143=0 单独关掉一个位移技能");
    // 幅度 → 比例 | Push -> fraction
    CHECK(dm_aim_frac(0.1f) == 0 && dm_aim_frac(0.25f) == 0 && dm_aim_frac(1.0f) == 1 && dm_aim_frac(0.95f) == 1, "死区内 0，推满 1");
    CHECK(dm_aim_frac(0.6f) > 0.49f && dm_aim_frac(0.6f) < 0.51f, "推到 0.6 ≈ 一半");
    printf("== %d 项，失败 %d ==\n", checks, fails);
    return fails != 0;
}
