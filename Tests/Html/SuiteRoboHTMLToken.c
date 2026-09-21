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
#include "RoboMem/RoboMem.h"
#include "RoboTest.h"

#include <stdint.h>
#include <string.h>

static void TokFixtureBegin(void)
{
    if (!BaseIsInitialized()) {
        RoboCheckEqInt(BaseInit(), RoboOk);
    }
    if (!HtmlIsInitialized()) {
        RoboCheckEqInt(HtmlInit(), RoboOk);
    }
}

static void TokFixtureEnd(void)
{
    HtmlShutdown();
}

/* Helper: create a tokenizer over a C string (borrowed). */
static Result TokFromString(const char *text,
                                     HtmlTokenizer **out)
{
    HtmlSource src;
    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, text, NULL), RoboOk);
    return HtmlTokenizerCreate(&src, out, NULL);
}

/* Helper: next() and expect RoboOk. */
static void TokNextOk(HtmlTokenizer *t, HtmlToken *tok)
{
    RoboCheckEqInt(HtmlTokenizerNext(t, tok, NULL), RoboOk);
}

/* ------------------------------------------------------------------ */
/* Creation / API misuse                                               */
/* ------------------------------------------------------------------ */

static void TestTokCreateMisuse(void)
{
    HtmlSource src;
    HtmlTokenizer *t = NULL;

    TokFixtureBegin();

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, "<p>", NULL), RoboOk);

    /* NULL out / NULL source / NONE-kind source refused. */
    RoboCheckEqInt(HtmlTokenizerCreate(&src, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlTokenizerCreate(NULL, &t, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(t, 0);

    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlTokenizerCreate(&src, &t, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(t, 0);

    /* next() with NULL args refused. */
    RoboCheckEqInt(TokFromString("x", &t), RoboOk);
    RoboCheckEqInt(HtmlTokenizerNext(NULL, &(HtmlToken){0},
                                               NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlTokenizerNext(t, NULL, NULL),
                      RoboInvalidArgument);
    HtmlTokenizerDestroy(t);
}

static void TestTokEmptyInput(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;

    TokFixtureBegin();
    RoboCheckEqInt(TokFromString("", &t), RoboOk);
    RoboCheckEqInt(HtmlTokenizerAtEof(t), 0);

    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);
    RoboCheckEqInt(HtmlTokenizerAtEof(t), 1);

    /* Repeated EOF reads are stable. */
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);

    HtmlTokenizerDestroy(t);
    TokFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */

static void TestTokPlainText(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;

    TokFixtureBegin();
    RoboCheckEqInt(TokFromString("Hello", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    RoboCheckStrEq(tok.u.characters.data, "Hello");
    RoboCheckEqInt(tok.u.characters.length, 5u);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);
    HtmlTokenizerDestroy(t);
    TokFixtureEnd();
}

static void TestTokTextRuns(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;

    TokFixtureBegin();
    /* Tags split character runs into separate tokens. */
    RoboCheckEqInt(TokFromString("a<div>b</div>c", &t), RoboOk);

    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    RoboCheckStrEq(tok.u.characters.data, "a");

    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenStartTag);
    RoboCheckStrEq(tok.u.tag.name, "div");

    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    RoboCheckStrEq(tok.u.characters.data, "b");

    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEndTag);
    RoboCheckStrEq(tok.u.tag.name, "div");

    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    RoboCheckStrEq(tok.u.characters.data, "c");

    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);
    HtmlTokenizerDestroy(t);
    TokFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Tags and attributes                                                 */
/* ------------------------------------------------------------------ */

static void TestTokStartEndTags(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;

    TokFixtureBegin();
    RoboCheckEqInt(TokFromString("<html></html>", &t), RoboOk);

    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenStartTag);
    RoboCheckStrEq(tok.u.tag.name, "html"); /* lowercased */
    RoboCheckEqInt(tok.u.tag.attrCount, 0u);
    RoboCheckEqInt(tok.u.tag.selfClosing, 0);

    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEndTag);
    RoboCheckStrEq(tok.u.tag.name, "html");

    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);
    HtmlTokenizerDestroy(t);
    TokFixtureEnd();
}

static void TestTokAttributes(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;
    char value[64];

    TokFixtureBegin();

    /* Double-quoted, single-quoted, unquoted, boolean, empty. */
    RoboCheckEqInt(TokFromString("<div class=\"test\" id='a b' lang=en "
                                      "hidden data-x=\"\">",
                                      &t),
                      RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenStartTag);
    RoboCheckEqInt(tok.u.tag.attrCount, 5u);
    RoboCheckStrEq(tok.u.tag.attrs[0].name, "class");
    RoboCheckStrEq(tok.u.tag.attrs[0].value, "test");
    RoboCheckStrEq(tok.u.tag.attrs[1].name, "id");
    RoboCheckStrEq(tok.u.tag.attrs[1].value, "a b");
    RoboCheckStrEq(tok.u.tag.attrs[2].name, "lang");
    RoboCheckStrEq(tok.u.tag.attrs[2].value, "en");
    RoboCheckStrEq(tok.u.tag.attrs[3].name, "hidden");
    RoboCheckStrEq(tok.u.tag.attrs[3].value, "");
    RoboCheckStrEq(tok.u.tag.attrs[4].name, "data-x");
    RoboCheckStrEq(tok.u.tag.attrs[4].value, "");

    /* copy_attr by (lowercased) name; unknown → NOT_FOUND. */
    RoboCheckEqInt(HtmlTokenCopyAttr(&tok, "CLASS", value,
                                                sizeof(value)),
                      RoboOk);
    RoboCheckStrEq(value, "test");
    RoboCheckEqInt(HtmlTokenCopyAttr(&tok, "missing", value,
                                                sizeof(value)),
                      RoboNotFound);

    /* Whitespace around attributes. */
    HtmlTokenizerDestroy(t);
    RoboCheckEqInt(TokFromString("<p  a = \"1\"  b='2' >", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenStartTag);
    RoboCheckEqInt(tok.u.tag.attrCount, 2u);
    RoboCheckStrEq(tok.u.tag.attrs[0].value, "1");
    RoboCheckStrEq(tok.u.tag.attrs[1].value, "2");

    /* Over-long value is refused, never truncated. */
    RoboCheckEqInt(HtmlTokenCopyAttr(&tok, "a", value, 1u),
                      RoboNotSupported);

    HtmlTokenizerDestroy(t);
    TokFixtureEnd();
}

static void TestTokSelfClosing(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;

    TokFixtureBegin();
    RoboCheckEqInt(TokFromString("<img src=test.png/>", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenStartTag);
    RoboCheckStrEq(tok.u.tag.name, "img");
    RoboCheckEqInt(tok.u.tag.selfClosing, 1);
    RoboCheckEqInt(tok.u.tag.attrCount, 1u);
    RoboCheckStrEq(tok.u.tag.attrs[0].name, "src");
    /* Unquoted value stops at '/', which is part of syntax. */
    RoboCheckStrEq(tok.u.tag.attrs[0].value, "test.png");

    HtmlTokenizerDestroy(t);
    RoboCheckEqInt(TokFromString("<br />", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenStartTag);
    RoboCheckEqInt(tok.u.tag.selfClosing, 1);
    RoboCheckStrEq(tok.u.tag.name, "br");

    HtmlTokenizerDestroy(t);
    TokFixtureEnd();
}

static void TestTokDuplicateAttrs(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;

    TokFixtureBegin();
    /* Duplicate attribute names are kept in the token (DOM drops dupes
     * later — that is tree-builder work). */
    RoboCheckEqInt(TokFromString("<a href=\"1\" href=\"2\">", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenStartTag);
    RoboCheckEqInt(tok.u.tag.attrCount, 2u);
    RoboCheckStrEq(tok.u.tag.attrs[0].value, "1");
    RoboCheckStrEq(tok.u.tag.attrs[1].value, "2");
    HtmlTokenizerDestroy(t);
    TokFixtureEnd();
}

static void TestTokMalformedAttrs(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;
    int diags = 0;

    TokFixtureBegin();

    /* Unterminated tag: tokenizer recovers, emits what it has. */
    RoboCheckEqInt(TokFromString("<div", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);
    /* Malformed input produced a diagnostic. */
    diags = (int)HtmlTokenizerDiagnosticCount(t);
    RoboCheck(diags > 0);
    HtmlTokenizerDestroy(t);

    /* `a=` at EOF → empty value attribute. */
    RoboCheckEqInt(TokFromString("<div a=", &t), RoboOk);
    TokNextOk(t, &tok);
    /* Recovered tag carries the attribute (value "") or is dropped at
     * EOF; either way the stream terminates with EOF without crashing. */
    while (1) {
        TokNextOk(t, &tok);
        if (tok.kind == RoboHtmlTokenEof) {
            break;
        }
    }
    HtmlTokenizerDestroy(t);

    /* Unterminated quote → recovers to EOF. */
    RoboCheckEqInt(TokFromString("<div a=\"", &t), RoboOk);
    TokNextOk(t, &tok);
    while (1) {
        TokNextOk(t, &tok);
        if (tok.kind == RoboHtmlTokenEof) {
            break;
        }
    }
    RoboCheck(HtmlTokenizerDiagnosticCount(t) > 0u);
    HtmlTokenizerDestroy(t);

    /* `<` alone → literal text. */
    RoboCheckEqInt(TokFromString("<", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    RoboCheckStrEq(tok.u.characters.data, "<");
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);
    HtmlTokenizerDestroy(t);

    /* `</` alone → bogus comment recovery. */
    RoboCheckEqInt(TokFromString("</>", &t), RoboOk);
    TokNextOk(t, &tok);
    /* First token is either a bogus comment or end tag; must not crash
     * and stream must reach EOF. */
    while (1) {
        TokNextOk(t, &tok);
        if (tok.kind == RoboHtmlTokenEof) {
            break;
        }
    }
    HtmlTokenizerDestroy(t);
    TokFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Comments                                                            */
/* ------------------------------------------------------------------ */

static void TestTokComments(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;

    TokFixtureBegin();

    RoboCheckEqInt(TokFromString("<!-- hello -->", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenComment);
    RoboCheckStrEq(tok.u.comment.data, " hello ");
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);

    HtmlTokenizerDestroy(t);

    /* Empty comment. */
    RoboCheckEqInt(TokFromString("<!---->", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenComment);
    RoboCheckStrEq(tok.u.comment.data, "");
    HtmlTokenizerDestroy(t);

    /* EOF inside a comment: recovers, emits the partial comment. */
    RoboCheckEqInt(TokFromString("<!-- never closed", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenComment);
    RoboCheckStrEq(tok.u.comment.data, " never closed");
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);
    RoboCheck(HtmlTokenizerDiagnosticCount(t) > 0u);
    HtmlTokenizerDestroy(t);

    /* `<!-->` — abrupt-closing malformed comment, recovered. */
    RoboCheckEqInt(TokFromString("<!-->", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenComment);
    HtmlTokenizerDestroy(t);

    TokFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* DOCTYPE                                                             */
/* ------------------------------------------------------------------ */

static void TestTokDoctype(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;

    TokFixtureBegin();

    RoboCheckEqInt(TokFromString("<!DOCTYPE html>", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenDoctype);
    RoboCheckStrEq(tok.u.doctype.name, "html");
    RoboCheckEqInt(tok.u.doctype.publicId == NULL, 1);
    RoboCheckEqInt(tok.u.doctype.systemId == NULL, 1);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);

    HtmlTokenizerDestroy(t);

    /* Case-insensitive keyword. */
    RoboCheckEqInt(TokFromString("<!doctype HTML>", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenDoctype);
    RoboCheckStrEq(tok.u.doctype.name, "html");
    HtmlTokenizerDestroy(t);

    /* Public + system identifiers. */
    RoboCheckEqInt(
        TokFromString("<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01//EN\" "
                        "\"http://www.w3.org/TR/html4/strict.dtd\">",
                        &t),
        RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenDoctype);
    RoboCheckStrEq(tok.u.doctype.publicId, "-//W3C//DTD HTML 4.01//EN");
    RoboCheckStrEq(tok.u.doctype.systemId,
                      "http://www.w3.org/TR/html4/strict.dtd");
    HtmlTokenizerDestroy(t);

    /* Malformed: missing '>' → EOF recovery with diagnostic. */
    RoboCheckEqInt(TokFromString("<!DOCTYPE", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenDoctype);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);
    RoboCheck(HtmlTokenizerDiagnosticCount(t) > 0u);
    HtmlTokenizerDestroy(t);

    TokFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Character references (tokenizer-level integration)                  */
/* ------------------------------------------------------------------ */

static void TestTokCharrefs(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;

    TokFixtureBegin();

    RoboCheckEqInt(TokFromString("Tom &amp; Jerry", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    RoboCheckStrEq(tok.u.characters.data, "Tom & Jerry");
    HtmlTokenizerDestroy(t);

    RoboCheckEqInt(TokFromString("&#65;&#x41;", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    RoboCheckStrEq(tok.u.characters.data, "AA");
    HtmlTokenizerDestroy(t);

    /* Attribute values decode references. */
    RoboCheckEqInt(TokFromString("<a title=\"a&amp;b\">", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenStartTag);
    RoboCheckStrEq(tok.u.tag.attrs[0].value, "a&b");
    HtmlTokenizerDestroy(t);

    /* Invalid reference stays literal '&' + diagnostic. */
    RoboCheckEqInt(TokFromString("a &nope; b", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    RoboCheckStrEq(tok.u.characters.data, "a &nope; b");
    RoboCheck(HtmlTokenizerDiagnosticCount(t) > 0u);
    HtmlTokenizerDestroy(t);

    TokFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Special characters / Unicode / CR / NUL                             */
/* ------------------------------------------------------------------ */

static void TestTokSpecialChars(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;

    TokFixtureBegin();

    /* CR and CRLF normalized to LF. */
    RoboCheckEqInt(TokFromString("a\r\nb\rc", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    RoboCheckStrEq(tok.u.characters.data, "a\nb\nc");
    HtmlTokenizerDestroy(t);

    /* NUL → U+FFFD replacement + diagnostic. */
    {
        HtmlSource s2;
        HtmlSourceInit(&s2);
        RoboCheckEqInt(HtmlSourceMemory(&s2, (const uint8_t *)"a\0b", 3u, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlTokenizerCreate(&s2, &t, NULL), RoboOk);
        TokNextOk(t, &tok);
        RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
        RoboCheckEqInt(tok.u.characters.length, 5u); /* a + FFFD(3) + b */
        RoboCheckEqInt((unsigned char)tok.u.characters.data[1], 0xEF);
        RoboCheckEqInt((unsigned char)tok.u.characters.data[2], 0xBF);
        RoboCheckEqInt((unsigned char)tok.u.characters.data[3], 0xBD);
        HtmlTokenizerDestroy(t);
    }

    /* Unicode text preserved (UTF-8 passthrough). */
    RoboCheckEqInt(TokFromString("\xC3\xA9\xE2\x82\xAC", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    RoboCheckStrEq(tok.u.characters.data, "\xC3\xA9\xE2\x82\xAC");
    RoboCheckEqInt(tok.u.characters.length, 5u);
    HtmlTokenizerDestroy(t);

    /* Ill-formed UTF-8 → replacement, never out-of-bounds. */
    {
        HtmlSource s3;
        HtmlSourceInit(&s3);
        RoboCheckEqInt(HtmlSourceMemory(&s3, (const uint8_t *)"\xFF\xFE", 2u, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlTokenizerCreate(&s3, &t, NULL), RoboOk);
        TokNextOk(t, &tok);
        RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
        RoboCheckEqInt(tok.u.characters.length, 6u); /* 2 × U+FFFD */
        HtmlTokenizerDestroy(t);
    }

    TokFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Positions / mixed input                                             */
/* ------------------------------------------------------------------ */

static void TestTokPositions(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;

    TokFixtureBegin();
    RoboCheckEqInt(TokFromString("<b>x</b>", &t), RoboOk);

    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenStartTag);
    RoboCheckEqInt(tok.pos.offset, 0u);
    RoboCheckEqInt(tok.pos.line, 1u);
    RoboCheckEqInt(tok.pos.column, 1u);

    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    RoboCheckEqInt(tok.pos.offset, 3u);

    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEndTag);
    RoboCheckEqInt(tok.pos.offset, 4u);

    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);

    HtmlTokenizerDestroy(t);

    /* Line tracking across a newline. */
    RoboCheckEqInt(TokFromString("a\n<b>", &t), RoboOk);
    TokNextOk(t, &tok);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenStartTag);
    RoboCheckEqInt(tok.pos.line, 2u);
    RoboCheckEqInt(tok.pos.column, 1u);
    HtmlTokenizerDestroy(t);

    TokFixtureEnd();
}

static void TestTokMixedInput(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;
    int counts[RoboHtmlTokenEof + 1u];

    TokFixtureBegin();
    memset(counts, 0, sizeof(counts));
    RoboCheckEqInt(TokFromString(
                          "<!DOCTYPE html><!-- c --><html><body><p>Hi</p>"
                          "<br/><span x=1>done</span></body></html>",
                          &t),
                      RoboOk);
    for (;;) {
        TokNextOk(t, &tok);
        RoboCheck(tok.kind >= RoboHtmlTokenNone);
        RoboCheck(tok.kind <= RoboHtmlTokenEof);
        counts[tok.kind]++;
        if (tok.kind == RoboHtmlTokenEof) {
            break;
        }
    }
    RoboCheckEqInt(counts[RoboHtmlTokenDoctype], 1);
    RoboCheckEqInt(counts[RoboHtmlTokenComment], 1);
    /* 5 start tags (html body p br span) and 4 end tags (p span body html),
     * plus the doctype, the comment, and at least two text runs. */
    RoboCheckEqInt(counts[RoboHtmlTokenStartTag], 5);
    RoboCheckEqInt(counts[RoboHtmlTokenEndTag], 4);
    RoboCheck(counts[RoboHtmlTokenCharacter] >= 2);
    HtmlTokenizerDestroy(t);
    TokFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Long input / reset / ownership                                      */
/* ------------------------------------------------------------------ */

static void TestTokLongInput(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;
    char *big;
    size_t i;
    size_t tagCount = 0u;

    TokFixtureBegin();
    /*
     * 20000 bytes of markup **plus the terminator**: `HtmlSourceString` takes a C string and
     * derives its length by scanning for the NUL, so the buffer has to hold one. Sizing this at
     * exactly 20000 made the scan read the byte past the allocation -- the terminator was the
     * 20001st, and it lived in ASan's redzone rather than in the buffer. The markup is still the
     * 20000 bytes the case is about; only the terminator is new.
     */
    big = (char *)Malloc(20001u);
    RoboCheck(big != NULL);
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
    big[i] = '\0';
    RoboCheckEqInt(TokFromString(big, &t), RoboOk);
    for (;;) {
        TokNextOk(t, &tok);
        if (tok.kind == RoboHtmlTokenStartTag ||
            tok.kind == RoboHtmlTokenEndTag) {
            tagCount++;
        }
        if (tok.kind == RoboHtmlTokenEof) {
            break;
        }
    }
    RoboCheckEqInt(tagCount, 2500u * 2u);
    HtmlTokenizerDestroy(t);
    RoboFree(big);
    TokFixtureEnd();
}

static void TestTokResetReuse(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;
    HtmlSource src;

    TokFixtureBegin();
    HtmlSourceInit(&src);
    RoboCheckEqInt(HtmlSourceString(&src, "<p>hi", NULL), RoboOk);
    RoboCheckEqInt(HtmlTokenizerCreate(&src, &t, NULL), RoboOk);

    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenStartTag);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);

    /* Reset to the start; same stream again. */
    RoboCheckEqInt(HtmlTokenizerReset(t, NULL), RoboOk);
    RoboCheckEqInt(HtmlTokenizerAtEof(t), 0);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenStartTag);
    RoboCheckStrEq(tok.u.tag.name, "p");

    HtmlTokenizerDestroy(t);
    TokFixtureEnd();
}

static void TestTokTokenCopyTagName(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;
    char name[16];

    TokFixtureBegin();
    RoboCheckEqInt(TokFromString("<DIV A=\"v\">", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(HtmlTokenCopyTagName(&tok, name,
                                                    sizeof(name)),
                      RoboOk);
    RoboCheckStrEq(name, "div");
    /* Too-small capacity refused, not truncated. */
    RoboCheckEqInt(HtmlTokenCopyTagName(&tok, name, 2u),
                      RoboNotSupported);
    /* NULL handling. */
    RoboCheckEqInt(HtmlTokenCopyTagName(NULL, name, sizeof(name)),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlTokenCopyTagName(&tok, NULL, sizeof(name)),
                      RoboInvalidArgument);
    HtmlTokenizerDestroy(t);
    TokFixtureEnd();
}

static void TestTokStateQuery(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;
    HtmlTokenizerState st;

    TokFixtureBegin();
    RoboCheckEqInt(TokFromString("<p>a</p>", &t), RoboOk);
    RoboCheckEqInt(HtmlTokenizerGetState(t, &st), RoboOk);
    RoboCheckEqInt(st, RoboHtmlTokenizerStateData);

    TokNextOk(t, &tok); /* <p> */
    RoboCheckEqInt(HtmlTokenizerGetState(t, &st), RoboOk);
    RoboCheckEqInt(st, RoboHtmlTokenizerStateData);
    TokNextOk(t, &tok); /* a */
    TokNextOk(t, &tok); /* </p> */
    TokNextOk(t, &tok); /* EOF */
    RoboCheckEqInt(HtmlTokenizerAtEof(t), 1);

    /* NULL args. */
    RoboCheckEqInt(HtmlTokenizerGetState(NULL, &st),
                      RoboInvalidArgument);
    RoboCheckEqInt(HtmlTokenizerReset(NULL, NULL),
                      RoboInvalidArgument);

    HtmlTokenizerDestroy(t);
    TokFixtureEnd();
}

static void TestTokDiagnosticsApi(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;
    HtmlDiag d;

    TokFixtureBegin();
    RoboCheckEqInt(TokFromString("<div a=\"", &t), RoboOk);
    TokNextOk(t, &tok);
    while (1) {
        TokNextOk(t, &tok);
        if (tok.kind == RoboHtmlTokenEof) {
            break;
        }
    }
    RoboCheck(HtmlTokenizerDiagnosticCount(t) > 0u);
    RoboCheckEqInt(HtmlTokenizerGetDiagnostic(t, 0u, &d), RoboOk);
    RoboCheckEqInt(d.code, RoboParseError);
    /* Out-of-range index → NOT_FOUND. */
    RoboCheckEqInt(HtmlTokenizerGetDiagnostic(
                          t, HtmlTokenizerDiagnosticCount(t), &d),
                      RoboNotFound);
    HtmlTokenizerDestroy(t);
    TokFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* '>' in text / token ownership                                       */
/* ------------------------------------------------------------------ */

static void TestTokTokenLifetime(void)
{
    HtmlTokenizer *t = NULL;
    HtmlToken tok;
    char name[16];
    char value[64];

    TokFixtureBegin();

    /* A '>' that does not close a tag is ordinary text. */
    RoboCheckEqInt(TokFromString("a > b < c", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenCharacter);
    RoboCheckStrEq(tok.u.characters.data, "a > b < c");
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);
    HtmlTokenizerDestroy(t);

    /*
     * Ownership: tokens borrow tokenizer-owned storage, so callers that
     * must keep data use the copy helpers. A copy taken with the helpers
     * stays valid after the tokenizer has moved on and after destroy.
     */
    RoboCheckEqInt(TokFromString("<Article data-Kind=\"prose\" hidden>",
                                      &t),
                      RoboOk);
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenStartTag);
    RoboCheckEqInt(HtmlTokenCopyTagName(&tok, name, sizeof(name)),
                      RoboOk);
    RoboCheckEqInt(HtmlTokenCopyAttr(&tok, "data-kind", value,
                                                sizeof(value)),
                      RoboOk);
    RoboCheckStrEq(name, "article");
    RoboCheckStrEq(value, "prose");

    /* Advance the tokenizer (and tokenize more tags) ... */
    TokNextOk(t, &tok);
    RoboCheckEqInt(tok.kind, RoboHtmlTokenEof);
    HtmlTokenizerDestroy(t);

    /* ... the caller-owned copies are unaffected. */
    RoboCheckStrEq(name, "article");
    RoboCheckStrEq(value, "prose");

    /* Reset reused a tokenizer: the borrowed view follows the new token. */
    RoboCheckEqInt(TokFromString("<p>", &t), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckStrEq(tok.u.tag.name, "p");
    RoboCheckEqInt(HtmlTokenizerReset(t, NULL), RoboOk);
    TokNextOk(t, &tok);
    RoboCheckStrEq(tok.u.tag.name, "p");
    HtmlTokenizerDestroy(t);

    /* HtmlTokenInit clears a caller-owned record. */
    {
        HtmlToken fresh;
        HtmlTokenInit(&fresh);
        RoboCheckEqInt(fresh.kind, RoboHtmlTokenNone);
        RoboCheckEqInt(fresh.u.characters.data == NULL, 1);
        HtmlTokenInit(NULL); /* safe */
    }

    TokFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Robustness: malformed / random byte streams                         */
/* ------------------------------------------------------------------ */

/* Bounded token walk: returns the token count, or 0 when the stream never
 * reached EOF within the step budget (the failure the test guards against). */
static size_t TokDrain(HtmlTokenizer *t, size_t budget)
{
    HtmlToken tok;
    size_t steps = 0u;
    size_t tokens = 0u;

    while (steps < budget) {
        RoboCheckEqInt(HtmlTokenizerNext(t, &tok, NULL), RoboOk);
        RoboCheck(tok.kind >= RoboHtmlTokenNone);
        RoboCheck(tok.kind <= RoboHtmlTokenEof);
        tokens++;
        steps++;
        if (tok.kind == RoboHtmlTokenEof) {
            return tokens;
        }
        /* Borrowed strings for tag/character tokens must be NUL-terminated
         * and stay inside the source-sized bound (no runaway growth). */
        if (tok.kind == RoboHtmlTokenCharacter) {
            RoboCheck(tok.u.characters.data != NULL);
            RoboCheck(tok.u.characters.length < 100000u);
        } else if (tok.kind == RoboHtmlTokenStartTag ||
                   tok.kind == RoboHtmlTokenEndTag) {
            RoboCheck(tok.u.tag.name != NULL);
        }
    }
    return 0u;
}

static void TestTokRobustnessMalformed(void)
{
    HtmlTokenizer *t = NULL;
    HtmlSource src;
    /* Deterministic corpus built from one document's prefixes. */
    static const char doc[] =
        "<!DOCTYPE html><html lang=en><head><title>t &amp; u</title></head>"
        "<body class='x'><p a=1 b=\"2\">hi<!-- c --><br/><input disabled>"
        "<![CDATA[x]]><?pi?><!bogus></body></html>";
    size_t i;

    TokFixtureBegin();

    /*
     * Every prefix of a realistic document, including prefixes that cut a
     * tag, comment, doctype, reference or UTF-8 sequence in half. The
     * tokenizer must always terminate at EOF without error.
     */
    for (i = 0u; i <= sizeof(doc) - 1u; i++) {
        HtmlSourceInit(&src);
        RoboCheckEqInt(
            HtmlSourceMemory(&src, (const uint8_t *)doc, i, NULL),
            RoboOk);
        RoboCheckEqInt(HtmlTokenizerCreate(&src, &t, NULL), RoboOk);
        RoboCheck(TokDrain(t, i * 4u + 64u) != 0u);
        HtmlTokenizerDestroy(t);
    }

    /* Every single byte value, alone and surrounded by markup. */
    for (i = 0u; i < 256u; i++) {
        uint8_t buf[8];
        uint8_t lone = (uint8_t)i;

        HtmlSourceInit(&src);
        RoboCheckEqInt(HtmlSourceMemory(&src, &lone, 1u, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlTokenizerCreate(&src, &t, NULL), RoboOk);
        RoboCheck(TokDrain(t, 64u) != 0u);
        HtmlTokenizerDestroy(t);

        buf[0] = '<';
        buf[1] = (uint8_t)i;
        buf[2] = '>';
        HtmlSourceInit(&src);
        RoboCheckEqInt(HtmlSourceMemory(&src, buf, 3u, NULL),
                          RoboOk);
        RoboCheckEqInt(HtmlTokenizerCreate(&src, &t, NULL), RoboOk);
        RoboCheck(TokDrain(t, 64u) != 0u);
        HtmlTokenizerDestroy(t);
    }

    /* Deterministic pseudo-random soup over a markup-heavy alphabet. */
    {
        /* Bytes, not a C string: NUL and ill-formed UTF-8 are part of the
         * corpus on purpose. */
        static const char alphabet[] = {
            '<', '>', '/', '&', ';', '"', '\'', '=', ' ', '#', '-', '!',
            'A', 'a', '1', (char)0x00, (char)0xFF, (char)0xC3, (char)0xA9
        };
        const size_t alphabetLen = sizeof(alphabet) / sizeof(alphabet[0]);
        uint32_t seed;
        uint8_t soup[96];

        for (seed = 1u; seed <= 64u; seed++) {
            uint32_t state = seed * 2654435761u;
            size_t j;

            for (j = 0u; j < sizeof(soup); j++) {
                state = state * 1664525u + 1013904223u;
                soup[j] = (uint8_t)alphabet[(state >> 16) % alphabetLen];
            }
            HtmlSourceInit(&src);
            RoboCheckEqInt(
                HtmlSourceMemory(&src, soup, sizeof(soup), NULL),
                RoboOk);
            RoboCheckEqInt(HtmlTokenizerCreate(&src, &t, NULL),
                              RoboOk);
            RoboCheck(TokDrain(t, sizeof(soup) * 8u + 64u) != 0u);
            HtmlTokenizerDestroy(t);
        }
    }

    TokFixtureEnd();
}

/* ------------------------------------------------------------------ */
/* Suite registration                                                  */
/* ------------------------------------------------------------------ */

void TestRegisterHtmlToken(void)
{
    TestRegister("html.token.create_misuse", TestTokCreateMisuse);
    TestRegister("html.token.empty_input", TestTokEmptyInput);
    TestRegister("html.token.plain_text", TestTokPlainText);
    TestRegister("html.token.text_runs", TestTokTextRuns);
    TestRegister("html.token.start_end_tags", TestTokStartEndTags);
    TestRegister("html.token.attributes", TestTokAttributes);
    TestRegister("html.token.self_closing", TestTokSelfClosing);
    TestRegister("html.token.duplicate_attrs", TestTokDuplicateAttrs);
    TestRegister("html.token.malformed_attrs", TestTokMalformedAttrs);
    TestRegister("html.token.comments", TestTokComments);
    TestRegister("html.token.doctype", TestTokDoctype);
    TestRegister("html.token.charrefs", TestTokCharrefs);
    TestRegister("html.token.special_chars", TestTokSpecialChars);
    TestRegister("html.token.positions", TestTokPositions);
    TestRegister("html.token.mixed_input", TestTokMixedInput);
    TestRegister("html.token.long_input", TestTokLongInput);
    TestRegister("html.token.reset_reuse", TestTokResetReuse);
    TestRegister("html.token.copy_tag_name", TestTokTokenCopyTagName);
    TestRegister("html.token.state_query", TestTokStateQuery);
    TestRegister("html.token.diagnostics_api", TestTokDiagnosticsApi);
    TestRegister("html.token.lifetime", TestTokTokenLifetime);
    TestRegister("html.token.robustness_malformed",
                       TestTokRobustnessMalformed);
}
