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

static void StFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static void StFixtureEnd(void)
{
    HtmlShutdown();
}

static Result StFromString(const char *text,
                                    HtmlTokenizer **out)
{
    HtmlSource src;
    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    return HtmlTokenizerCreate(&src, out, NULL);
}

/* ------------------------------------------------------------------ */
/* State model helpers                                                 */
/* ------------------------------------------------------------------ */

static void TestStModelHelpers(void)
{
    StFixtureBegin();

    /* Boundary values. */
    RoboCheckEqInt(HtmlTokenizerStateIsKnown(
                          RoboHtmlTokenizerStateData),
                      1);
    RoboCheckEqInt(HtmlTokenizerStateIsKnown(
                          RoboHtmlTokenizerStateScriptDataDoubleEscapedEndTagName),
                      1);
    RoboCheckEqInt(HtmlTokenizerStateIsKnown(
                          (HtmlTokenizerState)99),
                      0);
    RoboCheckEqInt(HtmlTokenizerStateIsKnown(
                          (HtmlTokenizerState)-1),
                      0);

    /* Names are non-NULL and stable for boundary states. */
    RoboCheck(HtmlTokenizerStateName(RoboHtmlTokenizerStateData) !=
               NULL);
    RoboCheck(HtmlTokenizerStateName((HtmlTokenizerState)77) !=
               NULL);

    /* Raw-content classification. */
    RoboCheckEqInt(HtmlTokenizerStateIsRawContent(
                          RoboHtmlTokenizerStateData),
                      0);
    RoboCheckEqInt(HtmlTokenizerStateIsRawContent(
                          RoboHtmlTokenizerStateTagOpen),
                      0);
    RoboCheckEqInt(HtmlTokenizerStateIsRawContent(
                          RoboHtmlTokenizerStatePlaintext),
                      1);
    RoboCheckEqInt(HtmlTokenizerStateIsRawContent(
                          RoboHtmlTokenizerStateRawtext),
                      1);
    RoboCheckEqInt(HtmlTokenizerStateIsRawContent(
                          RoboHtmlTokenizerStateRcdata),
                      1);
    RoboCheckEqInt(HtmlTokenizerStateIsRawContent(
                          RoboHtmlTokenizerStateScriptData),
                      1);
    RoboCheckEqInt(HtmlTokenizerStateIsRawContent(
                          RoboHtmlTokenizerStateScriptDataEscaped),
                      1);

    /* Switch validation accepts entry points, refuses others. */
    RoboCheckEqInt(HtmlTokenizerStateSwitchValidate(
                          RoboHtmlTokenizerStateData),
                      RoboOk);
    RoboCheckEqInt(HtmlTokenizerStateSwitchValidate(
                          RoboHtmlTokenizerStateRcdata),
                      RoboOk);
    RoboCheckEqInt(HtmlTokenizerStateSwitchValidate(
                          RoboHtmlTokenizerStateRawtext),
                      RoboOk);
    RoboCheckEqInt(HtmlTokenizerStateSwitchValidate(
                          RoboHtmlTokenizerStateScriptData),
                      RoboOk);
    RoboCheckEqInt(HtmlTokenizerStateSwitchValidate(
                          RoboHtmlTokenizerStatePlaintext),
                      RoboOk);
    RoboCheckEqInt(HtmlTokenizerStateSwitchValidate(
                          RoboHtmlTokenizerStateTagOpen),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlTokenizerStateSwitchValidate(
                          (HtmlTokenizerState)99),
                      RoboInvalidArgument);

    StFixtureEnd();
}

static void TestStSwitchMisuse(void)
{
    HtmlTokenizer *t = NULL;

    StFixtureBegin();
    RoboCheckEqInt(StFromString("x", &t), RoboOk);

    /* NULL tokenizer refused. */
    RoboCheckEqInt(HtmlTokenizerSwitchState(
                          NULL, RoboHtmlTokenizerStateRawtext, NULL),
                      RoboInvalidArgument);

    /* Non-entry-point states refused; state unchanged. */
    RoboCheckEqInt(HtmlTokenizerSwitchState(
                          t, RoboHtmlTokenizerStateComment, NULL),
                      RoboInvalidArgument);
    {
        HtmlTokenizerState s;
        RoboCheckEqInt(HtmlTokenizerGetState(t, &s), RoboOk);
        RoboCheckEqInt(s, RoboHtmlTokenizerStateData);
    }

    /* Valid switch changes the observable state. */
    RoboCheckEqInt(HtmlTokenizerSwitchState(
                          t, RoboHtmlTokenizerStatePlaintext, NULL),
                      RoboOk);
    {
        HtmlTokenizerState s;
        RoboCheckEqInt(HtmlTokenizerGetState(t, &s), RoboOk);
        RoboCheckEqInt(s, RoboHtmlTokenizerStatePlaintext);
    }

    /* Switching back to DATA is allowed. */
    RoboCheckEqInt(HtmlTokenizerSwitchState(
                          t, RoboHtmlTokenizerStateData, NULL),
                      RoboOk);

    HtmlTokenizerDestroy(t);
    StFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* PLAINTEXT: everything to EOF is text, no markup.                    */
/* ------------------------------------------------------------------ */

static void TestStPlaintext(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;
    char out[64];
    size_t len = 0u;

    StFixtureBegin();
    RoboCheckEqInt(StFromString("<b>a <div> &amp; b", &t), RoboOk);
    RoboCheckEqInt(HtmlTokenizerSwitchState(
                          t, RoboHtmlTokenizerStatePlaintext, NULL),
                      RoboOk);

    for (;;) {
        RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
        if (tok.kind == RoboHtmlTokenEof) {
            break;
        }
        RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
        RoboCheck(len + tok.u.characters.length < sizeof(out));
        memcpy(out + len, tok.u.characters.data, tok.u.characters.length);
        len += tok.u.characters.length;
    }
    out[len] = '\0';
    /* Markup is NOT interpreted; refs are NOT decoded in plaintext. */
    RoboCheckStrEq(out, "<b>a <div> &amp; b");

    HtmlTokenizerDestroy(t);
    StFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* RAWTEXT: no reference decoding, only the matching end tag closes.   */
/* ------------------------------------------------------------------ */

static void TestStRawtext(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;

    StFixtureBegin();
    /*
     * The leading <s> teaches the region its end-tag name; only </s>
     * closes it. Nested-looking markup (</i>) stays literal text, exactly
     * as a real <style>/<xmp> body would.
     */
    RoboCheckEqInt(StFromString("<s>x &amp; <i>y</i> tail</s>", &t),
                      RoboOk);
    RoboCheckEqInt(HtmlTokenizerSwitchState(
                          t, RoboHtmlTokenizerStateRawtext, NULL),
                      RoboOk);

    /* One text run up to the matching end tag. */
    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    /* '&' and nested markup stay literal in RAWTEXT. */
    RoboCheckStrEq(tok.u.characters.data, "x &amp; <i>y</i> tail");

    /* Matching end tag recognized despite nested markup. */
    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEndTag);
    RoboCheckStrEq(tok.u.tag.name, "s");

    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);

    HtmlTokenizerDestroy(t);
    StFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* RAWTEXT: wrong end tag does NOT close.                              */
/* ------------------------------------------------------------------ */

static void TestStRawtextWrongEnd(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;

    StFixtureBegin();
    RoboCheckEqInt(StFromString("a</div>b</style>", &t), RoboOk);
    RoboCheckEqInt(HtmlTokenizerSwitchState(
                          t, RoboHtmlTokenizerStateRawtext, NULL),
                      RoboOk);

    /* `</div>` is plain text for a style-rawtext context. */
    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    RoboCheckStrEq(tok.u.characters.data, "a</div>b");

    /* The real matching end tag closes rawtext. */
    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEndTag);
    RoboCheckStrEq(tok.u.tag.name, "style");

    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);

    HtmlTokenizerDestroy(t);
    StFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* RCDATA: references decode, only matching end tag closes.            */
/* ------------------------------------------------------------------ */

static void TestStRcdata(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;

    StFixtureBegin();
    RoboCheckEqInt(StFromString("A &amp; <em>B</em> &lt;", &t), RoboOk);
    RoboCheckEqInt(HtmlTokenizerSwitchState(
                          t, RoboHtmlTokenizerStateRcdata, NULL),
                      RoboOk);

    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    RoboCheckStrEq(tok.u.characters.data, "A & <em>B");

    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEndTag);
    RoboCheckStrEq(tok.u.tag.name, "em");

    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    RoboCheckStrEq(tok.u.characters.data, " <");

    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);

    HtmlTokenizerDestroy(t);
    StFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* SCRIPT DATA: raw like RAWTEXT; comments-in-script tolerated.        */
/* ------------------------------------------------------------------ */

static void TestStScriptData(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;

    StFixtureBegin();
    /* Script bodies may contain `<!--` and `<div>` without markup. */
    RoboCheckEqInt(StFromString("if (a<b) {} <!-- c --> var s", &t),
                      RoboOk);
    RoboCheckEqInt(HtmlTokenizerSwitchState(
                          t, RoboHtmlTokenizerStateScriptData, NULL),
                      RoboOk);

    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    RoboCheckStrEq(tok.u.characters.data,
                      "if (a<b) {} <!-- c --> var s");

    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);

    HtmlTokenizerDestroy(t);
    StFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Raw content reaching EOF without the end tag: recovers safely.      */
/* ------------------------------------------------------------------ */

static void TestStRawtextEofUnclosed(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;

    StFixtureBegin();
    RoboCheckEqInt(StFromString("never </closed", &t), RoboOk);
    RoboCheckEqInt(HtmlTokenizerSwitchState(
                          t, RoboHtmlTokenizerStateRawtext, NULL),
                      RoboOk);

    for (;;) {
        RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
        if (tok.kind == RoboHtmlTokenEof) {
            break;
        }
        RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    }
    HtmlTokenizerDestroy(t);
    StFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Switch after a start tag — the 8.3 integration shape.               */
/* ------------------------------------------------------------------ */

static void TestStSwitchAfterStartTag(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;

    StFixtureBegin();
    /*
     * Simulates the future tree-builder flow: tokenize `<textarea>`,
     * then switch into RCDATA for its contents, then `</textarea>`.
     * (Element semantics stay with 8.3; this only exercises the hook.)
     */
    RoboCheckEqInt(StFromString("<textarea>a &amp; b</textarea>", &t),
                      RoboOk);

    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenStartTag);
    RoboCheckStrEq(tok.u.tag.name, "textarea");

    RoboCheckEqInt(HtmlTokenizerSwitchState(
                          t, RoboHtmlTokenizerStateRcdata, NULL),
                      RoboOk);

    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    RoboCheckStrEq(tok.u.characters.data, "a & b");

    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEndTag);
    RoboCheckStrEq(tok.u.tag.name, "textarea");

    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);

    HtmlTokenizerDestroy(t);
    StFixtureEnd();
}

void TestRegisterHtmlTokenizerStates(void)
{
    TestRegister("html.tokstate.model_helpers",
                       TestStModelHelpers);
    TestRegister("html.tokstate.switch_misuse", TestStSwitchMisuse);
    TestRegister("html.tokstate.plaintext", TestStPlaintext);
    TestRegister("html.tokstate.rawtext", TestStRawtext);
    TestRegister("html.tokstate.rawtext_wrong_end",
                       TestStRawtextWrongEnd);
    TestRegister("html.tokstate.rcdata", TestStRcdata);
    TestRegister("html.tokstate.script_data", TestStScriptData);
    TestRegister("html.tokstate.rawtext_eof_unclosed",
                       TestStRawtextEofUnclosed);
    TestRegister("html.tokstate.switch_after_start_tag",
                       TestStSwitchAfterStartTag);
}
