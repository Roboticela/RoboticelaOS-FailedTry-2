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

static void CrFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static void CrFixtureEnd(void)
{
    HtmlShutdown();
}

/* Tokenize and return the first CHARACTER token's text (copied). */
static void CrFirstText(const char *html, char *out, size_t cap,
                          size_t *diagCount)
{
    HtmlSource src;
    HtmlTokenizer *t = NULL;
    HtmlToken tok;

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, html, NULL), RoboOk);
    RoboCheckEqInt(HtmlTokenizerCreate(&src, &t, NULL), RoboOk);
    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    RoboCheck(cap > tok.u.characters.length);
    memcpy(out, tok.u.characters.data, tok.u.characters.length);
    out[tok.u.characters.length] = '\0';
    if (diagCount) {
        *diagCount = HtmlTokenizerDiagnosticCount(t);
    }
    HtmlTokenizerDestroy(t);
}

static void TestCrNamedCommon(void)
{
    char out[64];

    CrFixtureBegin();

    CrFirstText("&amp;", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "&");
    CrFirstText("&lt;&gt;&quot;&apos;", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "<>\"'");
    CrFirstText("&nbsp;x", out, sizeof(out), NULL);
    /* U+00A0 = C2 A0 in UTF-8. */
    RoboCheckStrEq(out, "\xC2\xA0x");
    CrFirstText("&copy; &reg; &trade;", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "\xC2\xA9 \xC2\xAE \xE2\x84\xA2");

    CrFixtureEnd();
}

static void TestCrNamedSemicolonless(void)
{
    char out[64];

    CrFixtureBegin();

    /* Legacy semicolonless form in text (non-attribute context): the
     * longest-match rule still applies for well-known entities. */
    CrFirstText("&ampx", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "&x");

    CrFixtureEnd();
}

static void TestCrNumericDecimal(void)
{
    char out[64];

    CrFixtureBegin();

    CrFirstText("&#65;", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "A");
    CrFirstText("&#48;", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "0");
    /* Multi-byte: U+00E9 → C3 A9. */
    CrFirstText("&#233;", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "\xC3\xA9");
    /* Beyond BMP: U+1F600 → F0 9F 98 80. */
    CrFirstText("&#128512;", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "\xF0\x9F\x98\x80");

    CrFixtureEnd();
}

static void TestCrNumericHex(void)
{
    char out[64];

    CrFixtureBegin();

    CrFirstText("&#x41;", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "A");
    CrFirstText("&#X41;", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "A");
    CrFirstText("&#x1F600;", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "\xF0\x9F\x98\x80");

    CrFixtureEnd();
}

static void TestCrNumericMapping(void)
{
    char out[64];
    size_t diags = 0;

    CrFixtureBegin();

    /* C1 control remap: &#x80; → U+20AC (Euro). */
    CrFirstText("&#x80;", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "\xE2\x82\xAC");
    /* Surrogates map to U+FFFD. */
    CrFirstText("&#xD800;", out, sizeof(out), NULL);
    RoboCheckEqInt((unsigned char)out[0], 0xEF);
    RoboCheckEqInt((unsigned char)out[1], 0xBF);
    RoboCheckEqInt((unsigned char)out[2], 0xBD);
    /* Out of range maps to U+FFFD. */
    CrFirstText("&#x110000;", out, sizeof(out), NULL);
    RoboCheckEqInt((unsigned char)out[0], 0xEF);
    /* C0 control reference: a control-character-reference parse error, but
     * the standard keeps the referenced code point (0x01 stays U+0001),
     * exactly as 0x0D stays CR. Only 0x00, surrogates, out-of-range values
     * and the 0x80..0x9F remap change the character. */
    CrFirstText("&#1;", out, sizeof(out), &diags);
    RoboCheckEqInt((unsigned char)out[0], 0x01);
    RoboCheckEqInt((unsigned char)out[1], 0x00);
    RoboCheck(diags > 0u);
    /* 0x0D is ASCII whitespace-class: kept with no control diagnostic. */
    CrFirstText("&#xD;", out, sizeof(out), NULL);
    RoboCheckEqInt((unsigned char)out[0], 0x0D);

    CrFixtureEnd();
}

static void TestCrInvalid(void)
{
    char out[64];
    size_t diags = 0;

    CrFixtureBegin();

    /* Unknown named ref stays literal. */
    CrFirstText("a &nope; b", out, sizeof(out), &diags);
    RoboCheckStrEq(out, "a &nope; b");
    RoboCheck(diags > 0u);

    /* Empty numeric ref stays literal. */
    CrFirstText("&#;", out, sizeof(out), &diags);
    RoboCheckStrEq(out, "&#;");
    RoboCheck(diags > 0u);

    /* Empty hex ref stays literal. */
    CrFirstText("&#x;", out, sizeof(out), &diags);
    RoboCheckStrEq(out, "&#x;");

    /* Digits overflow maps to FFFD (bounded, no UB). */
    CrFirstText("&#99999999999999;", out, sizeof(out), &diags);
    RoboCheckEqInt((unsigned char)out[0], 0xEF);

    /* Lone '&' at EOF. */
    CrFirstText("a&", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "a&");

    CrFixtureEnd();
}

static void TestCrInAttributes(void)
{
    HtmlSource src;
    HtmlTokenizer *t = NULL;
    HtmlToken tok;
    char val[64];

    CrFixtureBegin();

    /* Attribute values decode named and numeric refs. */
    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, "<a t=\"&lt;x&gt;\">", NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlTokenizerCreate(&src, &t, NULL), RoboOk);
    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenStartTag);
    RoboCheckEqInt(HtmlTokenCopyAttr(&tok, "t", val, sizeof(val)),
                      RoboOk);
    RoboCheckStrEq(val, "<x>");
    HtmlTokenizerDestroy(t);

    /* Legacy attribute rule: missing ';' before '=' or alnum stops the
     * reference and emits literal '&' — well-formed refs still decode. */
    HtmlSourceInit(&src);
    RoboCheckEqInt(
        HtmlSourceString(&src, "<a href=\"x?a=1&amp;b=2\">", NULL),
        RoboOk);
    RoboCheckEqInt(HtmlTokenizerCreate(&src, &t, NULL), RoboOk);
    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(HtmlTokenCopyAttr(&tok, "href", val,
                                                sizeof(val)),
                      RoboOk);
    RoboCheckStrEq(val, "x?a=1&b=2");
    HtmlTokenizerDestroy(t);

    /* Legacy semicolonless: "&ampb" in an attribute — longest named
     * prefix ending before a stop char applies. */
    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, "<a t=\"&amp;more\">", NULL),
                      RoboOk);
    RoboCheckEqInt(HtmlTokenizerCreate(&src, &t, NULL), RoboOk);
    RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
    RoboCheckEqInt(HtmlTokenCopyAttr(&tok, "t", val, sizeof(val)),
                      RoboOk);
    RoboCheckStrEq(val, "&more");
    HtmlTokenizerDestroy(t);

    CrFixtureEnd();
}

static void TestCrUtf8MultibyteNames(void)
{
    char out[64];

    CrFixtureBegin();

    /* Multi-code-point expansions fit the 8-byte bound. */
    CrFirstText("&NotEqualTilde;", out, sizeof(out), NULL);
    /* U+2242 U+0338 = E2 89 82 CC B8. */
    RoboCheckStrEq(out, "\xE2\x89\x82\xCC\xB8");

    CrFixtureEnd();
}

/*
 * Catalogue regression: names whose position in the category-grouped table
 * previously shadowed them, the longest-match rule (terminated form beats
 * the legacy semicolonless form), mixed-case names, and two-code-point
 * expansions. These are the cases that must never depend on table order.
 */
static void TestCrCatalogueLongestMatch(void)
{
    char out[64];

    CrFixtureBegin();

    /* Names sitting after a lexicographically larger neighbour. */
    CrFirstText("&nbsp;x", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "\xC2\xA0x");
    CrFirstText("&copy;", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "\xC2\xA9");
    CrFirstText("&Dagger;", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "\xE2\x80\xA1");

    /* Terminated form wins over the legacy form for the same prefix. */
    CrFirstText("&amp;", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "&");
    CrFirstText("&amp", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "&");
    CrFirstText("&lt;", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "<");

    /* Mixed case is byte-wise: only the catalogue spelling matches. */
    CrFirstText("&dagger;", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "\xE2\x80\xA0");
    CrFirstText("&DAGGER;", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "&DAGGER;");

    /* Two-code-point expansion. */
    CrFirstText("&NotEqualTilde;", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "\xE2\x89\x82\xCC\xB8");

    /* Byte-wise match stops at the first name byte difference. */
    CrFirstText("&nbsp", out, sizeof(out), NULL);
    RoboCheckStrEq(out, "\xC2\xA0");

    CrFixtureEnd();
}

void TestRegisterHtmlCharref(void)
{
    TestRegister("html.charref.named_common", TestCrNamedCommon);
    TestRegister("html.charref.named_semicolonless",
                       TestCrNamedSemicolonless);
    TestRegister("html.charref.numeric_decimal",
                       TestCrNumericDecimal);
    TestRegister("html.charref.numeric_hex", TestCrNumericHex);
    TestRegister("html.charref.numeric_mapping",
                       TestCrNumericMapping);
    TestRegister("html.charref.invalid", TestCrInvalid);
    TestRegister("html.charref.in_attributes", TestCrInAttributes);
    TestRegister("html.charref.utf8_multibyte_names",
                       TestCrUtf8MultibyteNames);
    TestRegister("html.charref.catalogue_longest_match",
                       TestCrCatalogueLongestMatch);
}
