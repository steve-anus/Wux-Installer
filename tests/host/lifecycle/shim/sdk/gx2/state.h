#pragma once
#include <wut.h>
struct GX2Color { u8 r, g, b, a; };
enum GX2Boolean { GX2_DISABLE, GX2_ENABLE };
enum GX2CompareFunction { GX2_COMPARE_FUNC_ALWAYS, GX2_COMPARE_FUNC_LEQUAL };
constexpr s32 GX2_TV_SCAN_MODE_720P = 1;
constexpr s32 GX2_DRC_RENDER_MODE_SINGLE = 1;
void GX2SetDepthOnlyControl(GX2Boolean test, GX2Boolean write, GX2CompareFunction compare);
