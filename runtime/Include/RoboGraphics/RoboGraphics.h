/*
 * RoboGraphics — independent graphics subsystem (Goal 6.1).
 *
 * Architecture:
 *   API → Geometry / Color / PixelFormat → Surface / Image → Frame
 *       → Backend (raster | present) → future Window / QEMU
 *
 * Chromium/Skia are temporary technical reference only.
 * RoboGraphics must never depend on Chromium headers, libraries, Skia,
 * or OS window APIs.
 *
 * See graphics/README.md and graphics/docs/.
 */

#ifndef ROBO_GRAPHICS_H
#define ROBO_GRAPHICS_H

#include "RoboBase/RoboError.h"
#include "RoboGraphics/RoboGraphicsBackend.h"
#include "RoboGraphics/RoboGraphicsBitmap.h"
#include "RoboGraphics/RoboGraphicsCapabilities.h"
#include "RoboGraphics/RoboGraphicsColor.h"
#include "RoboGraphics/RoboGraphicsComposite.h"
#include "RoboGraphics/RoboGraphicsEncode.h"
#include "RoboGraphics/RoboGraphicsFrame.h"
#include "RoboGraphics/RoboGraphicsFramebuffer.h"
#include "RoboGraphics/RoboGraphicsGeometry.h"
#include "RoboGraphics/RoboGraphicsImage.h"
#include "RoboGraphics/RoboGraphicsLine.h"
#include "RoboGraphics/RoboGraphicsPath.h"
#include "RoboGraphics/RoboGraphicsPixelFormat.h"
#include "RoboGraphics/RoboGraphicsRaster.h"
#include "RoboGraphics/RoboGraphicsScreenshot.h"
#include "RoboGraphics/RoboGraphicsSurface.h"
#include "RoboGraphics/RoboGraphicsText.h"
#include "RoboGraphics/RoboGraphicsTransform.h"
#include "RoboGraphics/RoboGraphicsTypes.h"
#include "RoboGraphics/RoboGraphicsVersion.h"

#ifdef __cplusplus
extern "C" {
#endif

const char *robo_graphics_version_string(void);

/**
 * Process init/shutdown for future caches / backend registration.
 * Goal 6.1: idempotent success; shutdown is a no-op.
 */
robo_result_t robo_graphics_init(void);
void robo_graphics_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_GRAPHICS_H */
