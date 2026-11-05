// 共用 Core Animation 手柄瞄准指示；前端提供当前游戏可见矩形（同 dm_rift_hud.h）。
// Shared Core Animation controller aim indicator; each front end supplies its visible game rectangle (same as dm_rift_hud.h).
#pragma once
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#define DmAimView NSView
#else
#import <UIKit/UIKit.h>
#define DmAimView UIView
#endif
void dm_aim_hud_draw(DmAimView *host, CGRect world_rect, BOOL hide);
