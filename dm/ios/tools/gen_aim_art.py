#!/usr/bin/env python3
"""手柄瞄准指示的素材（dm_aim_hud.m）：全部取自原版手柄表情轮盘，不画原版没有的形状。
  · 地面圈 = hd/global/ui/Controller/Panel/EmoteWheel/Emote_Circle.sprite（鎏金旧铜圆环，正圆，前端压扁成地面椭圆）
  · 方向尖 = 同目录 Emote_Arrow.sprite 第 3 帧（朝上的鎏金三角嵌宝石；前端按瞄准方向在地面平面里转）
放大到 512 / 128 像素（前端在视网膜屏上画到约 250 像素宽，原图 155 太糊）。玩家可以拿这两张去给别的 AI 改画风，
再在「手柄 → 瞄准指示…」窗口里载入（存进自定义目录的 aim_ring.png / aim_tip.png，优先于这里的）。
产物（构建时生成，不进仓库）：Gen/assets/NativeHUD/aim/{aim_ring.png, aim_tip.png}。
用法：python3 dm/ios/tools/gen_aim_art.py <原版 RomFS（Update）>   （读包与解贴图走 romfs_art.py）
Art for the controller aim indicator (dm_aim_hud.m): all taken from the original controller emote wheel, no shapes the original does not have.
  · ground ring = hd/global/ui/Controller/Panel/EmoteWheel/Emote_Circle.sprite (a gilded worn-bronze ring, a true circle; the front end squashes it into the ground ellipse)
  · direction tip = frame 3 of Emote_Arrow.sprite in the same folder (an upward gilded, jewelled triangle; the front end turns it in the ground plane toward the aim)
Scaled to 512 / 128 pixels (the front end draws it about 250 pixels wide on a Retina screen; the original 155 is too soft). Players can take these two to another AI to restyle them,
then load them in the "Controller -> Aim Indicator..." window (stored as aim_ring.png / aim_tip.png in the custom directory, which win over these).
Output (generated at build time, not in the repo): Gen/assets/NativeHUD/aim/{aim_ring.png, aim_tip.png}.
Usage: python3 dm/ios/tools/gen_aim_art.py <original RomFS (Update)>   (package reading and texture decoding go through romfs_art.py)"""
import os, sys
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import romfs_art
from gen_paths import gen_dir

W = 'hd/global/ui/Controller/Panel/EmoteWheel/'


def main():
    if len(sys.argv) < 2: raise SystemExit(__doc__)
    rom, out = sys.argv[1], gen_dir('NativeHUD', 'aim')
    ring = romfs_art.sprite(rom, W + 'Emote_Circle.sprite')
    ring.resize((512, 512), Image.LANCZOS).save(os.path.join(out, 'aim_ring.png'))
    tip = romfs_art.frame(rom, W + 'Emote_Arrow.sprite', 3)   # 第 3 帧朝上（gen_map_goal_art.py 的 heading.png 同一帧） | frame 3 points up (the same frame as heading.png in gen_map_goal_art.py)
    tip.resize((128, 128), Image.LANCZOS).save(os.path.join(out, 'aim_tip.png'))
    print('瞄准指示素材 →', out)


if __name__ == '__main__':
    main()
