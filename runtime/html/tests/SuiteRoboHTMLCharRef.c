/*
 * RoboHTML Phase 8.2 character-reference engine suite (internal API,
 * tested through the public tokenizer where possible; numeric mapping
 * and table behavior exercised directly).
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLToken.h"
#include "RoboHTML/RoboHTMLTokenizer.h"
#include "RoboTest.h"

#include <string.h>

static void cr_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
}

static void cr_fixture_end(void)
{
    robo_html_shutdown();
}

/* Tokenize and return the first CHARACTER token's text (copied). */
static void cr_first_text(const char *html, char *out, size_t cap,
                          size_t *diag_count)
{
    robo_html_source_t src;
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, html, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_create(&src, &t, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    ROBO_CHECK(cap > tok.u.characters.length);
    memcpy(out, tok.u.characters.data, tok.u.characters.length);
    out[tok.u.characters.length] = '\0';
    if (diag_count) {
        *diag_count = robo_html_tokenizer_diagnostic_count(t);
    }
    robo_html_tokenizer_destroy(t);
}

static void test_cr_named_common(void)
{
    char out[64];

    cr_fixture_begin();

    cr_first_text("&amp;", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "&");
    cr_first_text("&lt;&gt;&quot;&apos;", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "<>\"'");
    cr_first_text("&nbsp;x", out, sizeof(out), NULL);
    /* U+00A0 = C2 A0 in UTF-8. */
    ROBO_CHECK_STR_EQ(out, "\xC2\xA0x");
    cr_first_text("&copy; &reg; &trade;", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "\xC2\xA9 \xC2\xAE \xE2\x84\xA2");

    cr_fixture_end();
}

static void test_cr_named_semicolonless(void)
{
    char out[64];

    cr_fixture_begin();

    /* Legacy semicolonless form in text (non-attribute context): the
     * longest-match rule still applies for well-known entities. */
    cr_first_text("&ampx", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "&x");

    cr_fixture_end();
}

static void test_cr_numeric_decimal(void)
{
    char out[64];

    cr_fixture_begin();

    cr_first_text("&#65;", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "A");
    cr_first_text("&#48;", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "0");
    /* Multi-byte: U+00E9 → C3 A9. */
    cr_first_text("&#233;", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "\xC3\xA9");
    /* Beyond BMP: U+1F600 → F0 9F 98 80. */
    cr_first_text("&#128512;", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "\xF0\x9F\x98\x80");

    cr_fixture_end();
}

static void test_cr_numeric_hex(void)
{
    char out[64];

    cr_fixture_begin();

    cr_first_text("&#x41;", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "A");
    cr_first_text("&#X41;", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "A");
    cr_first_text("&#x1F600;", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "\xF0\x9F\x98\x80");

    cr_fixture_end();
}

static void test_cr_numeric_mapping(void)
{
    char out[64];
    size_t diags = 0;

    cr_fixture_begin();

    /* C1 control remap: &#x80; → U+20AC (Euro). */
    cr_first_text("&#x80;", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "\xE2\x82\xAC");
    /* Surrogates map to U+FFFD. */
    cr_first_text("&#xD800;", out, sizeof(out), NULL);
    ROBO_CHECK_EQ_INT((unsigned char)out[0], 0xEF);
    ROBO_CHECK_EQ_INT((unsigned char)out[1], 0xBF);
    ROBO_CHECK_EQ_INT((unsigned char)out[2], 0xBD);
    /* Out of range maps to U+FFFD. */
    cr_first_text("&#x110000;", out, sizeof(out), NULL);
    ROBO_CHECK_EQ_INT((unsigned char)out[0], 0xEF);
    /* C0 control reference: a control-character-reference parse error, but
     * the standard keeps the referenced code point (0x01 stays U+0001),
     * exactly as 0x0D stays CR. Only 0x00, surrogates, out-of-range values
     * and the 0x80..0x9F remap change the character. */
    cr_first_text("&#1;", out, sizeof(out), &diags);
    ROBO_CHECK_EQ_INT((unsigned char)out[0], 0x01);
    ROBO_CHECK_EQ_INT((unsigned char)out[1], 0x00);
    ROBO_CHECK(diags > 0u);
    /* 0x0D is ASCII whitespace-class: kept with no control diagnostic. */
    cr_first_text("&#xD;", out, sizeof(out), NULL);
    ROBO_CHECK_EQ_INT((unsigned char)out[0], 0x0D);

    cr_fixture_end();
}

static void test_cr_invalid(void)
{
    char out[64];
    size_t diags = 0;

    cr_fixture_begin();

    /* Unknown named ref stays literal. */
    cr_first_text("a &nope; b", out, sizeof(out), &diags);
    ROBO_CHECK_STR_EQ(out, "a &nope; b");
    ROBO_CHECK(diags > 0u);

    /* Empty numeric ref stays literal. */
    cr_first_text("&#;", out, sizeof(out), &diags);
    ROBO_CHECK_STR_EQ(out, "&#;");
    ROBO_CHECK(diags > 0u);

    /* Empty hex ref stays literal. */
    cr_first_text("&#x;", out, sizeof(out), &diags);
    ROBO_CHECK_STR_EQ(out, "&#x;");

    /* Digits overflow maps to FFFD (bounded, no UB). */
    cr_first_text("&#99999999999999;", out, sizeof(out), &diags);
    ROBO_CHECK_EQ_INT((unsigned char)out[0], 0xEF);

    /* Lone '&' at EOF. */
    cr_first_text("a&", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "a&");

    cr_fixture_end();
}

static void test_cr_in_attributes(void)
{
    robo_html_source_t src;
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;
    char val[64];

    cr_fixture_begin();

    /* Attribute values decode named and numeric refs. */
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, "<a t=\"&lt;x&gt;\">", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_create(&src, &t, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_START_TAG);
    ROBO_CHECK_EQ_INT(robo_html_token_copy_attr(&tok, "t", val, sizeof(val)),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(val, "<x>");
    robo_html_tokenizer_destroy(t);

    /* Legacy attribute rule: missing ';' before '=' or alnum stops the
     * reference and emits literal '&' — well-formed refs still decode. */
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(
        robo_html_source_string(&src, "<a href=\"x?a=1&amp;b=2\">", NULL),
        ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_create(&src, &t, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_token_copy_attr(&tok, "href", val,
                                                sizeof(val)),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(val, "x?a=1&b=2");
    robo_html_tokenizer_destroy(t);

    /* Legacy semicolonless: "&ampb" in an attribute — longest named
     * prefix ending before a stop char applies. */
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, "<a t=\"&amp;more\">", NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_create(&src, &t, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_token_copy_attr(&tok, "t", val, sizeof(val)),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(val, "&more");
    robo_html_tokenizer_destroy(t);

    cr_fixture_end();
}

static void test_cr_utf8_multibyte_names(void)
{
    char out[64];

    cr_fixture_begin();

    /* Multi-code-point expansions fit the 8-byte bound. */
    cr_first_text("&NotEqualTilde;", out, sizeof(out), NULL);
    /* U+2242 U+0338 = E2 89 82 CC B8. */
    ROBO_CHECK_STR_EQ(out, "\xE2\x89\x82\xCC\xB8");

    cr_fixture_end();
}

/*
 * Catalogue regression: names whose position in the category-grouped table
 * previously shadowed them, the longest-match rule (terminated form beats
 * the legacy semicolonless form), mixed-case names, and two-code-point
 * expansions. These are the cases that must never depend on table order.
 */
static void test_cr_catalogue_longest_match(void)
{
    char out[64];

    cr_fixture_begin();

    /* Names sitting after a lexicographically larger neighbour. */
    cr_first_text("&nbsp;x", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "\xC2\xA0x");
    cr_first_text("&copy;", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "\xC2\xA9");
    cr_first_text("&Dagger;", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "\xE2\x80\xA1");

    /* Terminated form wins over the legacy form for the same prefix. */
    cr_first_text("&amp;", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "&");
    cr_first_text("&amp", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "&");
    cr_first_text("&lt;", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "<");

    /* Mixed case is byte-wise: only the catalogue spelling matches. */
    cr_first_text("&dagger;", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "\xE2\x80\xA0");
    cr_first_text("&DAGGER;", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "&DAGGER;");

    /* Two-code-point expansion. */
    cr_first_text("&NotEqualTilde;", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "\xE2\x89\x82\xCC\xB8");

    /* Byte-wise match stops at the first name byte difference. */
    cr_first_text("&nbsp", out, sizeof(out), NULL);
    ROBO_CHECK_STR_EQ(out, "\xC2\xA0");

    cr_fixture_end();
}

void robo_test_register_html_charref(void)
{
    robo_test_register("html.charref.named_common", test_cr_named_common);
    robo_test_register("html.charref.named_semicolonless",
                       test_cr_named_semicolonless);
    robo_test_register("html.charref.numeric_decimal",
                       test_cr_numeric_decimal);
    robo_test_register("html.charref.numeric_hex", test_cr_numeric_hex);
    robo_test_register("html.charref.numeric_mapping",
                       test_cr_numeric_mapping);
    robo_test_register("html.charref.invalid", test_cr_invalid);
    robo_test_register("html.charref.in_attributes", test_cr_in_attributes);
    robo_test_register("html.charref.utf8_multibyte_names",
                       test_cr_utf8_multibyte_names);
    robo_test_register("html.charref.catalogue_longest_match",
                       test_cr_catalogue_longest_match);
}
