/*
 * RoboCSS Phase 9.1 foundation suite:
 * module lifecycle, version, shared types, diagnostics, capabilities.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSVersion.h"

static void test_css_version(void)
{
    robo_i32 major = 0;
    robo_i32 minor = 0;
    robo_i32 patch = 0;

    ROBO_CHECK_STR_EQ(robo_css_version_string(), ROBO_CSS_VERSION_STRING);
    ROBO_CHECK_EQ_INT(ROBO_CSS_VERSION_MAJOR, 0);
    ROBO_CHECK_EQ_INT(ROBO_CSS_VERSION_MINOR, 9);
    ROBO_CHECK_EQ_INT(ROBO_CSS_VERSION_PATCH, 0);
    /* Architecture milestone: the surface grows all through Phase 9. */
    ROBO_CHECK_EQ_INT(ROBO_CSS_API_FROZEN, 0);

    ROBO_CHECK_EQ_INT(robo_css_get_version(NULL, &minor, &patch),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_get_version(&major, NULL, &patch),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_get_version(&major, &minor, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_get_version(&major, &minor, &patch), ROBO_OK);
    ROBO_CHECK_EQ_INT(major, 0);
    ROBO_CHECK_EQ_INT(minor, 9);
    ROBO_CHECK_EQ_INT(patch, 0);
}

static void test_css_module_init_shutdown(void)
{
    css_fixture();
    ROBO_CHECK(robo_css_is_initialized());

    /* Double init is refused and changes nothing. */
    ROBO_CHECK_EQ_INT(robo_css_init(), ROBO_ALREADY_EXISTS);
    ROBO_CHECK(robo_css_is_initialized());

    /* Live count is about stylesheets, not about the module being up. */
    ROBO_CHECK_EQ_INT(robo_css_live_stylesheet_count(), 0u);

    robo_css_shutdown();
    ROBO_CHECK_EQ_INT(robo_css_is_initialized(), 0);

    /* Idempotent, and the count of a down module is 0 rather than stale. */
    robo_css_shutdown();
    ROBO_CHECK_EQ_INT(robo_css_is_initialized(), 0);
    ROBO_CHECK_EQ_INT(robo_css_live_stylesheet_count(), 0u);

    ROBO_CHECK_EQ_INT(robo_css_init(), ROBO_OK);
    ROBO_CHECK(robo_css_is_initialized());
}

static void test_css_module_shutdown_releases_stylesheets(void)
{
    robo_css_stylesheet_id_t first = ROBO_CSS_STYLESHEET_ID_NONE;
    robo_css_stylesheet_id_t second = ROBO_CSS_STYLESHEET_ID_NONE;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&first, ROBO_CSS_ORIGIN_AUTHOR, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&second, ROBO_CSS_ORIGIN_RUNTIME, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_live_stylesheet_count(), 2u);
    ROBO_CHECK(robo_css_stylesheet_is_live(first));
    ROBO_CHECK(robo_css_stylesheet_is_live(second));

    robo_css_shutdown();
    ROBO_CHECK_EQ_INT(robo_css_live_stylesheet_count(), 0u);
    /* A destroyed stylesheet's id is dead, and stays dead. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_is_live(first), 0);

    ROBO_CHECK_EQ_INT(robo_css_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_is_live(first), 0);
}

static void test_css_stylesheet_requires_module(void)
{
    robo_css_stylesheet_id_t id = ROBO_CSS_STYLESHEET_ID_NONE;
    robo_css_diag_t diag;

    css_fixture();
    robo_css_shutdown();

    robo_css_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&id, ROBO_CSS_ORIGIN_AUTHOR, &diag),
                      ROBO_INIT_ERROR);
    ROBO_CHECK_EQ_INT(id, ROBO_CSS_STYLESHEET_ID_NONE);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_INIT_ERROR);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_CSS_DOMAIN_STYLESHEET);
    ROBO_CHECK(diag.message != NULL);

    ROBO_CHECK_EQ_INT(robo_css_init(), ROBO_OK);
}

static void test_css_slot_table_is_bounded(void)
{
    size_t i;
    robo_css_stylesheet_id_t id = ROBO_CSS_STYLESHEET_ID_NONE;
    robo_css_stylesheet_id_t live_ids[ROBO_CSS_MAX_STYLESHEETS];

    css_fixture();
    for (i = 0u; i < (size_t)ROBO_CSS_MAX_STYLESHEETS; i++) {
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&id, ROBO_CSS_ORIGIN_AUTHOR, NULL),
                          ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_css_live_stylesheet_count(), (size_t)ROBO_CSS_MAX_STYLESHEETS);
    /* The table is full: the next create is refused rather than overwriting. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&id, ROBO_CSS_ORIGIN_AUTHOR, NULL),
                      ROBO_OUT_OF_MEMORY);

    /* Snapshot first, destroy second. id_at enumerates the *compacted* live
     * set, so destroying slot 0 shifts every later index down one and a
     * single-pass loop would silently skip every other stylesheet — the loop
     * would end holding half the table and still report success. */
    for (i = 0u; i < (size_t)ROBO_CSS_MAX_STYLESHEETS; i++) {
        live_ids[i] = robo_css_stylesheet_id_at(i);
        ROBO_CHECK(live_ids[i] != ROBO_CSS_STYLESHEET_ID_NONE);
    }
    /* Beyond the live set there is nothing to name, and it says so. */
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_id_at((size_t)ROBO_CSS_MAX_STYLESHEETS),
                      ROBO_CSS_STYLESHEET_ID_NONE);
    for (i = 0u; i < (size_t)ROBO_CSS_MAX_STYLESHEETS; i++) {
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(live_ids[i], NULL), ROBO_OK);
    }
    ROBO_CHECK_EQ_INT(robo_css_live_stylesheet_count(), 0u);
    /* Every id from the snapshot is dead once destroyed, including the last. */
    for (i = 0u; i < (size_t)ROBO_CSS_MAX_STYLESHEETS; i++) {
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_is_live(live_ids[i]), 0);
    }
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_create(&id, ROBO_CSS_ORIGIN_AUTHOR, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(id, NULL), ROBO_OK);
}

static void test_css_types_slices(void)
{
    const uint8_t bytes[] = {'a', 'b', 'c'};
    robo_css_slice_t absent = robo_css_slice_empty();
    robo_css_slice_t three = robo_css_slice_from_bytes(bytes, 3u);
    char text[8];
    uint8_t raw[8];

    ROBO_CHECK(robo_css_slice_is_absent(absent));
    ROBO_CHECK(robo_css_slice_is_empty(absent));
    ROBO_CHECK(!robo_css_slice_is_absent(three));
    ROBO_CHECK(!robo_css_slice_is_empty(three));

    /* An absent slice and an empty slice are different facts but compare equal
     * by length, which is what makes a borrowed "nothing" safe to pass around. */
    ROBO_CHECK(robo_css_slice_equals(absent, robo_css_slice_empty()));
    ROBO_CHECK(!robo_css_slice_equals(absent, three));

    ROBO_CHECK(robo_css_slice_equals_ascii(three, "abc"));
    ROBO_CHECK(!robo_css_slice_equals_ascii(three, "ab"));
    ROBO_CHECK(robo_css_slice_equals_ascii_ci(robo_css_slice_from_text("CaRrOt"),
                                              "carrot"));
    ROBO_CHECK(!robo_css_slice_equals_ascii(robo_css_slice_from_text("CaRrOt"),
                                            "carrot"));
    ROBO_CHECK(robo_css_slice_starts_with_ascii(three, "ab"));
    ROBO_CHECK(!robo_css_slice_starts_with_ascii(three, "bc"));
    ROBO_CHECK(robo_css_slice_starts_with_ascii(three, ""));

    ROBO_CHECK_EQ_INT(robo_css_slice_copy_bytes(three, raw, sizeof(raw)), ROBO_OK);
    ROBO_CHECK_EQ_INT(raw[0], 'a');
    ROBO_CHECK_EQ_INT(raw[2], 'c');
    /* Too little capacity is refused rather than overflowing. */
    ROBO_CHECK_EQ_INT(robo_css_slice_copy_bytes(three, raw, 2u), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_slice_copy_string(three, text, sizeof(text)), ROBO_OK);
    ROBO_CHECK_STR_EQ(text, "abc");
    ROBO_CHECK_EQ_INT(robo_css_slice_copy_string(three, text, 3u), ROBO_INVALID_ARGUMENT);
}

static void test_css_types_split_whitespace(void)
{
    robo_css_slice_t fields[8];
    int truncated = 0;
    size_t count;

    count = robo_css_split_ascii_whitespace(robo_css_slice_from_text("  a\tbb\nccc "),
                                            fields, 8u, &truncated);
    ROBO_CHECK_EQ_INT(count, 3u);
    ROBO_CHECK_EQ_INT(truncated, 0);
    ROBO_CHECK(robo_css_slice_equals_ascii(fields[0], "a"));
    ROBO_CHECK(robo_css_slice_equals_ascii(fields[1], "bb"));
    ROBO_CHECK(robo_css_slice_equals_ascii(fields[2], "ccc"));

    /* Whitespace-only and empty text produce nothing at all. */
    ROBO_CHECK_EQ_INT(robo_css_split_ascii_whitespace(robo_css_slice_from_text("  \r\n "),
                                                      fields, 8u, &truncated),
                      0u);
    ROBO_CHECK_EQ_INT(robo_css_split_ascii_whitespace(robo_css_slice_empty(), fields,
                                                      8u, &truncated),
                      0u);
    ROBO_CHECK_EQ_INT(truncated, 0);

    /* A capacity smaller than the field count writes what fits and says so. */
    count = robo_css_split_ascii_whitespace(robo_css_slice_from_text("a b c d"),
                                            fields, 2u, &truncated);
    ROBO_CHECK_EQ_INT(count, 2u);
    ROBO_CHECK_EQ_INT(truncated, 1);
    ROBO_CHECK(robo_css_slice_equals_ascii(fields[0], "a"));
    ROBO_CHECK(robo_css_slice_equals_ascii(fields[1], "b"));
}

static void test_css_types_positions_and_ranges(void)
{
    robo_css_position_t position;
    robo_css_range_t range;

    position = robo_css_position_none();
    ROBO_CHECK(!robo_css_position_has_line(&position));
    position = robo_css_position_at(7u);
    ROBO_CHECK_EQ_INT(position.offset, 7u);
    ROBO_CHECK_EQ_INT(position.line, 0u);
    ROBO_CHECK(!robo_css_position_has_line(&position));

    range = robo_css_range_at(4u);
    ROBO_CHECK(robo_css_range_is_empty(range));
    ROBO_CHECK_EQ_INT(robo_css_range_length(range), 0u);
    ROBO_CHECK(!robo_css_range_contains(range, 4u));

    range = robo_css_range_span(2u, 3u);
    ROBO_CHECK_EQ_INT(range.begin, 2u);
    ROBO_CHECK_EQ_INT(range.end, 5u);
    ROBO_CHECK_EQ_INT(robo_css_range_length(range), 3u);
    ROBO_CHECK(!robo_css_range_is_empty(range));
    ROBO_CHECK(robo_css_range_contains(range, 2u));
    ROBO_CHECK(robo_css_range_contains(range, 4u));
    /* Half-open: the end offset itself is not inside. */
    ROBO_CHECK(!robo_css_range_contains(range, 5u));

    /* A span that would wrap saturates instead of overflowing. */
    range = robo_css_range_span((size_t)-1 - 1u, 8u);
    ROBO_CHECK_EQ_INT(range.end, (size_t)-1);
    /* An inverted range has no length and contains nothing. */
    range = (robo_css_range_t){5u, 2u};
    ROBO_CHECK_EQ_INT(robo_css_range_length(range), 0u);
    ROBO_CHECK(!robo_css_range_contains(range, 3u));
}

static void test_css_types_vocabulary(void)
{
    robo_css_origin_t origin;

    ROBO_CHECK(robo_css_origin_is_known(ROBO_CSS_ORIGIN_NONE));
    ROBO_CHECK(robo_css_origin_is_known(ROBO_CSS_ORIGIN_INLINE));
    ROBO_CHECK(!robo_css_origin_is_known((robo_css_origin_t)99));

    /* Inline is provenance without a stylesheet: it is never a sheet origin. */
    for (origin = ROBO_CSS_ORIGIN_NONE; origin < (robo_css_origin_t)ROBO_CSS_ORIGIN_COUNT;
         origin = (robo_css_origin_t)(origin + 1)) {
        const char *name = robo_css_origin_name(origin);

        ROBO_CHECK(name != NULL);
        ROBO_CHECK(strcmp(name, "unknown") != 0);
        if (origin == ROBO_CSS_ORIGIN_INLINE) {
            ROBO_CHECK(robo_css_origin_is_inline(origin));
            ROBO_CHECK(!robo_css_origin_is_stylesheet_origin(origin));
        } else if (origin != ROBO_CSS_ORIGIN_NONE) {
            ROBO_CHECK(robo_css_origin_is_stylesheet_origin(origin));
            ROBO_CHECK(!robo_css_origin_is_inline(origin));
        }
    }
    ROBO_CHECK_STR_EQ(robo_css_origin_name((robo_css_origin_t)99), "unknown");

    ROBO_CHECK(robo_css_stylesheet_state_is_known(ROBO_CSS_STYLESHEET_STATE_PARSED));
    ROBO_CHECK(!robo_css_stylesheet_state_is_known((robo_css_stylesheet_state_t)42));
    /* NONE is "no stylesheet", not a state a parse can settle into. */
    ROBO_CHECK(!robo_css_stylesheet_state_is_settled(ROBO_CSS_STYLESHEET_STATE_NONE));
    ROBO_CHECK(!robo_css_stylesheet_state_is_settled(ROBO_CSS_STYLESHEET_STATE_CREATED));
    ROBO_CHECK(robo_css_stylesheet_state_is_settled(ROBO_CSS_STYLESHEET_STATE_PARSED));
    ROBO_CHECK(robo_css_stylesheet_state_is_settled(ROBO_CSS_STYLESHEET_STATE_FAILED));

    ROBO_CHECK(robo_css_diagnostic_domain_is_known(ROBO_CSS_DOMAIN_PARSER));
    ROBO_CHECK(!robo_css_diagnostic_domain_is_known((robo_css_diagnostic_domain_t)99));
    ROBO_CHECK_STR_EQ(robo_css_diagnostic_domain_name(ROBO_CSS_DOMAIN_COMPUTED),
                      "computed");
    ROBO_CHECK_STR_EQ(robo_css_diagnostic_domain_name((robo_css_diagnostic_domain_t)99),
                      "unknown");
}

static void test_css_error_vocabulary(void)
{
    robo_css_error_kind_t kind;
    size_t recoverable = 0u;
    size_t fatal = 0u;

    ROBO_CHECK(robo_css_error_kind_is_known(ROBO_CSS_ERROR_KIND_NONE));
    ROBO_CHECK(!robo_css_error_kind_is_known((robo_css_error_kind_t)99));
    ROBO_CHECK_STR_EQ(robo_css_error_kind_name(ROBO_CSS_ERROR_KIND_UNSUPPORTED),
                      "unsupported");
    ROBO_CHECK_STR_EQ(robo_css_error_kind_name((robo_css_error_kind_t)99), "unknown");

    /* The two classes are disjoint and cover every kind except "not an error". */
    for (kind = ROBO_CSS_ERROR_KIND_NONE;
         kind < (robo_css_error_kind_t)ROBO_CSS_ERROR_KIND_COUNT;
         kind = (robo_css_error_kind_t)(kind + 1)) {
        int is_recoverable = robo_css_error_kind_is_recoverable(kind);
        int is_fatal = robo_css_error_kind_is_fatal(kind);

        ROBO_CHECK(!(is_recoverable && is_fatal));
        if (kind == ROBO_CSS_ERROR_KIND_NONE) {
            ROBO_CHECK(!robo_css_error_kind_is_error(kind));
            ROBO_CHECK(!is_recoverable);
            ROBO_CHECK(!is_fatal);
        } else {
            ROBO_CHECK(robo_css_error_kind_is_error(kind));
            ROBO_CHECK(is_recoverable || is_fatal);
        }
        recoverable += (size_t)(is_recoverable ? 1 : 0);
        fatal += (size_t)(is_fatal ? 1 : 0);
    }
    ROBO_CHECK_EQ_INT(recoverable + fatal, (size_t)ROBO_CSS_ERROR_KIND_COUNT - 1u);

    /* A malformed declaration is something a parser continues past; an
     * unsupported feature is not. That is the distinction this vocabulary
     * exists to make. */
    ROBO_CHECK(robo_css_error_kind_is_recoverable(
        ROBO_CSS_ERROR_KIND_MALFORMED_DECLARATION));
    ROBO_CHECK(robo_css_error_kind_is_recoverable(ROBO_CSS_ERROR_KIND_UNEXPECTED_EOF));
    ROBO_CHECK(robo_css_error_kind_is_fatal(ROBO_CSS_ERROR_KIND_OUT_OF_MEMORY));
}

static void test_css_diag_record(void)
{
    robo_css_diag_t diag;

    robo_css_diag_set(&diag, ROBO_NOT_FOUND, ROBO_CSS_DOMAIN_SOURCE, "gone", "f.c", 12);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_CSS_DOMAIN_SOURCE);
    ROBO_CHECK_STR_EQ(diag.message, "gone");
    ROBO_CHECK_STR_EQ(diag.file, "f.c");
    ROBO_CHECK_EQ_INT(diag.line, 12);

    /* ROBO_OK clears rather than recording a success. */
    ROBO_CHECK_EQ_INT(robo_css_diag_set(&diag, ROBO_OK, ROBO_CSS_DOMAIN_SOURCE, "x",
                                        "f.c", 1),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_OK);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_CSS_DOMAIN_NONE);
    ROBO_CHECK(diag.message == NULL);

    /* An optional diagnostic: the macro returns the code either way, and a
     * caller passing NULL gets no diagnostic rather than a crash. */
    ROBO_CHECK_EQ_INT(ROBO_CSS_DIAG(NULL, ROBO_CSS_DOMAIN_TOKEN, ROBO_TIMEOUT, "t"),
                      ROBO_TIMEOUT);
    ROBO_CHECK_EQ_INT(ROBO_CSS_DIAG(&diag, ROBO_CSS_DOMAIN_TOKEN, ROBO_TIMEOUT, "t"),
                      ROBO_TIMEOUT);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_CSS_DOMAIN_TOKEN);

    /* An unknown domain is recorded as NONE instead of being trusted. */
    robo_css_diag_set(&diag, ROBO_ERROR, (robo_css_diagnostic_domain_t)99, "m", NULL, 0);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_CSS_DOMAIN_NONE);
    ROBO_CHECK_EQ_INT(robo_css_diag_set(NULL, ROBO_ERROR, ROBO_CSS_DOMAIN_NONE, "m",
                                        NULL, 0),
                      ROBO_INVALID_ARGUMENT);
}

static void test_css_capabilities(void)
{
    robo_css_capabilities_t caps;

    ROBO_CHECK_EQ_INT(robo_css_get_capabilities(NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_get_capabilities(&caps), ROBO_OK);

    /* The model this phase builds is present... */
    ROBO_CHECK_EQ_INT(caps.source_model, 1);
    ROBO_CHECK_EQ_INT(caps.token_model, 1);
    ROBO_CHECK_EQ_INT(caps.value_model, 1);
    ROBO_CHECK_EQ_INT(caps.property_registry, 1);
    ROBO_CHECK_EQ_INT(caps.declaration_model, 1);
    ROBO_CHECK_EQ_INT(caps.selector_model, 1);
    ROBO_CHECK_EQ_INT(caps.rule_model, 1);
    ROBO_CHECK_EQ_INT(caps.stylesheet_model, 1);
    ROBO_CHECK_EQ_INT(caps.error_model, 1);

    /* Two capabilities under that model have become real: 9.2 turns bytes into
     * tokens and 9.3 turns tokens into rules, declarations and component values
     * — including an inline `style` attribute's text, which goes through the
     * same declaration-list grammar. Everything above them is still only
     * described. */
    ROBO_CHECK_EQ_INT(caps.tokenizer, 1);
    ROBO_CHECK_EQ_INT(caps.parser, 1);
    ROBO_CHECK_EQ_INT(caps.inline_styles, 1);

    /* 9.4 turned a prelude into selectors and made them matchable. Both flags
     * are 1 because both halves exist end to end — not because the model gained
     * a field, but because RoboCSSSelectorParser.c and RoboCSSMatcher.c back
     * them. */
    ROBO_CHECK_EQ_INT(caps.selector_parsing, 1);
    ROBO_CHECK_EQ_INT(caps.selector_matching, 1);

    /* 9.5 collects what applies to an element in cascade order and answers a
     * property whenever a level or the document order decides it. */
    ROBO_CHECK_EQ_INT(caps.cascade, 1);

    /* 9.6 turned that reported-undecided contest between two candidates of one
     * level from different selectors into a decision: the cascade consumes all
     * three keys and holds no specificity arithmetic of its own. */
    ROBO_CHECK_EQ_INT(caps.specificity, 1);

    /* 9.7 answers the question the cascade cannot: for every property an
     * element does not declare, whether it takes the parent's answer or the
     * property's own initial value. */
    ROBO_CHECK_EQ_INT(caps.inheritance, 1);

    /* 9.8 turns that resolved style into a *computed* one — a length with a
     * unit, a percentage, a colour, a keyword — through the property's declared
     * value shape. What it does not do is resolve: `50%` stays a percentage and
     * `var()` stays a reference, which is why nothing here claims a used value. */
    ROBO_CHECK_EQ_INT(caps.computed_values, 1);

    /* 9.9 gave `color` values a real representation: the standard named set, the
     * four hex forms, `rgb()`/`rgba()` and `hsl()`/`hsla()`, and `currentColor`
     * as a contextual colour the computed style resolves. The spaces this build
     * has no representation for are deferred rather than claimed, which is why
     * this is a flag for colours and not for the whole CSS Color specification. */
    ROBO_CHECK_EQ_INT(caps.colors, 1);

    /* ...and everything past that is still 0: no default stylesheet exists, and
     * nothing loads one. */
    ROBO_CHECK_EQ_INT(caps.default_styles, 0);
    ROBO_CHECK_EQ_INT(caps.stylesheet_loading, 0);

    /* Available before the module is up: it describes the build, not the state. */
    css_fixture();
    robo_css_shutdown();
    ROBO_CHECK_EQ_INT(robo_css_get_capabilities(&caps), ROBO_OK);
    ROBO_CHECK_EQ_INT(caps.parser, 1);
    ROBO_CHECK_EQ_INT(robo_css_init(), ROBO_OK);

    robo_css_capabilities_init(&caps);
    ROBO_CHECK_EQ_INT(caps.parser, 0);
    ROBO_CHECK_EQ_INT(caps.source_model, 0);
    robo_css_capabilities_init(NULL); /* no crash, no state */
}

static void test_css_default_stylesheet_boundary(void)
{
    size_t length = 123u;
    const char *text;

    css_fixture();
    text = robo_css_default_stylesheet_text(&length);
    /* The boundary exists and this build ships nothing: NULL with length 0,
     * which a caller must read as "no runtime styles" rather than as failure. */
    ROBO_CHECK(text == NULL);
    ROBO_CHECK_EQ_INT(length, 0u);

    length = 77u;
    ROBO_CHECK(robo_css_default_stylesheet_text(&length) == NULL);
    ROBO_CHECK_EQ_INT(length, 0u);
    /* The length parameter is optional. */
    ROBO_CHECK(robo_css_default_stylesheet_text(NULL) == NULL);
}

/*
 * The record-lifetime rule, asserted rather than only documented.
 *
 * ROBO_CSS_CLEARED is what lets a by-value RoboCSS record be declared without a
 * separate init call, and the whole model depends on the cleared state being
 * exactly what the init functions produce — because every make/append/dispose
 * releases what the record already holds. If a cleared record were ever *not*
 * the same as an init'd one, every caller of ROBO_CSS_CLEARED would be releasing
 * fields nobody set. This test pins that equivalence field by field.
 */
static void test_css_records_cleared_state(void)
{
    robo_css_value_t cleared_value = ROBO_CSS_CLEARED;
    robo_css_value_t init_value = ROBO_CSS_CLEARED;
    robo_css_token_t cleared_token = ROBO_CSS_CLEARED;
    robo_css_source_t cleared_source = ROBO_CSS_CLEARED;
    robo_css_source_t init_source = ROBO_CSS_CLEARED;
    robo_css_declaration_t cleared_declaration = ROBO_CSS_CLEARED;
    robo_css_declaration_t init_declaration = ROBO_CSS_CLEARED;
    robo_css_simple_selector_t cleared_selector = ROBO_CSS_CLEARED;
    robo_css_compound_selector_t cleared_compound = ROBO_CSS_CLEARED;
    robo_css_complex_selector_t cleared_complex = ROBO_CSS_CLEARED;
    robo_css_selector_list_t cleared_list = ROBO_CSS_CLEARED;
    robo_css_rule_t cleared_rule = ROBO_CSS_CLEARED;
    robo_css_rule_list_t cleared_rules = ROBO_CSS_CLEARED;
    robo_css_error_t cleared_error = ROBO_CSS_CLEARED;
    robo_css_parser_t cleared_parser = ROBO_CSS_CLEARED;

    css_fixture();

    robo_css_value_init(&init_value);
    ROBO_CHECK_EQ_INT(cleared_value.kind, init_value.kind);
    ROBO_CHECK_EQ_INT(cleared_value.kind, ROBO_CSS_VALUE_KIND_NONE);
    ROBO_CHECK(robo_css_value_is_empty(&cleared_value));
    ROBO_CHECK_EQ_INT(cleared_value.children.count, init_value.children.count);
    ROBO_CHECK(cleared_value.children.items == NULL);
    ROBO_CHECK_EQ_INT(cleared_value.children.capacity, 0u);
    ROBO_CHECK(robo_css_slice_is_absent(cleared_value.name));
    ROBO_CHECK(robo_css_range_is_empty(cleared_value.range));
    ROBO_CHECK_EQ_INT(robo_css_value_depth(&cleared_value), 0u);
    ROBO_CHECK_EQ_INT(robo_css_value_tree_count(&cleared_value), 0u);
    ROBO_CHECK_EQ_INT(robo_css_value_child_count(&cleared_value), 0u);
    ROBO_CHECK_EQ_INT(robo_css_value_child(&cleared_value, 0u) != NULL, 0);

    /* Every cleared record is a usable starting point, not an inert one: each
     * can be configured or released without a trip through its init function. */
    ROBO_CHECK_EQ_INT(robo_css_value_make_list(&cleared_value, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(cleared_value.kind, ROBO_CSS_VALUE_KIND_LIST);
    robo_css_value_dispose(&cleared_value);
    ROBO_CHECK(robo_css_value_is_empty(&cleared_value));
    robo_css_value_dispose(&cleared_value); /* idempotent */
    ROBO_CHECK_EQ_INT(robo_css_value_make_token(&cleared_value, &cleared_token, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(cleared_value.kind, ROBO_CSS_VALUE_KIND_TOKEN);
    ROBO_CHECK_EQ_INT(robo_css_value_token(&cleared_value).type, ROBO_CSS_TOKEN_NONE);
    robo_css_value_dispose(&cleared_value);

    ROBO_CHECK(!robo_css_source_is_configured(&cleared_source));
    robo_css_source_init(&init_source);
    ROBO_CHECK_EQ_INT(cleared_source.kind, init_source.kind);
    ROBO_CHECK_EQ_INT(cleared_source.kind, ROBO_CSS_SOURCE_KIND_NONE);
    ROBO_CHECK(robo_css_source_is_empty(&cleared_source));
    ROBO_CHECK(robo_css_source_bytes(&cleared_source) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_source_length(&cleared_source), 0u);
    ROBO_CHECK(robo_css_source_name(&cleared_source) != NULL);
    robo_css_source_dispose(&cleared_source);

    robo_css_declaration_init(&init_declaration);
    ROBO_CHECK_EQ_INT(cleared_declaration.state, init_declaration.state);
    ROBO_CHECK_EQ_INT(cleared_declaration.state, ROBO_CSS_DECLARATION_STATE_NONE);
    ROBO_CHECK(!robo_css_declaration_is_usable(&cleared_declaration));
    ROBO_CHECK(!robo_css_declaration_has_value(&cleared_declaration));
    ROBO_CHECK(robo_css_value_is_empty(&cleared_declaration.value));
    robo_css_declaration_dispose(&cleared_declaration);

    ROBO_CHECK(!robo_css_simple_selector_is_set(&cleared_selector));
    ROBO_CHECK_EQ_INT(cleared_selector.kind, ROBO_CSS_SIMPLE_SELECTOR_NONE);
    ROBO_CHECK_EQ_INT(robo_css_compound_selector_count(&cleared_compound), 0u);
    ROBO_CHECK_EQ_INT(robo_css_complex_selector_compound_count(&cleared_complex), 0u);
    ROBO_CHECK_EQ_INT(robo_css_selector_list_count(&cleared_list), 0u);
    ROBO_CHECK(!robo_css_selector_list_is_well_formed(&cleared_list));

    ROBO_CHECK_EQ_INT(cleared_rule.kind, ROBO_CSS_RULE_KIND_NONE);
    ROBO_CHECK_EQ_INT(robo_css_rule_list_count(&cleared_rules), 0u);

    ROBO_CHECK(!robo_css_error_is_error(&cleared_error));
    ROBO_CHECK_EQ_INT(cleared_error.kind, ROBO_CSS_ERROR_KIND_NONE);
    ROBO_CHECK_STR_EQ(robo_css_error_message(&cleared_error), "");

    ROBO_CHECK(robo_css_parser_source(&cleared_parser) == NULL);
    ROBO_CHECK_EQ_INT(robo_css_parser_offset(&cleared_parser), 0u);
    ROBO_CHECK(!robo_css_parser_has_error(&cleared_parser));

    /* Release is safe on every cleared record, including twice. */
    robo_css_value_dispose(&cleared_value);
    robo_css_value_clear_children(&cleared_value);
    robo_css_source_dispose(&cleared_source);
    robo_css_declaration_dispose(&cleared_declaration);
    robo_css_simple_selector_dispose(&cleared_selector);
    robo_css_compound_selector_dispose(&cleared_compound);
    robo_css_complex_selector_dispose(&cleared_complex);
    robo_css_selector_list_dispose(&cleared_list);
    robo_css_rule_dispose(&cleared_rule);
    robo_css_rule_list_dispose(&cleared_rules);
    robo_css_parser_dispose(&cleared_parser);
    robo_css_error_init(&cleared_error);
}

void robo_test_register_css(void);

void robo_test_register_css(void)
{
    robo_test_register("css.version", test_css_version);
    robo_test_register("css.module.init_shutdown", test_css_module_init_shutdown);
    robo_test_register("css.module.shutdown_releases_stylesheets",
                       test_css_module_shutdown_releases_stylesheets);
    robo_test_register("css.module.requires_init", test_css_stylesheet_requires_module);
    robo_test_register("css.module.slot_table_bounded", test_css_slot_table_is_bounded);
    robo_test_register("css.types.slices", test_css_types_slices);
    robo_test_register("css.types.split_whitespace", test_css_types_split_whitespace);
    robo_test_register("css.types.positions_and_ranges",
                       test_css_types_positions_and_ranges);
    robo_test_register("css.types.vocabulary", test_css_types_vocabulary);
    robo_test_register("css.error.vocabulary", test_css_error_vocabulary);
    robo_test_register("css.diag.record", test_css_diag_record);
    robo_test_register("css.capabilities", test_css_capabilities);
    robo_test_register("css.default_stylesheet.boundary",
                       test_css_default_stylesheet_boundary);
    robo_test_register("css.records.cleared_state", test_css_records_cleared_state);
}
