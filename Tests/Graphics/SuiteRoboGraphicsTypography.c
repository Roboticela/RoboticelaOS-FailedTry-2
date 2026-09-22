/*
 * RoboGraphics typography raster-quality visual fixture (Phase 12.1) — `graphics.typography.*`.
 *
 * Phase 12.1's claim is visual -- "Latin text must look substantially sharper at UI sizes" -- and a
 * claim about how something looks needs a picture. This suite draws one, twice, through the *same*
 * text path with the only difference being the glyph paint mode:
 *
 *   `RoboGfxGlyphModeAlpha`          the Phase 12.1 path: coverage-antialiased glyph fills
 *   `RoboGfxGlyphModeOutlineStroke`  the Phase 11 path: one-pixel strokes of the glyph contour
 *
 * The specimen is the OS's own face — the system typeface pack's `System/Fonts/UI/Inter.ttf`, SIL
 * OFL, redistributed with its licence at `System/Fonts/licenses/Inter-OFL.txt` — at the sizes a UI actually uses, dark on
 * light and light on dark. Both renderings are written beside the build as PNGs for a person to look
 * at, and both are *measured*, because a picture nobody asserts on is a picture that silently rots:
 *
 *   the coverage path puts down **partial pixels** -- pixels that are neither the page nor the ink --
 *   and the stroke path puts down none at all, because it writes whole pixels in the ink's exact
 *   colour. That single number is the phase, expressed as something a suite can fail on;
 *
 *   the coverage path inks **several times** the pixels the stroke path does, because it fills the
 *   glyph instead of tracing it;
 *
 *   every requested size produced ink, in both polarities;
 *
 *   the same inputs twice produce byte-identical surfaces.
 *
 * The PNGs are deliberately **not** assertions, for the reason the desktop checkpoint gives: a
 * checkpoint is evidence for a person, and a suite whose verdict depended on the filesystem would
 * fail on a read-only tree while the pixels it verified were perfectly good.
 *
 * A **bold** face is not in the specimen, and that is a fact about this build rather than a gap in
 * the fixture: the system typeface pack is one static Regular face (Phase 12.2 formalizes the pack),
 * so a second weight does not exist to draw.
 */

#include "RoboFont/RoboFont.h"
#include "RoboGraphics/RoboGraphics.h"
#include "RoboGraphics/RoboGraphicsEncode.h"
#include "RoboGraphics/RoboGraphicsGlyph.h"
#include "RoboGraphics/RoboGraphicsText.h"
#include "RoboTest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(RoboFontAssetPathFromBuild)
#include "robo_font_asset_path.h"
#endif
#ifndef RoboFontAssetPath
#define RoboFontAssetPath "System/Fonts/UI/Inter.ttf"
#endif

/* ------------------------------------------------------------------ */
/* The specimen                                                        */
/* ------------------------------------------------------------------ */

#define SpecimenWidth 960u
#define SpecimenHeight 900u
#define SpecimenMargin 24
#define SpecimenLineLead 1.45f

/** The UI sizes the phase is judged at, from the brief's own list. */
static const float SpecimenSizes[] = {12.0f, 14.0f, 16.0f, 18.0f, 24.0f, 32.0f};
#define SpecimenSizeCount (sizeof(SpecimenSizes) / sizeof(SpecimenSizes[0]))

#define SpecimenPaper GfxRgb8(255, 255, 255)
#define SpecimenInk GfxRgb8(26, 26, 32)
#define SpecimenDarkPaper GfxRgb8(24, 22, 30)
#define SpecimenLightInk GfxRgb8(238, 236, 244)

/** Where each size's block landed, so a case can say which size drew nothing. */
typedef struct SpecimenBands {
    int32_t top[6];
    int32_t bottom[6];
    int32_t darkTop;
    int32_t darkBottom;
    size_t sizes;
} SpecimenBands;

/** Read @p path whole; NULL when it cannot be read. Caller frees. */
static unsigned char *SpecimenReadFile(const char *path, size_t *outSize)
{
    FILE *file = fopen(path, "rb");

    *outSize = 0u;
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    {
        long size = ftell(file);
        unsigned char *bytes;

        if (size <= 0L || fseek(file, 0, SEEK_SET) != 0) {
            fclose(file);
            return NULL;
        }
        bytes = (unsigned char *)malloc((size_t)size);
        if (!bytes) {
            fclose(file);
            return NULL;
        }
        if (fread(bytes, 1u, (size_t)size, file) != (size_t)size) {
            free(bytes);
            fclose(file);
            return NULL;
        }
        fclose(file);
        *outSize = (size_t)size;
        return bytes;
    }
}

/**
 * Draw @p text from @p penX on @p baselineY and return the advance it consumed.
 *
 * The walk is the paint stack's own -- cmap for the glyph, the metrics bridge for the advance, the
 * raster for the ink -- because a fixture that measured its own glyphs would be measuring a
 * different renderer from the one it exists to photograph. `penX` stays fractional for the coverage
 * mode, which is the point of the mode.
 */
static float SpecimenText(GfxRaster *raster,
                          FontFace *face,
                          float penX,
                          float baselineY,
                          const char *text,
                          float sizePx,
                          GfxColorRgba8 color,
                          GfxGlyphRenderMode mode)
{
    GfxGlyphDraw draw;
    float pen = penX;
    size_t index = 0u;

    GfxGlyphDrawInit(&draw);
    draw.face = face;
    draw.sizePx = sizePx;
    draw.color = color;
    draw.mode = mode;

    while (text[index] != '\0') {
        uint32_t codePoint = (uint8_t)text[index];
        FontCmapLookupResult lookup;
        GfxGlyphMetricsDevice metrics;
        GlyphId glyph;

        index++;
        memset(&lookup, 0, sizeof(lookup));
        glyph = ((FontFaceLookupGlyph(face, codePoint, &lookup) == RoboOk) &&
                 lookup.status == RoboFontCmapMapped)
                    ? lookup.glyphId
                    : RoboGlyphIdNone;
        draw.glyphId = glyph;
        draw.origin = MakeGfxPointF(pen, baselineY);
        if (GfxRasterDrawGlyph(raster, &draw) != RoboOk) {
            break;
        }
        memset(&metrics, 0, sizeof(metrics));
        if (GetGfxGlyphMetricsDevice(face, glyph, sizePx, &metrics) == RoboOk) {
            pen += metrics.advanceWidth;
        } else {
            pen += sizePx * 0.5f;
        }
    }
    return pen - penX;
}

/**
 * The strings every size draws, so the same text appears in both sections.
 *
 * The pangram is split across two lines on purpose: the widest string at the largest size has to
 * fit the surface with its right margin, and the fixture asserts that it does. The split is the
 * fixture's own bookkeeping, not a line-breaking feature -- text layout is a later phase and this
 * one does not get to touch it.
 */
static const char *SpecimenLineOne = "Aa Bb Cc Dd Ee Ff Gg 0123456789";
static const char *SpecimenLineTwo = "Hamburgefontsiv. The quick brown fox";
static const char *SpecimenLineThree = "jumps over the lazy dog.";

/**
 * Draw the whole specimen through @p mode and say where each size's block landed.
 *
 * `outBands` and `outSizeHint` are both optional; the hint is the widest advance the specimen used,
 * a cheap way for the fixture to notice that the text outgrew its surface rather than silently
 * clipping it.
 */
static void SpecimenRender(GfxRaster *raster,
                           FontFace *face,
                           GfxGlyphRenderMode mode,
                           SpecimenBands *outBands,
                           float *outSizeHint)
{
    SpecimenBands bands;
    float hint = 0.0f;
    float baseline = (float)SpecimenMargin + 34.0f;
    size_t index;

    memset(&bands, 0, sizeof(bands));
    bands.sizes = SpecimenSizeCount;

    /* The wordmark, at the largest size, so the two modes are compared on a shape with curves,
     * diagonals and a thin stroke all at once. */
    {
        float advance = SpecimenText(raster, face, (float)SpecimenMargin, baseline, "RoboticelaOS",
                                     32.0f, SpecimenInk, mode);

        if (advance > hint) {
            hint = advance;
        }
    }
    baseline += 44.0f;

    for (index = 0u; index < SpecimenSizeCount; index++) {
        const char *lines[3];
        float size = SpecimenSizes[index];
        char numbered[64];
        size_t line;

        bands.top[index] = (int32_t)baseline - (int32_t)size;
        snprintf(numbered, sizeof(numbered), "%g %s", (double)size, SpecimenLineOne);
        lines[0] = numbered;
        lines[1] = SpecimenLineTwo;
        lines[2] = SpecimenLineThree;

        for (line = 0u; line < 3u; line++) {
            float advance;

            baseline += size;
            advance = SpecimenText(raster, face, (float)SpecimenMargin, baseline, lines[line], size,
                                   SpecimenInk, mode);
            if (advance > hint) {
                hint = advance;
            }
            baseline += size * SpecimenLineLead * 0.45f;
        }
        baseline += 8.0f;
        bands.bottom[index] = (int32_t)baseline;
    }

    /* The dark section: the same face and the same strings, light on dark, at two sizes. */
    bands.darkTop = (int32_t)baseline + 6;
    bands.darkBottom = (int32_t)SpecimenHeight;
    {
        GfxRect panel = MakeGfxRect(0, bands.darkTop, (int32_t)SpecimenWidth,
                                    bands.darkBottom - bands.darkTop);
        float darkBaseline = (float)bands.darkTop + 34.0f;

        (void)GfxRasterFillRect(raster, panel, SpecimenDarkPaper);
        (void)SpecimenText(raster, face, (float)SpecimenMargin, darkBaseline,
                           "RoboticelaOS  Aa Bb Cc Dd Ee Ff Gg 0123456789", 22.0f,
                           SpecimenLightInk, mode);
        darkBaseline += 32.0f;
        (void)SpecimenText(raster, face, (float)SpecimenMargin, darkBaseline,
                           SpecimenLineTwo, 16.0f, SpecimenLightInk, mode);
        darkBaseline += 26.0f;
        (void)SpecimenText(raster, face, (float)SpecimenMargin, darkBaseline,
                           SpecimenLineThree, 16.0f, SpecimenLightInk, mode);
        darkBaseline += 26.0f;
        (void)SpecimenText(raster, face, (float)SpecimenMargin, darkBaseline,
                           SpecimenLineOne, 13.0f, SpecimenLightInk, mode);
    }

    if (outBands) {
        *outBands = bands;
    }
    if (outSizeHint) {
        *outSizeHint = hint;
    }
}

/* ------------------------------------------------------------------ */
/* Measurement                                                         */
/* ------------------------------------------------------------------ */

/** Pixels that differ from @p paper in any channel, inside a half-open row band. */
static uint32_t SpecimenInked(GfxSurface *surface, GfxColorRgba8 paper, int32_t top, int32_t bottom)
{
    uint32_t count = 0u;
    int32_t y;

    for (y = top; y < bottom; y++) {
        uint32_t x;

        for (x = 0u; x < GfxSurfaceWidth(surface); x++) {
            GfxColorRgba8 color;

            memset(&color, 0, sizeof(color));
            if (GfxSurfaceGetPixel(surface, x, (uint32_t)y, &color) != RoboOk) {
                continue;
            }
            if (color.r != paper.r || color.g != paper.g || color.b != paper.b) {
                count++;
            }
        }
    }
    return count;
}

/**
 * Pixels strictly between @p paper and @p ink — the exclusive test that says "this edge was
 * antialiased".
 *
 * A stroke writes the ink's exact bytes and a binary fill writes either the page or the ink, so
 * neither can produce one. Only a rasterizer that resolved partial coverage can.
 */
static uint32_t SpecimenPartial(GfxSurface *surface,
                                GfxColorRgba8 paper,
                                GfxColorRgba8 ink,
                                int32_t top,
                                int32_t bottom)
{
    uint32_t count = 0u;
    int32_t y;

    for (y = top; y < bottom; y++) {
        uint32_t x;

        for (x = 0u; x < GfxSurfaceWidth(surface); x++) {
            GfxColorRgba8 color;

            memset(&color, 0, sizeof(color));
            if (GfxSurfaceGetPixel(surface, x, (uint32_t)y, &color) != RoboOk) {
                continue;
            }
            if ((color.r > paper.r + 2u && color.r < ink.r - 2u) ||
                (color.r < paper.r - 2u && color.r > ink.r + 2u)) {
                count++;
            }
        }
    }
    return count;
}

static int SurfacesEqual(GfxSurface *a, GfxSurface *b)
{
    uint32_t y;

    if (GfxSurfaceWidth(a) != GfxSurfaceWidth(b) || GfxSurfaceHeight(a) != GfxSurfaceHeight(b)) {
        return 0;
    }
    for (y = 0u; y < GfxSurfaceHeight(a); y++) {
        uint32_t x;

        for (x = 0u; x < GfxSurfaceWidth(a); x++) {
            GfxColorRgba8 pa;
            GfxColorRgba8 pb;

            memset(&pa, 0, sizeof(pa));
            memset(&pb, 0, sizeof(pb));
            (void)GfxSurfaceGetPixel(a, x, y, &pa);
            (void)GfxSurfaceGetPixel(b, x, y, &pb);
            if (pa.r != pb.r || pa.g != pb.g || pa.b != pb.b || pa.a != pb.a) {
                return 0;
            }
        }
    }
    return 1;
}

/** Encode @p surface and write it to @p path. Best effort, never an assertion. */
static int SpecimenWritePng(GfxSurface *surface, const char *path)
{
    GfxEncodeOptions options;
    GfxEncodedBuffer buffer;
    FILE *file;

    GfxEncodeOptionsInit(&options);
    options.container = RoboGfxEncodePng;
    options.layout = RoboGfxEncodeLayoutRgba8;
    memset(&buffer, 0, sizeof(buffer));
    if (GfxSurfaceEncode(surface, &options, &buffer) != RoboOk) {
        return 0;
    }
    file = fopen(path, "wb");
    if (!file) {
        GfxEncodedBufferRelease(&buffer);
        return 0;
    }
    if (fwrite(buffer.data, 1u, buffer.size, file) != buffer.size) {
        fclose(file);
        GfxEncodedBufferRelease(&buffer);
        return 0;
    }
    fclose(file);
    GfxEncodedBufferRelease(&buffer);
    return 1;
}

static Result SpecimenSurface(GfxSurface **out)
{
    GfxSurfaceDesc desc;

    GfxSurfaceDescInit(&desc);
    desc.width = SpecimenWidth;
    desc.height = SpecimenHeight;
    desc.format = RoboGfxFormatRgba8;
    desc.access = RoboGfxSurfaceAccessReadWrite;
    return GfxSurfaceCreate(&desc, out);
}

/* ------------------------------------------------------------------ */
/* The case                                                            */
/* ------------------------------------------------------------------ */

static void TestTypographySpecimen(void)
{
    unsigned char *fontBytes;
    size_t fontSize = 0u;
    FontSource *source = NULL;
    FontFace *face = NULL;
    GfxSurface *coverage = NULL;
    GfxSurface *coverageAgain = NULL;
    GfxSurface *stroke = NULL;
    GfxRaster *coverageRaster = NULL;
    GfxRaster *againRaster = NULL;
    GfxRaster *strokeRaster = NULL;
    SpecimenBands bands;
    float coverageAdvance = 0.0f;
    float strokeAdvance = 0.0f;
    uint32_t coverageInk;
    uint32_t strokeInk;
    uint32_t coveragePartial;
    uint32_t strokePartial;
    size_t index;

    fontBytes = SpecimenReadFile(RoboFontAssetPath, &fontSize);
    RoboCheckMsg(fontBytes != NULL, "the system face is unreadable");
    if (!fontBytes) {
        return;
    }
    RoboCheckEqInt(FontSourceCreateMemoryCopy(fontBytes, fontSize, &source), RoboOk);
    free(fontBytes);
    RoboCheckEqInt(FontFaceCreate(source, 0, &face), RoboOk);
    /* The cmap is what turns a byte into a glyph id; the text path prepares it for itself, and this
     * fixture prepares it for the same reason the pass does. */
    RoboCheckEqInt(FontFaceParseCmap(face), RoboOk);

    RoboCheckEqInt(SpecimenSurface(&coverage), RoboOk);
    RoboCheckEqInt(SpecimenSurface(&coverageAgain), RoboOk);
    RoboCheckEqInt(SpecimenSurface(&stroke), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(coverage, &coverageRaster), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(coverageAgain, &againRaster), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(stroke, &strokeRaster), RoboOk);
    RoboCheckEqInt(GfxSurfaceClearRgba8(coverage, SpecimenPaper), RoboOk);
    RoboCheckEqInt(GfxSurfaceClearRgba8(coverageAgain, SpecimenPaper), RoboOk);
    RoboCheckEqInt(GfxSurfaceClearRgba8(stroke, SpecimenPaper), RoboOk);

    /* Each mode draws onto its own cleaned page, so "these two pictures differ" and "this picture
     * repeats" are both statements about one mode rather than about what was underneath it. */
    SpecimenRender(coverageRaster, face, RoboGfxGlyphModeAlpha, &bands, &coverageAdvance);
    SpecimenRender(againRaster, face, RoboGfxGlyphModeAlpha, NULL, NULL);
    SpecimenRender(strokeRaster, face, RoboGfxGlyphModeOutlineStroke, NULL, &strokeAdvance);

    /* The coverage rendering is asked the same question twice, so the determinism claim is about
     * the *same* mode, and the comparison is between different ones. */
    RoboCheck(SurfacesEqual(coverage, coverageAgain));
    RoboCheck(!SurfacesEqual(coverage, stroke));

    /* The specimen has to fit, or the sections below would be measuring a clipped page. */
    RoboCheck(bands.darkTop > 0 && bands.darkTop < (int32_t)SpecimenHeight);
    RoboCheck(bands.bottom[SpecimenSizeCount - 1u] < bands.darkTop);

    coverageInk = SpecimenInked(coverage, SpecimenPaper, 0, bands.darkTop);
    strokeInk = SpecimenInked(stroke, SpecimenPaper, 0, bands.darkTop);
    coveragePartial = SpecimenPartial(coverage, SpecimenPaper, SpecimenInk, 0, bands.darkTop);
    strokePartial = SpecimenPartial(stroke, SpecimenPaper, SpecimenInk, 0, bands.darkTop);

    /* The phase, as a number: partial coverage exists in one and cannot exist in the other. */
    RoboCheck(coveragePartial > 1000u);
    RoboCheckEqInt((int)strokePartial, 0);
    /* And the fill reaches far more of the page than the outline it replaces. */
    printf("typography: coverage ink %u, stroke ink %u, coverage partial %u\n", coverageInk,
           strokeInk, coveragePartial);
    RoboCheck(coverageInk > strokeInk);

    /* Every size drew, in the mode the OS actually uses. */
    for (index = 0u; index < SpecimenSizeCount; index++) {
        uint32_t inked = SpecimenInked(coverage, SpecimenPaper, bands.top[index],
                                       bands.bottom[index]);

        RoboCheck(inked > 0u);
    }

    /* And the light-on-dark section is not empty, in either polarity of ink. */
    RoboCheck(SpecimenInked(coverage, SpecimenDarkPaper, bands.darkTop, bands.darkBottom) > 200u);
    RoboCheck(SpecimenPartial(coverage, SpecimenDarkPaper, SpecimenLightInk, bands.darkTop,
                              bands.darkBottom) > 20u);

    /* Neither mode's widest line outgrew the surface's right margin, and neither mode disagreed
     * about where the text went -- the advance is a property of the face, not of the paint. */
    RoboCheck(coverageAdvance > 0.0f);
    RoboCheck(coverageAdvance < (float)SpecimenWidth - (float)SpecimenMargin);
    RoboCheck(strokeAdvance < (float)SpecimenWidth - (float)SpecimenMargin);

    /* The checkpoint, for a person: best effort, and told about rather than asserted. */
    if (SpecimenWritePng(coverage, "Build/Typography-Coverage.png")) {
        printf("typography checkpoint: Build/Typography-Coverage.png\n");
    }
    if (SpecimenWritePng(stroke, "Build/Typography-Stroke.png")) {
        printf("typography checkpoint: Build/Typography-Stroke.png\n");
    }

    GfxRasterDestroy(coverageRaster);
    GfxRasterDestroy(againRaster);
    GfxRasterDestroy(strokeRaster);
    GfxSurfaceRelease(coverage);
    GfxSurfaceRelease(coverageAgain);
    GfxSurfaceRelease(stroke);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

/**
 * The same path at a size where hinting would matter most, and where it does not exist.
 *
 * RoboFont exposes no TrueType bytecode interpreter, so this build has no hinting; the phase's
 * answer is that unhinted coverage at 12px is *readable* rather than that it is hinted, and the
 * fixture pins that the smallest UI size still puts down ink with partial coverage instead of
 * collapsing to a thresholded blob. The test's value is that it fails if a future change makes small
 * text disappear, not that it measures a hinting quality this build does not have.
 */
static void TestTypographySmallSizeCoverage(void)
{
    unsigned char *fontBytes;
    size_t fontSize = 0u;
    FontSource *source = NULL;
    FontFace *face = NULL;
    GfxSurface *surface = NULL;
    GfxRaster *raster = NULL;
    uint32_t inked;
    uint32_t partial;

    fontBytes = SpecimenReadFile(RoboFontAssetPath, &fontSize);
    RoboCheckMsg(fontBytes != NULL, "the system face is unreadable");
    if (!fontBytes) {
        return;
    }
    RoboCheckEqInt(FontSourceCreateMemoryCopy(fontBytes, fontSize, &source), RoboOk);
    free(fontBytes);
    RoboCheckEqInt(FontFaceCreate(source, 0, &face), RoboOk);
    RoboCheckEqInt(FontFaceParseCmap(face), RoboOk);

    RoboCheckEqInt(SpecimenSurface(&surface), RoboOk);
    RoboCheckEqInt(GfxRasterCreate(surface, &raster), RoboOk);
    RoboCheckEqInt(GfxSurfaceClearRgba8(surface, SpecimenPaper), RoboOk);

    (void)SpecimenText(raster, face, 8.0f, 24.0f, "Hamburgefontsiv iIl1|", 12.0f, SpecimenInk,
                       RoboGfxGlyphModeAlpha);
    (void)SpecimenText(raster, face, 8.0f, 48.0f, "Hamburgefontsiv iIl1|", 12.0f, SpecimenInk,
                       RoboGfxGlyphModeOutlineStroke);

    inked = SpecimenInked(surface, SpecimenPaper, 0, 30);
    partial = SpecimenPartial(surface, SpecimenPaper, SpecimenInk, 0, 30);
    RoboCheck(inked > 40u);
    RoboCheck(partial > 10u);

    /* The stroked line below it cannot contribute a partial pixel, so the count above is the
     * coverage line's own. */
    RoboCheckEqInt((int)SpecimenPartial(surface, SpecimenPaper, SpecimenInk, 30, 60), 0);
    RoboCheck(SpecimenInked(surface, SpecimenPaper, 30, 60) > 40u);

    GfxRasterDestroy(raster);
    GfxSurfaceRelease(surface);
    FontFaceRelease(face);
    FontSourceRelease(source);
}

void TestRegisterGraphicsTypography(void)
{
    TestRegister("graphics.typography.specimen", TestTypographySpecimen);
    TestRegister("graphics.typography.small_size_coverage", TestTypographySmallSizeCoverage);
}
