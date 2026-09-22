/*
 * RoboTypography's document defaults (Phase 12.3) — `typography.document.*`.
 *
 * The phase asks for one authoritative place where a document's `body`, `h1`..`h3`, caption and code
 * type settings live, and for the defaults to *arrive* in a document rather than be re-declared in it.
 * This module supplies that place and emits the defaults as CSS; `RoboDesktopDocument` adds the text to
 * a document as a runtime-origin sheet, so the author's own rules still win.
 *
 * The suite is here rather than in the desktop's because the emitter is this module's -- and because
 * the *reason* it can be tested without a document layer is the boundary the module keeps: the output
 * is a string, and asserting on a string needs no CSS parser. What the desktop suite adds, one file
 * over, is that the string actually reaches a laid-out document.
 *
 * The golden case is deliberate. The sheet's bytes are part of a document's frame checksum, so
 * "deterministic" has to mean *this* text and not merely "the same as last time"; a golden string is
 * the only assertion that catches a change to a selector, a separator or an ordering.
 */

#include "RoboTypographyTestSupport.h"

/** The sheet the default scale must produce, byte for byte. */
#define TypographyDefaultSheet                                                                    \
    "body{font-size:16px;font-weight:400;line-height:24px;}"                                      \
    "h1{font-size:32px;font-weight:600;line-height:40px;}"                                        \
    "h2{font-size:24px;font-weight:600;line-height:32px;}"                                        \
    "h3{font-size:19px;font-weight:500;line-height:26px;}"                                        \
    "h4,h5,h6{font-size:19px;font-weight:500;line-height:26px;}"                                  \
    "small,figcaption,caption{font-size:13px;font-weight:400;line-height:18px;}"                  \
    "code,pre,kbd,samp,tt{font-size:14px;font-weight:400;line-height:20px;}"

static void TestDefaultSheet(void)
{
    char sheet[RoboTypographyDocumentSheetMaxBytes];
    char again[RoboTypographyDocumentSheetMaxBytes];
    size_t length = 0u;
    size_t againLength = 0u;
    size_t index;
    int openBraces = 0;
    int closeBraces = 0;

    RoboCheckEqInt((int)RoboTypographyBuildDocumentStyleSheet(NULL, 0u, sheet, sizeof(sheet),
                                                              &length),
                   (int)RoboOk);
    RoboCheckStrEq(sheet, TypographyDefaultSheet);
    RoboCheckEqInt((int)length, (int)strlen(TypographyDefaultSheet));

    /* Same scale, same bytes: the property a frame checksum depends on. */
    RoboCheckEqInt((int)RoboTypographyBuildDocumentStyleSheet(NULL, 0u, again, sizeof(again),
                                                              &againLength),
                   (int)RoboOk);
    RoboCheckStrEq(sheet, again);
    RoboCheckEqInt((int)length, (int)againLength);

    /* The text is a sequence of balanced rules rather than a concatenation of fragments: this is the
     * one structural check that can be made without a CSS parser, and it is enough to catch an
     * emitter that dropped a brace. */
    for (index = 0u; index < length; index++) {
        if (sheet[index] == '{') {
            openBraces++;
        } else if (sheet[index] == '}') {
            closeBraces++;
        }
    }
    RoboCheckEqInt(openBraces, 7);
    RoboCheckEqInt(closeBraces, openBraces);
    RoboCheck(sheet[length - 1u] == '}');
    /* No `font-family`: face selection is the pack's resolution and CSS font matching is a later
     * phase, so the sheet must not imply it has said anything about families. */
    RoboCheck(strstr(sheet, "font-family") == NULL);

    /* Argument shape, including the refusal that reports the length it needed. */
    {
        char tiny[16];
        size_t needed = 0u;

        RoboCheckEqInt((int)RoboTypographyBuildDocumentStyleSheet(NULL, 0u, tiny, sizeof(tiny),
                                                                  &needed),
                       (int)RoboNotSupported);
        RoboCheckEqInt((int)needed, (int)length);
        RoboCheckEqInt((int)RoboTypographyBuildDocumentStyleSheet(NULL, 0u, NULL, 64u, NULL),
                       (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTypographyBuildDocumentStyleSheet(NULL, 0u, sheet, 0u, NULL),
                       (int)RoboInvalidArgument);
    }
}

/**
 * Every element the vocabulary maps has a rule in the sheet, and every document role is reachable.
 *
 * This is the invariant that ties the two vocabularies together: a role the emitter writes and an
 * element the mapper answers must be the same set, and a change to either alone is caught here rather
 * than by a document that quietly stopped styling its headings.
 */
static void TestElementVocabulary(void)
{
    static const char *const mapped[] = {"body", "h1",  "h2",        "h3",   "h4",
                                         "h5",   "h6",  "small",     "caption", "figcaption",
                                         "code", "pre", "kbd",       "samp", "tt"};
    static const char *const unmapped[] = {"p", "li", "span", "div", "td", "a", "img"};
    char sheet[RoboTypographyDocumentSheetMaxBytes];
    size_t index;

    RoboCheckEqInt((int)RoboTypographyBuildDocumentStyleSheet(NULL, 0u, sheet, sizeof(sheet), NULL),
                   (int)RoboOk);

    for (index = 0u; index < sizeof(mapped) / sizeof(mapped[0]); index++) {
        RoboTypographyRole role = RoboTypographyRoleNone;

        RoboCheckMsg(RoboTypographyRoleForHtmlElement(mapped[index], 0u, &role) == RoboOk,
                     "an element with a default did not map");
        RoboCheck(RoboTypographyRoleIsDocument(role));
        RoboCheckMsg(strstr(sheet, mapped[index]) != NULL,
                     "a mapped element has no rule in the generated sheet");
    }
    /* Case is the document's, not the caller's: markup is lower-cased by the tree builder, but a
     * caller asking with a capitalised name should still get the right answer. */
    {
        RoboTypographyRole role = RoboTypographyRoleNone;

        RoboCheckEqInt((int)RoboTypographyRoleForHtmlElement("BODY", 0u, &role), (int)RoboOk);
        RoboCheckEqInt((int)role, (int)RoboTypographyRoleDocumentBody);
        RoboCheckEqInt((int)RoboTypographyRoleForHtmlElement("H1", 0u, &role), (int)RoboOk);
        RoboCheckEqInt((int)role, (int)RoboTypographyRoleDocumentHeading1);
    }

    /* An element with no default of its own answers RoboNotFound and inherits the body's setting --
     * which is exactly why the sheet declares nothing for it. */
    for (index = 0u; index < sizeof(unmapped) / sizeof(unmapped[0]); index++) {
        RoboTypographyRole role = RoboTypographyRoleDocumentBody;

        RoboCheckEqInt((int)RoboTypographyRoleForHtmlElement(unmapped[index], 0u, &role),
                       (int)RoboNotFound);
    }
    RoboCheckEqInt((int)RoboTypographyRoleForHtmlElement("", 0u, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTypographyRoleForHtmlElement(NULL, 3u, NULL), (int)RoboInvalidArgument);
    {
        RoboTypographyRole role;

        /* A length-limited slice is the *name*, so "bod" is not `body`: the slice rule is the same
         * one every parser in this tree follows. */
        RoboCheckEqInt((int)RoboTypographyRoleForHtmlElement("body", 4u, &role), (int)RoboOk);
        RoboCheckEqInt((int)RoboTypographyRoleForHtmlElement("body", 3u, &role), (int)RoboNotFound);
        RoboCheckEqInt((int)RoboTypographyRoleForHtmlElement("bodyx", 5u, &role), (int)RoboNotFound);
    }

    /* Each of the six document roles is reachable from at least one element: a role the emitter
     * writes but nothing maps would be a rule for an element that does not exist. */
    {
        RoboTypographyRole role = RoboTypographyRoleNone;

        RoboCheckEqInt((int)RoboTypographyRoleForHtmlElement("body", 0u, &role), (int)RoboOk);
        RoboCheckEqInt((int)role, (int)RoboTypographyRoleDocumentBody);
        RoboCheckEqInt((int)RoboTypographyRoleForHtmlElement("h1", 0u, &role), (int)RoboOk);
        RoboCheckEqInt((int)role, (int)RoboTypographyRoleDocumentHeading1);
        RoboCheckEqInt((int)RoboTypographyRoleForHtmlElement("h2", 0u, &role), (int)RoboOk);
        RoboCheckEqInt((int)role, (int)RoboTypographyRoleDocumentHeading2);
        RoboCheckEqInt((int)RoboTypographyRoleForHtmlElement("h3", 0u, &role), (int)RoboOk);
        RoboCheckEqInt((int)role, (int)RoboTypographyRoleDocumentHeading3);
        RoboCheckEqInt((int)RoboTypographyRoleForHtmlElement("h6", 0u, &role), (int)RoboOk);
        RoboCheckEqInt((int)role, (int)RoboTypographyRoleDocumentHeading3);
        RoboCheckEqInt((int)RoboTypographyRoleForHtmlElement("small", 0u, &role), (int)RoboOk);
        RoboCheckEqInt((int)role, (int)RoboTypographyRoleDocumentCaption);
        RoboCheckEqInt((int)RoboTypographyRoleForHtmlElement("code", 0u, &role), (int)RoboOk);
        RoboCheckEqInt((int)role, (int)RoboTypographyRoleDocumentCode);
    }
}

/** A caller's scale reaches the sheet, and a role it omits simply has no rule. */
static void TestCustomScale(void)
{
    RoboTypographyScaleEntry scale[RoboTypographyScaleCount];
    char sheet[RoboTypographyDocumentSheetMaxBytes];
    size_t length = 0u;
    size_t index;

    RoboCheckEqInt((int)RoboTypographyDefaultScale(scale, RoboTypographyScaleCount, NULL),
                   (int)RoboOk);

    /* A restyle is a dataset change: doubling the body size must reach the text. */
    for (index = 0u; index < RoboTypographyScaleCount; index++) {
        if (scale[index].role == RoboTypographyRoleDocumentBody) {
            scale[index].sizePx = 18.0f;
            scale[index].lineHeightPx = 27.0f;
        }
    }
    RoboCheckEqInt((int)RoboTypographyBuildDocumentStyleSheet(scale, RoboTypographyScaleCount, sheet,
                                                              sizeof(sheet), &length),
                   (int)RoboOk);
    RoboCheck(strstr(sheet, "body{font-size:18px;font-weight:400;line-height:27px;}") != NULL);
    /* The roles that did not change keep their own rules. */
    RoboCheck(strstr(sheet, "h1{font-size:32px;") != NULL);

    /* A scale with a document role removed emits no rule for it rather than half a rule: the element
     * then keeps the layout engine's own initial value, which is a defined state. */
    {
        RoboTypographyScaleEntry cut[RoboTypographyScaleCount - 1u];
        size_t written = 0u;

        for (index = 0u; index < RoboTypographyScaleCount; index++) {
            if (scale[index].role == RoboTypographyRoleDocumentCaption) {
                continue;
            }
            cut[written++] = scale[index];
        }
        RoboCheckEqInt((int)RoboTypographyBuildDocumentStyleSheet(cut, written, sheet,
                                                                  sizeof(sheet), &length),
                       (int)RoboOk);
        RoboCheck(strstr(sheet, "figcaption") == NULL);
        RoboCheck(strstr(sheet, "body{") != NULL);
    }
}

void TestRegisterTypographyDocument(void)
{
    TestRegister("typography.document.sheet", TestDefaultSheet);
    TestRegister("typography.document.element_vocabulary", TestElementVocabulary);
    TestRegister("typography.document.custom_scale", TestCustomScale);
}
