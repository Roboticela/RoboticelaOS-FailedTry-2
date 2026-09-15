/*
 * RoboHTML Phase 8.2 tokenizer states suite:
 * explicit state model, state names/classification, the controlled
 * raw-content switch hook (RAWTEXT/RCDATA/SCRIPT/PLAINTEXT), and
 * end-tag matching inside raw content.
 */

#include "RoboBase/RoboBase.h"
#include "RoboHTML/RoboHTML.h"
#include "RoboHTML/RoboHTMLSource.h"
#include "RoboHTML/RoboHTMLToken.h"
#include "RoboHTML/RoboHTMLTokenizer.h"
#include "RoboHTML/RoboHTMLTokenizerStates.h"
#include "RoboTest.h"

#include <string.h>

static void st_fixture_begin(void)
{
    if (!robo_base_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_base_init(), ROBO_OK);
    }
    if (!robo_html_is_initialized()) {
        ROBO_CHECK_EQ_INT(robo_html_init(), ROBO_OK);
    }
}

static void st_fixture_end(void)
{
    robo_html_shutdown();
}

static robo_result_t st_from_string(const char *text,
                                    robo_html_tokenizer_t **out)
{
    robo_html_source_t src;
    robo_html_source_init(&src);
    ROBO_CHECK_EQ_INT(robo_html_source_string(&src, text, NULL), ROBO_OK);
    return robo_html_tokenizer_create(&src, out, NULL);
}

/* ------------------------------------------------------------------ */
/* State model helpers                                                 */
/* ------------------------------------------------------------------ */

static void test_st_model_helpers(void)
{
    cr_begin:

    st_fixture_begin();

    /* Boundary values. */
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_state_is_known(
                          ROBO_HTML_TOKENIZER_STATE_DATA),
                      1);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_state_is_known(
                          ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_DOUBLE_ESCAPED_END_TAG_NAME),
                      1);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_state_is_known(
                          (robo_html_tokenizer_state_t)99),
                      0);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_state_is_known(
                          (robo_html_tokenizer_state_t)-1),
                      0);

    /* Names are non-NULL and stable for boundary states. */
    ROBO_CHECK(robo_html_tokenizer_state_name(ROBO_HTML_TOKENIZER_STATE_DATA) !=
               NULL);
    ROBO_CHECK(robo_html_tokenizer_state_name((robo_html_tokenizer_state_t)77) !=
               NULL);

    /* Raw-content classification. */
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_state_is_raw_content(
                          ROBO_HTML_TOKENIZER_STATE_DATA),
                      0);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_state_is_raw_content(
                          ROBO_HTML_TOKENIZER_STATE_TAG_OPEN),
                      0);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_state_is_raw_content(
                          ROBO_HTML_TOKENIZER_STATE_PLAINTEXT),
                      1);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_state_is_raw_content(
                          ROBO_HTML_TOKENIZER_STATE_RAWTEXT),
                      1);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_state_is_raw_content(
                          ROBO_HTML_TOKENIZER_STATE_RCDATA),
                      1);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_state_is_raw_content(
                          ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA),
                      1);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_state_is_raw_content(
                          ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA_ESCAPED),
                      1);

    /* Switch validation accepts entry points, refuses others. */
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_state_switch_validate(
                          ROBO_HTML_TOKENIZER_STATE_DATA),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_state_switch_validate(
                          ROBO_HTML_TOKENIZER_STATE_RCDATA),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_state_switch_validate(
                          ROBO_HTML_TOKENIZER_STATE_RAWTEXT),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_state_switch_validate(
                          ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_state_switch_validate(
                          ROBO_HTML_TOKENIZER_STATE_PLAINTEXT),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_state_switch_validate(
                          ROBO_HTML_TOKENIZER_STATE_TAG_OPEN),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_state_switch_validate(
                          (robo_html_tokenizer_state_t)99),
                      ROBO_INVALID_ARGUMENT);

    goto cr_begin;

    st_fixture_end();
    (void)0;
}

static void test_st_switch_misuse(void)
{
    robo_html_tokenizer_t *t = NULL;

    st_fixture_begin();
    ROBO_CHECK_EQ_INT(st_from_string("x", &t), ROBO_OK);

    /* NULL tokenizer refused. */
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_switch_state(
                          NULL, ROBO_HTML_TOKENIZER_STATE_RAWTEXT, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Non-entry-point states refused; state unchanged. */
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_switch_state(
                          t, ROBO_HTML_TOKENIZER_STATE_COMMENT, NULL),
                      ROBO_INVALID_ARGUMENT);
    {
        robo_html_tokenizer_state_t s;
        ROBO_CHECK_EQ_INT(robo_html_tokenizer_get_state(t, &s), ROBO_OK);
        ROBO_CHECK_EQ_INT(s, ROBO_HTML_TOKENIZER_STATE_DATA);
    }

    /* Valid switch changes the observable state. */
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_switch_state(
                          t, ROBO_HTML_TOKENIZER_STATE_PLAINTEXT, NULL),
                      ROBO_OK);
    {
        robo_html_tokenizer_state_t s;
        ROBO_CHECK_EQ_INT(robo_html_tokenizer_get_state(t, &s), ROBO_OK);
        ROBO_CHECK_EQ_INT(s, ROBO_HTML_TOKENIZER_STATE_PLAINTEXT);
    }

    /* Switching back to DATA is allowed. */
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_switch_state(
                          t, ROBO_HTML_TOKENIZER_STATE_DATA, NULL),
                      ROBO_OK);

    robo_html_tokenizer_destroy(t);
    st_fixture_end();
}

/* ------------------------------------------------------------------ */
/* PLAINTEXT: everything to EOF is text, no markup.                    */
/* ------------------------------------------------------------------ */

static void test_st_plaintext(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;
    char out[64];
    size_t len = 0u;

    st_fixture_begin();
    ROBO_CHECK_EQ_INT(st_from_string("<b>a <div> &amp; b", &t), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_switch_state(
                          t, ROBO_HTML_TOKENIZER_STATE_PLAINTEXT, NULL),
                      ROBO_OK);

    for (;;) {
        ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
        if (tok.kind == ROBO_HTML_TOKEN_EOF) {
            break;
        }
        ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
        ROBO_CHECK(len + tok.u.characters.length < sizeof(out));
        memcpy(out + len, tok.u.characters.data, tok.u.characters.length);
        len += tok.u.characters.length;
    }
    out[len] = '\0';
    /* Markup is NOT interpreted; refs are NOT decoded in plaintext. */
    ROBO_CHECK_STR_EQ(out, "<b>a <div> &amp; b");

    robo_html_tokenizer_destroy(t);
    st_fixture_end();
}

/* ------------------------------------------------------------------ */
/* RAWTEXT: no reference decoding, only the matching end tag closes.   */
/* ------------------------------------------------------------------ */

static void test_st_rawtext(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;

    st_fixture_begin();
    ROBO_CHECK_EQ_INT(st_from_string("<s>x &amp; <i>y</i> tail", &t), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_switch_state(
                          t, ROBO_HTML_TOKENIZER_STATE_RAWTEXT, NULL),
                      ROBO_OK);

    /* Text run up to the matching end tag. */
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    /* '&' stays literal in RAWTEXT. */
    ROBO_CHECK_STR_EQ(tok.u.characters.data, "x &amp; <i>y");

    /* Matching end tag recognized despite nested markup. */
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_END_TAG);
    ROBO_CHECK_STR_EQ(tok.u.tag.name, "s");

    /* Tail text, then EOF. */
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    ROBO_CHECK_STR_EQ(tok.u.characters.data, " tail");
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);

    robo_html_tokenizer_destroy(t);
    st_fixture_end();
}

/* ------------------------------------------------------------------ */
/* RAWTEXT: wrong end tag does NOT close.                              */
/* ------------------------------------------------------------------ */

static void test_st_rawtext_wrong_end(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;

    st_fixture_begin();
    ROBO_CHECK_EQ_INT(st_from_string("a</div>b</style>", &t), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_switch_state(
                          t, ROBO_HTML_TOKENIZER_STATE_RAWTEXT, NULL),
                      ROBO_OK);

    /* `</div>` is plain text for a style-rawtext context. */
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    ROBO_CHECK_STR_EQ(tok.u.characters.data, "a</div>b");

    /* The real matching end tag closes rawtext. */
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_END_TAG);
    ROBO_CHECK_STR_EQ(tok.u.tag.name, "style");

    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);

    robo_html_tokenizer_destroy(t);
    st_fixture_end();
}

/* ------------------------------------------------------------------ */
/* RCDATA: references decode, only matching end tag closes.            */
/* ------------------------------------------------------------------ */

static void test_st_rcdata(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;

    st_fixture_begin();
    ROBO_CHECK_EQ_INT(st_from_string("A &amp; <em>B</em> &lt;", &t), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_switch_state(
                          t, ROBO_HTML_TOKENIZER_STATE_RCDATA, NULL),
                      ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    ROBO_CHECK_STR_EQ(tok.u.characters.data, "A & <em>B");

    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_END_TAG);
    ROBO_CHECK_STR_EQ(tok.u.tag.name, "em");

    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    ROBO_CHECK_STR_EQ(tok.u.characters.data, " <");

    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);

    robo_html_tokenizer_destroy(t);
    st_fixture_end();
}

/* ------------------------------------------------------------------ */
/* SCRIPT DATA: raw like RAWTEXT; comments-in-script tolerated.        */
/* ------------------------------------------------------------------ */

static void test_st_script_data(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;

    st_fixture_begin();
    /* Script bodies may contain `<!--` and `<div>` without markup. */
    ROBO_CHECK_EQ_INT(st_from_string("if (a<b) {} <!-- c --> var s", &t),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_switch_state(
                          t, ROBO_HTML_TOKENIZER_STATE_SCRIPT_DATA, NULL),
                      ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    ROBO_CHECK_STR_EQ(tok.u.characters.data,
                      "if (a<b) {} <!-- c --> var s");

    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);

    robo_html_tokenizer_destroy(t);
    st_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Raw content reaching EOF without the end tag: recovers safely.      */
/* ------------------------------------------------------------------ */

static void test_st_rawtext_eof_unclosed(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;

    st_fixture_begin();
    ROBO_CHECK_EQ_INT(st_from_string("never </closed", &t), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_html_tokenizer_switch_state(
                          t, ROBO_HTML_TOKENIZER_STATE_RAWTEXT, NULL),
                      ROBO_OK);

    for (;;) {
        ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
        if (tok.kind == ROBO_HTML_TOKEN_EOF) {
            break;
        }
        ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    }
    robo_html_tokenizer_destroy(t);
    st_fixture_end();
}

/* ------------------------------------------------------------------ */
/* Switch after a start tag — the 8.3 integration shape.               */
/* ------------------------------------------------------------------ */

static void test_st_switch_after_start_tag(void)
{
    robo_html_tokenizer_t *t = NULL;
    robo_html_token_t tok;

    st_fixture_begin();
    /*
     * Simulates the future tree-builder flow: tokenize `<textarea>`,
     * then switch into RCDATA for its contents, then `</textarea>`.
     * (Element semantics stay with 8.3; this only exercises the hook.)
     */
    ROBO_CHECK_EQ_INT(st_from_string("<textarea>a &amp; b</textarea>", &t),
                      ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_START_TAG);
    ROBO_CHECK_STR_EQ(tok.u.tag.name, "textarea");

    ROBO_CHECK_EQ_INT(robo_html_tokenizer_switch_state(
                          t, ROBO_HTML_TOKENIZER_STATE_RCDATA, NULL),
                      ROBO_OK);

    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_CHARACTER);
    ROBO_CHECK_STR_EQ(tok.u.characters.data, "a & b");

    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_END_TAG);
    ROBO_CHECK_STR_EQ(tok.u.tag.name, "textarea");

    ROBO_CHECK_EQ_INT(robo_html_tokenizer_next(t, &tok, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(tok.kind, ROBO_HTML_TOKEN_EOF);

    robo_html_tokenizer_destroy(t);
    st_fixture_end();
}

void robo_test_register_html_tokenizer_states(void)
{
    robo_test_register("html.tokstate.model_helpers",
                       test_st_model_helpers);
    robo_test_register("html.tokstate.switch_misuse", test_st_switch_misuse);
    robo_test_register("html.tokstate.plaintext", test_st_plaintext);
    robo_test_register("html.tokstate.rawtext", test_st_rawtext);
    robo_test_register("html.tokstate.rawtext_wrong_end",
                       test_st_rawtext_wrong_end);
    robo_test_register("html.tokstate.rcdata", test_st_rcdata);
    robo_test_register("html.tokstate.script_data", test_st_script_data);
    robo_test_register("html.tokstate.rawtext_eof_unclosed",
                       test_st_rawtext_eof_unclosed);
    robo_test_register("html.tokstate.switch_after_start_tag",
                       test_st_switch_after_start_tag);
}
