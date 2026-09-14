/*
 * RoboFont — font subsystem (Goals 5.1–5.22).
 *
 * Phase 5 public API is FROZEN (ROBO_FONT_API_FROZEN == 1, RoboFont API v1).
 * Applications include this umbrella or the individual RoboFont*.h headers.
 * Internal headers under Source/RoboFont/ are NOT part of the frozen API.
 *
 * Independent RoboticelaOS runtime subsystem. Chromium is only a temporary
 * technical reference. RoboFont must never depend on Chromium headers,
 * libraries, source, build targets, Skia, or OS font managers.
 *
 * Architecture:
 *   Discovery → Registry → Loader/Cache → Selection → Coverage → Fallback
 *     → Generic Families → Emoji → Variation → Color → Validate
 *     → Conformance → Face → Instance
 *
 * See font/ApiFreeze.md, font/Architecture.md, font/Api.md.
 */

#ifndef ROBO_FONT_H
#define ROBO_FONT_H

#include "RoboFont/RoboFontBinary.h"
#include "RoboFont/RoboFontCff.h"
#include "RoboFont/RoboFontClassification.h"
#include "RoboFont/RoboFontCmap.h"
#include "RoboFont/RoboFontColor.h"
#include "RoboFont/RoboFontConformance.h"
#include "RoboFont/RoboFontCoverage.h"
#include "RoboFont/RoboFontDiscovery.h"
#include "RoboFont/RoboFontEmoji.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontFallback.h"
#include "RoboFont/RoboFontGenericFamily.h"
#include "RoboFont/RoboFontInstance.h"
#include "RoboFont/RoboFontLoader.h"
#include "RoboFont/RoboFontMetrics.h"
#include "RoboFont/RoboFontName.h"
#include "RoboFont/RoboFontOutline.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontRegistry.h"
#include "RoboFont/RoboFontSelection.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontSource.h"
#include "RoboFont/RoboFontStyle.h"
#include "RoboFont/RoboFontTt.h"
#include "RoboFont/RoboFontTypes.h"
#include "RoboFont/RoboFontValidate.h"
#include "RoboFont/RoboFontVariation.h"
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
