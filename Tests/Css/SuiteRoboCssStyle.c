/*
 * RoboCSS Phase 9.1 style-boundary suite:
 * the element view, the style context, and the computed-style container.
 *
 * The two resolution entry points are tested as boundaries: they refuse, they
 * change nothing, and they say why. What they will do in 9.4, 9.5 and 9.8 is
 * not asserted here, and could not be.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSComputed.h"
#include "RoboCSS/RoboCSSStyle.h"

/* ------------------------------------------------------------------ */
/* Element view                                                        */
/* ------------------------------------------------------------------ */

static void TestCssElementView(void)
{
    CssElementView view = RoboCssCleared;

    CssFixture();
    CssElementViewInit(&view);
    RoboCheck(!CssElementViewIsSet(&view));
    RoboCheckEqInt(view.ref, RoboCssElementRefNone);
    RoboCheck(CssSliceIsEmpty(view.tagName));
    RoboCheck(view.classes == NULL);
    RoboCheckEqInt(view.classCount, 0u);
    RoboCheck(view.attributes == NULL);
    RoboCheckEqInt(view.attributeCount, 0u);

    RoboCheckEqInt(CssElementViewSetElement(&view, 42u,
                                                        CssSliceFromText("div"),
                                                        NULL),
                      RoboOk);
    RoboCheck(CssElementViewIsSet(&view));
    RoboCheckEqInt(view.ref, 42u);
    RoboCheck(CssSliceEqualsAscii(view.tagName, "div"));

    /* The ref is the element's identity: 0 means "no element" and is refused. */
    RoboCheckEqInt(CssElementViewSetElement(&view, RoboCssElementRefNone,
                                                        CssSliceFromText("div"),
                                                        NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssElementViewSetElement(&view, 42u,
                                                        CssSliceEmpty(), NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(view.ref, 42u);

    RoboCheckEqInt(CssElementViewSetId(&view,
                                                   CssSliceFromText("main"), NULL),
                      RoboOk);
    RoboCheck(CssElementViewIdEquals(&view, CssSliceFromText("main")));
    RoboCheck(!CssElementViewIdEquals(&view,
                                               CssSliceFromText("Main")));

    /* An empty id is a real id (`id=""`), distinct from having none: the view
     * records what the DOM said. */
    RoboCheckEqInt(CssElementViewSetId(&view, CssSliceEmpty(), NULL),
                      RoboOk);
    RoboCheck(CssElementViewIdEquals(&view, CssSliceEmpty()));
    RoboCheck(!CssElementViewIdEquals(&view, CssSliceFromText("main")));

    RoboCheckEqInt(CssElementViewSetElement(&view, 42u,
                                                        CssSliceFromText("div"),
                                                        NULL),
                      RoboOk);
    RoboCheckEqInt(CssElementViewSetElement(NULL, 42u,
                                                        CssSliceFromText("div"),
                                                        NULL),
                      RoboInvalidArgument);
    RoboCheck(!CssElementViewIsSet(NULL));
    RoboCheck(!CssElementViewIdEquals(NULL, CssSliceFromText("x")));
    RoboCheck(!CssElementViewHasClass(NULL, CssSliceFromText("x")));
    RoboCheck(!CssElementViewFindAttribute(NULL, CssSliceFromText("x"),
                                                     NULL));
    CssElementViewInit(NULL);
}

static void TestCssElementViewClassesAndAttributes(void)
{
    CssElementView view = RoboCssCleared;
    CssSlice classes[4];
    int truncated = 0;
    CssAttribute attributes[3];
    CssSlice value;

    CssFixture();
    CssElementViewInit(&view);
    RoboCheckEqInt(CssElementViewSetElement(&view, 7u,
                                                        CssSliceFromText("input"),
                                                        NULL),
                      RoboOk);

    /* The class attribute is text; splitting it is the one helper this phase
     * owns for it, and the host may equally hand over a list it already has. */
    RoboCheckEqInt((int)CssSplitAsciiWhitespace(
                          CssSliceFromText("  card  wide\tselected "), classes,
                          4u, &truncated),
                      3);
    RoboCheckEqInt(truncated, 0);
    RoboCheckEqInt(CssElementViewSetClasses(&view, classes, 3u, NULL),
                      RoboOk);
    RoboCheck(CssElementViewHasClass(&view, CssSliceFromText("card")));
    RoboCheck(CssElementViewHasClass(&view, CssSliceFromText("wide")));
    RoboCheck(CssElementViewHasClass(&view,
                                               CssSliceFromText("selected")));
    /* Class matching is byte-exact, unlike an attribute name. */
    RoboCheck(!CssElementViewHasClass(&view, CssSliceFromText("Card")));
    RoboCheck(!CssElementViewHasClass(&view, CssSliceFromText("car")));
    RoboCheck(!CssElementViewHasClass(&view, CssSliceEmpty()));

    attributes[0].name = CssSliceFromText("type");
    attributes[0].value = CssSliceFromText("text");
    attributes[1].name = CssSliceFromText("disabled");
    attributes[1].value = CssSliceEmpty();
    attributes[2].name = CssSliceFromText("placeholder");
    attributes[2].value = CssSliceFromText("\xD8\xA7\xD8\xB3\xD9\x85");
    RoboCheckEqInt(CssElementViewSetAttributes(&view, attributes, 3u, NULL),
                      RoboOk);

    /* Finding an attribute is ASCII case-insensitive and yields its value,
     * which may be empty and is therefore distinguished by the return value. */
    RoboCheck(CssElementViewFindAttribute(&view,
                                                    CssSliceFromText("type"),
                                                    &value));
    RoboCheck(CssSliceEqualsAscii(value, "text"));
    RoboCheck(CssElementViewFindAttribute(&view,
                                                    CssSliceFromText("TYPE"),
                                                    &value));
    RoboCheck(CssSliceEqualsAscii(value, "text"));
    RoboCheck(CssElementViewFindAttribute(&view,
                                                    CssSliceFromText("DISABLED"),
                                                    &value));
    RoboCheck(CssSliceIsEmpty(value));
    RoboCheck(!CssElementViewFindAttribute(&view,
                                                     CssSliceFromText("nope"),
                                                     &value));
    RoboCheck(!CssElementViewFindAttribute(&view, CssSliceEmpty(),
                                                     &value));
    /* A prefix and a longer name are different attributes. */
    RoboCheck(!CssElementViewFindAttribute(&view,
                                                     CssSliceFromText("typ"),
                                                     &value));
    RoboCheck(!CssElementViewFindAttribute(
        &view, CssSliceFromText("type-long"), &value));
    /* Unicode values are returned verbatim. */
    RoboCheck(CssElementViewFindAttribute(
        &view, CssSliceFromText("placeholder"), &value));
    /* Three Arabic letters, six UTF-8 bytes, returned untouched. */
    RoboCheckEqInt(value.length, 6u);
    RoboCheckEqInt(value.bytes[0], 0xD8u);
    RoboCheckEqInt(value.bytes[1], 0xA7u);

    /* A NULL array with a non-zero count is a caller mistake; an empty view is
     * described by passing NULL with 0. */
    RoboCheckEqInt(CssElementViewSetClasses(&view, NULL, 3u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssElementViewSetAttributes(&view, NULL, 1u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssElementViewSetClasses(&view, NULL, 0u, NULL), RoboOk);
    RoboCheckEqInt(view.classCount, 0u);
    RoboCheck(!CssElementViewHasClass(&view, CssSliceFromText("card")));
    RoboCheckEqInt(CssElementViewSetClasses(&view, classes, 3u, NULL),
                      RoboOk);
    RoboCheckEqInt(CssElementViewSetClasses(NULL, classes, 3u, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssElementViewSetAttributes(NULL, attributes, 1u, NULL),
                      RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Style context                                                       */
/* ------------------------------------------------------------------ */

static void TestCssStyleContextSheets(void)
{
    CssStyleContext *context = NULL;
    CssStylesheetId authorA = RoboCssStylesheetIdNone;
    CssStylesheetId authorB = RoboCssStylesheetIdNone;
    CssStylesheetId runtime = RoboCssStylesheetIdNone;
    CssDiag diag;

    CssFixture();
    RoboCheckEqInt(CssStyleContextCreate(NULL, NULL), RoboInvalidArgument);
    RoboCheckEqInt(CssStyleContextCreate(&context, NULL), RoboOk);
    RoboCheck(context != NULL);
    RoboCheckEqInt(CssStyleContextTotalSheetCount(context), 0u);
    RoboCheckEqInt(CssStyleContextSheetCount(context, RoboCssOriginAuthor),
                      0u);
    RoboCheck(!CssStyleContextHasRuntimeSheet(context));
    RoboCheck(CssStyleContextPropertyTable(context) == NULL);
    RoboCheck(CssStyleContextElementLookup(context) == NULL);
    RoboCheck(CssStyleContextInlineStyle(context) == NULL);

    RoboCheckEqInt(CssStylesheetCreate(&authorA, RoboCssOriginAuthor,
                                                 NULL),
                      RoboOk);
    RoboCheckEqInt(CssStylesheetCreate(&authorB, RoboCssOriginAuthor,
                                                 NULL),
                      RoboOk);
    RoboCheckEqInt(CssStylesheetCreate(&runtime, RoboCssOriginRuntime,
                                                 NULL),
                      RoboOk);

    RoboCheckEqInt(CssStyleContextAddSheet(context, RoboCssOriginAuthor,
                                                       authorA, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStyleContextAddSheet(context, RoboCssOriginAuthor,
                                                       authorB, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStyleContextAddSheet(context, RoboCssOriginRuntime,
                                                       runtime, NULL),
                      RoboOk);
    RoboCheckEqInt(CssStyleContextSheetCount(context, RoboCssOriginAuthor),
                      2u);
    RoboCheckEqInt(CssStyleContextSheetCount(context, RoboCssOriginRuntime),
                      1u);
    RoboCheckEqInt(CssStyleContextSheetCount(context, RoboCssOriginUser),
                      0u);
    RoboCheckEqInt(CssStyleContextTotalSheetCount(context), 3u);
    /* The order a sheet was added in is the order it is read back, which is the
     * order a cascade will need. */
    RoboCheckEqInt(CssStyleContextSheet(context, RoboCssOriginAuthor, 0u),
                      authorA);
    RoboCheckEqInt(CssStyleContextSheet(context, RoboCssOriginAuthor, 1u),
                      authorB);
    RoboCheckEqInt(CssStyleContextSheet(context, RoboCssOriginAuthor, 2u),
                      RoboCssStylesheetIdNone);
    RoboCheckEqInt(CssStyleContextSheet(context, RoboCssOriginUser, 0u),
                      RoboCssStylesheetIdNone);

    /* The runtime origin is the default-stylesheet boundary: this phase ships
     * no default, and a caller can tell that from the context. */
    RoboCheck(CssStyleContextHasRuntimeSheet(context));

    RoboCheckEqInt(CssStyleContextAddSheet(context, RoboCssOriginInline,
                                                       authorA, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssStyleContextAddSheet(context, RoboCssOriginNone,
                                                       authorA, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssStyleContextAddSheet(context, RoboCssOriginAuthor,
                                                       RoboCssStylesheetIdNone, NULL),
                      RoboInvalidArgument);
    /* A dead id is refused where it is added, not later during a resolution. */
    CssDiagReset(&diag);
    RoboCheckEqInt(CssStyleContextAddSheet(context, RoboCssOriginAuthor,
                                                       (CssStylesheetId)9999u,
                                                       &diag),
                      RoboNotFound);
    RoboCheckEqInt(diag.domain, RoboCssDomainStyle);
    RoboCheckEqInt(CssStyleContextTotalSheetCount(context), 3u);
    RoboCheckEqInt(CssStyleContextAddSheet(NULL, RoboCssOriginAuthor,
                                                       authorA, NULL),
                      RoboInvalidArgument);

    CssStyleContextClearOrigin(context, RoboCssOriginAuthor);
    RoboCheckEqInt(CssStyleContextTotalSheetCount(context), 1u);
    RoboCheckEqInt(CssStyleContextSheetCount(context, RoboCssOriginAuthor),
                      0u);
    CssStyleContextClear(context);
    RoboCheckEqInt(CssStyleContextTotalSheetCount(context), 0u);
    RoboCheck(!CssStyleContextHasRuntimeSheet(context));
    CssStyleContextClearOrigin(context, RoboCssOriginInline);
    CssStyleContextClear(context);

    /* Destroying the context does not destroy the sheets it borrowed: they are
     * still live, and still the caller's. */
    CssStyleContextDestroy(context);
    RoboCheck(CssStylesheetIsLive(authorA));
    RoboCheck(CssStylesheetIsLive(runtime));
    RoboCheckEqInt(CssStyleContextTotalSheetCount(NULL), 0u);
    RoboCheckEqInt(CssStyleContextSheetCount(NULL, RoboCssOriginAuthor),
                      0u);
    RoboCheckEqInt(CssStyleContextSheet(NULL, RoboCssOriginAuthor, 0u),
                      RoboCssStylesheetIdNone);
    RoboCheck(!CssStyleContextHasRuntimeSheet(NULL));
    RoboCheck(CssStyleContextPropertyTable(NULL) == NULL);
    CssStyleContextDestroy(NULL);
    CssStyleContextClear(NULL);
    CssStyleContextClearOrigin(NULL, RoboCssOriginAuthor);

    RoboCheckEqInt(CssStylesheetDestroy(authorA, NULL), RoboOk);
    RoboCheckEqInt(CssStylesheetDestroy(authorB, NULL), RoboOk);
    RoboCheckEqInt(CssStylesheetDestroy(runtime, NULL), RoboOk);
}

static int CssTestLookup(void *userdata, CssElementRef element,
                           CssElementView *outView)
{
    int *calls = (int *)userdata;

    *calls += 1;
    if (element != 5u) {
        return RoboNotFound;
    }
    CssElementViewInit(outView);
    return CssElementViewSetElement(outView, element,
                                             CssSliceFromText("div"), NULL);
}

static int CssTestInline(void *userdata, CssElementRef element,
                           CssSlice *outText)
{
    int *calls = (int *)userdata;

    *calls += 1;
    if (element != 5u) {
        return RoboNotFound;
    }
    *outText = CssSliceFromText("color: red");
    return RoboOk;
}

static void TestCssStyleContextBorrows(void)
{
    CssStyleContext *context = NULL;
    CssPropertyTable *table = NULL;
    int lookupCalls = 0;
    int inlineCalls = 0;
    CssElementView view = RoboCssCleared;
    CssSlice text;

    CssFixture();
    RoboCheckEqInt(CssPropertyTableCreate(&table, NULL), RoboOk);
    RoboCheckEqInt(CssStyleContextCreate(&context, NULL), RoboOk);

    /* The table is borrowed: the context holds a reference, not a copy, and
     * NULL is a legal way to say "no identities available". */
    RoboCheckEqInt(CssStyleContextSetPropertyTable(context, table, NULL),
                      RoboOk);
    RoboCheck(CssStyleContextPropertyTable(context) == table);
    RoboCheckEqInt(CssStyleContextSetPropertyTable(context, NULL, NULL),
                      RoboOk);
    RoboCheck(CssStyleContextPropertyTable(context) == NULL);
    RoboCheckEqInt(CssStyleContextSetPropertyTable(NULL, table, NULL),
                      RoboInvalidArgument);

    /* Both callbacks are borrowed with their userdata, and both can be cleared. */
    RoboCheckEqInt(CssStyleContextSetElementLookup(context, CssTestLookup,
                                                                &lookupCalls, NULL),
                      RoboOk);
    RoboCheck(CssStyleContextElementLookup(context) == CssTestLookup);
    RoboCheckEqInt(CssStyleContextSetInlineStyle(context, CssTestInline,
                                                              &inlineCalls, NULL),
                      RoboOk);
    RoboCheck(CssStyleContextInlineStyle(context) == CssTestInline);
    RoboCheckEqInt(CssStyleContextSetElementLookup(NULL, CssTestLookup,
                                                                NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssStyleContextSetInlineStyle(NULL, CssTestInline, NULL,
                                                              NULL),
                      RoboInvalidArgument);

    /* The seam works in isolation: the host answers a view, and answers the
     * inline text. RoboCSS calls neither yet — that is 9.4's and 9.8's job — so
     * this test drives them the way those phases will, and asserts the contract
     * a host has to satisfy. */
    CssElementViewInit(&view);
    RoboCheckEqInt(CssTestLookup(&lookupCalls, 5u, &view), RoboOk);
    RoboCheck(CssElementViewIsSet(&view));
    RoboCheck(CssSliceEqualsAscii(view.tagName, "div"));
    /* Installing a callback is not calling it: the count is one, from this
     * test's own call. */
    RoboCheckEqInt(lookupCalls, 1);
    /* An element the host cannot answer is reported missing, which is how a
     * matcher learns to stop rather than to guess. */
    RoboCheckEqInt(CssTestLookup(&lookupCalls, 6u, &view), RoboNotFound);
    RoboCheckEqInt(lookupCalls, 2);

    text = CssSliceEmpty();
    RoboCheckEqInt(CssTestInline(&inlineCalls, 5u, &text), RoboOk);
    RoboCheck(CssSliceEqualsAscii(text, "color: red"));
    RoboCheckEqInt(CssTestInline(&inlineCalls, 6u, &text), RoboNotFound);
    RoboCheckEqInt(inlineCalls, 2);

    RoboCheckEqInt(CssStyleContextSetElementLookup(context, NULL, NULL, NULL),
                      RoboOk);
    RoboCheck(CssStyleContextElementLookup(context) == NULL);
    RoboCheckEqInt(CssStyleContextSetInlineStyle(context, NULL, NULL, NULL),
                      RoboOk);
    RoboCheck(CssStyleContextInlineStyle(context) == NULL);

    CssStyleContextDestroy(context);
    RoboCheckEqInt(CssPropertyTableCount(table), 0u);
    CssPropertyTableDestroy(table);
}

/* ------------------------------------------------------------------ */
/* Resolution boundaries                                               */
/* ------------------------------------------------------------------ */

/*
 * The collection door, exercised where it is *not* about CSS at all: an empty
 * context declares nothing for any element, and that is a complete answer rather
 * than a missing feature. What a context with sheets in it does is the cascade
 * suites' business (css.cascade.*); what belongs here is the door's own shape —
 * what it refuses, what it fills, and what it leaves behind for a caller that
 * ignores the result code.
 */
static void TestCssStyleMatchDeclarationsEmptyContext(void)
{
    CssStyleContext *context = NULL;
    CssCascade cascade = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssDiag diag;

    CssFixture();
    RoboCheckEqInt(CssStyleContextCreate(&context, NULL), RoboOk);

    CssDiagReset(&diag);
    RoboCheckEqInt(CssStyleContextMatchDeclarations(context, 5u, &cascade,
                                                                &error, &diag),
                      RoboOk);
    RoboCheckEqInt(CssCascadeCount(&cascade), 0u);
    /* No sheets, so nothing was even looked at — and an empty answer over clean
     * input is complete, which is the distinction the result has to keep. */
    RoboCheckEqInt(cascade.unsupported, 0);
    RoboCheckEqInt(CssCascadeIsComplete(&cascade), 1);
    RoboCheckEqInt(cascade.element, 5u);
    RoboCheckEqInt(CssCascadeAt(&cascade, 0u) == NULL, 1);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssErrorIsError(&error), 0);
    /* Nothing failed, so there is nothing to diagnose. */
    RoboCheckEqInt(diag.code, RoboOk);
    CssCascadeDispose(&cascade);

    /* An element is not optional: nothing applies to no element, and an empty
     * result would be indistinguishable from a real "nothing matched". */
    RoboCheckEqInt(CssStyleContextMatchDeclarations(
                          context, RoboCssElementRefNone, &cascade, &error, &diag),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssCascadeCount(&cascade), 0u);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(diag.code, RoboInvalidArgument);
    RoboCheckEqInt(diag.domain, RoboCssDomainStyle);
    CssCascadeDispose(&cascade);

    RoboCheckEqInt(CssStyleContextMatchDeclarations(context, 5u, NULL,
                                                                &error, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssStyleContextMatchDeclarations(NULL, 5u, &cascade,
                                                                &error, NULL),
                      RoboInvalidArgument);
    CssCascadeDispose(&cascade);
    /* The error record is optional. */
    RoboCheckEqInt(CssStyleContextMatchDeclarations(context, 5u, &cascade,
                                                                NULL, NULL),
                      RoboOk);
    RoboCheckEqInt(CssCascadeCount(&cascade), 0u);
    CssCascadeDispose(&cascade);

    CssStyleContextDestroy(context);
}

/**
 * The computation door's argument contract.
 *
 * 9.8 is the phase that made this door real, so the assertions here are about a
 * door that answers rather than about one that refuses. An element of NONE is
 * still refused — "no element" is not an element — and a context with no host, no
 * table and no registry is still *accepted*, because a document with no
 * stylesheets is a document and not an error: its cascade is empty, nothing
 * inherits, and the answer is an empty style rather than a failed call.
 */
static void TestCssStyleComputeArguments(void)
{
    CssStyleContext *context = NULL;
    CssComputedStyle *style = NULL;
    CssError error = RoboCssCleared;
    CssDiag diag;

    CssFixture();
    RoboCheckEqInt(CssStyleContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);

    CssDiagReset(&diag);
    /* No host, no property table, no registry: an empty context still computes
     * a style, and the style is empty because nothing was there to make it
     * otherwise. An incomplete input is not an incomplete call. */
    RoboCheckEqInt(CssStyleContextComputeStyle(context, 5u, style, &error,
                                                           &diag),
                      RoboOk);
    RoboCheckEqInt(CssComputedStyleCount(style), 0u);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssComputedStyleIsTruncated(style), 0);

    RoboCheckEqInt(CssStyleContextComputeStyle(context, 5u, NULL, &error,
                                                           NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    RoboCheckEqInt(CssStyleContextComputeStyle(NULL, 5u, style, &error,
                                                           NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssStyleContextComputeStyle(
                          context, RoboCssElementRefNone, style, &error, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(error.kind, RoboCssErrorKindNone);
    /* The error record is optional, as every other door in this file says. */
    RoboCheckEqInt(CssStyleContextComputeStyle(context, 5u, style, NULL,
                                                           NULL),
                      RoboOk);

    CssComputedStyleDestroy(style);
    CssStyleContextDestroy(context);
}

/* ------------------------------------------------------------------ */
/* Computed container                                                  */
/* ------------------------------------------------------------------ */

static void TestCssComputedContainer(void)
{
    CssComputedStyle *style = NULL;
    CssPropertyTable *table = NULL;
    CssPropertyId color = RoboCssPropertyIdNone;
    CssPropertyId padding = RoboCssPropertyIdNone;
    CssValue value = RoboCssCleared;
    CssPropertyId property = RoboCssPropertyIdNone;

    CssFixture();
    RoboCheckEqInt(CssComputedStyleCreate(NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCount(style), 0u);

    RoboCheckEqInt(CssPropertyTableCreate(&table, NULL), RoboOk);
    RoboCheckEqInt(CssPropertyTableRegister(table, "color", 0u, &color, NULL),
                      RoboOk);
    RoboCheckEqInt(CssPropertyTableRegister(table, "padding", 0u, &padding,
                                                       NULL),
                      RoboOk);

    RoboCheck(!CssComputedStyleHas(style, color));
    RoboCheck(CssComputedStyleGet(style, color) == NULL);

    RoboCheckEqInt(CssValueMakeList(&value, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleSet(style, color, &value, NULL), RoboOk);
    RoboCheck(CssValueIsEmpty(&value)); /* moved in */
    RoboCheck(CssComputedStyleHas(style, color));
    RoboCheck(CssComputedStyleGet(style, color) != NULL);
    RoboCheckEqInt(CssComputedStyleCount(style), 1u);

    RoboCheckEqInt(CssValueMakeList(&value, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleSet(style, padding, &value, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedStyleCount(style), 2u);

    /* Iteration order is insertion order, and replacing a property keeps its
     * position rather than moving it to the end: order must not depend on how
     * often a property was written. */
    RoboCheckEqInt(CssComputedStylePropertyAt(style, 0u, &property, NULL),
                      RoboOk);
    RoboCheckEqInt(property, color);
    RoboCheckEqInt(CssComputedStylePropertyAt(style, 1u, &property, NULL),
                      RoboOk);
    RoboCheckEqInt(property, padding);

    RoboCheckEqInt(CssValueMakeList(&value, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleSet(style, color, &value, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCount(style), 2u);
    RoboCheckEqInt(CssComputedStylePropertyAt(style, 0u, &property, NULL),
                      RoboOk);
    RoboCheckEqInt(property, color);

    RoboCheck(CssComputedStyleValueAt(style, 0u) != NULL);
    RoboCheck(CssComputedStyleValueAt(style, 2u) == NULL);
    RoboCheckEqInt(CssComputedStylePropertyAt(style, 2u, &property, NULL),
                      RoboNotFound);

    /* Removing keeps the rest in order too. */
    RoboCheckEqInt(CssComputedStyleRemove(style, color, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleCount(style), 1u);
    RoboCheckEqInt(CssComputedStylePropertyAt(style, 0u, &property, NULL),
                      RoboOk);
    RoboCheckEqInt(property, padding);
    RoboCheckEqInt(CssComputedStyleRemove(style, color, NULL), RoboNotFound);

    /* A property that was never assigned has no value at all, and "no property"
     * is refused rather than stored. */
    RoboCheckEqInt(CssValueMakeList(&value, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleSet(style, RoboCssPropertyIdNone,
                                                  &value, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(value.kind, RoboCssValueKindList);
    CssValueDispose(&value);
    RoboCheckEqInt(CssComputedStyleSet(style, color, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssComputedStyleSet(NULL, color, &value, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssComputedStylePropertyAt(style, 0u, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(CssComputedStylePropertyAt(NULL, 0u, &property, NULL),
                      RoboInvalidArgument);

    CssComputedStyleClear(style);
    RoboCheckEqInt(CssComputedStyleCount(style), 0u);
    RoboCheck(!CssComputedStyleHas(style, padding));
    /* Clearing leaves a usable container. */
    RoboCheckEqInt(CssValueMakeList(&value, NULL), RoboOk);
    RoboCheckEqInt(CssComputedStyleSet(style, padding, &value, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedStyleCount(style), 1u);

    RoboCheckEqInt(CssComputedStyleCount(NULL), 0u);
    RoboCheck(CssComputedStyleGet(NULL, color) == NULL);
    RoboCheck(!CssComputedStyleHas(NULL, color));
    RoboCheck(CssComputedStyleValueAt(NULL, 0u) == NULL);
    CssComputedStyleClear(NULL);
    CssComputedStyleDestroy(NULL);

    CssComputedStyleDestroy(style);
    CssPropertyTableDestroy(table);
}

static void TestCssComputedLifetimeBalances(void)
{
    CssSource source = RoboCssCleared;
    int weEnabled;
    size_t baseline;

    CssFixture();
    CssSourceOfString(&source, "4px");
    weEnabled = CssAllocatorEnsureEnabled();
    baseline = MemDebugLiveCount();
    {
        int i;

        for (i = 0; i < 24; i++) {
            CssComputedStyle *style = NULL;
            CssValue function = RoboCssCleared;
            CssValue child = RoboCssCleared;
            int j;

            RoboCheckEqInt(CssComputedStyleCreate(&style, NULL), RoboOk);
            for (j = 1; j <= 6; j++) {
                RoboCheckEqInt(CssValueMakeFunction(
                                      &function, CssSliceFromText("f"),
                                      CssRangeAt(0u), NULL),
                                  RoboOk);
                CssValueTokenAt(&child, &source, "4px", NULL);
                RoboCheckEqInt(CssValueAppend(&function, &child, NULL),
                                  RoboOk);
                RoboCheckEqInt(CssComputedStyleSet(
                                      style, (CssPropertyId)j, &function, NULL),
                                  RoboOk);
            }
            RoboCheckEqInt(CssComputedStyleCount(style), 6u);
            /* Replacing every property must not accumulate values. */
            for (j = 1; j <= 6; j++) {
                CssValueTokenAt(&child, &source, "4px", NULL);
                RoboCheckEqInt(CssComputedStyleSet(
                                      style, (CssPropertyId)j, &child, NULL),
                                  RoboOk);
            }
            RoboCheckEqInt(CssComputedStyleCount(style), 6u);
            CssComputedStyleDestroy(style);
            RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
        }
    }
    RoboCheckEqInt((int)MemDebugLiveCount(), (int)baseline);
    CssAllocatorRestore(weEnabled);
    CssSourceDispose(&source);
}

void TestRegisterCssStyle(void);

void TestRegisterCssStyle(void)
{
    TestRegister("css.style.element_view", TestCssElementView);
    TestRegister("css.style.element_view_classes_and_attributes",
                       TestCssElementViewClassesAndAttributes);
    TestRegister("css.style.context_sheets", TestCssStyleContextSheets);
    TestRegister("css.style.context_borrows", TestCssStyleContextBorrows);
    TestRegister("css.style.match_declarations_empty_context",
                       TestCssStyleMatchDeclarationsEmptyContext);
    TestRegister("css.style.compute_arguments",
                       TestCssStyleComputeArguments);
    TestRegister("css.computed.container", TestCssComputedContainer);
    TestRegister("css.computed.lifetime_balances",
                       TestCssComputedLifetimeBalances);
}
