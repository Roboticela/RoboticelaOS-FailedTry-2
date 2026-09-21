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
 *                                 `set` with `ROBO_OK` clearing, `record` never failing a caller who
 *                                 passed no record);
 *
 *   **the target**                 the facts a frame is described in, the bounds rectangle that is
 *                                 the coordinate statement as a value, the two refusals (no pixels,
 *                                 above the maximum), the byte doors, and the lifetime rule that a
 *                                 destroyed target is gone and a NULL one is harmless;
 *
 *   **the record's own rules**     `robo_render_frame_is_valid` is *closed*: a record whose fields
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

static void test_render_version_and_capabilities(void)
{
    robo_render_capabilities_t capabilities;
    robo_i32 major = -1;
    robo_i32 minor = -1;
    robo_i32 patch = -1;

    render_fixture();

    ROBO_CHECK_STR_EQ(robo_render_version_string(), ROBO_RENDER_VERSION_STRING);
    ROBO_CHECK_EQ_INT(robo_render_get_version(&major, &minor, &patch), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)major, 0);
    /* 11.12 is the release whose subject is pixels rather than descriptions, so its first version is
     * 0.1.0 -- the module did not exist in a form that could be wrong before it. 11.13 is the second
     * release and adds the presentation door, which is why the minor moved and the patch did not. */
    ROBO_CHECK_EQ_INT((int)minor, 2);
    ROBO_CHECK_EQ_INT((int)patch, 0);
    ROBO_CHECK_EQ_INT(ROBO_RENDER_API_FROZEN, 0);
    ROBO_CHECK_EQ_INT(robo_render_get_version(NULL, &minor, &patch), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_render_get_version(&major, NULL, &patch), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_render_get_version(&major, &minor, NULL), ROBO_INVALID_ARGUMENT);

    /* The flag and the doors, asserted together: a constant, the function that reports it and the
     * capability fields that are read from it. */
    ROBO_CHECK_EQ_INT(ROBO_RENDER_RASTERIZATION_IMPLEMENTED, 1);
    ROBO_CHECK_EQ_INT(robo_render_rasterization_is_implemented(), 1);
    /* And the flag 11.13 turned, asserted the same three ways: the constant, the function, and the
     * capability field, which are all read from each other rather than written three times. */
    ROBO_CHECK_EQ_INT(ROBO_RENDER_PRESENTATION_IMPLEMENTED, 1);
    ROBO_CHECK_EQ_INT(robo_render_presentation_is_implemented(), 1);
    ROBO_CHECK_EQ_INT(robo_render_get_capabilities(&capabilities), ROBO_OK);
    ROBO_CHECK_EQ_INT(capabilities.rasterizer, robo_render_rasterization_is_implemented());
    ROBO_CHECK_EQ_INT(capabilities.presentation, robo_render_presentation_is_implemented());
    ROBO_CHECK_EQ_INT(capabilities.pixel_targets, 1);
    ROBO_CHECK_EQ_INT(robo_render_get_capabilities(NULL), ROBO_INVALID_ARGUMENT);

    /* The pair: `RoboPaint`'s `raster` and this module's claim describe one fact, so a build where
     * one turned and the other did not is a build with a table that lies. */
    {
        robo_paint_capabilities_t paint;

        paint_fixture();
        ROBO_CHECK_EQ_INT(robo_paint_get_capabilities(&paint), ROBO_OK);
        ROBO_CHECK_EQ_INT(paint.raster, 1);
        ROBO_CHECK_EQ_INT(capabilities.rasterizer, 1);
    }

    /* `capabilities_init` writes the "nothing" state rather than a build's list, so a caller can tell
     * "I did not ask" from "I asked and this build does nothing". */
    robo_render_capabilities_init(&capabilities);
    {
        int cleared[sizeof(robo_render_capabilities_t) / sizeof(int)];
        size_t index;

        memset(cleared, 0, sizeof(cleared));
        memcpy(cleared, &capabilities, sizeof(capabilities));
        for (index = 0u; index < sizeof(cleared) / sizeof(cleared[0]); index++) {
            ROBO_CHECK_EQ_INT(cleared[index], 0);
        }
    }
    robo_render_capabilities_init(NULL); /* safe */
}

/** One capability field: its name, and what this build must report for it. */
typedef struct render_capability_row {
    const char *name;
    int value;
} render_capability_row_t;

/*
 * Declaration order, and the order is the assertion: the walk below pairs each name with the field at
 * the same offset, so a field inserted anywhere but the end shows up as every row after it
 * disagreeing -- which is the loudest possible failure and the right one.
 */
static const render_capability_row_t render_capability_rows[] = {
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

static void test_render_conformance_capabilities(void)
{
    robo_render_capabilities_t capabilities;
    int values[sizeof(robo_render_capabilities_t) / sizeof(int)];
    size_t index;

    render_fixture();
    /* The walk is positional, so the counts are what make it sound: the struct has exactly one `int`
     * per row and no padding, or a row would be asserting the neighbouring field. */
    ROBO_CHECK_EQ_INT((int)(sizeof(robo_render_capabilities_t) / sizeof(int)),
                      (int)(sizeof(render_capability_rows) / sizeof(render_capability_rows[0])));
    ROBO_CHECK_EQ_INT((int)sizeof(robo_render_capabilities_t),
                      (int)(sizeof(render_capability_rows) / sizeof(render_capability_rows[0])) *
                          (int)sizeof(int));
    ROBO_CHECK_EQ_INT(robo_render_get_capabilities(&capabilities), ROBO_OK);

    memset(values, 0, sizeof(values));
    memcpy(values, &capabilities, sizeof(capabilities));
    for (index = 0u; index < sizeof(render_capability_rows) / sizeof(render_capability_rows[0]);
         index++) {
        if (values[index] != render_capability_rows[index].value) {
            robo_test_fail(__FILE__, __LINE__, "capability %s is %d, expected %d",
                           render_capability_rows[index].name, values[index],
                           render_capability_rows[index].value);
        }
        /* Every field is a flag, so a value that is neither 0 nor 1 cannot be read as one. */
        ROBO_CHECK(values[index] == 0 || values[index] == 1);
    }
}

static void test_render_conformance_vocabulary(void)
{
    size_t index;

    render_fixture();

    /* The target format is one, stated, and the constant beside it is the format layer's own name for
     * it -- so a reader of a frame's stride and a reader of a pixel format's metadata are reading the
     * same format. */
    ROBO_CHECK_EQ_INT((int)ROBO_RENDER_TARGET_FORMAT, (int)ROBO_GFX_FORMAT_RGBA8);
    {
        robo_gfx_pixel_format_info_t info;
        uint8_t bpp = 0u;

        memset(&info, 0, sizeof(info));
        ROBO_CHECK_EQ_INT(robo_gfx_pixel_format_info(ROBO_RENDER_TARGET_FORMAT, &info), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)info.is_implemented, 1);
        ROBO_CHECK_EQ_INT((int)info.has_alpha, 1);
        ROBO_CHECK_EQ_INT(robo_gfx_pixel_format_bytes_per_pixel(ROBO_RENDER_TARGET_FORMAT, &bpp),
                          ROBO_OK);
        /* Four bytes: the stride arithmetic and the record's own validity rule are built on this
         * number, so it is asserted rather than assumed. */
        ROBO_CHECK_EQ_INT((int)bpp, 4);
    }

    /* The diagnostic domains: all five known, named, and the unknown case answering a *string*
     * rather than NULL -- a dump that printed `(null)` for an undefined domain would be a dump a
     * reader could not tell from a missing field. */
    ROBO_CHECK_EQ_INT((int)ROBO_RENDER_DOMAIN_COUNT, 5);
    for (index = 0u; index < (size_t)ROBO_RENDER_DOMAIN_COUNT; index++) {
        robo_render_diagnostic_domain_t domain = (robo_render_diagnostic_domain_t)index;
        const char *name = robo_render_domain_name(domain);

        ROBO_CHECK(name != NULL);
        ROBO_CHECK(name[0] != '\0');
        ROBO_CHECK_STR_EQ(name, robo_render_domain_name(domain));
        ROBO_CHECK_EQ_INT(robo_render_domain_is_known(domain), 1);
    }
    ROBO_CHECK_STR_EQ(robo_render_domain_name(ROBO_RENDER_DOMAIN_NONE), "none");
    ROBO_CHECK_STR_EQ(robo_render_domain_name(ROBO_RENDER_DOMAIN_TARGET), "target");
    ROBO_CHECK_STR_EQ(robo_render_domain_name(ROBO_RENDER_DOMAIN_LIST), "list");
    ROBO_CHECK_STR_EQ(robo_render_domain_name(ROBO_RENDER_DOMAIN_FRAME), "frame");
    ROBO_CHECK_STR_EQ(robo_render_domain_name(ROBO_RENDER_DOMAIN_DUMP), "dump");
    ROBO_CHECK_EQ_INT(robo_render_domain_is_known((robo_render_diagnostic_domain_t)99), 0);
    ROBO_CHECK_STR_EQ(robo_render_domain_name((robo_render_diagnostic_domain_t)99), "unknown");
    ROBO_CHECK_STR_EQ(robo_render_domain_name((robo_render_diagnostic_domain_t)-1), "unknown");
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
static void test_render_conformance_presenter(void)
{
    robo_render_capabilities_t capabilities;
    robo_render_present_record_t record;

    render_fixture();

    ROBO_CHECK_EQ_INT(ROBO_RENDER_PRESENTATION_IMPLEMENTED, 1);
    ROBO_CHECK_EQ_INT(robo_render_presentation_is_implemented(), 1);
    ROBO_CHECK_EQ_INT(robo_render_get_capabilities(&capabilities), ROBO_OK);
    ROBO_CHECK_EQ_INT(capabilities.presentation, 1);
    /* A frame into one destination is not a compositor. */
    ROBO_CHECK_EQ_INT(capabilities.compositor, 0);

    /* The record's "nothing" state: no frame, no destination, nothing presented. A cleared record is
     * *invalid* rather than valid-and-empty -- it describes no frame -- which is the same reading
     * `robo_render_frame_is_valid` gives a zeroed frame. */
    robo_render_present_record_init(&record);
    ROBO_CHECK_EQ_INT((int)record.frame_width, 0);
    ROBO_CHECK_EQ_INT((int)record.dest_format, (int)ROBO_GFX_FORMAT_UNKNOWN);
    ROBO_CHECK_EQ_INT(record.complete, 0);
    ROBO_CHECK_EQ_INT(record.presented_pixels, 0u);
    ROBO_CHECK_EQ_INT(record.clipped_pixels, 0u);
    ROBO_CHECK_EQ_INT(robo_render_present_record_is_valid(&record), 0);
    ROBO_CHECK_EQ_INT(robo_render_present_record_is_valid(NULL), 0);
    robo_render_present_record_init(NULL); /* safe */

    /* The frame domain is the one a presentation failure names, and it is the same domain a run's own
     * failure uses -- there is no fourth "present" row to look for. */
    ROBO_CHECK_STR_EQ(robo_render_domain_name(ROBO_RENDER_DOMAIN_FRAME), "frame");
    ROBO_CHECK_EQ_INT((int)ROBO_RENDER_DOMAIN_COUNT, 5);
}

static void test_render_conformance_diagnostics(void)
{
    robo_render_diag_t diag;

    render_fixture();

    robo_render_diag_reset(&diag);
    ROBO_CHECK_EQ_INT((int)diag.code, (int)ROBO_OK);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_RENDER_DOMAIN_NONE);
    ROBO_CHECK(diag.message == NULL);
    robo_render_diag_reset(NULL); /* safe */

    /* `set` fills a record for a failure and refuses a NULL record. */
    ROBO_CHECK_EQ_INT(robo_render_diag_set(&diag, ROBO_ERROR, ROBO_RENDER_DOMAIN_FRAME, "boom",
                                           "file.c", 7),
                      ROBO_ERROR);
    ROBO_CHECK_EQ_INT((int)diag.code, (int)ROBO_ERROR);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_RENDER_DOMAIN_FRAME);
    ROBO_CHECK_STR_EQ(diag.message, "boom");
    ROBO_CHECK_STR_EQ(diag.file, "file.c");
    ROBO_CHECK_EQ_INT(diag.line, 7);
    ROBO_CHECK_EQ_INT(robo_render_diag_set(NULL, ROBO_ERROR, ROBO_RENDER_DOMAIN_FRAME, "boom",
                                           "file.c", 7),
                      ROBO_INVALID_ARGUMENT);

    /* `set` with ROBO_OK clears, which is how the same record is reused across a loop. */
    ROBO_CHECK_EQ_INT(robo_render_diag_set(&diag, ROBO_OK, ROBO_RENDER_DOMAIN_FRAME, "boom",
                                           "file.c", 7),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)diag.code, (int)ROBO_OK);
    ROBO_CHECK(diag.message == NULL);

    /* `record` never changes a caller's result code, with or without a record. */
    ROBO_CHECK_EQ_INT(ROBO_RENDER_DIAG(&diag, ROBO_RENDER_DOMAIN_LIST, ROBO_NOT_SUPPORTED, "x"),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT((int)diag.code, (int)ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_RENDER_DOMAIN_LIST);
    ROBO_CHECK_EQ_INT(ROBO_RENDER_DIAG(NULL, ROBO_RENDER_DOMAIN_LIST, ROBO_NOT_SUPPORTED, "x"),
                      ROBO_NOT_SUPPORTED);
    /* And a success recorded through it leaves the record alone rather than clearing it: `record` is
     * the non-strict door, and the result code is authoritative. */
    ROBO_CHECK_EQ_INT(ROBO_RENDER_DIAG(&diag, ROBO_RENDER_DOMAIN_LIST, ROBO_OK, "x"), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)diag.code, (int)ROBO_NOT_SUPPORTED);
}

/* ------------------------------------------------------------------ */
/* 2. The target                                                       */
/* ------------------------------------------------------------------ */

static void test_render_target_facts(void)
{
    robo_render_target_t *target;
    robo_layout_rect_t bounds;
    size_t size = 0u;
    const uint8_t *bytes;

    render_fixture();
    target = render_target(320u, 240u);

    ROBO_CHECK_EQ_INT((int)robo_render_target_width(target), 320);
    ROBO_CHECK_EQ_INT((int)robo_render_target_height(target), 240);
    ROBO_CHECK_EQ_INT((int)robo_render_target_format(target), (int)ROBO_RENDER_TARGET_FORMAT);
    /* Tightly packed, so the stride is a function of the width and the format. */
    ROBO_CHECK_EQ_INT((int)robo_render_target_stride(target), 320 * 4);
    ROBO_CHECK_EQ_INT((int)robo_render_target_byte_size(target), 320 * 240 * 4);
    ROBO_CHECK_EQ_INT((int)robo_render_target_pixel_count(target), 320 * 240);

    /* The coordinate statement as a value: the target's own pixel area is `(0, 0, w, h)` in paint
     * coordinates, which is the region a full frame executes. */
    bounds = robo_render_target_bounds(target);
    paint_rect_is(0, 0, 320, 240, bounds, "the target's bounds");

    bytes = robo_render_target_bytes(target, &size);
    ROBO_CHECK(bytes != NULL);
    ROBO_CHECK_EQ_INT((int)size, 320 * 240 * 4);
    /* Zero-filled at creation, which is the one initial state a caller can predict -- and the reason
     * a frame that drew nothing has a checksum of a buffer of zeroes rather than an undefined one. */
    {
        size_t index;

        for (index = 0u; index < size; index++) {
            if (bytes[index] != 0u) {
                robo_test_fail(__FILE__, __LINE__, "a fresh target's byte %u is %u, expected 0",
                               (unsigned)index, (unsigned)bytes[index]);
                break;
            }
        }
    }
    ROBO_CHECK(robo_render_target_surface(target) != NULL);
    ROBO_CHECK(robo_render_target_surface_const(target) != NULL);
    ROBO_CHECK(robo_render_target_surface(NULL) == NULL);
    ROBO_CHECK(robo_render_target_surface_const(NULL) == NULL);

    /* The accessors answer nothing for NULL rather than dereferencing, which is the rule every door
     * in this tree follows for the "I have no target" case. */
    ROBO_CHECK_EQ_INT((int)robo_render_target_width(NULL), 0);
    ROBO_CHECK_EQ_INT((int)robo_render_target_height(NULL), 0);
    ROBO_CHECK_EQ_INT((int)robo_render_target_format(NULL), (int)ROBO_GFX_FORMAT_UNKNOWN);
    ROBO_CHECK_EQ_INT((int)robo_render_target_stride(NULL), 0);
    ROBO_CHECK_EQ_INT((int)robo_render_target_byte_size(NULL), 0);
    ROBO_CHECK_EQ_INT((int)robo_render_target_pixel_count(NULL), 0);
    ROBO_CHECK(robo_render_target_bytes(NULL, &size) == NULL);
    ROBO_CHECK_EQ_INT((int)size, 0);
    ROBO_CHECK_EQ_INT((int)robo_render_target_checksum(NULL), 0);
    ROBO_CHECK_EQ_INT((int)robo_render_target_bounds(NULL).width, 0);

    render_target_end(target);
    robo_render_target_destroy(NULL); /* safe */
}

static void test_render_target_refusals(void)
{
    robo_render_target_t *target = NULL;
    robo_render_diag_t diag;

    render_fixture();
    robo_render_diag_reset(&diag);

    ROBO_CHECK_EQ_INT(robo_render_target_create(NULL, 4u, 4u, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_render_target_create(&target, 0u, 4u, &diag), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_RENDER_DOMAIN_TARGET);
    ROBO_CHECK(diag.message != NULL);
    ROBO_CHECK_EQ_INT(robo_render_target_create(&target, 4u, 0u, &diag), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_RENDER_DOMAIN_TARGET);
    /* Above the maximum: the refusal a caller gets *before* an allocation, which is what makes the
     * byte arithmetic inside every other door provably safe. */
    ROBO_CHECK_EQ_INT(robo_render_target_create(&target, ROBO_RENDER_MAX_DIMENSION + 1u, 4u, &diag),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_RENDER_DOMAIN_TARGET);
    ROBO_CHECK_EQ_INT(robo_render_target_create(&target, 4u, ROBO_RENDER_MAX_DIMENSION + 1u, &diag),
                      ROBO_INVALID_ARGUMENT);
    /* And the refusals leave the out-parameter alone rather than handing back a half-made target. */
    ROBO_CHECK(target == NULL);

    ROBO_CHECK_EQ_INT(robo_render_target_clear(NULL, robo_gfx_rgb8(0, 0, 0), &diag),
                      ROBO_INVALID_ARGUMENT);
}

static void test_render_target_pixels_and_equality(void)
{
    robo_render_target_t *a;
    robo_render_target_t *b;
    robo_gfx_color_rgba8_t page = robo_gfx_rgba8(0x10, 0x20, 0x30, 255);
    robo_gfx_color_rgba8_t half = robo_gfx_rgba8(0x40, 0x50, 0x60, 128);

    render_fixture();
    a = render_target(8u, 4u);
    b = render_target(8u, 4u);

    /* Two fresh targets are equal: zero is a state, not an accident. */
    ROBO_CHECK_EQ_INT(robo_render_target_equal(a, b), 1);
    ROBO_CHECK_EQ_INT((int)robo_render_target_checksum(a), (int)robo_render_target_checksum(b));
    ROBO_CHECK_EQ_INT(robo_render_target_equal(a, a), 1);
    ROBO_CHECK_EQ_INT(robo_render_target_equal(a, NULL), 0);
    ROBO_CHECK_EQ_INT(robo_render_target_equal(NULL, NULL), 1);

    ROBO_CHECK_EQ_INT(robo_render_target_clear(a, page, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_render_target_equal(a, b), 0);
    ROBO_CHECK(robo_render_target_checksum(a) != robo_render_target_checksum(b));
    {
        uint32_t x;
        uint32_t y;

        for (y = 0u; y < 4u; y++) {
            for (x = 0u; x < 8u; x++) {
                render_pixel_is(a, x, y, page, "a cleared target");
            }
        }
    }

    /* Clearing is a **replacement**: an alpha below 255 is stored as itself rather than blended with
     * what was there, so a baseline is a state a caller can name. */
    ROBO_CHECK_EQ_INT(robo_render_target_clear(a, half, NULL), ROBO_OK);
    render_pixel_is(a, 0u, 0u, half, "a clear that stores alpha");
    /* Clearing twice with the same colour is the same picture -- the property that makes a baseline a
     * baseline rather than a step. */
    ROBO_CHECK_EQ_INT(robo_render_target_clear(a, half, NULL), ROBO_OK);
    {
        robo_render_target_t *c = render_target(8u, 4u);

        ROBO_CHECK_EQ_INT(robo_render_target_clear(c, half, NULL), ROBO_OK);
        render_targets_equal(a, c, "two targets cleared to the same colour");
        render_target_end(c);
    }

    render_target_end(b);
    render_target_end(a);
}

/* ------------------------------------------------------------------ */
/* 3. The frame record                                                 */
/* ------------------------------------------------------------------ */

/** A valid bounded record, so the validity cases can vary one field at a time. */
static robo_render_frame_t render_good_record(void)
{
    robo_render_frame_t frame;

    robo_render_frame_init(&frame);
    frame.width = 16u;
    frame.height = 8u;
    frame.format = ROBO_RENDER_TARGET_FORMAT;
    frame.stride = 16u * 4u;
    frame.target_pixels = 16u * 8u;
    frame.region = robo_gfx_rect(2, 2, 4, 4);
    frame.region_pixels = 16u;
    frame.regions = 1u;
    frame.limited = 1;
    frame.checksum = 0x1234u;
    return frame;
}

static void test_render_record_rules(void)
{
    robo_render_frame_t frame;
    robo_render_frame_t other;

    render_fixture();

    robo_render_frame_init(&frame);
    ROBO_CHECK_EQ_INT((int)frame.format, (int)ROBO_GFX_FORMAT_UNKNOWN);
    ROBO_CHECK_EQ_INT((int)frame.width, 0);
    ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(&frame), 0);
    ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(NULL), 0);
    robo_render_frame_init(NULL); /* safe */

    frame = render_good_record();
    ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(&frame), 1);
    ROBO_CHECK_EQ_INT(robo_render_frame_equal(&frame, &frame), 1);
    other = frame;
    ROBO_CHECK_EQ_INT(robo_render_frame_equal(&frame, &other), 1);
    other.checksum = 0x1235u;
    ROBO_CHECK_EQ_INT(robo_render_frame_equal(&frame, &other), 0);
    ROBO_CHECK_EQ_INT(robo_render_frame_equal(&frame, NULL), 0);
    ROBO_CHECK_EQ_INT(robo_render_frame_equal(NULL, NULL), 1);
    other = frame;
    other.pass.draws = 3u;
    /* The embedded pass record is part of the equality, which is what makes two frames comparable as
     * *runs* rather than only as pictures. */
    ROBO_CHECK_EQ_INT(robo_render_frame_equal(&frame, &other), 0);

    /*
     * One clause at a time, because a validity rule is only as good as the cases that reach each
     * clause -- and each of these is a record two fields of which disagree about what it is.
     */
    {
        robo_render_frame_t bad;

        bad = render_good_record();
        bad.width = 0u;
        ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(&bad), 0);

        bad = render_good_record();
        bad.width = ROBO_RENDER_MAX_DIMENSION + 1u;
        ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(&bad), 0);

        bad = render_good_record();
        bad.format = ROBO_GFX_FORMAT_BGRA8;
        ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(&bad), 0);

        bad = render_good_record();
        bad.stride = 8u; /* narrower than one row of RGBA8 at width 16 */
        ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(&bad), 0);

        bad = render_good_record();
        bad.target_pixels = 10u;
        ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(&bad), 0);

        bad = render_good_record();
        bad.region.width = -1;
        ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(&bad), 0);

        bad = render_good_record();
        bad.region_pixels = bad.target_pixels + 1u;
        ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(&bad), 0);

        /* A bounded record that names no regions and no pixels is the "nothing owed" shape and is
         * *valid* -- it is the state the empty-set door produces. */
        bad = render_good_record();
        bad.regions = 0u;
        bad.region = robo_gfx_rect_empty();
        bad.region_pixels = 0u;
        ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(&bad), 1);

        /* A record claiming to be *unbounded* has to look like a full frame: no regions, the whole
         * target as its rectangle and every pixel counted. Anything else is a record whose `limited`
         * flag disagrees with its fields. */
        bad = render_good_record();
        bad.limited = 0;
        ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(&bad), 0);

        bad = render_good_record();
        bad.limited = 0;
        bad.regions = 0u;
        bad.region = robo_gfx_rect(0, 0, 16, 8);
        bad.region_pixels = bad.target_pixels;
        ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(&bad), 1);

        bad = render_good_record();
        bad.limited = 0;
        bad.regions = 2u; /* a full frame that claims it was given regions */
        bad.region = robo_gfx_rect(0, 0, 16, 8);
        bad.region_pixels = bad.target_pixels;
        ROBO_CHECK_EQ_INT(robo_render_frame_is_valid(&bad), 0);
    }
}

/* ------------------------------------------------------------------ */
/* 4. The dump                                                         */
/* ------------------------------------------------------------------ */

/** Count the newlines in @p text. */
static size_t render_line_count(const char *text)
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

static void test_render_dump(void)
{
    robo_render_frame_t frame;
    char text[1024];
    char again[1024];
    size_t needed = 0u;
    size_t second = 0u;

    render_fixture();
    frame = render_good_record();
    frame.pass.commands = 42u;
    frame.pass.draws = 18u;
    frame.pass.skipped = 24u;
    frame.pass.bounded_out = 12u;
    frame.pass.backgrounds = 6u;
    frame.pass.borders = 4u;
    frame.pass.texts = 3u;
    frame.pass.images = 1u;
    frame.pass.filled = 14u;
    frame.pass.clip_scopes = 2u;
    frame.pass.opacity_scopes = 1u;
    frame.checksum = 0x2f1c9e0b7a4d3355ull;

    ROBO_CHECK_EQ_INT(robo_render_frame_write(&frame, text, sizeof(text), &needed, NULL), ROBO_OK);
    ROBO_CHECK(needed > 0u);
    ROBO_CHECK_EQ_INT((int)needed, (int)strlen(text));
    /* Seven lines, one per group of facts: a reader comparing two frames compares lines. */
    ROBO_CHECK_EQ_INT((int)render_line_count(text), 7);
    ROBO_CHECK(strstr(text, "frame 16x8 RGBA8 stride=64") != NULL);
    ROBO_CHECK(strstr(text, "region=2,2,4,4") != NULL);
    ROBO_CHECK(strstr(text, "region_pixels=16") != NULL);
    ROBO_CHECK(strstr(text, "commands=42 draws=18 skipped=24 bounded_out=12") != NULL);
    ROBO_CHECK(strstr(text, "backgrounds=6 borders=4 texts=3 images=1 filled=14") != NULL);
    ROBO_CHECK(strstr(text, "checksum=2f1c9e0b7a4d3355") != NULL);
    /* No address, ever: `0x` appears nowhere in a dump, because a pointer would make two runs of one
     * document print differently. */
    /* `=0x` rather than a bare `0x`, so the assertion is about an address being printed and not about
     * the substring a dimension such as `16x8` happens to contain. */
    ROBO_CHECK(strstr(text, "=0x") == NULL);

    /* The same record dumps to the same text, byte for byte. */
    ROBO_CHECK_EQ_INT(robo_render_frame_write(&frame, again, sizeof(again), &second, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)second, (int)needed);
    ROBO_CHECK_STR_EQ(again, text);

    /* A buffer that is too small reports the length it needed rather than truncating: the contract
     * every `_write` in this tree keeps. */
    {
        char small[8];
        robo_render_diag_t diag;

        robo_render_diag_reset(&diag);
        ROBO_CHECK_EQ_INT(robo_render_frame_write(&frame, small, sizeof(small), &second, &diag),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT((int)second, (int)needed);
        ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_RENDER_DOMAIN_DUMP);
        ROBO_CHECK(small[0] != '\0');
    }

    ROBO_CHECK_EQ_INT(robo_render_frame_write(NULL, text, sizeof(text), &needed, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_render_frame_write(&frame, NULL, sizeof(text), &needed, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_render_frame_write(&frame, text, sizeof(text), NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_render_frame_write(&frame, text, 0u, &needed, NULL),
                      ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_render(void)
{
    robo_test_register("render.version_and_capabilities",
                       test_render_version_and_capabilities);
    robo_test_register("render.conformance.capabilities",
                       test_render_conformance_capabilities);
    robo_test_register("render.conformance.vocabulary", test_render_conformance_vocabulary);
    robo_test_register("render.conformance.diagnostics", test_render_conformance_diagnostics);
    robo_test_register("render.conformance.presenter", test_render_conformance_presenter);
    robo_test_register("render.target_facts", test_render_target_facts);
    robo_test_register("render.target_refusals", test_render_target_refusals);
    robo_test_register("render.target_pixels_and_equality",
                       test_render_target_pixels_and_equality);
    robo_test_register("render.record_rules", test_render_record_rules);
    robo_test_register("render.dump", test_render_dump);
}
