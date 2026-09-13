/*
 * RoboFont — font subsystem (Goals 5.1–5.8).
 *
 * Independent RoboticelaOS runtime subsystem. Chromium is only a temporary
 * technical reference. RoboFont must never depend on Chromium headers,
 * libraries, source, build targets, Skia, or OS font managers.
 *
 * Architecture:
 *   Font Source → SFNT → TrueType / OpenType / CFF·CFF2 → cmap → metrics → outlines → Face
 *
 * Goal 5.8 converts glyf / CharStrings into a unified geometric outline.
 */

#ifndef ROBO_FONT_H
#define ROBO_FONT_H

#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontCff.h"
#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontInstance.h"
#include "RoboFont/RoboFontMetrics.h"
#include "RoboFont/RoboFontOutline.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboFont/RoboFontStyle.h"
#include "RoboFont/RoboFontTt.h"
#include "RoboFont/RoboFontTypes.h"
#include "RoboFont/RoboFontVersion.h"

#ifdef __cplusplus
extern "C" {
#endif

const char *robo_font_version_string(void);

/**
 * Process init/shutdown reserved for future caches.
 * Goal 5.1: init is idempotent success; shutdown is a no-op.
 */
robo_result_t robo_font_init(void);
void robo_font_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_H */
