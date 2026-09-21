/*
 * RoboFont unified glyph outlines (Goal 5.8).
 *
 * Converts TrueType glyf / CFF / CFF2 CharStrings into an immutable
 * geometric path in font design units. No rasterization, hinting, or shaping.
 *
 *   TrueType glyf ─────┐
 *                      ├──> RoboGlyphOutline
 *   CFF CharStrings ───┤
 *   CFF2 CharStrings ──┘
 */

#ifndef ROBO_FONT_OUTLINE_H
#define ROBO_FONT_OUTLINE_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontCff.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontTt.h"
#include "RoboFont/RoboFontTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FontTt FontTt;
typedef struct FontCffFont FontCffFont;
typedef struct FontFace FontFace;

/* ---- Resource limits ---- */

#define RoboFontOutlineMaxContours 1024u
#define RoboFontOutlineMaxSegments 65536u
#define RoboFontOutlineMaxPoints 65536u
#define RoboFontOutlineMaxCompositeDepth RoboFontTtMaxCompositeDepth
#define RoboFontOutlineMaxCompositeComponents 1024u
#define RoboFontOutlineCacheMax 512u

/* ---- Geometry ---- */

typedef struct FontPoint {
    FontUnit x;
    FontUnit y;
} FontPoint;

typedef enum FontPathOp {
    RoboFontPathMove = 0,
    RoboFontPathLine = 1,
    RoboFontPathQuad = 2,  /* quadratic: c1 = control, p = end */
    RoboFontPathCubic = 3, /* cubic: c1,c2 = controls, p = end */
    RoboFontPathClose = 4
} FontPathOp;

typedef struct FontPathSeg {
    FontPathOp op;
    FontPoint p;  /* destination (MOVE/LINE/QUAD/CUBIC) */
    FontPoint c1; /* QUAD/CUBIC control 1 */
    FontPoint c2; /* CUBIC control 2 */
} FontPathSeg;

typedef struct FontContour {
    uint32_t segCount;
    FontPathSeg *segs; /* owned by outline */
    uint8_t closed;
    FontBbox bounds;
    uint8_t hasBounds;
} FontContour;

typedef enum FontOutlineStatus {
    RoboFontOutlineStatusOk = 0,
    RoboFontOutlineStatusEmpty = 1,
    RoboFontOutlineStatusInvalidId = 2,
    RoboFontOutlineStatusUnavailable = 3,
    RoboFontOutlineStatusMalformed = 4,
    RoboFontOutlineStatusUnsupported = 5,
    RoboFontOutlineStatusLimit = 6,
    RoboFontOutlineStatusOverflow = 7
} FontOutlineStatus;

typedef struct FontOutline FontOutline;

/* ---- Affine (font units; matrix in F2DOT14 × 2^14 scale) ---- */

typedef struct FontAffine {
    int16_t xscale; /* F2DOT14; 0x4000 = 1.0 */
    int16_t scale01;
    int16_t scale10;
    int16_t yscale;
    int32_t dx;
    int32_t dy;
} FontAffine;

void FontAffineIdentity(FontAffine *m);
Result FontAffineMultiply(const FontAffine *a,
                                        const FontAffine *b,
                                        FontAffine *out);
Result FontAffineApply(const FontAffine *m,
                                     FontUnit x,
                                     FontUnit y,
                                     FontUnit *outX,
                                     FontUnit *outY);

/* ---- Outline accessors ---- */

void FontOutlineRetain(FontOutline *o);
void FontOutlineRelease(FontOutline *o);

GlyphId FontOutlineGlyphId(const FontOutline *o);
FontOutlineStatus MakeFontOutlineStatus(const FontOutline *o);
FontOutlineTech MakeFontOutlineTech(const FontOutline *o);

uint32_t FontOutlineContourCount(const FontOutline *o);
const FontContour *FontOutlineContour(const FontOutline *o,
                                                     uint32_t index);

int FontOutlineHasBounds(const FontOutline *o);
Result FontOutlineBounds(const FontOutline *o,
                                       FontBbox *out);

uint32_t FontOutlineSegmentCount(const FontOutline *o);

/**
 * Deterministic golden dump (ASCII). Writes up to @p cap-1 chars + NUL.
 * Format: "gid=<id> status=<s> contours=<n> bounds=x0,y0,x1,y1\\n"
 * then per contour "C <i> closed=<0|1> segs=<n>\\n" and "  OP x y [c1x c1y] [c2x c2y]\\n"
 */
Result FontOutlineDump(const FontOutline *o,
                                     char *buf,
                                     size_t cap,
                                     size_t *outLen);

/* ---- Construction from backends ---- */

Result FontOutlineFromTt(FontTt *tt,
                                        GlyphId glyphId,
                                        FontOutline **out);

Result FontOutlineFromCff(FontCffFont *cff,
                                         GlyphId glyphId,
                                         FontOutline **out);

/**
 * Build from SFNT using outline-tech detection.
 * Optionally reuses @p tt / @p cff if non-NULL (retained).
 */
Result FontOutlineFromSfnt(FontSfnt *sfnt,
                                          FontTt *ttOpt,
                                          FontCffFont *cffOpt,
                                          GlyphId glyphId,
                                          FontOutline **out);

/** Fuzz entry: arbitrary SFNT / table bytes. */
Result FontOutlineFuzzParse(const uint8_t *data, size_t size);

/* ---- Face-local cache ---- */

typedef struct FontOutlineCache FontOutlineCache;

Result FontOutlineCacheCreate(FontOutlineCache **out);
void FontOutlineCacheRetain(FontOutlineCache *c);
void FontOutlineCacheRelease(FontOutlineCache *c);

/**
 * Get or build outline. Retains returned outline for the caller.
 * Thread-safe for concurrent readers after the cache is attached to a face
 * with backends already prepared (no lazy backend init inside lock-free path:
 * prepare backends before concurrent queries).
 */
Result FontOutlineCacheGet(FontOutlineCache *cache,
                                          FontSfnt *sfnt,
                                          FontTt *ttOpt,
                                          FontCffFont *cffOpt,
                                          GlyphId glyphId,
                                          FontOutline **out);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_OUTLINE_H */
