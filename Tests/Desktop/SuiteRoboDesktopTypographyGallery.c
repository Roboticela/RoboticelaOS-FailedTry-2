/*
 * The typography galleries (Phase 12.3) — `desktop.typography.gallery.*`.
 *
 * Three deterministic pictures, one per surface group the phase binds, and each of them is *the*
 * surface group rather than a drawing of it:
 *
 *   desktop     the shell's own panel — its identity title, its applications label and its status.
 *               No window at all, because a window's title is chrome and belongs to the next gallery.
 *   chrome      three windows, so the window title band is exercised at three different titles, plus
 *               the panel labels beside them.
 *   document    a specimen page built through `RoboDesktopDocument`, so every line on it is a
 *               RoboHTML element whose type setting came from the resolver's generated default sheet.
 *
 * ## Why a gallery and not only an assertion
 *
 * `SuiteRoboDesktopTypography` proves the *binding*: that a role reaches a face, that a document's
 * heading is display-sized, that the shell draws four labels and counts none unshaped. What it cannot
 * say in a number is whether the result reads as one coherent type system — whether the title sits
 * above the label, whether the leading leaves a page calm. That judgement needs pixels a person can
 * look at, so each case below writes its frame to `Build/` and prints the path, exactly as the 12.1
 * raster fixture does (`Build/Typography-Coverage.png`).
 *
 * ## What each case *asserts*, given that a picture is not a verdict
 *
 * Three things, and they are the three things a screenshot test can honestly be:
 *
 *   **the same state renders the same frame.** Two renders of one desktop are compared with
 *   `DesktopFrameRecordEqual` *and* by checksum. Without this, a "reference image" would not be a
 *   reference for anything, because the next run might legitimately differ in every pixel.
 *
 *   **the scale is the authority for the picture.** Each case builds a *second* resolver over the
 *   same pack whose scale has one or two of the surface's own roles resized, and asserts the frame
 *   checksum changes. If a surface read a size from anywhere else — a constant at its paint site, a
 *   leftover from the pre-12.3 binding — the two frames would match and this check would fail. That is
 *   the regression the phase is most likely to suffer, and this is how it is caught.
 *
 *   **the ladder reaches the pixels end to end.** The document case renders the same markup with the
 *   system's generated sheet and with the layout engine's own initial type setting, and compares the
 *   *lowest row that has ink*. A 32 px h1 followed by a 13 px caption stacks visibly taller than the
 *   same lines at a flat 16 px, so the number is a statement about every step of the scale rather than
 *   about one role.
 *
 * ## The four weights, and why they look alike here
 *
 * The specimen names Regular, Medium, Semibold and Bold. On the shipped family they render
 * **identically**, and that is correct rather than a defect: the packed face is Inter *Variable*, the
 * selector resolves the requested weight and records it in the style (`RoboTypographyStyle.weight`),
 * and applying the `wght` axis to an instance is a later goal — 12.3 binds the weight, it does not
 * instance it. The lines are in the gallery on purpose: they are what a reader compares against once
 * the axis *is* applied, and a change that made them differ will show up here first.
 */

#include "RoboDesktopTestSupport.h"
#include "RoboTypographyTestSupport.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The evidence                                                        */
/* ------------------------------------------------------------------ */

#define GALLERY_DESKTOP_PATH "Build/Typography-Desktop.png"
#define GALLERY_CHROME_PATH "Build/Typography-Chrome.png"
#define GALLERY_DOCUMENT_PATH "Build/Typography-Document.png"

/* ------------------------------------------------------------------ */
/* The fixture                                                         */
/* ------------------------------------------------------------------ */

/**
 * A desktop whose chrome draws from @p resolver, with **no** single face configured.
 *
 * Passing no `face` is deliberate and it is the same posture `SuiteRoboDesktopTypography` takes: if a
 * chrome surface were still reading the legacy field, these galleries would come out blank and every
 * case would fail on the unshaped count. A gallery drawn through the resolver alone is the phase's
 * claim, made visible.
 */
static Desktop *GalleryDesktop(int32_t width, int32_t height, TypographyResolver *resolver)
{
    DesktopConfig config;
    Desktop *desktop = NULL;

    DesktopFixture();
    DesktopConfigInit(&config);
    config.width = width;
    config.height = height;
    config.typography = resolver;
    RoboCheckEqInt(DesktopCreate(&config, &desktop, NULL), RoboOk);
    RoboCheck(desktop != NULL);
    return desktop;
}

/** One role's replacement numbers, for the "the scale is the authority" case. */
typedef struct galleryRescale {
    RoboTypographyRole role;
    float sizePx;
    float lineHeightPx;
} GalleryRescale;

/**
 * A resolver over @p fixture's pack whose scale is the fixture's with @p changes applied.
 *
 * The storage is the caller's because a resolver *borrows* its scale
 * (`RoboTypographyResolverConfig.scale`): the array must outlive the resolver, so it cannot be a local
 * of this function.
 */
static TypographyResolver *GalleryRescaledResolver(const TypographyFixture *fixture,
                                                   RoboTypographyScaleEntry *storage,
                                                   const GalleryRescale *changes,
                                                   size_t changeCount)
{
    RoboTypographyResolverConfig config;
    TypographyResolver *resolver = NULL;
    size_t index;

    RoboCheckEqInt(RoboTypographyDefaultScale(storage, RoboTypographyScaleCount, NULL), RoboOk);
    for (index = 0u; index < changeCount; index++) {
        RoboTypographyScaleEntry *entry =
            (RoboTypographyScaleEntry *)RoboTypographyScaleFindEntry(storage,
                                                                    RoboTypographyScaleCount,
                                                                    changes[index].role);

        RoboCheckMsg(entry != NULL, "the scale has no entry for a role the case resizes");
        entry->sizePx = changes[index].sizePx;
        entry->lineHeightPx = changes[index].lineHeightPx;
        RoboCheckMsg(RoboTypographyScaleEntryIsValid(entry),
                     "a resized entry is not a type setting the module accepts");
    }

    RoboTypographyResolverConfigInit(&config);
    config.pack = fixture->pack;
    config.fallback = fixture->fallback;
    config.scale = storage;
    config.scaleCount = RoboTypographyScaleCount;
    RoboCheckEqInt(RoboTypographyResolverCreate(&config, &resolver, NULL), RoboOk);
    RoboCheck(resolver != NULL);
    return resolver;
}

/**
 * Build @p html into @p id's client area through @p resolver and attach it to the window.
 *
 * `config.typography` is set and `config.face` is not, so the document's face *and* its default sheet
 * both come from the resolver — which is the whole of the document half of this phase in one call.
 */
static DesktopDocument *GalleryWindowContent(Desktop *desktop,
                                             DesktopWindowId id,
                                             const char *html,
                                             const char *css,
                                             TypographyResolver *resolver,
                                             FontFace *face)
{
    DesktopDocumentConfig config;
    DesktopDocument *document = NULL;
    GfxRect client;

    RoboCheckEqInt(DesktopWindowClientBounds(desktop, id, &client), RoboOk);
    DesktopDocumentConfigInit(&config);
    config.html = html;
    config.css = css;
    config.width = client.width;
    config.height = client.height;
    config.face = face;
    config.typography = resolver;
    RoboCheckEqInt(DesktopDocumentBuild(&config, &document, NULL), RoboOk);
    RoboCheck(document != NULL);
    RoboCheckEqInt(DesktopWindowSetContent(desktop, id, DesktopDocumentDisplayList(document),
                                           DesktopDocumentFace(document), NULL),
                   RoboOk);
    return document;
}

/** Pixels in a half-open rectangle that differ from @p background in any channel. */
static int32_t GalleryInk(const Desktop *desktop, GfxRect rect, GfxColorRgba8 background)
{
    const GfxSurface *surface = DesktopFrameSurface(desktop);
    int32_t count = 0;
    int32_t y;

    RoboCheck(surface != NULL);
    for (y = rect.y; y < rect.y + rect.height; y++) {
        int32_t x;

        for (x = rect.x; x < rect.x + rect.width; x++) {
            GfxColorRgba8 color;

            if (x < 0 || y < 0 || x >= (int32_t)DesktopScreenSize(desktop).width ||
                y >= (int32_t)DesktopScreenSize(desktop).height) {
                continue;
            }
            memset(&color, 0, sizeof(color));
            if (GfxSurfaceGetPixel(surface, (uint32_t)x, (uint32_t)y, &color) != RoboOk) {
                continue;
            }
            if (color.r != background.r || color.g != background.g || color.b != background.b) {
                count++;
            }
        }
    }
    return count;
}

/** The greatest y in @p rect whose whole row is not @p background, or -1 when the rectangle is clear. */
static int32_t GalleryLastInkRow(const Desktop *desktop, GfxRect rect, GfxColorRgba8 background)
{
    int32_t last = -1;
    int32_t y;

    for (y = rect.y; y < rect.y + rect.height; y++) {
        int32_t x;

        for (x = rect.x; x < rect.x + rect.width; x++) {
            GfxColorRgba8 color;

            if (x < 0 || y < 0 || x >= (int32_t)DesktopScreenSize(desktop).width ||
                y >= (int32_t)DesktopScreenSize(desktop).height) {
                continue;
            }
            memset(&color, 0, sizeof(color));
            if (GfxSurfaceGetPixel(DesktopFrameSurface(desktop), (uint32_t)x, (uint32_t)y,
                                   &color) != RoboOk) {
                continue;
            }
            if (color.r != background.r || color.g != background.g || color.b != background.b) {
                last = y;
                break;
            }
        }
    }
    return last;
}

/** Render twice and assert the two frames are one frame. Returns the checksum. */
static uint32_t GalleryDeterministic(Desktop *desktop)
{
    DesktopFrameRecord first;
    DesktopFrameRecord second;

    RoboCheckEqInt(DesktopRender(desktop, &first, NULL), RoboOk);
    RoboCheckEqInt(DesktopRender(desktop, &second, NULL), RoboOk);
    RoboCheckMsg(DesktopFrameRecordEqual(&first, &second),
                 "two renders of one typography state produced different frames");
    RoboCheck(DesktopFrameChecksum(desktop) == second.checksum);
    return second.checksum;
}

/* ------------------------------------------------------------------ */
/* DesktopTypographyGallery                                            */
/* ------------------------------------------------------------------ */

/**
 * The shell's own panel: identity, applications and status — three labels, three roles.
 *
 * The identity is `DesktopTitle` (13 px semibold, display class), the applications label is
 * `ChromeLabel` and the status is `ChromeStatus`; a reader looking at the picture is looking at all
 * three at once, which is the point of the gallery. Nothing else is on the desktop, so there is no
 * second type surface to confuse the verdict with.
 */
static void TestDesktopGallery(void)
{
    TypographyFixture fixture = TypographyFixtureOpen(NULL);
    RoboTypographyScaleEntry storage[RoboTypographyScaleCount];
    static const GalleryRescale heavier[] = {
        {RoboTypographyRoleDesktopTitle, 20.0f, 24.0f},
        {RoboTypographyRoleChromeLabel, 20.0f, 24.0f},
    };
    Desktop *desktop;
    Desktop *rescaled;
    TypographyResolver *other;
    DesktopFrameRecord record;
    uint32_t checksum;
    uint32_t rescaledChecksum;

    if (!fixture.resolver) {
        return;
    }

    desktop = GalleryDesktop(900, 600, fixture.resolver);
    RoboCheckEqInt(DesktopSetStatus(desktop, "Phase 12.3", NULL), RoboOk);

    RoboCheckEqInt(DesktopRender(desktop, &record, NULL), RoboOk);
    /* Exactly the panel's three labels, and not one of them unshaped. */
    RoboCheckEqInt((int)record.labels, 3);
    RoboCheckEqInt((int)record.labelsUnshaped, 0);
    RoboCheckEqInt((int)record.windows, 0);

    {
        GfxRect panel = DesktopGeometryOf(desktop).panel;

        RoboCheckMsg(GalleryInk(desktop, panel, DesktopPixel(desktop, 0u, (uint32_t)panel.y)) > 0,
                     "the panel drew no text at all");
    }

    checksum = GalleryDeterministic(desktop);
    if (DesktopWritePng(desktop, GALLERY_DESKTOP_PATH)) {
        printf("typography gallery: %s\n", GALLERY_DESKTOP_PATH);
    }

    /* The same desktop at a bigger identity and label size is a different picture. */
    other = GalleryRescaledResolver(&fixture, storage, heavier, 2u);
    rescaled = GalleryDesktop(900, 600, other);
    RoboCheckEqInt(DesktopSetStatus(rescaled, "Phase 12.3", NULL), RoboOk);
    RoboCheckEqInt(DesktopRender(rescaled, NULL, NULL), RoboOk);
    rescaledChecksum = DesktopFrameChecksum(rescaled);
    RoboCheckMsg(rescaledChecksum != checksum,
                 "the shell's label sizes did not come from the typography scale");

    DesktopClose(rescaled);
    RoboTypographyResolverDestroy(other);
    DesktopClose(desktop);
    TypographyFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* ChromeTypographyGallery                                             */
/* ------------------------------------------------------------------ */

/**
 * Three windows, so the window title band is on screen at three sizes of title.
 *
 * The label budget is exact and worth stating: the record's `labels` counts the *chrome*, so three
 * window titles plus the panel's three, which is six — and `labelsUnshaped` zero, with no `face`
 * configured anywhere. A window title that stopped resolving its role would put a different number on
 * the record.
 */
static void TestChromeGallery(void)
{
    TypographyFixture fixture = TypographyFixtureOpen(NULL);
    RoboTypographyScaleEntry storage[RoboTypographyScaleCount];
    static const GalleryRescale heavier[] = {
        {RoboTypographyRoleWindowTitle, 20.0f, 24.0f},
    };
    Desktop *desktop;
    Desktop *rescaled;
    TypographyResolver *other;
    DesktopFrameRecord record;
    uint32_t checksum;
    uint32_t rescaledChecksum;
    unsigned index;

    if (!fixture.resolver) {
        return;
    }

    desktop = GalleryDesktop(1000, 640, fixture.resolver);
    RoboCheckEqInt(DesktopSetStatus(desktop, "3 windows", NULL), RoboOk);
    for (index = 0u; index < 3u; index++) {
        static const char *const titles[] = {"RoboticelaOS", "Document", "System UI Text"};
        int32_t x = 24 + (int32_t)index * 326;

        RoboCheck(DesktopAddWindow(desktop, titles[index], x, 64, 300, 220) !=
                  RoboDesktopWindowIdNone);
    }

    RoboCheckEqInt(DesktopRender(desktop, &record, NULL), RoboOk);
    RoboCheckEqInt((int)record.labels, 6);
    RoboCheckEqInt((int)record.labelsUnshaped, 0);
    RoboCheckEqInt((int)record.windows, 3);

    checksum = GalleryDeterministic(desktop);
    if (DesktopWritePng(desktop, GALLERY_CHROME_PATH)) {
        printf("typography gallery: %s\n", GALLERY_CHROME_PATH);
    }

    /* A larger window title is a different picture, which is what makes the role a binding and not a
     * comment. The label *count* is unchanged, so the two frames stay comparable. */
    other = GalleryRescaledResolver(&fixture, storage, heavier, 1u);
    rescaled = GalleryDesktop(1000, 640, other);
    RoboCheckEqInt(DesktopSetStatus(rescaled, "3 windows", NULL), RoboOk);
    for (index = 0u; index < 3u; index++) {
        static const char *const titles[] = {"RoboticelaOS", "Document", "System UI Text"};
        int32_t x = 24 + (int32_t)index * 326;

        RoboCheck(DesktopAddWindow(rescaled, titles[index], x, 64, 300, 220) !=
                  RoboDesktopWindowIdNone);
    }
    RoboCheckEqInt(DesktopRender(rescaled, &record, NULL), RoboOk);
    RoboCheckEqInt((int)record.labels, 6);
    rescaledChecksum = DesktopFrameChecksum(rescaled);
    RoboCheckMsg(rescaledChecksum != checksum,
                 "the window title's size did not come from the typography scale");

    DesktopClose(rescaled);
    RoboTypographyResolverDestroy(other);
    DesktopClose(desktop);
    TypographyFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* DocumentTypographyGallery                                           */
/* ------------------------------------------------------------------ */

/**
 * The specimen page. Every line is an element, and every element's type setting is the system's.
 *
 * The author sheet is deliberately *only* geometry — `display` and the box resets — plus the four
 * `font-weight` classes the weight lines name. It carries **no** `font-size` at all, so every size on
 * the page comes from the generated default sheet and therefore from the resolver's scale. That is the
 * property the case below measures, and it is why a version of this document with the sheet missing
 * stacks visibly shorter.
 */
static const char galleryMarkup[] =
    "<html><body>"
    "<h1>RoboticelaOS</h1>"
    "<h2>System UI Display</h2>"
    "<h3>System UI Text</h3>"
    "<p>ABCDEFGHIJKLMNOPQRSTUVWXYZ</p>"
    "<p>abcdefghijklmnopqrstuvwxyz</p>"
    "<p>0123456789</p>"
    "<p>.,;:!?()[]{}@#%*+=/ - The quick brown fox jumps over the lazy dog.</p>"
    "<small>Caption - the smallest step of the scale</small>"
    "<code>code - the system monospace role</code>"
    "<p class=\"w400\">Regular</p>"
    "<p class=\"w500\">Medium</p>"
    "<p class=\"w600\">Semibold</p>"
    "<p class=\"w700\">Bold</p>"
    "</body></html>";

static const char gallerySheet[] =
    "html, body, h1, h2, h3, p, small, code { display: block; margin: 0; padding: 0; }"
    ".w400 { font-weight: 400; }"
    ".w500 { font-weight: 500; }"
    ".w600 { font-weight: 600; }"
    ".w700 { font-weight: 700; }";

/**
 * The specimen, the ladder it stands on, and the flat version of it that proves the ladder is real.
 *
 * The three documents share one face (the resolver's body face), one markup and one author sheet. What
 * differs is which of the system's type settings reached them: the full generated sheet, the generated
 * sheet with heading 1 cut to the body's size, or no generated sheet at all. So the comparison is of
 * *type settings* and nothing else — the geometry, the face and the text are held constant by
 * construction.
 */
static void TestDocumentGallery(void)
{
    TypographyFixture fixture = TypographyFixtureOpen(NULL);
    RoboTypographyScaleEntry storage[RoboTypographyScaleCount];
    static const GalleryRescale flattened[] = {
        {RoboTypographyRoleDocumentHeading1, 16.0f, 24.0f},
        {RoboTypographyRoleDocumentHeading2, 16.0f, 24.0f},
        {RoboTypographyRoleDocumentHeading3, 16.0f, 24.0f},
    };
    Desktop *desktop;
    Desktop *chopped;
    Desktop *initial;
    TypographyResolver *flat;
    DesktopWindowId window;
    DesktopDocument *gallery;
    DesktopDocument *shortGallery;
    DesktopDocument *plain;
    FontFace *bodyFace = NULL;
    GfxRect client;
    GfxColorRgba8 paper;
    DesktopFrameRecord record;
    int32_t galleryLast;
    int32_t choppedLast;
    int32_t plainLast;
    uint32_t checksum;
    uint32_t choppedChecksum;

    if (!fixture.resolver) {
        return;
    }
    {
        RoboTypographyStyle body;

        RoboCheckEqInt(RoboTypographyResolve(fixture.resolver, RoboTypographyRoleDocumentBody, &body,
                                            NULL),
                       RoboOk);
        bodyFace = body.face;
        RoboCheck(bodyFace != NULL);
    }

    desktop = GalleryDesktop(760, 620, fixture.resolver);
    /* Set, because an unset status is an empty label rather than a status label: the panel then draws two
     * of its three labels and the budget below would be off by one for a reason that has nothing to do with
     * typography. */
    RoboCheckEqInt(DesktopSetStatus(desktop, "Document", NULL), RoboOk);
    window = DesktopAddWindow(desktop, "Typography", 24, 40, 700, 540);
    gallery = GalleryWindowContent(desktop, window, galleryMarkup, gallerySheet, fixture.resolver,
                                   bodyFace);
    RoboCheckEqInt(DesktopWindowClientBounds(desktop, window, &client), RoboOk);
    RoboCheckEqInt(DesktopRender(desktop, &record, NULL), RoboOk);
    /*
     * The record's `labels` is the *chrome* -- the panel's three and this window's title -- while the
     * specimen itself is window *content*, and window content is counted by `contentRuns`. So the pair to
     * assert is that the chrome shaped cleanly and that the page ran runs at all; "every line drew" is what
     * the ink and the ladder below are for, because a run count cannot tell a drawn line from a blank one.
     */
    RoboCheckEqInt((int)record.labels, 4);
    RoboCheckEqInt((int)record.labelsUnshaped, 0);
    RoboCheckEqInt((int)record.windows, 1);
    RoboCheckMsg(record.contentRuns > 0u, "the specimen's page ran no content runs at all");

    paper = DesktopPixel(desktop, (uint32_t)client.x, (uint32_t)client.y);
    galleryLast = GalleryLastInkRow(desktop, client, paper);
    RoboCheckMsg(galleryLast > 0, "the specimen's page is blank");
    /* The document's face is the resolver's: measured with it, painted through it. */
    RoboCheck(DesktopDocumentFace(gallery) == bodyFace);

    checksum = GalleryDeterministic(desktop);
    if (DesktopWritePng(desktop, GALLERY_DOCUMENT_PATH)) {
        printf("typography gallery: %s\n", GALLERY_DOCUMENT_PATH);
    }

    /* Headings at the body's size: the same page, one type setting flatter, a shorter stack. */
    flat = GalleryRescaledResolver(&fixture, storage, flattened, 3u);
    chopped = GalleryDesktop(760, 620, flat);
    RoboCheckEqInt(DesktopSetStatus(chopped, "Document", NULL), RoboOk);
    window = DesktopAddWindow(chopped, "Typography", 24, 40, 700, 540);
    shortGallery = GalleryWindowContent(chopped, window, galleryMarkup, gallerySheet, flat, bodyFace);
    RoboCheckEqInt(DesktopWindowClientBounds(chopped, window, &client), RoboOk);
    RoboCheckEqInt(DesktopRender(chopped, NULL, NULL), RoboOk);
    choppedChecksum = DesktopFrameChecksum(chopped);
    choppedLast = GalleryLastInkRow(chopped, client, paper);
    RoboCheckMsg(choppedChecksum != checksum,
                 "the document's type settings did not come from the typography scale");
    RoboCheckMsg(choppedLast < galleryLast,
                 "flattening the headings did not shorten the page's type ladder");

    /* And no system defaults at all: the layout engine's initial type setting, one face, same page. */
    initial = GalleryDesktop(760, 620, fixture.resolver);
    RoboCheckEqInt(DesktopSetStatus(initial, "Document", NULL), RoboOk);
    window = DesktopAddWindow(initial, "Typography", 24, 40, 700, 540);
    plain = GalleryWindowContent(initial, window, galleryMarkup, gallerySheet, NULL, bodyFace);
    RoboCheckEqInt(DesktopWindowClientBounds(initial, window, &client), RoboOk);
    RoboCheckEqInt(DesktopRender(initial, NULL, NULL), RoboOk);
    plainLast = GalleryLastInkRow(initial, client, paper);
    RoboCheckMsg(plainLast > 0, "the page drew nothing without the system defaults");
    RoboCheckMsg(plainLast < galleryLast,
                 "the system's document defaults were not taller than the layout engine's initial type");
    RoboCheck(DesktopDocumentFace(plain) == bodyFace);

    DesktopDocumentDestroy(plain);
    DesktopClose(initial);
    DesktopDocumentDestroy(shortGallery);
    DesktopClose(chopped);
    RoboTypographyResolverDestroy(flat);
    DesktopDocumentDestroy(gallery);
    DesktopClose(desktop);
    TypographyFixtureClose(&fixture);
}

void TestRegisterDesktopTypographyGallery(void)
{
    TestRegister("desktop.typography.gallery.desktop", TestDesktopGallery);
    TestRegister("desktop.typography.gallery.chrome", TestChromeGallery);
    TestRegister("desktop.typography.gallery.document", TestDocumentGallery);
}
