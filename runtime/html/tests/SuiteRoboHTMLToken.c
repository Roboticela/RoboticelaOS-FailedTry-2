/*
 * RoboHTML Phase 8.2 tokenizer suite:
 * token kinds, tags, attributes, comments, doctypes, positions,
 * malformed input recovery, API misuse, ownership/lifetime.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLToken.h"
#include "RoboHTML/RoboHTMLTokenizer.h"
#include "RoboTest.h"

#include <string.h>

static void tok_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
}

static void tok_fixture_end(void)
{
    robo_html_shutdown();
}

/* Helper: create a tokenizer over a C string (borrowed). */
static robo_result_t tok_from_string(const char *text,
                                     robo_html_tokenizer_t **out)
{
    robo_html_source_t src;
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, text, NULL), ROBO_OK);
    return robo_html_tokenizer_create(&src, out, NULL);
}

/* Helper: next() and expect ROBO_OK. */
static void tok_next_ok(robo_html_tokenizer_t *t, robo_html_token_t *tok)
{
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, tok, NULL), ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Creation / API misuse                                               */
/* ------------------------------------------------------------------ */

static void test_tok_create_misuse(void)
{
    robo_html_source_t src;
    robo_html_tokenizer_t *t = NULL;

    tok_fixture_begin();

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, "<p>", NULL), ROBO_OK);

    /* NULL out / NULL source / NONE-kind source refused. */
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_create(&src, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_create(NULL, &t, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(t, 0);

    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_create(&src, &t, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(t, 0);

    /* next() with NULL args refused. */
    ROBO_CHECK_EQ_INT(tok_from_string("x", &t), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(NULL, &(robo_html_token_t){0},
                                               NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    robo_html_tokenizer_destroy(t);
}

static void test_tok_empty_input(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;

    tok_fixture_begin();
    ROBO_CHECK_EQ_INT(tok_from_string("", &t), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_at_eof(t), 0);

    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_at_eof(t), 1);

    /* Repeated EOF reads are stable. */
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);

    robo_html_tokenizer_destroy(t);
    tok_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */

static void test_tok_plain_text(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;

    tok_fixture_begin();
    ROBO_CHECK_EQ_INT(tok_from_string("Hello", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    ROBO_CHECK_STR_EQ(tok.u.characters.data, "Hello");
    ROBO_CHECK_EQ_INT(tok.u.characters.length, 5u);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);
    robo_html_tokenizer_destroy(t);
    tok_fixture_end();
}

static void test_tok_text_runs(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;

    tok_fixture_begin();
    /* Tags split character runs into separate tokens. */
    ROBO_CHECK_EQ_INT(tok_from_string("a<div>b</div>c", &t), ROBO_OK);

    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    ROBO_CHECK_STR_EQ(tok.u.characters.data, "a");

    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_START_TAG);
    ROBO_CHECK_STR_EQ(tok.u.tag.name, "div");

    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    ROBO_CHECK_STR_EQ(tok.u.characters.data, "b");

    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_END_TAG);
    ROBO_CHECK_STR_EQ(tok.u.tag.name, "div");

    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    ROBO_CHECK_STR_EQ(tok.u.characters.data, "c");

    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);
    robo_html_tokenizer_destroy(t);
    tok_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Tags and attributes                                                 */
/* ------------------------------------------------------------------ */

static void test_tok_start_end_tags(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;

    tok_fixture_begin();
    ROBO_CHECK_EQ_INT(tok_from_string("<html></html>", &t), ROBO_OK);

    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_START_TAG);
    ROBO_CHECK_STR_EQ(tok.u.tag.name, "html"); /* lowercased */
    ROBO_CHECK_EQ_INT(tok.u.tag.attr_count, 0u);
    ROBO_CHECK_EQ_INT(tok.u.tag.self_closing, 0);

    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_END_TAG);
    ROBO_CHECK_STR_EQ(tok.u.tag.name, "html");

    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);
    robo_html_tokenizer_destroy(t);
    tok_fixture_end();
}

static void test_tok_attributes(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;
    char value[64];

    tok_fixture_begin();

    /* Double-quoted, single-quoted, unquoted, boolean, empty. */
    ROBO_CHECK_EQ_INT(tok_from_string("<div class=\"test\" id='a b' lang=en "
                                      "hidden data-x=\"\">",
                                      &t),
                      ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_START_TAG);
    ROBO_CHECK_EQ_INT(tok.u.tag.attr_count, 5u);
    ROBO_CHECK_STR_EQ(tok.u.tag.attrs[0].name, "class");
    ROBO_CHECK_STR_EQ(tok.u.tag.attrs[0].value, "test");
    ROBO_CHECK_STR_EQ(tok.u.tag.attrs[1].name, "id");
    ROBO_CHECK_STR_EQ(tok.u.tag.attrs[1].value, "a b");
    ROBO_CHECK_STR_EQ(tok.u.tag.attrs[2].name, "lang");
    ROBO_CHECK_STR_EQ(tok.u.tag.attrs[2].value, "en");
    ROBO_CHECK_STR_EQ(tok.u.tag.attrs[3].name, "hidden");
    ROBO_CHECK_STR_EQ(tok.u.tag.attrs[3].value, "");
    ROBO_CHECK_STR_EQ(tok.u.tag.attrs[4].name, "data-x");
    ROBO_CHECK_STR_EQ(tok.u.tag.attrs[4].value, "");

    /* copy_attr by (lowercased) name; unknown → NOT_FOUND. */
    ROBO_CHECK_EQ_INT(robo_html_token_copy_attr(&tok, "CLASS", value,
                                                sizeof(value)),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(value, "test");
    ROBO_CHECK_EQ_INT(robo_html_token_copy_attr(&tok, "missing", value,
                                                sizeof(value)),
                      ROBO_NOT_FOUND);

    /* Whitespace around attributes. */
    robo_html_tokenizer_destroy(t);
    ROBO_CHECK_EQ_INT(tok_from_string("<p  a = \"1\"  b='2' >", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_START_TAG);
    ROBO_CHECK_EQ_INT(tok.u.tag.attr_count, 2u);
    ROBO_CHECK_STR_EQ(tok.u.tag.attrs[0].value, "1");
    ROBO_CHECK_STR_EQ(tok.u.tag.attrs[1].value, "2");

    /* Over-long value is refused, never truncated. */
    ROBO_CHECK_EQ_INT(robo_html_token_copy_attr(&tok, "a", value, 1u),
                      ROBO_NOT_SUPPORTED);

    robo_html_tokenizer_destroy(t);
    tok_fixture_end();
}

static void test_tok_self_closing(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;

    tok_fixture_begin();
    ROBO_CHECK_EQ_INT(tok_from_string("<img src=test.png/>", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_START_TAG);
    ROBO_CHECK_STR_EQ(tok.u.tag.name, "img");
    ROBO_CHECK_EQ_INT(tok.u.tag.self_closing, 1);
    ROBO_CHECK_EQ_INT(tok.u.tag.attr_count, 1u);
    ROBO_CHECK_STR_EQ(tok.u.tag.attrs[0].name, "src");
    /* Unquoted value stops at '/', which is part of syntax. */
    ROBO_CHECK_STR_EQ(tok.u.tag.attrs[0].value, "test.png");

    robo_html_tokenizer_destroy(t);
    ROBO_CHECK_EQ_INT(tok_from_string("<br />", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_START_TAG);
    ROBO_CHECK_EQ_INT(tok.u.tag.self_closing, 1);
    ROBO_CHECK_STR_EQ(tok.u.tag.name, "br");

    robo_html_tokenizer_destroy(t);
    tok_fixture_end();
}

static void test_tok_duplicate_attrs(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;

    tok_fixture_begin();
    /* Duplicate attribute names are kept in the token (DOM drops dupes
     * later — that is tree-builder work). */
    ROBO_CHECK_EQ_INT(tok_from_string("<a href=\"1\" href=\"2\">", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_START_TAG);
    ROBO_CHECK_EQ_INT(tok.u.tag.attr_count, 2u);
    ROBO_CHECK_STR_EQ(tok.u.tag.attrs[0].value, "1");
    ROBO_CHECK_STR_EQ(tok.u.tag.attrs[1].value, "2");
    robo_html_tokenizer_destroy(t);
    tok_fixture_end();
}

static void test_tok_malformed_attrs(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;
    int diags = 0;

    tok_fixture_begin();

    /* Unterminated tag: tokenizer recovers, emits what it has. */
    ROBO_CHECK_EQ_INT(tok_from_string("<div", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);
    /* Malformed input produced a diagnostic. */
    diags = (int)robo_html_tokenizer_diagnostic_count(t);
    ROBO_CHECK(diags > 0);
    robo_html_tokenizer_destroy(t);

    /* `a=` at EOF → empty value attribute. */
    ROBO_CHECK_EQ_INT(tok_from_string("<div a=", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    /* Recovered tag carries the attribute (value "") or is dropped at
     * EOF; either way the stream terminates with EOF without crashing. */
    while (1) {
        tok_next_ok(t, &tok);
        if (tok.kind == ROBO_HTML_TOKEN_EOF) {
            break;
        }
    }
    robo_html_tokenizer_destroy(t);

    /* Unterminated quote → recovers to EOF. */
    ROBO_CHECK_EQ_INT(tok_from_string("<div a=\"", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    while (1) {
        tok_next_ok(t, &tok);
        if (tok.kind == ROBO_HTML_TOKEN_EOF) {
            break;
        }
    }
    ROBO_CHECK(robo_html_tokenizer_diagnostic_count(t) > 0u);
    robo_html_tokenizer_destroy(t);

    /* `<` alone → literal text. */
    ROBO_CHECK_EQ_INT(tok_from_string("<", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    ROBO_CHECK_STR_EQ(tok.u.characters.data, "<");
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);
    robo_html_tokenizer_destroy(t);

    /* `</` alone → bogus comment recovery. */
    ROBO_CHECK_EQ_INT(tok_from_string("</>", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    /* First token is either a bogus comment or end tag; must not crash
     * and stream must reach EOF. */
    while (1) {
        tok_next_ok(t, &tok);
        if (tok.kind == ROBO_HTML_TOKEN_EOF) {
            break;
        }
    }
    robo_html_tokenizer_destroy(t);
    tok_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Comments                                                            */
/* ------------------------------------------------------------------ */

static void test_tok_comments(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;

    tok_fixture_begin();

    ROBO_CHECK_EQ_INT(tok_from_string("<!-- hello -->", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_COMMENT);
    ROBO_CHECK_STR_EQ(tok.u.comment.data, " hello ");
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);

    robo_html_tokenizer_destroy(t);

    /* Empty comment. */
    ROBO_CHECK_EQ_INT(tok_from_string("<!---->", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_COMMENT);
    ROBO_CHECK_STR_EQ(tok.u.comment.data, "");
    robo_html_tokenizer_destroy(t);

    /* EOF inside a comment: recovers, emits the partial comment. */
    ROBO_CHECK_EQ_INT(tok_from_string("<!-- never closed", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_COMMENT);
    ROBO_CHECK_STR_EQ(tok.u.comment.data, " never closed");
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);
    ROBO_CHECK(robo_html_tokenizer_diagnostic_count(t) > 0u);
    robo_html_tokenizer_destroy(t);

    /* `<!-->` — abrupt-closing malformed comment, recovered. */
    ROBO_CHECK_EQ_INT(tok_from_string("<!-->", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_COMMENT);
    robo_html_tokenizer_destroy(t);

    tok_fixture_end();
}

/* ------------------------------------------------------------------ */
/* DOCTYPE                                                             */
/* ------------------------------------------------------------------ */

static void test_tok_doctype(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;

    tok_fixture_begin();

    ROBO_CHECK_EQ_INT(tok_from_string("<!DOCTYPE html>", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_DOCTYPE);
    ROBO_CHECK_STR_EQ(tok.u.doctype.name, "html");
    ROBO_CHECK_EQ_INT(tok.u.doctype.public_id == NULL, 1);
    ROBO_CHECK_EQ_INT(tok.u.doctype.system_id == NULL, 1);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);

    robo_html_tokenizer_destroy(t);

    /* Case-insensitive keyword. */
    ROBO_CHECK_EQ_INT(tok_from_string("<!doctype HTML>", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_DOCTYPE);
    ROBO_CHECK_STR_EQ(tok.u.doctype.name, "html");
    robo_html_tokenizer_destroy(t);

    /* Public + system identifiers. */
    ROBO_CHECK_EQ_INT(
        tok_from_string("<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01//EN\" "
                        "\"http://www.w3.org/TR/html4/strict.dtd\">",
                        &t),
        ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_DOCTYPE);
    ROBO_CHECK_STR_EQ(tok.u.doctype.public_id, "-//W3C//DTD HTML 4.01//EN");
    ROBO_CHECK_STR_EQ(tok.u.doctype.system_id,
                      "http://www.w3.org/TR/html4/strict.dtd");
    robo_html_tokenizer_destroy(t);

    /* Malformed: missing '>' → EOF recovery with diagnostic. */
    ROBO_CHECK_EQ_INT(tok_from_string("<!DOCTYPE", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_DOCTYPE);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);
    ROBO_CHECK(robo_html_tokenizer_diagnostic_count(t) > 0u);
    robo_html_tokenizer_destroy(t);

    tok_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Character references (tokenizer-level integration)                  */
/* ------------------------------------------------------------------ */

static void test_tok_charrefs(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;

    tok_fixture_begin();

    ROBO_CHECK_EQ_INT(tok_from_string("Tom &amp; Jerry", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    ROBO_CHECK_STR_EQ(tok.u.characters.data, "Tom & Jerry");
    robo_html_tokenizer_destroy(t);

    ROBO_CHECK_EQ_INT(tok_from_string("&#65;&#x41;", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    ROBO_CHECK_STR_EQ(tok.u.characters.data, "AA");
    robo_html_tokenizer_destroy(t);

    /* Attribute values decode references. */
    ROBO_CHECK_EQ_INT(tok_from_string("<a title=\"a&amp;b\">", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_START_TAG);
    ROBO_CHECK_STR_EQ(tok.u.tag.attrs[0].value, "a&b");
    robo_html_tokenizer_destroy(t);

    /* Invalid reference stays literal '&' + diagnostic. */
    ROBO_CHECK_EQ_INT(tok_from_string("a &nope; b", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    ROBO_CHECK_STR_EQ(tok.u.characters.data, "a &nope; b");
    ROBO_CHECK(robo_html_tokenizer_diagnostic_count(t) > 0u);
    robo_html_tokenizer_destroy(t);

    tok_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Special characters / Unicode / CR / NUL                             */
/* ------------------------------------------------------------------ */

static void test_tok_special_chars(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;

    tok_fixture_begin();

    /* CR and CRLF normalized to LF. */
    ROBO_CHECK_EQ_INT(tok_from_string("a\r\nb\rc", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    ROBO_CHECK_STR_EQ(tok.u.characters.data, "a\nb\nc");
    robo_html_tokenizer_destroy(t);

    /* NUL → U+FFFD replacement + diagnostic. */
    {
        robo_html_source_t s2;
        robo_html_source_init(&s2);
        ROBO_CHECK_EQ_INT(robo_html_source_memory(&s2, "a\0b", 3u, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_tokenizer_create(&s2, &t, NULL), ROBO_OK);
        tok_next_ok(t, &tok);
        ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
        ROBO_CHECK_EQ_INT(tok.u.characters.length, 5u); /* a + FFFD(3) + b */
        ROBO_CHECK_EQ_INT((unsigned char)tok.u.characters.data[1], 0xEF);
        ROBO_CHECK_EQ_INT((unsigned char)tok.u.characters.data[2], 0xBF);
        ROBO_CHECK_EQ_INT((unsigned char)tok.u.characters.data[3], 0xBD);
        robo_html_tokenizer_destroy(t);
    }

    /* Unicode text preserved (UTF-8 passthrough). */
    ROBO_CHECK_EQ_INT(tok_from_string("\xC3\xA9\xE2\x82\xAC", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    ROBO_CHECK_STR_EQ(tok.u.characters.data, "\xC3\xA9\xE2\x82\xAC");
    ROBO_CHECK_EQ_INT(tok.u.characters.length, 5u);
    robo_html_tokenizer_destroy(t);

    /* Ill-formed UTF-8 → replacement, never out-of-bounds. */
    {
        robo_html_source_t s3;
        robo_html_source_init(&s3);
        ROBO_CHECK_EQ_INT(robo_html_source_memory(&s3, "\xFF\xFE", 2u, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_tokenizer_create(&s3, &t, NULL), ROBO_OK);
        tok_next_ok(t, &tok);
        ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
        ROBO_CHECK_EQ_INT(tok.u.characters.length, 6u); /* 2 × U+FFFD */
        robo_html_tokenizer_destroy(t);
    }

    tok_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Positions / mixed input                                             */
/* ------------------------------------------------------------------ */

static void test_tok_positions(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;

    tok_fixture_begin();
    ROBO_CHECK_EQ_INT(tok_from_string("<b>x</b>", &t), ROBO_OK);

    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_START_TAG);
    ROBO_CHECK_EQ_INT(tok.pos.offset, 0u);
    ROBO_CHECK_EQ_INT(tok.pos.line, 1u);
    ROBO_CHECK_EQ_INT(tok.pos.column, 1u);

    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    ROBO_CHECK_EQ_INT(tok.pos.offset, 3u);

    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_END_TAG);
    ROBO_CHECK_EQ_INT(tok.pos.offset, 4u);

    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);

    robo_html_tokenizer_destroy(t);

    /* Line tracking across a newline. */
    ROBO_CHECK_EQ_INT(tok_from_string("a\n<b>", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_START_TAG);
    ROBO_CHECK_EQ_INT(tok.pos.line, 2u);
    ROBO_CHECK_EQ_INT(tok.pos.column, 1u);
    robo_html_tokenizer_destroy(t);

    tok_fixture_end();
}

static void test_tok_mixed_input(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;
    int counts[ROBO_HTML_TOKEN_EOF + 1u];

    tok_fixture_begin();
    memset(counts, 0, sizeof(counts));
    ROBO_CHECK_EQ_INT(tok_from_string(
                          "<!DOCTYPE html><!-- c --><html><body><p>Hi</p>"
                          "<br/><span x=1>done</span></body></html>",
                          &t),
                      ROBO_OK);
    for (;;) {
        tok_next_ok(t, &tok);
        ROBO_CHECK(tok.kind >= ROBO_HTML_TOKEN_NONE);
        ROBO_CHECK(tok.kind <= ROBO_HTML_TOKEN_EOF);
        counts[tok.kind]++;
        if (tok.kind == ROBO_HTML_TOKEN_EOF) {
            break;
        }
    }
    ROBO_CHECK_EQ_INT(counts[ROBO_HTML_TOKEN_DOCTYPE], 1);
    ROBO_CHECK_EQ_INT(counts[ROBO_HTML_TOKEN_COMMENT], 1);
    /* 7 start tags: html body p br span + closing body/html do not add. */
    ROBO_CHECK_EQ_INT(counts[ROBO_HTML_TOKEN_START_TAG], 5);
    ROBO_CHECK_EQ_INT(counts[ROBO_HTML_TOKEN_END_TAG], 5);
    ROBO_CHECK(counts[ROBO_HTML_TOKEN_CHARACTER] >= 2);
    robo_html_tokenizer_destroy(t);
    tok_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Long input / reset / ownership                                      */
/* ------------------------------------------------------------------ */

static void test_tok_long_input(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;
    char *big;
    size_t i;
    size_t tag_count = 0u;

    tok_fixture_begin();
    big = (char *)robo_malloc(20000u);
    ROBO_CHECK(big != NULL);
    for (i = 0u; i < 20000u;) {
        big[i++] = '<';
        big[i++] = 'p';
        big[i++] = '>';
        big[i++] = 'x';
        big[i++] = '<';
        big[i++] = '/';
        big[i++] = 'p';
        big[i++] = '>';
    }
    ROBO_CHECK_EQ_INT(tok_from_string(big, &t), ROBO_OK);
    for (;;) {
        tok_next_ok(t, &tok);
        if (tok.kind == ROBO_HTML_TOKEN_START_TAG ||
            tok.kind == ROBO_HTML_TOKEN_END_TAG) {
            tag_count++;
        }
        if (tok.kind == ROBO_HTML_TOKEN_EOF) {
            break;
        }
    }
    ROBO_CHECK_EQ_INT(tag_count, 2500u * 2u);
    robo_html_tokenizer_destroy(t);
    robo_free(big);
    tok_fixture_end();
}

static void test_tok_reset_reuse(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;
    robo_html_source_t src;

    tok_fixture_begin();
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, "<p>hi", NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_create(&src, &t, NULL), ROBO_OK);

    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_START_TAG);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);

    /* Reset to the start; same stream again. */
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_reset(t, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_at_eof(t), 0);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_START_TAG);
    ROBO_CHECK_STR_EQ(tok.u.tag.name, "p");

    robo_html_tokenizer_destroy(t);
    tok_fixture_end();
}

static void test_tok_token_copy_tag_name(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;
    char name[16];

    tok_fixture_begin();
    ROBO_CHECK_EQ_INT(tok_from_string("<DIV A=\"v\">", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    ROBO_CHECK_EQ_INT(robo_html_token_copy_tag_name(&tok, name,
                                                    sizeof(name)),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(name, "div");
    /* Too-small capacity refused, not truncated. */
    ROBO_CHECK_EQ_INT(robo_html_token_copy_tag_name(&tok, name, 2u),
                      ROBO_NOT_SUPPORTED);
    /* NULL handling. */
    ROBO_CHECK_EQ_INT(robo_html_token_copy_tag_name(NULL, name, sizeof(name)),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_token_copy_tag_name(&tok, NULL, sizeof(name)),
                      ROBO_INVALID_ARGUMENT);
    robo_html_tokenizer_destroy(t);
    tok_fixture_end();
}

static void test_tok_state_query(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;
    robo_html_tokenizer_state_t st;

    tok_fixture_begin();
    ROBO_CHECK_EQ_INT(tok_from_string("<p>a</p>", &t), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_get_state(t, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st, ROBO_HTML_TOKENIZER_STATE_DATA);

    tok_next_ok(t, &tok); /* <p> */
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_get_state(t, &st), ROBO_OK);
    ROBO_CHECK_EQ_INT(st, ROBO_HTML_TOKENIZER_STATE_DATA);
    tok_next_ok(t, &tok); /* a */
    tok_next_ok(t, &tok); /* </p> */
    tok_next_ok(t, &tok); /* EOF */
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_at_eof(t), 1);

    /* NULL args. */
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_get_state(NULL, &st),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_reset(NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_html_tokenizer_destroy(t);
    tok_fixture_end();
}

static void test_tok_diagnostics_api(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;
    robo_html_diag_t d;

    tok_fixture_begin();
    ROBO_CHECK_EQ_INT(tok_from_string("<div a=\"", &t), ROBO_OK);
    tok_next_ok(t, &tok);
    while (1) {
        tok_next_ok(t, &tok);
        if (tok.kind == ROBO_HTML_TOKEN_EOF) {
            break;
        }
    }
    ROBO_CHECK(robo_html_tokenizer_diagnostic_count(t) > 0u);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_get_diagnostic(t, 0u, &d), ROBO_OK);
    ROBO_CHECK_EQ_INT(d.code, ROBO_PARSE_ERROR);
    /* Out-of-range index → NOT_FOUND. */
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_get_diagnostic(
                          t, robo_html_tokenizer_diagnostic_count(t), &d),
                      ROBO_NOT_FOUND);
    robo_html_tokenizer_destroy(t);
    tok_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Suite registration                                                  */
/* ------------------------------------------------------------------ */

void robo_test_register_html_token(void)
{
    robo_test_register("html.token.create_misuse", test_tok_create_misuse);
    robo_test_register("html.token.empty_input", test_tok_empty_input);
    robo_test_register("html.token.plain_text", test_tok_plain_text);
    robo_test_register("html.token.text_runs", test_tok_text_runs);
    robo_test_register("html.token.start_end_tags", test_tok_start_end_tags);
    robo_test_register("html.token.attributes", test_tok_attributes);
    robo_test_register("html.token.self_closing", test_tok_self_closing);
    robo_test_register("html.token.duplicate_attrs", test_tok_duplicate_attrs);
    robo_test_register("html.token.malformed_attrs", test_tok_malformed_attrs);
    robo_test_register("html.token.comments", test_tok_comments);
    robo_test_register("html.token.doctype", test_tok_doctype);
    robo_test_register("html.token.charrefs", test_tok_charrefs);
    robo_test_register("html.token.special_chars", test_tok_special_chars);
    robo_test_register("html.token.positions", test_tok_positions);
    robo_test_register("html.token.mixed_input", test_tok_mixed_input);
    robo_test_register("html.token.long_input", test_tok_long_input);
    robo_test_register("html.token.reset_reuse", test_tok_reset_reuse);
    robo_test_register("html.token.copy_tag_name", test_tok_token_copy_tag_name);
    robo_test_register("html.token.state_query", test_tok_state_query);
    robo_test_register("html.token.diagnostics_api", test_tok_diagnostics_api);
}
