/*
 * RoboCSS module lifecycle, slot table and version (Phase 9.1).
 *
 * The module owns exactly one thing: the stylesheet slot table. Everything else
 * in the subsystem — property tables, style contexts, parsers, values,
 * declarations, selectors, rules — belongs to the caller that created it, which
 * is why shutdown has nothing else to release and why "no hidden global state"
 * survives an architecture this wide.
 *
 * The table is a fixed array rather than a heap allocation: it is bounded by
 * ROBO_CSS_MAX_STYLESHEETS, and a bounded array is what lets
 * robo_css_init be a state transition with nothing that can fail.
 */

#include "RoboCSS/RoboCSS.h"

#include "RoboCSSInternal.h"
#include "RoboBase/RoboBase.h"
#include "RoboBase/RoboLog.h"
#include "RoboMem/RoboMem.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <string.h>

static int g_initialized;
static robo_css_stylesheet_slot_t g_slots[ROBO_CSS_MAX_STYLESHEETS];
static robo_css_stylesheet_id_t g_next_stylesheet_id = 1u;

const char *robo_css_version_string(void)
{
    return ROBO_CSS_VERSION_STRING;
}

robo_result_t robo_css_get_version(robo_i32 *out_major,
                                   robo_i32 *out_minor,
                                   robo_i32 *out_patch)
{
    if (!out_major || !out_minor || !out_patch) {
        return ROBO_INVALID_ARGUMENT;
    }
    *out_major = ROBO_CSS_VERSION_MAJOR;
    *out_minor = ROBO_CSS_VERSION_MINOR;
    *out_patch = ROBO_CSS_VERSION_PATCH;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Slot table                                                          */
/* ------------------------------------------------------------------ */

robo_css_stylesheet_slot_t *robo_css_slot_find(robo_css_stylesheet_id_t id)
{
    size_t i;

    if (id == ROBO_CSS_STYLESHEET_ID_NONE) {
        return NULL;
    }
    for (i = 0u; i < (size_t)ROBO_CSS_MAX_STYLESHEETS; i++) {
        if (g_slots[i].in_use && g_slots[i].id == id) {
            return &g_slots[i];
        }
    }
    return NULL;
}

robo_css_stylesheet_slot_t *robo_css_slot_alloc(void)
{
    size_t i;

    for (i = 0u; i < (size_t)ROBO_CSS_MAX_STYLESHEETS; i++) {
        if (!g_slots[i].in_use) {
            return &g_slots[i];
        }
    }
    return NULL;
}

robo_css_stylesheet_id_t robo_css_slot_claim(robo_css_stylesheet_slot_t *slot,
                                             robo_css_origin_t origin)
{
    robo_css_stylesheet_id_t id;

    if (!slot) {
        return ROBO_CSS_STYLESHEET_ID_NONE;
    }
    /* Zero the slot first: a recycled slot must not inherit the previous
     * stylesheet's rule list, source or error. */
    memset(slot, 0, sizeof(*slot));
    id = g_next_stylesheet_id++;
    slot->in_use = 1;
    slot->id = id;
    slot->origin = origin;
    slot->state = ROBO_CSS_STYLESHEET_STATE_CREATED;
    slot->name[0] = '\0';
    robo_css_source_init(&slot->source);
    robo_css_rule_list_init(&slot->rules);
    slot->has_error = 0;
    robo_css_error_init(&slot->error);
    return id;
}

void robo_css_slot_release(robo_css_stylesheet_slot_t *slot)
{
    if (!slot) {
        return;
    }
    robo_css_source_dispose(&slot->source);
    robo_css_rule_list_dispose(&slot->rules);
    memset(slot, 0, sizeof(*slot));
    robo_css_error_init(&slot->error);
}

robo_css_stylesheet_id_t robo_css_slot_id_at(size_t index)
{
    size_t i;
    size_t seen = 0u;

    for (i = 0u; i < (size_t)ROBO_CSS_MAX_STYLESHEETS; i++) {
        if (!g_slots[i].in_use) {
            continue;
        }
        if (seen == index) {
            return g_slots[i].id;
        }
        seen++;
    }
    return ROBO_CSS_STYLESHEET_ID_NONE;
}

size_t robo_css_slot_live_count(void)
{
    size_t i;
    size_t live = 0u;

    for (i = 0u; i < (size_t)ROBO_CSS_MAX_STYLESHEETS; i++) {
        if (g_slots[i].in_use) {
            live++;
        }
    }
    return live;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

robo_result_t robo_css_init(void)
{
    if (g_initialized) {
        return ROBO_ALREADY_EXISTS;
    }
    if (!robo_base_is_initialized()) {
        robo_log_error("robo_css: init refused; RoboBase not initialized");
        return ROBO_INIT_ERROR;
    }
    /* Slots are static, so they start zeroed; the zero value of every field is
     * the disposed value (in_use 0, KIND_NONE source, empty rule list, no
     * error), which is what makes the table safe before the first claim. */
    g_initialized = 1;
    robo_log_debug("robo_css: module initialized (slots=%u)", ROBO_CSS_MAX_STYLESHEETS);
    return ROBO_OK;
}

void robo_css_shutdown(void)
{
    size_t i;
    size_t released = 0u;

    if (!g_initialized) {
        return;
    }
    for (i = 0u; i < (size_t)ROBO_CSS_MAX_STYLESHEETS; i++) {
        if (g_slots[i].in_use) {
            robo_css_slot_release(&g_slots[i]);
            released++;
        }
    }
    g_initialized = 0;
    robo_log_debug("robo_css: module shut down (stylesheets released=%llu)",
                   (unsigned long long)released);
}

int robo_css_is_initialized(void)
{
    return g_initialized;
}

size_t robo_css_live_stylesheet_count(void)
{
    if (!g_initialized) {
        return 0u;
    }
    return robo_css_slot_live_count();
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

void robo_css_capabilities_init(robo_css_capabilities_t *capabilities)
{
    if (!capabilities) {
        return;
    }
    memset(capabilities, 0, sizeof(*capabilities));
}

robo_result_t robo_css_get_capabilities(robo_css_capabilities_t *out)
{
    if (!out) {
        return ROBO_INVALID_ARGUMENT;
    }
    robo_css_capabilities_init(out);

    /* The model this phase builds. Every one of these is backed by a header, an
     * implementation and a suite; none of them interprets CSS. */
    out->source_model = 1;
    out->token_model = 1;
    out->value_model = 1;
    out->property_registry = 1;
    out->declaration_model = 1;
    out->selector_model = 1;
    out->rule_model = 1;
    out->stylesheet_model = 1;
    out->error_model = 1;

    /* Everything that would interpret it is 0, and stays 0 until its own phase
     * lands. A caller that reads this is never told a feature works because a
     * type for it exists. */
    /* 9.2 produced a tokenizer and 9.3 produced the grammar that consumes it:
     * both flags name something that now exists end to end, so both are 1, and
     * the suites assert 1. `inline_styles` goes with the grammar rather than
     * with a phase of its own, because the boundary it names is
     * robo_css_declaration_list_parse_text, which is a declaration-list parse —
     * the same code path a block uses. Each later flag is 1 only once its own
     * phase has landed, which is what the two comments below record. */
    out->tokenizer = 1;
    out->parser = 1;
    out->inline_styles = 1;
    /* 9.4 turns a prelude into selectors and matches them against a host's
     * elements. Both flags name something end to end: the selector grammar in
     * RoboCSSSelectorParser.c, and the matcher in RoboCSSMatcher.c. What is left
     * above them — specificity, inheritance, computed values — still has no
     * implementation, and says so below. */
    out->selector_parsing = 1;
    out->selector_matching = 1;
    /* 9.5 collects what applies to an element — every origin, every matched
     * rule, the inline attribute — in cascade order, and answers the winner for
     * a property by level, specificity and document order. 9.6 supplied the
     * middle key and removed the reported-undecided outcome, so the two flags
     * below now name a complete ordering rather than half of one. */
    out->cascade = 1;
    /* 9.6 computes a selector's specificity and hands it to the cascade as
     * structured data. The cascade consumes it through one comparison function
     * and holds no arithmetic of its own, which is what makes this a flag rather
     * than a second implementation. */
    out->specificity = 1;
    /* 9.7 takes a cascade and the parent's resolved style and produces a
     * resolved style: every declared property, every inherited one, and every
     * inherited-by-default default, each carrying where its value came from. */
    out->inheritance = 1;
    /* 9.8 turns that resolved style into a computed one: every entry carries a
     * typed value — a length with its unit, a percentage, a colour, a keyword, a
     * family list, or the tokens themselves — normalised through the property's
     * declared value shape. What it does *not* do is resolve: `50%` stays a
     * percentage, `2em` stays relative, `var()` and `calc()` stay references, and
     * `currentColor` stays contextual, which is why this is a flag for computing
     * and not for laying anything out. */
    out->computed_values = 1;
    out->default_styles = 0;
    out->stylesheet_loading = 0;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Runtime default stylesheet boundary                                 */
/* ------------------------------------------------------------------ */

const char *robo_css_default_stylesheet_text(size_t *out_length)
{
    if (out_length) {
        *out_length = 0u;
    }
    /* No runtime styles exist yet. Returning NULL rather than an empty string
     * keeps "there is no default stylesheet" from being confusable with "the
     * default stylesheet is empty", which is a different (and temporary) state
     * a later phase will be in while the sheet is being written. */
    return NULL;
}
