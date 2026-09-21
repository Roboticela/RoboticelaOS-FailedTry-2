/*
 * RoboFontColor — OpenType color fonts (Goal 5.19).
 *
 * Unified representation of COLR/CPAL, CBDT/CBLC, sbix, and SVG glyph data.
 * Parses and validates color structures for later rendering.
 *
 * Does NOT implement:
 *   color glyph rasterization, COLR paint execution, gradient/bitmap/SVG
 *   rendering, CSS font-palette, emoji shaping, or compositor integration.
 */

#ifndef ROBO_FONT_COLOR_H
#define ROBO_FONT_COLOR_H

#include "RoboBase/RoboError.h"
#include "RoboFont/RoboFontFace.h"
#include "RoboFont/RoboFontOt.h"
#include "RoboFont/RoboFontSfnt.h"
#include "RoboFont/RoboFontTypes.h"
#include "RoboFont/RoboFontVariation.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RoboFontColorMaxPalettes 256u
#define RoboFontColorMaxPaletteEntries 1024u
#define RoboFontColorMaxLayers 1024u
#define RoboFontColorMaxPaintNodes 4096u
#define RoboFontColorMaxPaintDepth 32u
#define RoboFontColorMaxStrikes 64u
#define RoboFontColorMaxSvgDocs 4096u
#define RoboFontColorMaxSvgPayload (1024u * 1024u)
#define RoboFontColorForegroundPalette 0xFFFFu

/* ---- Capabilities / formats ---- */

typedef enum FontColorCapability {
    RoboFontColorCapNone = 0,
    RoboFontColorCapColr = 1u << 0,
    RoboFontColorCapCpal = 1u << 1,
    RoboFontColorCapColrV0 = 1u << 2,
    RoboFontColorCapColrV1 = 1u << 3,
    RoboFontColorCapCbdt = 1u << 4,
    RoboFontColorCapCblc = 1u << 5,
    RoboFontColorCapSbix = 1u << 6,
    RoboFontColorCapSvg = 1u << 7,
    RoboFontColorCapVariation = 1u << 8 /* VarStore / var paints present */
} FontColorCapability;

typedef enum FontColorGlyphFormat {
    RoboFontColorGlyphNone = 0,
    RoboFontColorGlyphMonochrome = 1,
    RoboFontColorGlyphLayers = 2,   /* COLR v0 layers */
    RoboFontColorGlyphPaint = 3,    /* COLR v1 paint tree */
    RoboFontColorGlyphBitmap = 4,   /* CBDT or sbix */
    RoboFontColorGlyphSvg = 5,
    RoboFontColorGlyphUnsupported = 6
} FontColorGlyphFormat;

typedef enum FontColorPaintOp {
    RoboFontColorPaintNone = 0,
    RoboFontColorPaintColrLayers = 1,
    RoboFontColorPaintSolid = 2,
    RoboFontColorPaintLinearGradient = 4,
    RoboFontColorPaintRadialGradient = 6,
    RoboFontColorPaintSweepGradient = 8,
    RoboFontColorPaintGlyph = 10,
    RoboFontColorPaintColrGlyph = 11,
    RoboFontColorPaintTransform = 12,
    RoboFontColorPaintTranslate = 14,
    RoboFontColorPaintScale = 16,
    RoboFontColorPaintRotate = 24,
    RoboFontColorPaintSkew = 28,
    RoboFontColorPaintComposite = 32,
    RoboFontColorPaintUnsupported = 255
} FontColorPaintOp;

typedef struct FontColorRgba {
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a; /* unpremultiplied per OpenType CPAL */
} FontColorRgba;

typedef struct FontColorPaletteEntry {
    FontColorRgba color;
    uint16_t paletteIndex; /* index within palette */
} FontColorPaletteEntry;

typedef struct FontColorPalette {
    uint16_t index;
    uint16_t entryCount;
    uint16_t firstColorIndex; /* into CPAL colorRecords */
    uint16_t flags;             /* CPAL v1; 0 if absent */
    uint16_t nameId;           /* 0xFFFF if absent */
} FontColorPalette;

typedef struct FontColorGlyphLayer {
    GlyphId glyphId;
    uint16_t paletteIndex; /* 0xFFFF = foreground */
} FontColorGlyphLayer;

typedef struct FontColorPaint {
    FontColorPaintOp op;
    uint8_t format; /* raw COLR format number */
    uint8_t depth;
    uint8_t unsupported;
    uint16_t paletteIndex;
    uint8_t alphaF2dot14Hi; /* packed; prefer alphaU8 when solid */
    uint8_t alphaU8;
    GlyphId glyphId;
    uint32_t childIndex;     /* first child node index; UINT32_MAX = none */
    uint32_t siblingIndex;   /* next sibling; UINT32_MAX = none */
    int16_t tx;               /* F2DOT14 or FWORD depending on op — stored raw */
    int16_t ty;
    int16_t xx;
    int16_t xy;
    int16_t yx;
    int16_t yy;
    uint8_t compositeMode;
    uint8_t hasVariation; /* Var* paint formats */
} FontColorPaint;

typedef struct FontColorGlyphImage {
    uint16_t ppemX;
    uint16_t ppemY;
    int16_t originX;
    int16_t originY;
    uint32_t graphicType; /* four-char tag for sbix; 0 for CBDT */
    uint8_t format;        /* CBDT image format if applicable */
    FontSpan data; /* borrowed from font source */
} FontColorGlyphImage;

typedef struct FontColorGlyphSvg {
    GlyphId startGlyphId;
    GlyphId endGlyphId;
    FontSpan document; /* borrowed; inert UTF-8/XML bytes */
    uint8_t hasExternalRefHint; /* 1 if payload contains "http://" / "href" heuristics */
} FontColorGlyphSvg;

typedef struct FontColorGlyph {
    GlyphId glyphId;
    FontColorGlyphFormat format;
    uint32_t layerCount;
    const FontColorGlyphLayer *layers; /* owned by info when LAYERS */
    uint32_t paintRoot; /* index into paint nodes; UINT32_MAX if none */
    uint8_t hasImage;
    uint8_t hasSvg;
    uint8_t variationDependent;
} FontColorGlyph;

typedef struct FontColorInfo FontColorInfo;

typedef struct FontColorPaletteSelection {
    uint16_t paletteIndex; /* selected CPAL palette */
    uint8_t hasOverrides;
    uint16_t overrideCount;
    const uint16_t *overrideIndices; /* borrowed */
    const FontColorRgba *overrideColors; /* borrowed */
} FontColorPaletteSelection;

void FontColorPaletteSelectionInit(FontColorPaletteSelection *sel);

/* ---- Construction ---- */

/**
 * Build immutable color-font info from SFNT tables.
 * RoboNotFound if no color tables are present.
 */
Result FontColorInfoCreate(FontSfnt *sfnt,
                                          const FontVariationCoords *coordsOpt,
                                          FontColorInfo **out);

Result FontColorInfoCreateFromFace(
    FontFace *face,
    const FontVariationCoords *coordsOpt,
    FontColorInfo **out);

void FontColorInfoRetain(FontColorInfo *info);
void FontColorInfoRelease(FontColorInfo *info);

uint32_t FontColorInfoCapabilities(const FontColorInfo *info);
int FontColorInfoIsColorCapable(const FontColorInfo *info);
uint16_t FontColorInfoColrVersion(const FontColorInfo *info);

/* ---- Palettes ---- */

uint16_t FontColorInfoPaletteCount(const FontColorInfo *info);
Result FontColorInfoPalette(const FontColorInfo *info,
                                           uint16_t paletteIndex,
                                           FontColorPalette *out);
Result FontColorInfoPaletteEntry(const FontColorInfo *info,
                                                 uint16_t paletteIndex,
                                                 uint16_t entryIndex,
                                                 FontColorRgba *out);

/**
 * Resolve a palette index for a glyph layer, applying selection + overrides.
 * paletteIndex 0xFFFF → RoboNotFound (foreground; caller supplies).
 */
Result FontColorResolvePaletteColor(
    const FontColorInfo *info,
    const FontColorPaletteSelection *sel,
    uint16_t paletteIndex,
    FontColorRgba *out);

/* ---- Glyph queries ---- */

Result FontColorInfoGlyph(const FontColorInfo *info,
                                         GlyphId glyphId,
                                         FontColorGlyph *out);

Result FontColorInfoGlyphLayers(const FontColorInfo *info,
                                                GlyphId glyphId,
                                                const FontColorGlyphLayer **outLayers,
                                                uint32_t *outCount);

/**
 * Build / fetch COLR v1 paint tree for a glyph.
 * Returns RoboNotFound if no paint; RoboNotSupported if format unreadable.
 * Paint nodes are owned by info (immutable after first successful build).
 */
Result FontColorInfoGlyphPaint(const FontColorInfo *info,
                                               GlyphId glyphId,
                                               const FontColorPaint **outNodes,
                                               uint32_t *outNodeCount,
                                               uint32_t *outRootIndex);

Result FontColorInfoGlyphImage(const FontColorInfo *info,
                                               GlyphId glyphId,
                                               uint16_t preferPpem,
                                               FontColorGlyphImage *out);

Result FontColorInfoGlyphSvg(const FontColorInfo *info,
                                             GlyphId glyphId,
                                             FontColorGlyphSvg *out);

/** Borrowed variation coords attached at create time (may be NULL). */
const FontVariationCoords *FontColorInfoVariationCoords(
    const FontColorInfo *info);

/* ---- Bitmap / sbix / SVG table summaries ---- */

uint16_t FontColorInfoSbixStrikeCount(const FontColorInfo *info);
uint16_t FontColorInfoCblcStrikeCount(const FontColorInfo *info);
uint32_t FontColorInfoSvgDocumentCount(const FontColorInfo *info);

/* ---- Fuzz ---- */

Result FontColorFuzzParse(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_FONT_COLOR_H */
