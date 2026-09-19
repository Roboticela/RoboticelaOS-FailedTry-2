/*
 * RoboLayout module lifecycle, version and capabilities (Phase 10.1-10.2).
 *
 * The module owns *nothing*: every tree, context, bridge and record belongs to
 * the caller that created it. That is why init cannot fail for an allocation
 * reason, why shutdown releases no state, and why the whole file is a flag, a
 * version and a table of things this build does not do yet.
 */

#include "RoboLayout/RoboLayout.h"

#include "RoboLayout/RoboLayoutVersion.h"

#include <string.h>

static int g_layout_initialized;

void robo_layout_capabilities_init(robo_layout_capabilities_t *capabilities)
{
    if (!capabilities) {
        return;
    }
    memset(capabilities, 0, sizeof(*capabilities));
}

robo_result_t robo_layout_get_capabilities(robo_layout_capabilities_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    memset(out, 0, sizeof(*out));

    /* 10.1, and every one of these is asserted by
     * `layout.conformance.capabilities` -- by name, not by trusting this
     * file. A flag is a promise about the build, and an untested promise is one
     * that stops being true without anybody noticing. */
    out->object_model = 1;
    out->constraints = 1;
    out->result_model = 1;
    out->context = 1;
    out->dom_build = 1;
    out->computed_style_link = 1;

    /* 10.2: the pass classifies every box into a formatting context, and that
     * classification is a finished feature rather than a flag waiting to be
     * flipped -- which is why it is 1 here while `geometry` was still 0. */
    out->formatting_contexts = 1;

    /*
     * 10.3, and the two flags that have to be read together.
     *
     * `geometry` becomes 1 because this build really does compute coordinates:
     * `robo_layout_block_layout` writes positions and sizes, and a caller that ran
     * a pass with a viewport has boxes whose results are COMPUTED. It is a
     * statement about the *build*, not about any particular box -- a box the block
     * pass did not place is still UNRESOLVED, and its result state is the per-box
     * authority that says so.
     *
     * `block_layout` is 1 and `block_and_inline_layout` is still 0, which is the
     * distinction worth keeping: the block direction is laid out and the inline
     * direction is not, so a caller that asked the wider question has to be told no.
     * `inline_layout` is 0 for the same reason and is spelled out rather than left
     * to be inferred from the absence of the wider flag.
     *
     * `painting` stays 0: nothing here draws.
     */
    out->geometry = ROBO_LAYOUT_GEOMETRY_IMPLEMENTED;
    out->block_layout = ROBO_LAYOUT_BLOCK_LAYOUT_IMPLEMENTED;
    out->inline_layout = 0;
    out->block_and_inline_layout = 0;
    out->painting = 0;
    return ROBO_OK;
}

const char *robo_layout_version_string(void)
{
    return ROBO_LAYOUT_VERSION_STRING;
}

robo_result_t robo_layout_get_version(robo_i32 *out_major,
                                      robo_i32 *out_minor,
                                      robo_i32 *out_patch)
{
    if (!out_major || !out_minor || !out_patch) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_major = ROBO_LAYOUT_VERSION_MAJOR;
    *out_minor = ROBO_LAYOUT_VERSION_MINOR;
    *out_patch = ROBO_LAYOUT_VERSION_PATCH;
    return ROBO_OK;
}

int robo_layout_geometry_is_implemented(void)
{
    /*
     * One macro, used by both doors, so the capability field and the function
     * cannot disagree -- which is exactly the drift a caller would have no way to
     * see.
     */
    return ROBO_LAYOUT_GEOMETRY_IMPLEMENTED;
}

robo_result_t robo_layout_init(void)
{
    if (g_layout_initialized) {
        return ROBO_ALREADY_EXISTS;
    }
    g_layout_initialized = 1;
    return ROBO_OK;
}

void robo_layout_shutdown(void)
{
    /* Nothing to release: the module owns no state. Stated rather than left as an
     * empty function body, because "there is nothing here" is the fact a reader
     * needs. */
    g_layout_initialized = 0;
}

int robo_layout_is_initialized(void)
{
    return g_layout_initialized;
}
