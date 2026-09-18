/*
 * RoboCSS positioning vocabulary (Phase 9.13) — see RoboCSSPosition.h.
 *
 * One table, and everything else in this file reads it:
 *
 *   row        one value, its keyword, whether it stays in flow, and whether it
 *              is a positioned box
 *
 * Five rows today. Adding `position: running()` is adding a row; adding the
 * sticky variants is adding rows. Neither requires touching a function below,
 * which is the entire justification for the table: every question in this module
 * used to be a place a spelling could be compared wrongly, and there is now
 * exactly one place a spelling appears.
 *
 * The keywords are lowercase and unhyphenated because that is how CSS writes them
 * and how `robo_css_positioning_write` must print them; matching is ASCII
 * case-insensitive against these spellings and against nothing else, so
 * `posabsolute` is not a position and neither is a prefix of one.
 */

#include "RoboCSS/RoboCSSPosition.h"

#include "RoboCSSInternal.h"

#include <string.h>

/*
 * The vocabulary.
 *
 * `positioned` is false for `static` alone, and the two flags are not each
 * other's opposite: `relative` and `sticky` are both in flow *and* positioned,
 * which is the pair that reads as a contradiction until you notice they are two
 * questions. A relative box takes up room where flow put it and is still a
 * containing block for an absolute descendant; a sticky box is the same, with a
 * scroll constraint on top.
 */
typedef struct position_row {
    robo_css_positioning_t value;
    const char *keyword;
    int in_flow;
    int positioned;
} position_row_t;

static const position_row_t position_vocabulary[] = {
    { ROBO_CSS_POSITIONING_STATIC,   "static",   1, 0 },
    { ROBO_CSS_POSITIONING_RELATIVE, "relative", 1, 1 },
    { ROBO_CSS_POSITIONING_ABSOLUTE, "absolute", 0, 1 },
    { ROBO_CSS_POSITIONING_FIXED,    "fixed",    0, 1 },
    { ROBO_CSS_POSITIONING_STICKY,   "sticky",   1, 1 }
};

#define POSITION_VOCABULARY_COUNT \
    (sizeof(position_vocabulary) / sizeof(position_vocabulary[0]))

/**
 * The row for @p position, or NULL when it is not one of them.
 *
 * The one lookup every accessor below goes through, so a table search exists in
 * exactly one place and a row added to the table is reachable by all of them.
 */
static const position_row_t *position_row_of(robo_css_positioning_t position)
{
    size_t index;

    for (index = 0u; index < POSITION_VOCABULARY_COUNT; index++) {
        if (position_vocabulary[index].value == position) {
            return &position_vocabulary[index];
        }
    }
    return NULL;
}

int robo_css_positioning_is_known(robo_css_positioning_t position)
{
    return position_row_of(position) != NULL;
}

const char *robo_css_positioning_name(robo_css_positioning_t position)
{
    const position_row_t *row = position_row_of(position);

    return row ? row->keyword : "";
}

size_t robo_css_positioning_count(void)
{
    return POSITION_VOCABULARY_COUNT;
}

robo_css_positioning_t robo_css_positioning_from_slice(robo_css_slice_t keyword)
{
    size_t index;

    if (robo_css_slice_is_absent(keyword) || keyword.length == 0u) {
        return ROBO_CSS_POSITIONING_STATIC;
    }
    for (index = 0u; index < POSITION_VOCABULARY_COUNT; index++) {
        if (robo_css_bytes_equal_ascii_ci(keyword.bytes, keyword.length,
                                         position_vocabulary[index].keyword)) {
            return position_vocabulary[index].value;
        }
    }
    /* Not one of them. STATIC is the answer a caller reads *after* asking
     * robo_css_positioning_slice_is_known — see the header: `static` is a value, so
     * a result alone cannot say whether the identifier was recognised. */
    return ROBO_CSS_POSITIONING_STATIC;
}

robo_css_positioning_t robo_css_positioning_from_text(const char *keyword)
{
    if (!keyword) {
        return ROBO_CSS_POSITIONING_STATIC;
    }
    return robo_css_positioning_from_slice(
        robo_css_slice_from_bytes((const uint8_t *)keyword, strlen(keyword)));
}

int robo_css_positioning_slice_is_known(robo_css_slice_t keyword)
{
    size_t index;

    if (robo_css_slice_is_absent(keyword) || keyword.length == 0u) {
        return 0;
    }
    for (index = 0u; index < POSITION_VOCABULARY_COUNT; index++) {
        if (robo_css_bytes_equal_ascii_ci(keyword.bytes, keyword.length,
                                         position_vocabulary[index].keyword)) {
            return 1;
        }
    }
    return 0;
}

robo_result_t robo_css_positioning_write(robo_css_positioning_t position,
                                      char *buffer,
                                      size_t capacity,
                                      size_t *out_length,
                                      robo_css_diag_t *diag)
{
    const position_row_t *row;
    const char *keyword;
    size_t length;

    if (!buffer || !out_length || capacity == 0u) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_POSITION, ROBO_INVALID_ARGUMENT,
                             "buffer and out_length are required");
    }
    buffer[0] = '\0';
    *out_length = 0u;
    row = position_row_of(position);
    if (!row) {
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_POSITION, ROBO_INVALID_ARGUMENT,
                             "not a position value");
    }
    keyword = row->keyword;
    length = strlen(keyword);
    if (length >= capacity) {
        /* The length it would have needed, so a caller can size a buffer from the
         * refusal rather than from a constant it has to keep in step. */
        *out_length = length;
        return ROBO_CSS_DIAG(diag, ROBO_CSS_DOMAIN_POSITION, ROBO_NOT_SUPPORTED,
                             "the position keyword does not fit");
    }
    memcpy(buffer, keyword, length);
    buffer[length] = '\0';
    *out_length = length;
    return ROBO_OK;
}

/* ------------------------------------------------------------------ */
/* Classification                                                      */
/* ------------------------------------------------------------------ */

int robo_css_positioning_is_positioned(robo_css_positioning_t position)
{
    const position_row_t *row = position_row_of(position);

    /* Not `!= STATIC`, although the two agree today: `position: running()` is not
     * positioned in this sense, and the comparison would have to be revisited on
     * the day it is a row. */
    return row ? row->positioned : 0;
}

int robo_css_positioning_is_in_flow(robo_css_positioning_t position)
{
    const position_row_t *row = position_row_of(position);

    return row ? row->in_flow : 0;
}

int robo_css_positioning_is_out_of_flow(robo_css_positioning_t position)
{
    const position_row_t *row = position_row_of(position);

    /* Asked of the row rather than written as `!in_flow`, so that an unknown
     * enumerator answers 0 to both questions instead of claiming the box is
     * somewhere else. */
    return row && !row->in_flow;
}
