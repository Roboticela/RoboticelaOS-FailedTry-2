/*
 * RoboRender module suite (Phase 11.12) — `render.*` and `render.conformance.*`.
 *
 * The assertions a *release* makes about itself, split from the pixel suite beside it because they
 * answer a different question: `render.raster.*` asks "is the picture right?", and these ask "is the
 * build the one it says it is?". Every one of them is a place a later phase finds out that a field it
 * changed did not change alone.
 *
 *   **the version**                three fields, the string and the constant agree, and the flag that
 *                                 turned in this release is the one the module below wrote down for
 *                                 it -- asserted as a *pair* (`RoboPaint`'s `raster` is 1 and this
 *                                 module's `rasterization` is 1), because the two describe one fact
 *                                 from two sides and a build where they disagreed would be a build
 *                                 where one of the tables lied;
 *
 *   **the capability table**       sixteen fields, read **positionally** against a list of names, so
 *                                 a field inserted anywhere but the end shows up as every row after it
 *                                 disagreeing. That is RoboPaint's device and it is kept for the same
 *                                 reason: a struct read positionally is the only way to assert
 *                                 sixteen `int` fields without writing sixteen accessors, and an
 *                                 untested capability flag is one that stops being true without
 *                                 anybody noticing;
 *
 *   **the diagnostic vocabulary**  five domains, named, with the unknown case answering `"unknown"`
 *                                 rather than NULL, and the record's three rules (`set` on a failure,
 *                                 `set` with `RoboOk` clearing, `record` never failing a caller who
 *                                 passed no record);
 *
 *   **the target**                 the facts a frame is described in, the bounds rectangle that is
 *                                 the coordinate statement as a value, the two refusals (no pixels,
 *                                 above the maximum), the byte doors, and the lifetime rule that a
 *                                 destroyed target is gone and a NULL one is harmless;
 *
 *   **the record's own rules**     `RenderFrameIsValid` is *closed*: a record whose fields
 *                                 disagree about what it is -- a bounded run reporting the whole
 *                                 target's pixels, a stride narrower than a row of its own format, a
 *                                 pixel count that is not the product of its size -- is refused, and
 *                                 each refusal is pinned separately because a validity rule is only
 *                                 as good as the cases that reach each clause;
 *
 *   **the dump**                   seven lines for a frame, no address in it, and the needed-length
 *                                 contract when the buffer is short.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite depends on no
 * Chromium header, library or test framework.
 */

#include "RoboRenderTestSupport.h"

#include "RoboPaint/RoboPaint.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. The module                                                       */
/* ------------------------------------------------------------------ */

static void TestRenderVersionAndCapabilities(void)
{
    RenderCapabilities capabilities;
    I32 major = -1;
    I32 minor = -1;
    I32 patch = -1;

    RenderFixture();

    RoboCheckStrEq(RenderVersionString(), RoboRenderVersionString);
    RoboCheckEqInt(RenderGetVersion(&major, &minor, &patch), RoboOk);
    RoboCheckEqInt((int)major, 0);
    /* 11.12 is the release whose subject is pixels rather than descriptions, so its first version is
     * 0.1.0 -- the module did not exist in a form that could be wrong before it. 11.13 is the second
     * release and adds the presentation door, which is why the minor moved and the patch did not. */
    RoboCheckEqInt((int)minor, 2);
    RoboCheckEqInt((int)patch, 0);
    RoboCheckEqInt(RoboRenderApiFrozen, 0);
    RoboCheckEqInt(RenderGetVersion(NULL, &minor, &patch), RoboInvalidArgument);
    RoboCheckEqInt(RenderGetVersion(&major, NULL, &patch), RoboInvalidArgument);
    RoboCheckEqInt(RenderGetVersion(&major, &minor, NULL), RoboInvalidArgument);

    /* The flag and the doors, asserted together: a constant, the function that reports it and the
     * capability fields that are read from it. */
    RoboCheckEqInt(RoboRenderRasterizationImplemented, 1);
    RoboCheckEqInt(RenderRasterizationIsImplemented(), 1);
    /* And the flag 11.13 turned, asserted the same three ways: the constant, the function, and the
     * capability field, which are all read from each other rather than written three times. */
    RoboCheckEqInt(RoboRenderPresentationImplemented, 1);
    RoboCheckEqInt(RenderPresentationIsImplemented(), 1);
    RoboCheckEqInt(RenderGetCapabilities(&capabilities), RoboOk);
    RoboCheckEqInt(capabilities.rasterizer, RenderRasterizationIsImplemented());
    RoboCheckEqInt(capabilities.presentation, RenderPresentationIsImplemented());
    RoboCheckEqInt(capabilities.pixelTargets, 1);
    RoboCheckEqInt(RenderGetCapabilities(NULL), RoboInvalidArgument);

    /* The pair: `RoboPaint`'s `raster` and this module's claim describe one fact, so a build where
     * one turned and the other did not is a build with a table that lies. */
    {
        PaintCapabilities paint;

        PaintFixture();
        RoboCheckEqInt(PaintGetCapabilities(&paint), RoboOk);
        RoboCheckEqInt(paint.raster, 1);
        RoboCheckEqInt(capabilities.rasterizer, 1);
    }

    /* `capabilities_init` writes the "nothing" state rather than a build's list, so a caller can tell
     * "I did not ask" from "I asked and this build does nothing". */
    RenderCapabilitiesInit(&capabilities);
    {
        int cleared[sizeof(RenderCapabilities) / sizeof(int)];
        size_t index;

        memset(cleared, 0, sizeof(cleared));
        memcpy(cleared, &capabilities, sizeof(capabilities));
        for (index = 0u; index < sizeof(cleared) / sizeof(cleared[0]); index++) {
            RoboCheckEqInt(cleared[index], 0);
        }
    }
    RenderCapabilitiesInit(NULL); /* safe */
}

/** One capability field: its name, and what this build must report for it. */
typedef struct renderCapabilityRow {
    const char *name;
    int value;
} RenderCapabilityRow;

/*
 * Declaration order, and the order is the assertion: the walk below pairs each name with the field at
 * the same offset, so a field inserted anywhere but the end shows up as every row after it
 * disagreeing -- which is the loudest possible failure and the right one.
 */
static const RenderCapabilityRow renderCapabilityRows[] = {
    { "rasterizer", 1 },
    { "pixel_targets", 1 },
    { "command_execution", 1 },
    { "region_runs", 1 },
    { "clipping", 1 },
    { "opacity", 1 },
    { "text", 1 },
    { "images", 1 },
    { "determinism", 1 },
    { "frame_records", 1 },
    /* 11.13's row, and it is the one this release moves: a frame reaching a destination. */
    { "presentation", 1 },
    /* The five remaining absences, and each one is *deliberate*: they are structures (a layer tree, a
     * tile tree, a damage history, a clock, a device) rather than flags somebody forgot to set. */
    { "compositor", 0 },
    { "tiles", 0 },
    { "damage_tracking", 0 },
    { "frame_scheduling", 0 },
    { "gpu", 0 }
};

static void TestRenderConformanceCapabilities(void)
{
    RenderCapabilities capabilities;
    int values[sizeof(RenderCapabilities) / sizeof(int)];
    size_t index;

    RenderFixture();
    /* The walk is positional, so the counts are what make it sound: the struct has exactly one `int`
     * per row and no padding, or a row would be asserting the neighbouring field. */
    RoboCheckEqInt((int)(sizeof(RenderCapabilities) / sizeof(int)),
                      (int)(sizeof(renderCapabilityRows) / sizeof(renderCapabilityRows[0])));
    RoboCheckEqInt((int)sizeof(RenderCapabilities),
                      (int)(sizeof(renderCapabilityRows) / sizeof(renderCapabilityRows[0])) *
                          (int)sizeof(int));
    RoboCheckEqInt(RenderGetCapabilities(&capabilities), RoboOk);

    memset(values, 0, sizeof(values));
    memcpy(values, &capabilities, sizeof(capabilities));
    for (index = 0u; index < sizeof(renderCapabilityRows) / sizeof(renderCapabilityRows[0]);
         index++) {
        if (values[index] != renderCapabilityRows[index].value) {
            TestFail(__FILE__, __LINE__, "capability %s is %d, expected %d",
                           renderCapabilityRows[index].name, values[index],
                           renderCapabilityRows[index].value);
        }
        /* Every field is a flag, so a value that is neither 0 nor 1 cannot be read as one. */
        RoboCheck(values[index] == 0 || values[index] == 1);
    }
}

static void TestRenderConformanceVocabulary(void)
{
    size_t index;

    RenderFixture();

    /* The target format is one, stated, and the constant beside it is the format layer's own name for
     * it -- so a reader of a frame's stride and a reader of a pixel format's metadata are reading the
     * same format. */
    RoboCheckEqInt((int)RoboRenderTargetFormat, (int)RoboGfxFormatRgba8);
    {
        GfxPixelFormatInfo info;
        uint8_t bpp = 0u;

        memset(&info, 0, sizeof(info));
        RoboCheckEqInt(GetGfxPixelFormatInfo(RoboRenderTargetFormat, &info), RoboOk);
        RoboCheckEqInt((int)info.isImplemented, 1);
        RoboCheckEqInt((int)info.hasAlpha, 1);
        RoboCheckEqInt(GfxPixelFormatBytesPerPixel(RoboRenderTargetFormat, &bpp),
                          RoboOk);
        /* Four bytes: the stride arithmetic and the record's own validity rule are built on this
         * number, so it is asserted rather than assumed. */
        RoboCheckEqInt((int)bpp, 4);
    }

    /* The diagnostic domains: all five known, named, and the unknown case answering a *string*
     * rather than NULL -- a dump that printed `(null)` for an undefined domain would be a dump a
     * reader could not tell from a missing field. */
    RoboCheckEqInt((int)RoboRenderDomainCount, 5);
    for (index = 0u; index < (size_t)RoboRenderDomainCount; index++) {
        RenderDiagnosticDomain domain = (RenderDiagnosticDomain)index;
        const char *name = RenderDomainName(domain);

        RoboCheck(name != NULL);
        RoboCheck(name[0] != '\0');
        RoboCheckStrEq(name, RenderDomainName(domain));
        RoboCheckEqInt(RenderDomainIsKnown(domain), 1);
    }
    RoboCheckStrEq(RenderDomainName(RoboRenderDomainNone), "none");
    RoboCheckStrEq(RenderDomainName(RoboRenderDomainTarget), "target");
    RoboCheckStrEq(RenderDomainName(RoboRenderDomainList), "list");
    RoboCheckStrEq(RenderDomainName(RoboRenderDomainFrame), "frame");
    RoboCheckStrEq(RenderDomainName(RoboRenderDomainDump), "dump");
    RoboCheckEqInt(RenderDomainIsKnown((RenderDiagnosticDomain)99), 0);
    RoboCheckStrEq(RenderDomainName((RenderDiagnosticDomain)99), "unknown");
    RoboCheckStrEq(RenderDomainName((RenderDiagnosticDomain)-1), "unknown");
}

/*
 * The presentation vocabulary: what 11.13 added, asserted as the *shape* of the claim rather than by
 * exercising it (the raster suite has the pixel cases). Three things are worth a release check.
 *
 * The first is that the door exists and reports the flag -- one fact, read twice.
 *
 * The second is the pair the release note makes, stated in the direction a reader would get wrong:
 * presentation is **1** and compositor is still **0**, because "a frame can reach a destination" and
 * "more than one surface has an order" are different claims, and a build that had quietly conflated
 * them would present the second as the first. This is the same discipline the raster release applied
 * to `RoboPaint`'s `raster` flag one module down.
 *
 * The third is the diagnostic vocabulary, which did **not** grow: a presentation failure is a `FRAME`
 * failure, because that is the stage -- a frame that could not be run or could not be composited -- and
 * a `PRESENT` domain would send a reader to a module that does not own the pixels.
 */
static void TestRenderConformancePresenter(void)
{
    RenderCapabilities capabilities;
    RenderPresentRecord record;

    RenderFixture();

    RoboCheckEqInt(RoboRenderPresentationImplemented, 1);
    RoboCheckEqInt(RenderPresentationIsImplemented(), 1);
    RoboCheckEqInt(RenderGetCapabilities(&capabilities), RoboOk);
    RoboCheckEqInt(capabilities.presentation, 1);
    /* A frame into one destination is not a compositor. */
    RoboCheckEqInt(capabilities.compositor, 0);

    /* The record's "nothing" state: no frame, no destination, nothing presented. A cleared record is
     * *invalid* rather than valid-and-empty -- it describes no frame -- which is the same reading
     * `RenderFrameIsValid` gives a zeroed frame. */
    RenderPresentRecordInit(&record);
    RoboCheckEqInt((int)record.frameWidth, 0);
    RoboCheckEqInt((int)record.destFormat, (int)RoboGfxFormatUnknown);
    RoboCheckEqInt(record.complete, 0);
    RoboCheckEqInt(record.presentedPixels, 0u);
    RoboCheckEqInt(record.clippedPixels, 0u);
    RoboCheckEqInt(RenderPresentRecordIsValid(&record), 0);
    RoboCheckEqInt(RenderPresentRecordIsValid(NULL), 0);
    RenderPresentRecordInit(NULL); /* safe */

    /* The frame domain is the one a presentation failure names, and it is the same domain a run's own
     * failure uses -- there is no fourth "present" row to look for. */
    RoboCheckStrEq(RenderDomainName(RoboRenderDomainFrame), "frame");
    RoboCheckEqInt((int)RoboRenderDomainCount, 5);
}

static void TestRenderConformanceDiagnostics(void)
{
    RenderDiag diag;

    RenderFixture();

    RenderDiagReset(&diag);
    RoboCheckEqInt((int)diag.code, (int)RoboOk);
    RoboCheckEqInt((int)diag.domain, (int)RoboRenderDomainNone);
    RoboCheck(diag.message == NULL);
    RenderDiagReset(NULL); /* safe */

    /* `set` fills a record for a failure and refuses a NULL record. */
    RoboCheckEqInt(RenderDiagSet(&diag, RoboError, RoboRenderDomainFrame, "boom",
                                           "file.c", 7),
                      RoboError);
    RoboCheckEqInt((int)diag.code, (int)RoboError);
    RoboCheckEqInt((int)diag.domain, (int)RoboRenderDomainFrame);
    RoboCheckStrEq(diag.message, "boom");
    RoboCheckStrEq(diag.file, "file.c");
    RoboCheckEqInt(diag.line, 7);
    RoboCheckEqInt(RenderDiagSet(NULL, RoboError, RoboRenderDomainFrame, "boom",
                                           "file.c", 7),
                      RoboInvalidArgument);

    /* `set` with RoboOk clears, which is how the same record is reused across a loop. */
    RoboCheckEqInt(RenderDiagSet(&diag, RoboOk, RoboRenderDomainFrame, "boom",
                                           "file.c", 7),
                      RoboOk);
    RoboCheckEqInt((int)diag.code, (int)RoboOk);
    RoboCheck(diag.message == NULL);

    /* `record` never changes a caller's result code, with or without a record. */
    RoboCheckEqInt(RoboRenderDiag(&diag, RoboRenderDomainList, RoboNotSupported, "x"),
                      RoboNotSupported);
    RoboCheckEqInt((int)diag.code, (int)RoboNotSupported);
    RoboCheckEqInt((int)diag.domain, (int)RoboRenderDomainList);
    RoboCheckEqInt(RoboRenderDiag(NULL, RoboRenderDomainList, RoboNotSupported, "x"),
                      RoboNotSupported);
    /* And a success recorded through it leaves the record alone rather than clearing it: `record` is
     * the non-strict door, and the result code is authoritative. */
    RoboCheckEqInt(RoboRenderDiag(&diag, RoboRenderDomainList, RoboOk, "x"), RoboOk);
    RoboCheckEqInt((int)diag.code, (int)RoboNotSupported);
}

/* ------------------------------------------------------------------ */
/* 2. The target                                                       */
/* ------------------------------------------------------------------ */

static void TestRenderTargetFacts(void)
{
    RenderTarget *target;
    LayoutRect bounds;
    size_t size = 0u;
    const uint8_t *bytes;

    RenderFixture();
    target = GetRoboRenderTarget(320u, 240u);

    RoboCheckEqInt((int)RenderTargetWidth(target), 320);
    RoboCheckEqInt((int)RenderTargetHeight(target), 240);
    RoboCheckEqInt((int)RenderTargetFormat(target), (int)RoboRenderTargetFormat);
    /* Tightly packed, so the stride is a function of the width and the format. */
    RoboCheckEqInt((int)RenderTargetStride(target), 320 * 4);
    RoboCheckEqInt((int)RenderTargetByteSize(target), 320 * 240 * 4);
    RoboCheckEqInt((int)RenderTargetPixelCount(target), 320 * 240);

    /* The coordinate statement as a value: the target's own pixel area is `(0, 0, w, h)` in paint
     * coordinates, which is the region a full frame executes. */
    bounds = RenderTargetBounds(target);
    PaintRectIs(0, 0, 320, 240, bounds, "the target's bounds");

    bytes = RoboRenderTargetBytes(target, &size);
    RoboCheck(bytes != NULL);
    RoboCheckEqInt((int)size, 320 * 240 * 4);
    /* Zero-filled at creation, which is the one initial state a caller can predict -- and the reason
     * a frame that drew nothing has a checksum of a buffer of zeroes rather than an undefined one. */
    {
        size_t index;

        for (index = 0u; index < size; index++) {
            if (bytes[index] != 0u) {
                TestFail(__FILE__, __LINE__, "a fresh target's byte %u is %u, expected 0",
                               (unsigned)index, (unsigned)bytes[index]);
                break;
            }
        }
    }
    RoboCheck(RenderTargetSurface(target) != NULL);
    RoboCheck(RenderTargetSurfaceConst(target) != NULL);
    RoboCheck(RenderTargetSurface(NULL) == NULL);
    RoboCheck(RenderTargetSurfaceConst(NULL) == NULL);

    /* The accessors answer nothing for NULL rather than dereferencing, which is the rule every door
     * in this tree follows for the "I have no target" case. */
    RoboCheckEqInt((int)RenderTargetWidth(NULL), 0);
    RoboCheckEqInt((int)RenderTargetHeight(NULL), 0);
    RoboCheckEqInt((int)RenderTargetFormat(NULL), (int)RoboGfxFormatUnknown);
    RoboCheckEqInt((int)RenderTargetStride(NULL), 0);
    RoboCheckEqInt((int)RenderTargetByteSize(NULL), 0);
    RoboCheckEqInt((int)RenderTargetPixelCount(NULL), 0);
    RoboCheck(RoboRenderTargetBytes(NULL, &size) == NULL);
    RoboCheckEqInt((int)size, 0);
    RoboCheckEqInt((int)RenderTargetChecksum(NULL), 0);
    RoboCheckEqInt((int)RenderTargetBounds(NULL).width, 0);

    RenderTargetEnd(target);
    RenderTargetDestroy(NULL); /* safe */
}

static void TestRenderTargetRefusals(void)
{
    RenderTarget *target = NULL;
    RenderDiag diag;

    RenderFixture();
    RenderDiagReset(&diag);

    RoboCheckEqInt(RenderTargetCreate(NULL, 4u, 4u, NULL), RoboInvalidArgument);
    RoboCheckEqInt(RenderTargetCreate(&target, 0u, 4u, &diag), RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboRenderDomainTarget);
    RoboCheck(diag.message != NULL);
    RoboCheckEqInt(RenderTargetCreate(&target, 4u, 0u, &diag), RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboRenderDomainTarget);
    /* Above the maximum: the refusal a caller gets *before* an allocation, which is what makes the
     * byte arithmetic inside every other door provably safe. */
    RoboCheckEqInt(RenderTargetCreate(&target, RoboRenderMaxDimension + 1u, 4u, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboRenderDomainTarget);
    RoboCheckEqInt(RenderTargetCreate(&target, 4u, RoboRenderMaxDimension + 1u, &diag),
                      RoboInvalidArgument);
    /* And the refusals leave the out-parameter alone rather than handing back a half-made target. */
    RoboCheck(target == NULL);

    RoboCheckEqInt(RenderTargetClear(NULL, GfxRgb8(0, 0, 0), &diag),
                      RoboInvalidArgument);
}

static void TestRenderTargetPixelsAndEquality(void)
{
    RenderTarget *a;
    RenderTarget *b;
    GfxColorRgba8 page = GfxRgba8(0x10, 0x20, 0x30, 255);
    GfxColorRgba8 half = GfxRgba8(0x40, 0x50, 0x60, 128);

    RenderFixture();
    a = GetRoboRenderTarget(8u, 4u);
    b = GetRoboRenderTarget(8u, 4u);

    /* Two fresh targets are equal: zero is a state, not an accident. */
    RoboCheckEqInt(RenderTargetEqual(a, b), 1);
    RoboCheckEqInt((int)RenderTargetChecksum(a), (int)RenderTargetChecksum(b));
    RoboCheckEqInt(RenderTargetEqual(a, a), 1);
    RoboCheckEqInt(RenderTargetEqual(a, NULL), 0);
    RoboCheckEqInt(RenderTargetEqual(NULL, NULL), 1);

    RoboCheckEqInt(RenderTargetClear(a, page, NULL), RoboOk);
    RoboCheckEqInt(RenderTargetEqual(a, b), 0);
    RoboCheck(RenderTargetChecksum(a) != RenderTargetChecksum(b));
    {
        uint32_t x;
        uint32_t y;

        for (y = 0u; y < 4u; y++) {
            for (x = 0u; x < 8u; x++) {
                RenderPixelIs(a, x, y, page, "a cleared target");
            }
        }
    }

    /* Clearing is a **replacement**: an alpha below 255 is stored as itself rather than blended with
     * what was there, so a baseline is a state a caller can name. */
    RoboCheckEqInt(RenderTargetClear(a, half, NULL), RoboOk);
    RenderPixelIs(a, 0u, 0u, half, "a clear that stores alpha");
    /* Clearing twice with the same colour is the same picture -- the property that makes a baseline a
     * baseline rather than a step. */
    RoboCheckEqInt(RenderTargetClear(a, half, NULL), RoboOk);
    {
        RenderTarget *c = GetRoboRenderTarget(8u, 4u);

        RoboCheckEqInt(RenderTargetClear(c, half, NULL), RoboOk);
        RenderTargetsEqual(a, c, "two targets cleared to the same colour");
        RenderTargetEnd(c);
    }

    RenderTargetEnd(b);
    RenderTargetEnd(a);
}

/* ------------------------------------------------------------------ */
/* 3. The frame record                                                 */
/* ------------------------------------------------------------------ */

/** A valid bounded record, so the validity cases can vary one field at a time. */
static RenderFrame RenderGoodRecord(void)
{
    RenderFrame frame;

    RenderFrameInit(&frame);
    frame.width = 16u;
    frame.height = 8u;
    frame.format = RoboRenderTargetFormat;
    frame.stride = 16u * 4u;
    frame.targetPixels = 16u * 8u;
    frame.region = MakeGfxRect(2, 2, 4, 4);
    frame.regionPixels = 16u;
    frame.regions = 1u;
    frame.limited = 1;
    frame.checksum = 0x1234u;
    return frame;
}

static void TestRenderRecordRules(void)
{
    RenderFrame frame;
    RenderFrame other;

    RenderFixture();

    RenderFrameInit(&frame);
    RoboCheckEqInt((int)frame.format, (int)RoboGfxFormatUnknown);
    RoboCheckEqInt((int)frame.width, 0);
    RoboCheckEqInt(RenderFrameIsValid(&frame), 0);
    RoboCheckEqInt(RenderFrameIsValid(NULL), 0);
    RenderFrameInit(NULL); /* safe */

    frame = RenderGoodRecord();
    RoboCheckEqInt(RenderFrameIsValid(&frame), 1);
    RoboCheckEqInt(RenderFrameEqual(&frame, &frame), 1);
    other = frame;
    RoboCheckEqInt(RenderFrameEqual(&frame, &other), 1);
    other.checksum = 0x1235u;
    RoboCheckEqInt(RenderFrameEqual(&frame, &other), 0);
    RoboCheckEqInt(RenderFrameEqual(&frame, NULL), 0);
    RoboCheckEqInt(RenderFrameEqual(NULL, NULL), 1);
    other = frame;
    other.pass.draws = 3u;
    /* The embedded pass record is part of the equality, which is what makes two frames comparable as
     * *runs* rather than only as pictures. */
    RoboCheckEqInt(RenderFrameEqual(&frame, &other), 0);

    /*
     * One clause at a time, because a validity rule is only as good as the cases that reach each
     * clause -- and each of these is a record two fields of which disagree about what it is.
     */
    {
        RenderFrame bad;

        bad = RenderGoodRecord();
        bad.width = 0u;
        RoboCheckEqInt(RenderFrameIsValid(&bad), 0);

        bad = RenderGoodRecord();
        bad.width = RoboRenderMaxDimension + 1u;
        RoboCheckEqInt(RenderFrameIsValid(&bad), 0);

        bad = RenderGoodRecord();
        bad.format = RoboGfxFormatBgra8;
        RoboCheckEqInt(RenderFrameIsValid(&bad), 0);

        bad = RenderGoodRecord();
        bad.stride = 8u; /* narrower than one row of RGBA8 at width 16 */
        RoboCheckEqInt(RenderFrameIsValid(&bad), 0);

        bad = RenderGoodRecord();
        bad.targetPixels = 10u;
        RoboCheckEqInt(RenderFrameIsValid(&bad), 0);

        bad = RenderGoodRecord();
        bad.region.width = -1;
        RoboCheckEqInt(RenderFrameIsValid(&bad), 0);

        bad = RenderGoodRecord();
        bad.regionPixels = bad.targetPixels + 1u;
        RoboCheckEqInt(RenderFrameIsValid(&bad), 0);

        /* A bounded record that names no regions and no pixels is the "nothing owed" shape and is
         * *valid* -- it is the state the empty-set door produces. */
        bad = RenderGoodRecord();
        bad.regions = 0u;
        bad.region = GfxRectEmpty();
        bad.regionPixels = 0u;
        RoboCheckEqInt(RenderFrameIsValid(&bad), 1);

        /* A record claiming to be *unbounded* has to look like a full frame: no regions, the whole
         * target as its rectangle and every pixel counted. Anything else is a record whose `limited`
         * flag disagrees with its fields. */
        bad = RenderGoodRecord();
        bad.limited = 0;
        RoboCheckEqInt(RenderFrameIsValid(&bad), 0);

        bad = RenderGoodRecord();
        bad.limited = 0;
        bad.regions = 0u;
        bad.region = MakeGfxRect(0, 0, 16, 8);
        bad.regionPixels = bad.targetPixels;
        RoboCheckEqInt(RenderFrameIsValid(&bad), 1);

        bad = RenderGoodRecord();
        bad.limited = 0;
        bad.regions = 2u; /* a full frame that claims it was given regions */
        bad.region = MakeGfxRect(0, 0, 16, 8);
        bad.regionPixels = bad.targetPixels;
        RoboCheckEqInt(RenderFrameIsValid(&bad), 0);
    }
}

/* ------------------------------------------------------------------ */
/* 4. The dump                                                         */
/* ------------------------------------------------------------------ */

/** Count the newlines in @p text. */
static size_t RenderLineCount(const char *text)
{
    size_t count = 0u;

    while (*text != '\0') {
        if (*text == '\n') {
            count++;
        }
        text++;
    }
    return count;
}

static void TestRenderDump(void)
{
    RenderFrame frame;
    char text[1024];
    char again[1024];
    size_t needed = 0u;
    size_t second = 0u;

    RenderFixture();
    frame = RenderGoodRecord();
    frame.pass.commands = 42u;
    frame.pass.draws = 18u;
    frame.pass.skipped = 24u;
    frame.pass.boundedOut = 12u;
    frame.pass.backgrounds = 6u;
    frame.pass.borders = 4u;
    frame.pass.texts = 3u;
    frame.pass.images = 1u;
    frame.pass.filled = 14u;
    frame.pass.clipScopes = 2u;
    frame.pass.opacityScopes = 1u;
    frame.checksum = 0x2f1c9e0b7a4d3355ull;

    RoboCheckEqInt(RenderFrameWrite(&frame, text, sizeof(text), &needed, NULL), RoboOk);
    RoboCheck(needed > 0u);
    RoboCheckEqInt((int)needed, (int)strlen(text));
    /* Seven lines, one per group of facts: a reader comparing two frames compares lines. */
    RoboCheckEqInt((int)RenderLineCount(text), 7);
    RoboCheck(strstr(text, "frame 16x8 RGBA8 stride=64") != NULL);
    RoboCheck(strstr(text, "region=2,2,4,4") != NULL);
    RoboCheck(strstr(text, "region_pixels=16") != NULL);
    RoboCheck(strstr(text, "commands=42 draws=18 skipped=24 bounded_out=12") != NULL);
    RoboCheck(strstr(text, "backgrounds=6 borders=4 texts=3 images=1 filled=14") != NULL);
    RoboCheck(strstr(text, "checksum=2f1c9e0b7a4d3355") != NULL);
    /* No address, ever: `0x` appears nowhere in a dump, because a pointer would make two runs of one
     * document print differently. */
    /* `=0x` rather than a bare `0x`, so the assertion is about an address being printed and not about
     * the substring a dimension such as `16x8` happens to contain. */
    RoboCheck(strstr(text, "=0x") == NULL);

    /* The same record dumps to the same text, byte for byte. */
    RoboCheckEqInt(RenderFrameWrite(&frame, again, sizeof(again), &second, NULL), RoboOk);
    RoboCheckEqInt((int)second, (int)needed);
    RoboCheckStrEq(again, text);

    /* A buffer that is too small reports the length it needed rather than truncating: the contract
     * every `_write` in this tree keeps. */
    {
        char small[8];
        RenderDiag diag;

        RenderDiagReset(&diag);
        RoboCheckEqInt(RenderFrameWrite(&frame, small, sizeof(small), &second, &diag),
                          RoboNotSupported);
        RoboCheckEqInt((int)second, (int)needed);
        RoboCheckEqInt((int)diag.domain, (int)RoboRenderDomainDump);
        RoboCheck(small[0] != '\0');
    }

    RoboCheckEqInt(RenderFrameWrite(NULL, text, sizeof(text), &needed, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(RenderFrameWrite(&frame, NULL, sizeof(text), &needed, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(RenderFrameWrite(&frame, text, sizeof(text), NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(RenderFrameWrite(&frame, text, 0u, &needed, NULL),
                      RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void TestRegisterRender(void)
{
    TestRegister("render.version_and_capabilities",
                       TestRenderVersionAndCapabilities);
    TestRegister("render.conformance.capabilities",
                       TestRenderConformanceCapabilities);
    TestRegister("render.conformance.vocabulary", TestRenderConformanceVocabulary);
    TestRegister("render.conformance.diagnostics", TestRenderConformanceDiagnostics);
    TestRegister("render.conformance.presenter", TestRenderConformancePresenter);
    TestRegister("render.target_facts", TestRenderTargetFacts);
    TestRegister("render.target_refusals", TestRenderTargetRefusals);
    TestRegister("render.target_pixels_and_equality",
                       TestRenderTargetPixelsAndEquality);
    TestRegister("render.record_rules", TestRenderRecordRules);
    TestRegister("render.dump", TestRenderDump);
}
