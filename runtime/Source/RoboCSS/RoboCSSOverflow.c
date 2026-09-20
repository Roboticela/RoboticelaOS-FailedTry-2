/*
 * RoboCSS overflow vocabulary (Phase 10.13) — see RoboCSSOverflow.h.
 *
 * One table, and everything else in this file reads it:
 *
 *   row      one value, its keyword, and whether it clips
 *
 * Two rows. Adding `scroll` is a row whose `clips` is 1 and whose *behaviour* is a
 * later phase's; adding `clip` is a row whose clipping boundary differs from
 * `hidden`'s, which is why the boundary question is asked of the table's classification
 * rather than spelled out at the call site. Neither addition touches a function below,
 * which is the entire justification for the table: every question in this module used
 * to be a place a spelling could be compared wrongly, and there is now exactly one
 * place a spelling appears.
 *
 * The keywords are lowercase because that is how CSS writes them and how
 * `robo_css_overflow_write` must print them; matching is ASCII case-insensitive
 * against these spellings and against nothing else, so `hidd` is not an overflow
 * value and neither is a prefix of one.
 */

#include "RoboCSS/RoboCSSOverflow.h"

#include "RoboCSSInternal.h"

#include <string.h>

/*
 * The vocabulary.
 *
 * `clips` is 1 for `hidden` and 0 for `visible`, and it is a field rather than a
 * comparison against the enumerator for the reason the header gives: a consumer asks
 * "does this axis have a boundary?" and the day a third row answers *differently*
 * from `hidden` -- `clip`, whose boundary is the same rectangle but whose content is
 * not scrollable -- the answer changes here rather than at every call site.
 */
typedef struct overflow_row {
    robo_css_overflow_t value;
    const char *keyword;
    int clips;
} overflow_row_t;

static const overflow_row_t overflow_vocabulary[] = {
    { ROBO_CSS_OVERFLOW_VISIBLE, "visible", 0 },
    { ROBO_CSS_OVERFLOW_HIDDEN,  "hidden",  1 }
};

#define OVERFLOW_VOCABULARY_COUNT \
    (sizeof(overflow_vocabulary) / sizeof(overflow_vocabulary[0]))

/**
 * The row for @p overflow, or NULL when it is not one of them.
 *
 * The one lookup every accessor below goes through, so a table search exists in
 * exactly one place and a row added to the table is reachable by all of them.
 */
static const overflow_row_t *overflow_row_of(robo_css_overflow_t overflow)
{
    size_t index;

    for (index = 0u; index < OVERFLOW_VOCABULARY_COUNT; index++) {
        if (overflow_vocabulary[index].value == overflow) {
            return &overflow_vocabulary[index];
        }
    }
    return NULL;
}

int robo_css_overflow_is_known(robo_css_overflow_t overflow)
{
    return overflow_row_of(overflow) != NULL;
}

const char *robo_css_overflow_name(robo_css_overflow_t overflow)
{
    const overflow_row_t *row = overflow_row_of(overflow);

    return row ? row->keyword : "";
}

size_t robo_css_overflow_count(void)
{
    return OVERFLOW_VOCABULARY_COUNT;
}

robo_css_overflow_t robo_css_overflow_from_slice(robo_css_slice_t keyword)
{
    size_t index;

    if (robo_css_slice_is_absent(keyword) || keyword.length == 0u) {
        return ROBO_CSS_OVERFLOW_VISIBLE;
    }
    for (index = 0u; index < OVERFLOW_VOCABULARY_COUNT; index++) {
        if (robo_css_bytes_equal_ascii_ci(keyword.bytes, keyword.length,
                                          overflow_vocabulary[index].keyword)) {
            return overflow_vocabulary[index].value;
        }
    }
    /* Not one of them. VISIBLE is the answer a caller reads *after* asking
     * robo_css_overflow_slice_is_known — see the header: `visible` is a value, so a
     * result alone cannot say whether the identifier was recognised. */
    return ROBO_CSS_OVERFLOW_VISIBLE;
}

robo_css_overflow_t robo_css_overflow_from_text(const char *keyword)
{
    if (!keyword) {
        return ROBO_CSS_OVERFLOW_VISIBLE;
    }
    return robo_css_overflow_from_slice(
        robo_css_slice_from_bytes((const uint8_t *)keyword, strlen(keyword)));
}

int robo_css_overflow_slice_is_known(robo_css_slice_t keyword)
{
    size_t index;

    if (robo_css_slice_is_absent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < OVERFLOW_VOCABULARY_COUNT; index++) {
        if (robo_css_bytes_equal_ascii_ci(keyword.bytes, keyword.length,
                                          overflow_vocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

robo_result_t robo_css_overflow_write(robo_css_overflow_t overflow,
                                      char *buffer,
                                      size_t capacity,
                                      size_t *out_length,
                                      robo_css_diag_t *diag)
{
    const overflow_row_t *row;
    const char *keyword;
    size_t length;

    if (!buffer || !out_length || capacity == 0u) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_OVERFLOW, ROBO_INVALID_ARGUMENT,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *out_length = 0u;
    row = overflow_row_of(overflow);
    if (!row) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_OVERFLOW, ROBO_INVALID_ARGUMENT,
                             "not an overflow value");
    }
    keyword = row->keyword;
    length = strlen(keyword);
    if (length >= capacity) {
        /* The length it would have needed, so a caller can size a buffer from the
         * refusal rather than from a constant it has to keep in step. */
        *out_length = length;
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_OVERFLOW, ROBO_NOT_SUPPORTED,
                             "the overflow keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *out_length = length;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

int robo_css_overflow_clips(robo_css_overflow_t overflow)
{
    const overflow_row_t *row = overflow_row_of(overflow);

    /* An unknown enumerator answers 0 rather than 1, and the direction matters: a
     * record nothing can read must not make a box clip its contents, because clipping
     * throws geometry away and *not* clipping only draws outside a box. */
    return row ? row->clips : 0;
}

int robo_css_overflow_is_visible(robo_css_overflow_t overflow)
{
    return overflow == ROBO_CSS_OVERFLOW_VISIBLE;
}
