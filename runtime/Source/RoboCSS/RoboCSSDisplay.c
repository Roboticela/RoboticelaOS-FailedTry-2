/*
 * RoboCSS display vocabulary (Phase 9.12) — see RoboCSSDisplay.h.
 *
 * One table, and everything else in this file reads it:
 *
 *   row        one value, its keyword, its outer display and its inner display
 *
 * Four rows today. Adding `flex` is adding a row with an outer of BLOCK and an
 * inner this build would then have to define; adding `contents` is adding a row
 * with an outer of NONE that is *not* the value `none`. Neither requires
 * touching a function below, which is the entire justification for the table:
 * every question in this module used to be a place a spelling could be compared
 * wrongly, and there is now exactly one place a spelling appears.
 *
 * The keywords are lowercase and hyphenated because that is how CSS writes them
 * and how `robo_css_display_write` must print them; matching is ASCII
 * case-insensitive against these spellings and against nothing else, so
 * `inlineblock` is not a display value and neither is a prefix of one.
 */

#include "RoboCSS/RoboCSSDisplay.h"

#include "RoboCSSInternal.h"

#include <string.h>

/*
 * The vocabulary.
 *
 * `inner` for `none` is INNER_NONE rather than FLOW, and that is a fact about
 * `none` rather than a placeholder: a value that generates no box has no contents
 * to lay out. `block` and `inline` share FLOW, because sharing its parent's
 * formatting context is what makes them ordinary; `inline-block` takes a
 * formatting context of its own, which is the whole of what makes it atomic.
 */
typedef struct display_row {
    robo_css_display_t value;
    const char *keyword;
    robo_css_display_outer_t outer;
    robo_css_display_inner_t inner;
} display_row_t;

static const display_row_t display_vocabulary[] = {
    { ROBO_CSS_DISPLAY_NONE,         "none",         ROBO_CSS_DISPLAY_OUTER_NONE,
      ROBO_CSS_DISPLAY_INNER_NONE },
    { ROBO_CSS_DISPLAY_BLOCK,        "block",        ROBO_CSS_DISPLAY_OUTER_BLOCK,
      ROBO_CSS_DISPLAY_INNER_FLOW },
    { ROBO_CSS_DISPLAY_INLINE,       "inline",       ROBO_CSS_DISPLAY_OUTER_INLINE,
      ROBO_CSS_DISPLAY_INNER_FLOW },
    { ROBO_CSS_DISPLAY_INLINE_BLOCK, "inline-block", ROBO_CSS_DISPLAY_OUTER_INLINE,
      ROBO_CSS_DISPLAY_INNER_FLOW_ROOT }
};

#define DISPLAY_VOCABULARY_COUNT \
    (sizeof(display_vocabulary) / sizeof(display_vocabulary[0]))

/**
 * The row for @p display, or NULL when it is not one of them.
 *
 * The one lookup every accessor below goes through, so a table search exists in
 * exactly one place and a row added to the table is reachable by all of them.
 */
static const display_row_t *display_row_of(robo_css_display_t display)
{
    size_t index;

    for (index = 0u; index < DISPLAY_VOCABULARY_COUNT; index++) {
        if (display_vocabulary[index].value == display) {
            return &display_vocabulary[index];
        }
    }
    return NULL;
}

int robo_css_display_is_known(robo_css_display_t display)
{
    return display_row_of(display) != NULL;
}

const char *robo_css_display_name(robo_css_display_t display)
{
    const display_row_t *row = display_row_of(display);

    return row ? row->keyword : "";
}

size_t robo_css_display_count(void)
{
    return DISPLAY_VOCABULARY_COUNT;
}

robo_css_display_t robo_css_display_from_slice(robo_css_slice_t keyword)
{
    size_t index;

    if (robo_css_slice_is_absent(keyword) || keyword.length == 0u) {
        return ROBO_CSS_DISPLAY_NONE;
    }
    for (index = 0u; index < DISPLAY_VOCABULARY_COUNT; index++) {
        if (robo_css_bytes_equal_ascii_ci(keyword.bytes, keyword.length,
                                         display_vocabulary[index].keyword)) {
            return display_vocabulary[index].value;
        }
    }
    /* Not one of them. NONE is the answer a caller reads *after* asking
     * robo_css_display_slice_is_known — see the header: `none` is a value, so a
     * result alone cannot say whether the identifier was recognised. */
    return ROBO_CSS_DISPLAY_NONE;
}

robo_css_display_t robo_css_display_from_text(const char *keyword)
{
    if (!keyword) {
        return ROBO_CSS_DISPLAY_NONE;
    }
    return robo_css_display_from_slice(
        robo_css_slice_from_bytes((const uint8_t *)keyword, strlen(keyword)));
}

int robo_css_display_slice_is_known(robo_css_slice_t keyword)
{
    size_t index;

    if (robo_css_slice_is_absent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < DISPLAY_VOCABULARY_COUNT; index++) {
        if (robo_css_bytes_equal_ascii_ci(keyword.bytes, keyword.length,
                                         display_vocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

robo_result_t robo_css_display_write(robo_css_display_t display,
                                     char *buffer,
                                     size_t capacity,
                                     size_t *out_length,
                                     robo_css_diag_t *diag)
{
    const display_row_t *row;
    const char *keyword;
    size_t length;

    if (!buffer || !out_length || capacity == 0u) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_DISPLAY, ROBO_INVALID_ARGUMENT,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *out_length = 0u;
    row = display_row_of(display);
    if (!row) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_DISPLAY, ROBO_INVALID_ARGUMENT,
                             "not a display value");
    }
    keyword = row->keyword;
    length = strlen(keyword);
    if (length >= capacity) {
        /* The length it would have needed, so a caller can size a buffer from the
         * refusal rather than from a constant it has to keep in step. */
        *out_length = length;
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_DISPLAY, ROBO_NOT_SUPPORTED,
                             "the display keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *out_length = length;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

robo_css_display_outer_t robo_css_display_outer(robo_css_display_t display)
{
    const display_row_t *row = display_row_of(display);

    return row ? row->outer : ROBO_CSS_DISPLAY_OUTER_NONE;
}

robo_css_display_inner_t robo_css_display_inner(robo_css_display_t display)
{
    const display_row_t *row = display_row_of(display);

    return row ? row->inner : ROBO_CSS_DISPLAY_INNER_NONE;
}

int robo_css_display_generates_box(robo_css_display_t display)
{
    return robo_css_display_outer(display) != ROBO_CSS_DISPLAY_OUTER_NONE;
}

int robo_css_display_is_none(robo_css_display_t display)
{
    return display == ROBO_CSS_DISPLAY_NONE;
}

int robo_css_display_is_block_level(robo_css_display_t display)
{
    return robo_css_display_outer(display) == ROBO_CSS_DISPLAY_OUTER_BLOCK;
}

int robo_css_display_is_inline_level(robo_css_display_t display)
{
    return robo_css_display_outer(display) == ROBO_CSS_DISPLAY_OUTER_INLINE;
}

int robo_css_display_is_inline_block(robo_css_display_t display)
{
    /* Both halves, from the table: inline-level *and* with a formatting context
     * of its own. One half alone would call `inline` atomic. */
    return robo_css_display_outer(display) == ROBO_CSS_DISPLAY_OUTER_INLINE &&
           robo_css_display_inner(display) == ROBO_CSS_DISPLAY_INNER_FLOW_ROOT;
}

const char *robo_css_display_outer_name(robo_css_display_outer_t outer)
{
    switch (outer) {
    case ROBO_CSS_DISPLAY_OUTER_NONE:
        return "none";
    case ROBO_CSS_DISPLAY_OUTER_BLOCK:
        return "block";
    case ROBO_CSS_DISPLAY_OUTER_INLINE:
        return "inline";
    default:
        return "unknown";
    }
}

const char *robo_css_display_inner_name(robo_css_display_inner_t inner)
{
    switch (inner) {
    case ROBO_CSS_DISPLAY_INNER_NONE:
        return "none";
    case ROBO_CSS_DISPLAY_INNER_FLOW:
        return "flow";
    case ROBO_CSS_DISPLAY_INNER_FLOW_ROOT:
        return "flow-root";
    default:
        return "unknown";
    }
}
