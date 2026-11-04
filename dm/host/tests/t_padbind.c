// 实体手柄键位重映射（dm_padbind.h）：解析、原样、改键、组合、按下锁定、按位或、分段与尾巴、菜单原样、改表时放键、按键组合、修饰键单按与宿主动作。
// Physical controller remapping (dm_padbind.h): parsing, identity, remap, combinations, latch on press, OR of outputs, staging and tail, identity in menus, releasing on table change, button combos, modifier taps and host actions.
#include <stdio.h>
#include "../dm_padbind.h"
static int checks, fails;
#define CHECK(c, ...) do { checks++; if (c) printf("  ✓ "); else { fails++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
int main(void) {
    dm_padbind_table t, id; dm_padbind_state st; int w; dm_padbind_identity(&id);
    // 解析 | Parsing
    CHECK(dm_padbind_parse("", &t) && dm_padbind_is_identity(&t), "空配置 = 原样");
    CHECK(dm_padbind_parse("LS=ZL; ZL = ZL+X ;RS=NONE", &t) && t.out[4] == DM_BTN_ZL && t.out[8] == (DM_BTN_ZL | DM_BTN_X) && t.out[5] == 0 && t.out[0] == DM_BTN_A, "LS→ZL、ZL→ZL+X、RS→不发，其余原样");
    CHECK(dm_padbind_parse("# comentário\nSTAGE=40\nLS=ZL", &t) && t.stage_ms == 40 && t.out[4] == DM_BTN_ZL, "注释行跳过，STAGE 给分段毫秒");
    CHECK(!dm_padbind_parse("STAGE=abc", &t) && !dm_padbind_parse("STAGE=900", &t), "STAGE 不是数 / 超范围：拒绝");
    CHECK(dm_padbind_parse("ls=zl\nzr=zr\n", &t) && t.out[4] == DM_BTN_ZL, "大小写不论、换行分条");
    dm_padbind_table keep = t;
    CHECK(!dm_padbind_parse("LS=ZL;FOO=A", &t) && !memcmp(&t, &keep, sizeof t), "认不出的键：整份不生效，表不动");
    CHECK(!dm_padbind_parse("LS", &t) && !dm_padbind_parse("LS=A+NONE", &t) && !dm_padbind_parse("LS=A+", &t), "缺等号 / NONE 混用 / 空键名都拒绝");
    // 写回文本再读：同一张表 | Format then parse back: the same table
    { char buf[512]; dm_padbind_table a, b; dm_padbind_parse("STAGE=50;LS=ZL;ZL=ZL+A;RS=NONE", &a);
      CHECK(dm_padbind_format(&a, buf, sizeof buf) && !strcmp(buf, "STAGE=50\nLS=ZL\nRS=NONE\nZL=A+ZL\n"), "写成文本：只写改过的键，按键序（组合里也按键序）");
      CHECK(dm_padbind_parse(buf, &b) && !memcmp(&a, &b, sizeof a), "写出的文本读回来是同一张表");
      CHECK(dm_padbind_format(&id, buf, sizeof buf) && !strcmp(buf, "STAGE=0\n"), "原样表只写 STAGE");
      CHECK(!dm_padbind_format(&a, buf, 12), "放不下返回 0"); }
    // 原样：没有配置时输出 = 输入 | Identity: without configuration output = input
    memset(&st, 0, sizeof st); uint64_t all = 0xffff;
    CHECK(dm_padbind_step(&id, &st, all, 0, 0, 0, &w) == all && w == -1, "原样表：16 颗全按，原样发出");
    CHECK(dm_padbind_step(&id, &st, 1ull << 40, 1, 0, 0, &w) == 1ull << 40, "16 位以外的位（前端自用）原样透传");
    // 改键 + 按位或 | Remap + OR
    dm_padbind_parse("LS=ZL;ZL=ZL+X", &t); memset(&st, 0, sizeof st);
    CHECK(dm_padbind_step(&t, &st, DM_BTN_LS, 0, 0, 0, &w) == DM_BTN_ZL, "按 L3 → 游戏收到 ZL");
    CHECK(dm_padbind_step(&t, &st, DM_BTN_LS | DM_BTN_ZL, 1, 0, 0, &w) == (DM_BTN_ZL | DM_BTN_X), "再按 L2 → ZL+X");
    CHECK(dm_padbind_step(&t, &st, DM_BTN_LS, 2, 0, 0, &w) == DM_BTN_ZL, "松 L2：ZL 仍由 L3 撑着");
    CHECK(dm_padbind_step(&t, &st, 0, 3, 0, 0, &w) == 0, "全松 = 0");
    // 按下锁定：按着时改表，松开放掉的是按下时那组 | Latch: change the table while held, the release drops the set sent on press
    memset(&st, 0, sizeof st); dm_padbind_parse("LS=ZL", &t);
    dm_padbind_step(&t, &st, DM_BTN_LS, 0, 0, 0, &w); dm_padbind_parse("LS=A", &t);
    CHECK(dm_padbind_step(&t, &st, DM_BTN_LS, 1, 0, 0, &w) == DM_BTN_ZL, "按着时表变了：仍发按下时的 ZL");
    CHECK(dm_padbind_step(&t, &st, 0, 2, 0, 0, &w) == 0, "松开：ZL 放掉，不卡");
    CHECK(dm_padbind_step(&t, &st, DM_BTN_LS, 3, 0, 0, &w) == DM_BTN_A, "再按：按新表发 A");
    // 改表时放键 | Release on table change
    dm_padbind_reset(&st, DM_BTN_LS);
    CHECK(dm_padbind_step(&t, &st, DM_BTN_LS, 4, 0, 0, &w) == 0, "改表那一刻按着的键：松开前不算数");
    CHECK(dm_padbind_step(&t, &st, 0, 5, 0, 0, &w) == 0 && dm_padbind_step(&t, &st, DM_BTN_LS, 6, 0, 0, &w) == DM_BTN_A, "松开再按：恢复");
    // 菜单里新按下的键原样 | In menus newly pressed buttons are unchanged
    memset(&st, 0, sizeof st); dm_padbind_parse("ZL=ZL+X;LS=ZL", &t);
    CHECK(dm_padbind_step(&t, &st, DM_BTN_ZL, 0, 1, 0, &w) == DM_BTN_ZL, "菜单里按 ZL：原样 ZL（翻页签）");
    CHECK(dm_padbind_step(&t, &st, DM_BTN_ZL, 1, 0, 0, &w) == DM_BTN_ZL, "按着时关了菜单：仍发按下时那组");
    // 分段：先 ZL，stage 毫秒后再发技能键；松开先放技能键，ZL 再撑 stage 毫秒 | Staging
    memset(&st, 0, sizeof st);
    CHECK(dm_padbind_step(&t, &st, DM_BTN_ZL, 100, 0, 40, &w) == DM_BTN_ZL && w == 40, "分段：按下先只发 ZL，40 毫秒后再叫");
    CHECK(dm_padbind_step(&t, &st, DM_BTN_ZL, 120, 0, 40, &w) == DM_BTN_ZL && w == 20, "20 毫秒时仍只有 ZL");
    CHECK(dm_padbind_step(&t, &st, DM_BTN_ZL, 140, 0, 40, &w) == (DM_BTN_ZL | DM_BTN_X) && w == -1, "到点：ZL+X");
    CHECK(dm_padbind_step(&t, &st, 0, 200, 0, 40, &w) == DM_BTN_ZL && w == 40, "松开：先放 X，ZL 还撑着");
    CHECK(dm_padbind_step(&t, &st, 0, 240, 0, 40, &w) == 0 && w == -1, "尾巴到期：全放");
    CHECK(dm_padbind_step(&t, &st, DM_BTN_LS, 300, 0, 40, &w) == DM_BTN_ZL && w == -1, "单颗 ZL 输出不分段");
    // 组合与宿主动作 | Combos and host actions
    CHECK(dm_padbind_parse("MINUS+UP=MAPSIZE;L=TOWNPORTAL", &t) && t.ncombo == 1 && t.combo[0].mods == DM_BTN_MINUS && t.combo[0].btn == 13 && t.combo[0].out == DM_PADACT_MAPSIZE && t.out[6] == DM_PADACT_TOWNPORTAL && !dm_padbind_is_identity(&t),
          "解析组合 MINUS+UP=MAPSIZE 与单键宿主动作 L=TOWNPORTAL");
    CHECK(!dm_padbind_parse("UP+UP=A", &t) && !dm_padbind_parse("MINUS+FOO=A", &t) && !dm_padbind_parse("MINUS+UP=BAR", &t), "组合里重复键 / 认不出的键 / 认不出的动作：拒绝");
    { char buf[512]; dm_padbind_table a, b; dm_padbind_parse("STAGE=50;ZL+LS+R=ZL+X;MINUS+UP=MAPSIZE;L=TOWNPORTAL+A", &a);
      CHECK(dm_padbind_format(&a, buf, sizeof buf) && !strcmp(buf, "STAGE=50\nL=A+TOWNPORTAL\nLS+ZL+R=X+ZL\nMINUS+UP=MAPSIZE\n"), "组合写成文本：修饰键按键序，触发键最后；动作排在键后面");
      CHECK(dm_padbind_parse(buf, &b) && !memcmp(&a, &b, sizeof a), "组合写出再读回：同一张表"); }
    dm_padbind_parse("MINUS+UP=MAPSIZE;MINUS+DOWN=X", &t); memset(&st, 0, sizeof st);
    CHECK(dm_padbind_step(&t, &st, DM_BTN_MINUS, 0, 0, 0, &w) == 0 && w == DM_PADBIND_CHORD_MS, "按下当修饰键用的 −：先不发，等组合 CHORD 毫秒");
    CHECK(dm_padbind_step(&t, &st, DM_BTN_MINUS | DM_BTN_UP, 10, 0, 0, &w) == 0 && st.fired == DM_PADACT_MAPSIZE, "按着 − 再按 ↑：不发键，记下宿主动作 MAPSIZE");
    st.fired = 0;
    CHECK(dm_padbind_step(&t, &st, DM_BTN_MINUS, 20, 0, 0, &w) == 0 && !st.fired, "松 ↑：没东西，动作不重复");
    CHECK(dm_padbind_step(&t, &st, DM_BTN_MINUS | DM_BTN_DOWN, 30, 0, 0, &w) == DM_BTN_X, "同一次按着 − 再按 ↓：发 X");
    CHECK(dm_padbind_step(&t, &st, 0, 40, 0, 0, &w) == 0 && w == -1, "全松：组过组合的 − 不补发");
    CHECK(dm_padbind_step(&t, &st, DM_BTN_MINUS, 100, 0, 0, &w) == 0, "− 单按：按下不发");
    CHECK(dm_padbind_step(&t, &st, 0, 150, 0, 0, &w) == DM_BTN_MINUS && w == DM_PADBIND_TAP_MS, "− 单按松开：补发 −（地图开关照旧）");
    CHECK(dm_padbind_step(&t, &st, 0, 150 + DM_PADBIND_TAP_MS, 0, 0, &w) == 0 && w == -1, "补发到期：放掉");
    CHECK(dm_padbind_step(&t, &st, DM_BTN_UP, 300, 0, 0, &w) == DM_BTN_UP && !st.fired, "↑ 单独按：原样 ↑（喝药），不触发组合");
    dm_padbind_step(&t, &st, 0, 310, 0, 0, &w); memset(&st, 0, sizeof st);
    CHECK(dm_padbind_step(&t, &st, DM_BTN_MINUS, 0, 1, 0, &w) == DM_BTN_MINUS && dm_padbind_step(&t, &st, DM_BTN_MINUS | DM_BTN_UP, 1, 1, 0, &w) == (DM_BTN_MINUS | DM_BTN_UP) && !st.fired, "菜单里：组合不生效，键原样");
    dm_padbind_parse("ZL+X=A;LS+ZL+X=B", &t); memset(&st, 0, sizeof st);
    dm_padbind_step(&t, &st, DM_BTN_LS, 0, 0, 0, &w); dm_padbind_step(&t, &st, DM_BTN_LS | DM_BTN_ZL, 1, 0, 0, &w);
    CHECK(dm_padbind_step(&t, &st, DM_BTN_LS | DM_BTN_ZL | DM_BTN_X, 2, 0, 0, &w) == DM_BTN_B, "修饰键最多的组合优先");
    dm_padbind_parse("STAGE=40;MINUS=ZL+X;MINUS+UP=A", &t); memset(&st, 0, sizeof st);
    dm_padbind_step(&t, &st, DM_BTN_MINUS, 0, 0, 40, &w);
    CHECK(dm_padbind_step(&t, &st, 0, 10, 0, 40, &w) == DM_BTN_ZL && w == 40, "修饰键单按补发第二套：先 ZL");
    CHECK(dm_padbind_step(&t, &st, 0, 50, 0, 40, &w) == (DM_BTN_ZL | DM_BTN_X) && w == DM_PADBIND_TAP_MS, "到点：ZL+X 发 TAP 毫秒");
    CHECK(dm_padbind_step(&t, &st, 0, 50 + DM_PADBIND_TAP_MS, 0, 40, &w) == 0, "补发到期：全放");
    { dm_padbind_table c; dm_padbind_identity(&c); dm_padbind_set_combo(&c, DM_BTN_MINUS, 13, DM_BTN_A); dm_padbind_set_combo(&c, DM_BTN_MINUS, 13, DM_BTN_B);
      CHECK(c.ncombo == 1 && c.combo[0].out == DM_BTN_B, "同一组合再设：换掉，不重复");
      dm_padbind_set_combo(&c, DM_BTN_MINUS, 13, ~0ull); CHECK(c.ncombo == 0 && dm_padbind_is_identity(&c), "删掉组合：回到原样"); }
    // 修饰键按住：等够了照常发自己的输出（按住连放）；按住途中组合照样成立 | Modifier held: after the wait it sends its own output as usual (hold to keep casting); a combo still forms while holding
    dm_padbind_parse("LS=A+ZL;LS+ZL=TOWNPORTAL", &t); memset(&st, 0, sizeof st);
    CHECK(dm_padbind_step(&t, &st, DM_BTN_LS, 0, 0, 0, &w) == 0 && w == DM_PADBIND_CHORD_MS, "L3（当修饰键）按下：先等");
    CHECK(dm_padbind_step(&t, &st, DM_BTN_LS, DM_PADBIND_CHORD_MS, 0, 0, &w) == (DM_BTN_A | DM_BTN_ZL), "等够了没组合：按住发 A+ZL（第二套技能连放）");
    CHECK(dm_padbind_step(&t, &st, DM_BTN_LS, 1000, 0, 0, &w) == (DM_BTN_A | DM_BTN_ZL), "一直按着：一直发");
    CHECK(dm_padbind_step(&t, &st, DM_BTN_LS | DM_BTN_ZL, 1010, 0, 0, &w) == 0 && st.fired == DM_PADACT_TOWNPORTAL, "按住途中再按 ZL：组合成立，L3 的输出当场放掉");
    st.fired = 0;
    CHECK(dm_padbind_step(&t, &st, DM_BTN_LS, 1020, 0, 0, &w) == 0 && dm_padbind_step(&t, &st, 0, 1030, 0, 0, &w) == 0 && w == -1, "组过组合的 L3：松开前不再发，松开不补发");
    CHECK(dm_padbind_step(&t, &st, DM_BTN_LS, 2000, 0, 0, &w) == 0 && dm_padbind_step(&t, &st, DM_BTN_LS | DM_BTN_ZL, 2050, 0, 0, &w) == 0 && st.fired == DM_PADACT_TOWNPORTAL, "等的时间里按 ZL：组合，L3 什么都没发过");
    dm_padbind_step(&t, &st, 0, 2100, 0, 0, &w); st.fired = 0;
    // 按着的宿主动作 | Held host actions
    dm_padbind_parse("L+LEFT=MAPLEFT", &t); memset(&st, 0, sizeof st);
    dm_padbind_step(&t, &st, DM_BTN_L, 0, 0, 0, &w); dm_padbind_step(&t, &st, DM_BTN_L | DM_BTN_LEFT, 10, 0, 0, &w);
    CHECK(st.acts == DM_PADACT_MAPLEFT && st.fired == DM_PADACT_MAPLEFT, "L+← 按着：acts 里有 MAPLEFT");
    dm_padbind_step(&t, &st, DM_BTN_L, 20, 0, 0, &w); CHECK(st.acts == 0, "松 ←：acts 清掉");
    // 自由瞄准：按住型走 acts，切换型走 fired；写回去认得 | Free aim: the hold kind goes through acts, the toggle kind through fired; formatting reads back
    CHECK(dm_padbind_parse("R=FREEAIM;MINUS+R=FREEAIMTOGGLE", &t), "FREEAIM / FREEAIMTOGGLE 认得"); memset(&st, 0, sizeof st);
    CHECK(dm_padbind_step(&t, &st, DM_BTN_R, 0, 0, 0, &w) == 0 && st.acts == DM_PADACT_FREEAIM, "按住 R：acts 里有 FREEAIM，不发给游戏");
    dm_padbind_step(&t, &st, 0, 10, 0, 0, &w); CHECK(st.acts == 0, "松 R：FREEAIM 清掉");
    { char buf[256]; dm_padbind_table t2; CHECK(dm_padbind_format(&t, buf, sizeof buf) && strstr(buf, "R=FREEAIM\n") && strstr(buf, "MINUS+R=FREEAIMTOGGLE") && dm_padbind_parse(buf, &t2), "FREEAIM 写回去再读得回来"); }
    printf("== %d 项，失败 %d ==\n", checks, fails);
    return fails != 0;
}
