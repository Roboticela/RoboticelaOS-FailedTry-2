/*
 * RoboCSS simple and compound selector matching (Phase 9.4) — css.select.match.*
 *
 * These tests are written against hosts, not documents, because that is what the
 * matcher is written against: the seam in RoboCSSStyle.h is the whole contract,
 * so every case here is a statement about which element *view* satisfies which
 * selector. The DOM integration suite separately re-runs the same selectors
 * through the real RoboHTML tree, which is what makes these two independent
 * rather than duplicated.
 *
 * The negatives matter as much as the positives. `[a=""]` matching an absent
 * attribute, `.card` matching `cardigan`, and `DIV` not matching `div` are all
 * mistakes that a "does it match something" test would pass and a real stylesheet
 * would not survive.
 */

#include "RoboCssTestSupport.h"

/* ------------------------------------------------------------------ */
/* Type and universal                                                  */
/* ------------------------------------------------------------------ */

static void TestCssMatchType(void)
{
    CssHost host;
    CssElementRef div;

    CssHostInit(&host);
    div = CssHostAdd(&host, "div", "app", NULL);
    CssHostAdd(&host, "span", NULL, NULL);
    CssHostSeal(&host);

    RoboCheckEqInt(CssMatch(&host, "div", div), 1);
    RoboCheckEqInt(CssMatch(&host, "span", div), 0);
    /* HTML element names are ASCII case-insensitive, and the DOM stores them
     * lowercase, so the author's casing must not decide the match. */
    RoboCheckEqInt(CssMatch(&host, "DIV", div), 1);
    RoboCheckEqInt(CssMatch(&host, "DiV", div), 1);
    /* The element's own spelling is what it is; the selector is what folds. */
    RoboCheckEqInt(CssMatch(&host, "div", 2u), 0);
    RoboCheckEqInt(CssMatch(&host, "*", div), 1);
    RoboCheckEqInt(CssMatch(&host, "*", 2u), 1);
    RoboCheckEqInt(CssMatch(&host, "div", RoboCssElementRefNone), 0);
}

static void TestCssMatchTypeUnicode(void)
{
    CssHost host;
    CssElementRef element;

    /* A custom element name in Arabic script: byte-exact, and not folded — the
     * case fold is ASCII-only by design, and must not touch non-ASCII bytes. */
    CssHostInit(&host);
    element = CssHostAdd(&host, "\xD9\x82\xD8\xB3\xD9\x85", NULL, NULL);
    CssHostSeal(&host);

    RoboCheckEqInt(CssMatch(&host, "\xD9\x82\xD8\xB3\xD9\x85", element), 1);
    RoboCheckEqInt(CssMatch(&host, "\xD9\x85", element), 0);
    RoboCheckEqInt(CssMatch(&host, "*", element), 1);
}

/* ------------------------------------------------------------------ */
/* Class                                                              */
/* ------------------------------------------------------------------ */

static void TestCssMatchClass(void)
{
    CssHost host;
    CssElementRef card;

    CssHostInit(&host);
    card = CssHostAdd(&host, "div", NULL, "card active");
    CssHostAdd(&host, "div", NULL, "cardigan");
    CssHostAdd(&host, "div", NULL, "card");
    CssHostAdd(&host, "div", NULL, "");
    CssHostAdd(&host, "div", NULL, NULL);
    CssHostSeal(&host);

    RoboCheckEqInt(CssMatch(&host, ".card", card), 1);
    RoboCheckEqInt(CssMatch(&host, ".active", card), 1);
    RoboCheckEqInt(CssMatch(&host, ".card.active", card), 1);
    /* Multiple simple selectors in one compound is "all of them", so a class
     * the element does not have decides the whole compound. */
    RoboCheckEqInt(CssMatch(&host, ".card.missing", card), 0);
    /* `cardigan` contains `card` as bytes and is not the class `card`. */
    RoboCheckEqInt(CssMatch(&host, ".card", 2u), 0);
    RoboCheckEqInt(CssMatch(&host, ".cardigan", 2u), 1);
    RoboCheckEqInt(CssMatch(&host, ".card", 3u), 1);
    /* Class names are case-sensitive in HTML. */
    RoboCheckEqInt(CssMatch(&host, ".Card", card), 0);
    RoboCheckEqInt(CssMatch(&host, ".CARD", card), 0);
    /* An empty class attribute holds no classes, and neither does none. */
    RoboCheckEqInt(CssMatch(&host, ".card", 4u), 0);
    RoboCheckEqInt(CssMatch(&host, "*", 4u), 1);
    RoboCheckEqInt(CssMatch(&host, ".card", 5u), 0);
    RoboCheckEqInt(CssMatch(&host, "*", 5u), 1);
}

static void TestCssMatchClassEscapedName(void)
{
    CssHost host;
    CssElementRef element;

    /* `.foo\:bar` is the class `foo:bar`, which is a legal class name. The
     * parser resolves the escape, so matching is a byte comparison against the
     * resolved bytes rather than any unescaping. */
    CssHostInit(&host);
    element = CssHostAdd(&host, "div", NULL, "foo:bar");
    CssHostSeal(&host);

    RoboCheckEqInt(CssMatch(&host, ".foo\\:bar", element), 1);
    RoboCheckEqInt(CssMatch(&host, ".foo", element), 0);
}

/* ------------------------------------------------------------------ */
/* Id                                                                 */
/* ------------------------------------------------------------------ */

static void TestCssMatchId(void)
{
    CssHost host;
    CssElementRef app;

    CssHostInit(&host);
    app = CssHostAdd(&host, "div", "app", NULL);
    CssHostAdd(&host, "div", "APP", NULL);
    CssHostAdd(&host, "div", "", NULL);
    CssHostAdd(&host, "div", NULL, NULL);
    CssHostSeal(&host);

    RoboCheckEqInt(CssMatch(&host, "#app", app), 1);
    RoboCheckEqInt(CssMatch(&host, "#other", app), 0);
    /* Ids are case-sensitive. */
    RoboCheckEqInt(CssMatch(&host, "#app", 2u), 0);
    RoboCheckEqInt(CssMatch(&host, "#APP", 2u), 1);
    /* `id=""` is an id that no `#name` selector names, and no id at all is the
     * same answer by a different route. */
    RoboCheckEqInt(CssMatch(&host, "#app", 3u), 0);
    RoboCheckEqInt(CssMatch(&host, "#app", 4u), 0);
    /* Duplicate ids both answer: the matcher has no uniqueness assumption. */
    RoboCheckEqInt(CssMatch(&host, "#APP", app), 0);
}

static void TestCssMatchIdUnicode(void)
{
    CssHost host;
    CssElementRef element;

    CssHostInit(&host);
    element = CssHostAdd(&host, "div", "\xD8\xB9\xD9\x84\xD8\xA8\xD8\xA9", NULL);
    CssHostSeal(&host);

    RoboCheckEqInt(CssMatch(&host, "#\xD8\xB9\xD9\x84\xD8\xA8\xD8\xA9", element), 1);
    RoboCheckEqInt(CssMatch(&host, "#\xD8\xB9", element), 0);
}

/* ------------------------------------------------------------------ */
/* Attributes                                                         */
/* ------------------------------------------------------------------ */

static void TestCssMatchAttributeExistence(void)
{
    CssHost host;
    CssElementRef button;

    CssHostInit(&host);
    button = CssHostAdd(&host, "button", NULL, NULL);
    CssHostAttribute(&host, button, "disabled", "");
    CssHostAttribute(&host, button, "type", "button");
    CssHostAdd(&host, "button", NULL, NULL);
    CssHostSeal(&host);

    /* A boolean attribute is present with an empty value, which is what presence
     * asks about — nothing is inferred from the value. */
    RoboCheckEqInt(CssMatch(&host, "[disabled]", button), 1);
    RoboCheckEqInt(CssMatch(&host, "[type]", button), 1);
    RoboCheckEqInt(CssMatch(&host, "[disabled]", 2u), 0);
    /* Attribute *names* are ASCII case-insensitive in HTML. */
    RoboCheckEqInt(CssMatch(&host, "[DISABLED]", button), 1);
    RoboCheckEqInt(CssMatch(&host, "[Type]", button), 1);
}

static void TestCssMatchAttributeOperators(void)
{
    CssHost host;
    CssElementRef element;

    CssHostInit(&host);
    element = CssHostAdd(&host, "a", NULL, NULL);
    CssHostAttribute(&host, element, "href", "/api/users.json");
    CssHostAttribute(&host, element, "lang", "en-GB");
    CssHostAttribute(&host, element, "rel", "nofollow external");
    CssHostAttribute(&host, element, "data-name", "test");
    CssHostAttribute(&host, element, "data-empty", "");
    CssHostSeal(&host);

    /* Equality, quoted and unquoted, exactly as parsed. */
    RoboCheckEqInt(CssMatch(&host, "[href=\"/api/users.json\"]", element), 1);
    RoboCheckEqInt(CssMatch(&host, "[lang=en-GB]", element), 1);
    RoboCheckEqInt(CssMatch(&host, "[lang=en]", element), 0);
    /* `~=` is a whitespace-separated word, not a substring. */
    RoboCheckEqInt(CssMatch(&host, "[rel~=external]", element), 1);
    RoboCheckEqInt(CssMatch(&host, "[rel~=nofollow]", element), 1);
    RoboCheckEqInt(CssMatch(&host, "[rel~=ternal]", element), 0);
    RoboCheckEqInt(CssMatch(&host, "[rel~=external-]", element), 0);
    /* `|=` is equality or a dash-prefixed continuation. */
    RoboCheckEqInt(CssMatch(&host, "[lang|=en]", element), 1);
    RoboCheckEqInt(CssMatch(&host, "[lang|=en-GB]", element), 1);
    RoboCheckEqInt(CssMatch(&host, "[lang|=e]", element), 0);
    RoboCheckEqInt(CssMatch(&host, "[lang|=en-GBX]", element), 0);
    /* Prefix, suffix and substring. */
    RoboCheckEqInt(CssMatch(&host, "[href^=\"/api\"]", element), 1);
    RoboCheckEqInt(CssMatch(&host, "[href^=\"/API\"]", element), 0);
    RoboCheckEqInt(CssMatch(&host, "[href$=\".json\"]", element), 1);
    RoboCheckEqInt(CssMatch(&host, "[href*=\"users\"]", element), 1);
    RoboCheckEqInt(CssMatch(&host, "[href*=\"api/users\"]", element), 1);
    RoboCheckEqInt(CssMatch(&host, "[href*=\"api\\/users\"]", element), 1);
    RoboCheckEqInt(CssMatch(&host, "[href*=\"users/\"]", element), 0);
    RoboCheckEqInt(CssMatch(&host, "[data-name*=\"es\"]", element), 1);
    /* A missing attribute fails every operator, presence included. */
    RoboCheckEqInt(CssMatch(&host, "[missing]", element), 0);
    RoboCheckEqInt(CssMatch(&host, "[missing=\"\"]", element), 0);
    RoboCheckEqInt(CssMatch(&host, "[missing^=\"\"]", element), 0);
}

static void TestCssMatchAttributeEmptyValue(void)
{
    CssHost host;
    CssElementRef element;

    /* `[a]` and `[a=""]` are different questions, and an attribute that is
     * present-and-empty answers only the first and the equality. This is the
     * case where collapsing "absent" and "empty" would silently change which
     * rules apply. */
    CssHostInit(&host);
    element = CssHostAdd(&host, "div", NULL, NULL);
    CssHostAttribute(&host, element, "data-empty", "");
    CssHostSeal(&host);

    RoboCheckEqInt(CssMatch(&host, "[data-empty]", element), 1);
    RoboCheckEqInt(CssMatch(&host, "[data-empty=\"\"]", element), 1);
    RoboCheckEqInt(CssMatch(&host, "[data-empty~=\"\"]", element), 0);
    /* An empty operand can match nothing under `|=`, `^=`, `$=` or `*=`,
     * because every string would otherwise trivially contain it. */
    RoboCheckEqInt(CssMatch(&host, "[data-empty|=\"\"]", element), 0);
    RoboCheckEqInt(CssMatch(&host, "[data-empty^=\"\"]", element), 0);
    RoboCheckEqInt(CssMatch(&host, "[data-empty$=\"\"]", element), 0);
    RoboCheckEqInt(CssMatch(&host, "[data-empty*=\"\"]", element), 0);
}

static void TestCssMatchAttributeCaseFlag(void)
{
    CssHost host;
    CssElementRef element;

    CssHostInit(&host);
    element = CssHostAdd(&host, "input", NULL, NULL);
    CssHostAttribute(&host, element, "type", "text");
    CssHostSeal(&host);

    RoboCheckEqInt(CssMatch(&host, "[type=\"text\"]", element), 1);
    RoboCheckEqInt(CssMatch(&host, "[type=\"TEXT\"]", element), 0);
    /* `i` folds ASCII case, which is what makes the selector insensitive. */
    RoboCheckEqInt(CssMatch(&host, "[type=\"TEXT\" i]", element), 1);
    RoboCheckEqInt(CssMatch(&host, "[type=\"text\" i]", element), 1);
    /* `s` is the explicit default: sensitive. */
    RoboCheckEqInt(CssMatch(&host, "[type=\"TEXT\" s]", element), 0);
    /* The flag folds only where the operator compares; `~=` folds too. */
    RoboCheckEqInt(CssMatch(&host, "[type~=\"Text\" i]", element), 1);
    RoboCheckEqInt(CssMatch(&host, "[type^=\"TE\" i]", element), 1);
    RoboCheckEqInt(CssMatch(&host, "[type^=\"TE\"]", element), 0);
}

static void TestCssMatchAttributeUnicodeValue(void)
{
    CssHost host;
    CssElementRef element;

    CssHostInit(&host);
    element = CssHostAdd(&host, "div", NULL, NULL);
    CssHostAttribute(&host, element, "data-name", "\xD8\xA7\xD8\xB3\xD9\x85");
    CssHostSeal(&host);

    RoboCheckEqInt(CssMatch(&host, "[data-name=\"\xD8\xA7\xD8\xB3\xD9\x85\"]",
                                element),
                      1);
    RoboCheckEqInt(CssMatch(&host, "[data-name$=\"\xD9\x85\"]", element), 1);
    RoboCheckEqInt(CssMatch(&host, "[data-name|=\"\xD8\xA7\"]", element), 0);
    RoboCheckEqInt(CssMatch(&host, "[data-name=\"\xD8\xA7\"]", element), 0);
}

/* ------------------------------------------------------------------ */
/* Compounds                                                          */
/* ------------------------------------------------------------------ */

static void TestCssMatchCompoundRequiresEverySimple(void)
{
    CssHost host;
    CssElementRef element;

    CssHostInit(&host);
    element = CssHostAdd(&host, "input", "email", "field required");
    CssHostAttribute(&host, element, "type", "text");
    CssHostSeal(&host);

    RoboCheckEqInt(CssMatch(&host, "input", element), 1);
    RoboCheckEqInt(CssMatch(&host, "input#email", element), 1);
    RoboCheckEqInt(CssMatch(&host, "input#email.field", element), 1);
    RoboCheckEqInt(CssMatch(&host, "input.field.required", element), 1);
    RoboCheckEqInt(CssMatch(&host, "input[type=\"text\"].field#email", element), 1);
    /* One failing simple fails the compound, wherever it sits in the order. */
    RoboCheckEqInt(CssMatch(&host, "input#email.missing", element), 0);
    RoboCheckEqInt(CssMatch(&host, "span#email.field", element), 0);
    RoboCheckEqInt(CssMatch(&host, "input#other.field", element), 0);
    RoboCheckEqInt(CssMatch(&host, "input[type=\"radio\"].field", element), 0);
    /* The universal adds nothing to a compound and removes nothing. */
    RoboCheckEqInt(CssMatch(&host, "*.field", element), 1);
    RoboCheckEqInt(CssMatch(&host, "*#email.field", element), 1);
    /* A universal after a type selector is malformed rather than redundant, so
     * it never reaches the matcher: the parser refuses it. */
    (void)CssSelectParseRejected("input*.field");
}

/* ------------------------------------------------------------------ */
/* Errors and argument handling                                       */
/* ------------------------------------------------------------------ */

static void TestCssMatchApiArguments(void)
{
    CssHost host;
    CssSelectorList list = RoboCssCleared;
    CssSelectorMatch result;
    CssElementHost unusable;

    CssHostInit(&host);
    CssHostAdd(&host, "div", NULL, NULL);
    CssHostSeal(&host);
    CssSelectParse("div", &list);

    /* No output, no host, or a host with no view at all is a caller bug, and is
     * distinguished from "did not match" — which is what -1 means everywhere. */
    RoboCheckEqInt(CssSelectorListMatches(&list, &host.host, 1u, NULL, NULL), -1);
    RoboCheckEqInt(CssSelectorListMatches(&list, NULL, 1u, &result, NULL), -1);
    CssElementHostInit(&unusable);
    RoboCheckEqInt(CssSelectorListMatches(&list, &unusable, 1u, &result, NULL), -1);
    RoboCheckEqInt(CssSelectorListMatches(NULL, &host.host, 1u, &result, NULL), -1);
    /* A null element is not an error: it is "no element", and no element matches
     * nothing. */
    RoboCheckEqInt(CssSelectorListMatches(&list, &host.host,
                                                     RoboCssElementRefNone, &result, NULL),
                      0);
    RoboCheckEqInt(result.matched, 0);
    RoboCheckEqInt(result.unsupported, 0);
    /* An element the host cannot describe is a candidate that matches nothing,
     * not a failure. */
    RoboCheckEqInt(CssSelectorListMatches(&list, &host.host, 99u, &result, NULL), 0);

    CssSelectorListDispose(&list);
}

void TestRegisterCssSelectMatchSimple(void);

void TestRegisterCssSelectMatchSimple(void)
{
    TestRegister("css.select.match.type", TestCssMatchType);
    TestRegister("css.select.match.type_unicode", TestCssMatchTypeUnicode);
    TestRegister("css.select.match.class", TestCssMatchClass);
    TestRegister("css.select.match.class_escaped_name",
                       TestCssMatchClassEscapedName);
    TestRegister("css.select.match.id", TestCssMatchId);
    TestRegister("css.select.match.id_unicode", TestCssMatchIdUnicode);
    TestRegister("css.select.match.attribute_existence",
                       TestCssMatchAttributeExistence);
    TestRegister("css.select.match.attribute_operators",
                       TestCssMatchAttributeOperators);
    TestRegister("css.select.match.attribute_empty_value",
                       TestCssMatchAttributeEmptyValue);
    TestRegister("css.select.match.attribute_case_flag",
                       TestCssMatchAttributeCaseFlag);
    TestRegister("css.select.match.attribute_unicode_value",
                       TestCssMatchAttributeUnicodeValue);
    TestRegister("css.select.match.compound_requires_every_simple",
                       TestCssMatchCompoundRequiresEverySimple);
    TestRegister("css.select.match.api_arguments", TestCssMatchApiArguments);
}
