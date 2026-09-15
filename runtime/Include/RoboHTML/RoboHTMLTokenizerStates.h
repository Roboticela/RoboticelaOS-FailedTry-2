/*
 * RoboHTML tokenizer states (Phase 8.2).
 *
 * An explicit, testable state machine following the HTML tokenizer model.
 * State names are Roboticela's own; the behavior contracts reference the
 * WHATWG tokenizer states conceptually (the standard is public; no
 * Chromium code, data structures, or identifiers are used).
 *
 * The four content families (data / PLAINTEXT / RCDATA / RAWTEXT / script
 * data) are reached two ways:
 *   - the default DATA flow, and
 *   - robo_html_tokenizer_switch_state(), the minimal controlled hook the
 *     future tree builder (8.3) calls after start tags such as <textarea>
 *     (RCDATA), <title> (RCDATA), <style>/<xmp>/iframe contents (RAWTEXT),
 *     <script> (script data) and after <plaintext>.
 *
 * Phase 8.2 implements the data + tag + comment + doctype + character
 * reference states fully; RAWTEXT/RCDATA/script/plaintext families are
 * wired (states exist, entry point exists, basic text behavior works) but
 * their special sub-rules (script escaping, RCDATA reference handling) are
 * flagged for the tree-builder phase.
 */

#ifndef ROBO_HTML_TOKENIZER_STATES_H
#define ROBO_HTML_TOKENIZER_STATES_H

#include "RoboBase/RoboError.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum robo_html_tokenizer_state {
    /* --- data family --------------------------------------------------- */
    ROBO_HTML_TOKENIZER_STATE_DATA = 0,
    ROBO_HTML_TOKENIZER_STATE_PLAINTEXT = 1,

    /* --- markup entry --------------------------------------------------- */
    ROBO_HTML_TOKENIZER_STATE_TAG_OPEN = 2,
    ROBO_HTML_TOKENIZER_STATE_END_TAG_OPEN = 3,
    ROBO_HTML_TOKENIZER_STATE_TAG_NAME = 4,

    /* --- attributes ------------------------------------------------------ */
    ROBO_HTML_TOKENIZER_STATE_BEFORE_ATTRIBUTE_NAME = 5,
    ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_NAME = 6,
    ROBO_HTML_TOKENIZER_STATE_AFTER_ATTRIBUTE_NAME = 7,
    ROBO_HTML_TOKENIZER_STATE_BEFORE_ATTRIBUTE_VALUE = 8,
    ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_VALUE_DOUBLE_QUOTED = 9,
    ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_VALUE_SINGLE_QUOTED = 10,
    ROBO_HTML_TOKENIZER_STATE_ATTRIBUTE_VALUE_UNQUOTED = 11,
    ROBO_HTML_TOKENIZER_STATE_AFTER_ATTRIBUTE_VALUE_QUOTED = 12,
    ROBO_HTML_TOKENIZER_STATE_SELF_CLOSING_START_TAG = 13,

    /* --- markup declarations -------------------------------------------- */
    ROBO_HTML_TOKENIZER_STATE_MARKUP_DECLARATION_OPEN = 14,
    ROBO_HTML_TOKENIZER_STATE_BOGUS_COMMENT = 15,
    ROBO_HTML_TOKENIZER_STATE_COMMENT_START = 16,
    ROBO_HTML_TOKENIZER_STATE_COMMENT_START_DASH = 17,
    ROBO_HTML_TOKENIZER_STATE_COMMENT = 18,
    ROBO_HTML_TOKENIZER_STATE_COMMENT_LESS_THAN = 19,
    ROBO_HTML_TOKENIZER_STATE_COMMENT_LESS_THAN_BANG = 20,
    ROBO_HTML_TOKENIZER_STATE_COMMENT_LESS_THAN_BANG_DASHES = 21,
    ROBO_HTML_TOKENIZER_STATE_COMMENT_END_DASH = 22,
    ROBO_HTML_TOKENIZER_STATE_COMMENT_END = 23,
    ROBO_HTML_TOKENIZER_STATE_COMMENT_END_BANG = 24,

    /* --- doctype ---------------------------------------------------------- */
    ROBO_HTML_TOKENIZER_STATE_DOCTYPE = 25,
    ROBO_HTML_TOKENIZER_STATE_BEFORE_DOCTYPE_NAME = 26,
    ROBO_HTML_TOKENIZER_STATE_DOCTYPE_NAME = 27,
    ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_NAME = 28,
    ROBO_HTML_TOKENIZER_STATE_BOGUS_DOCTYPE = 29,
    ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_PUBLIC_KEYWORD = 30,
    ROBO_HTML_TOKENIZER_STATE_BEFORE_DOCTYPE_PUBLIC_IDENTIFIER = 31,
    ROBO_HTML_TOKENIZER_STATE_DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED = 32,
    ROBO_HTML_TOKENIZER_STATE_DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED = 33,
    ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_PUBLIC_IDENTIFIER = 34,
    ROBO_HTML_TOKENIZER_STATE_BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS = 35,
    ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_SYSTEM_KEYWORD = 36,
    ROBO_HTML_TOKENIZER_STATE_BEFORE_DOCTYPE_SYSTEM_IDENTIFIER = 37,
    ROBO_HTML_TOKENIZER_STATE_DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED = 38,
    ROBO_HTML_TOKENIZER_STATE_DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED = 39,
    ROBO_HTML_TOKENIZER_STATE_AFTER_DOCTYPE_SYSTEM_IDENTIFIER = 40,
    ROBO_HTML_TOKENIZER_STATE_BOGUS_DOCTYPE_QUIRKS = 41,

    /* --- character references --------------------------------------------- */
    ROBO_HTML_TOKENIZER_STATE_CHARACTER_REFERENCE = 42,
    ROBO_HTML_TOKENIZER_STATE_CHARACTER_REFERENCE_NAMED = 43,
    ROBO_HTML_TOKENIZER_STATE_CHARACTER_REFERENCE_NUMERIC = 44,
    ROBO_HTML_TOKENIZER_STATE_CHARACTER_REFERENCE_NUMERIC_HEX = 45,
    ROBO_HTML_TOKENIZER_STATE_CHARACTER_REFERENCE_NUMERIC_END = 46,

    /* --- raw content families (tree-builder entry via switch_state) ------ */
    ROBO_HTML_TOKENIZER_STATE_RAWTEXT = 47,
    ROBO_HTML_TOKENIZER_STATE_RAWTEXT_LESS_THAN_SIGN = 48,
    ROBO_HTML_TOKENIZER_STATE_RAWTEXT_END_TAG_OPEN = 49,
    ROBO_HTML_TOKENIZER_STATE_RAWTEXT_END_TAG_NAME = 50,
    ROBO_HTML_TOKENIZER_STATE_RCDATA = 51,
    ROBO_HTML_TOKENIZER_STATE_RCDATA_LESS_THAN_SIGN = 52,
    ROBO_HTML_TOKENIZER_STATE_RCDATA_END_TAG_OPEN = 53,
    ROBO_HTML_TOKENIZER_STATE_RCDATA_END_TAG_NAME = 54,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA = 55,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_LESS_THAN_SIGN = 56,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_END_TAG_OPEN = 57,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_END_TAG_NAME = 58,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPE_START = 59,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPE_START_DASH = 60,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED = 61,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED_DASH = 62,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED_DASH_DASH = 63,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED_LESS_THAN_SIGN = 64,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED_END_TAG_OPEN = 65,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED_END_TAG_NAME = 66,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPE_START = 67,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED = 68,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_DASH = 69,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH = 70,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_LESS_THAN_SIGN = 71,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_OPEN = 72,
    ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_NAME = 73,

    ROBO_HTML_TOKENIZER_STATE_COUNT_ = 74
} robo_html_tokenizer_state_t;

/** Non-zero if @p state is a defined state enumerator. */
int robo_html_tokenizer_state_is_known(robo_html_tokenizer_state_t state);

/** Human-readable state name (snake_case); never NULL. */
const char *robo_html_tokenizer_state_name(robo_html_tokenizer_state_t state);

/**
 * Non-zero when @p state belongs to a raw content family (RAWTEXT /
 * RCDATA / script data / PLAINTEXT) in which the tokenizer must not emit
 * start/end-tag tokens from arbitrary markup (only matching end tags).
 */
int robo_html_tokenizer_state_is_raw_content(
    robo_html_tokenizer_state_t state);

/**
 * Minimal controlled state switch for the future tree builder (8.3):
 * after emitting a start tag for e.g. <textarea>, the builder asks the
 * tokenizer to continue in RCDATA; <style> → RAWTEXT; <script> → script
 * data; <plaintext> → PLAINTEXT; DATA resets to ordinary content.
 * @return ROBO_OK for those five entry points; ROBO_INVALID_ARGUMENT for
 *         tokenizer sub-states (unreachable from outside) and for
 *         undefined enumerators.
 * (Phase 8.2 exposes the mechanism; no element semantics are applied.)
 */
robo_result_t robo_html_tokenizer_state_switch_validate(
    robo_html_tokenizer_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_HTML_TOKENIZER_STATES_H */
