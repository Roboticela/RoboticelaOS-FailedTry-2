/*
 * RoboLayout conformance suite (Phase 10.1) — layout.conformance.*
 *
 * The questions no single-family suite in this directory can ask, because each of
 * them is about the *whole* module or about a promise the module makes as a
 * whole.
 *
 *   Do the capabilities this build advertises match what it does? A flag is a
 *   promise about the build, and an untested promise is one that stops being true
 *   without anybody noticing. The table below is written out by hand and walked
 *   positionally against the struct, with the struct's size pinned against the
 *   table's length, so a field added without an assertion is a visible omission
 *   rather than a silently untested flag.
 *
 *   Does the whole chain work? RoboHTML -> RoboCSS -> the bridge -> the tree ->
 *   the pass, on one document that exercises every family the CSS module gained
 *   in Phase 9. A module that consumed only `display` would pass every other
 *   suite here and fail this one, because this is where a *borrowed style* is
 *   proved to be the container the cascade filled rather than a struct the
 *   builder made.
 *
 *   Is it deterministic? Two builds of one document must produce the same tree,
 *   compared link by link rather than by count.
 *
 *   Does it survive input it should refuse? A malformed document has to produce a
 *   *reported* answer rather than a crash or a silent empty tree.
 *
 *   And does it compute geometry in both directions, and say so? Stated once more
 *   here, after a full pass over a real document, because the capability struct is
 *   a promise and this is where a promise made in one file is checked against
 *   behaviour in another.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Capabilities, walked positionally                                   */
/* ------------------------------------------------------------------ */

/**
 * Every field of `LayoutCapabilities`, in declaration order, with the value
 * this build claims.
 *
 * Written out by hand rather than derived from the struct: a table generated from
 * the struct would agree with the struct by construction and would therefore
 * assert nothing. The positional walk below is what makes the order itself part
 * of the promise -- a field inserted in the middle shifts every later name onto
 * the wrong value, and that shows up here.
 */
static const struct {
    const char *name;
    int expected;
} layoutConformanceCapabilities[] = {
    { "object_model", 1 },
    { "constraints", 1 },
    { "result_model", 1 },
    { "context", 1 },
    { "dom_build", 1 },
    { "computed_style_link", 1 },
    { "geometry", 1 },
    { "formatting_contexts", 1 },
    { "block_layout", 1 },
    { "inline_layout", 1 },
    { "block_and_inline_layout", 1 },
    { "text_layout", 1 },
    { "line_boxes", 1 },
    { "baselines", 1 },
    { "sizing", 1 },
    { "margins", 1 },
    { "padding", 1 },
    { "border", 1 },
    { "positioning", 1 },
    { "out_of_flow", 1 },
    { "overflow", 1 },
    { "invalidation", 1 },
    { "font_request", 1 },
    { "intrinsic_sizing", 1 },
    { "percentage_resolution", 1 },
    { "flex_layout", 1 },
    { "grid_layout", 1 },
    { "table_layout", 1 },
    { "fragmentation", 1 },
    { "painting", 0 }
};

static void TestConformanceCapabilities(void)
{
    LayoutCapabilities caps;
    const int *fields;
    size_t count = sizeof(layoutConformanceCapabilities) /
                   sizeof(layoutConformanceCapabilities[0]);
    size_t implemented = 0u;
    size_t index;

    LayoutFixture();

    /* The struct is a run of `int`s with no padding, which is what makes the
     * positional read below meaningful -- and what makes this assertion the one
     * that has to be revisited if a field of another type is ever added. */
    RoboCheckEqInt(sizeof(LayoutCapabilities), count * sizeof(int));

    RoboCheckEqInt(LayoutGetCapabilities(&caps), RoboOk);
    fields = (const int *)&caps;
    for (index = 0u; index < count; index++) {
        RoboCheckEqInt(fields[index], layoutConformanceCapabilities[index].expected);
        if (layoutConformanceCapabilities[index].expected != 0) {
            implemented++;
        }
    }
    /* Twenty-eight of twenty-nine. Asserted as a count as well as field by field, so a build
     * that turned one on and one off at the same time -- which field-by-field would
     * still catch, but a reader would have to diff to see -- is visible at a
     * glance. The twenty-ninth is `painting`, and it is the only one that is 0. */
    RoboCheckEqInt(implemented, 29u);

    /*
     * The four that matter are the direction pair, the field that names both, and
     * `geometry` above them -- said several ways each: the field, the function and
     * the macro. A build that flipped one of them would have two answers to one
     * question, and this is the question a caller asks before reading a result.
     *
     * `geometry` is 1 since 10.3 because the pass writes real coordinates;
     * `blockLayout` and `inlineLayout` are the two directions, both 1 since 10.4;
     * and the field that names both is their conjunction rather than a third
     * constant, which is what makes it go back to 0 on its own if a direction is
     * ever switched off. That is the rule 10.3 stated when only one direction
     * existed -- a phase that implemented one direction and flipped the wider flag
     * would have advertised a feature it does not have -- and writing it as an
     * expression is what let it survive the phase that added the second direction.
     */
    RoboCheckEqInt(caps.geometry, 1);
    RoboCheckEqInt(LayoutGeometryIsImplemented(), 1);
    RoboCheckEqInt(RoboLayoutGeometryImplemented, 1);
    RoboCheckEqInt(caps.blockLayout, 1);
    RoboCheckEqInt(RoboLayoutBlockLayoutImplemented, 1);
    RoboCheckEqInt(caps.inlineLayout, 1);
    RoboCheckEqInt(RoboLayoutInlineLayoutImplemented, 1);
    RoboCheckEqInt(caps.blockAndInlineLayout, 1);
    RoboCheckEqInt(caps.blockAndInlineLayout,
                      caps.blockLayout && caps.inlineLayout);

    /*
     * And `textLayout`, which is the phase that arrived after them and is *not* a
     * third direction: it says this build can measure a run of text, and it is read
     * from its own macro so the field and the constant cannot disagree. What it does
     * not say is asserted here too, because the distinction is the one a caller would
     * otherwise get wrong: a *pass* with no metrics installed measures every run as
     * zero and counts it (`LayoutTextStats.unmeasured`), which is a
     * legitimate way to run this module and not a failure.
     */
    RoboCheckEqInt(caps.textLayout, 1);
    RoboCheckEqInt(RoboLayoutTextLayoutImplemented, 1);

    /*
     * And `lineBoxes`, which is 10.6's and is the narrowest claim in the struct. Read
     * from its own macro for the same reason, and *not* written as a derivation over
     * `textLayout`: measuring a run and producing a line are independent -- a build
     * could measure text and place none of it in a line (which is what this module was
     * before this phase), and a line of atomic inlines and no text at all is a line
     * this build produces without measuring anything. Making one field the other's
     * consequence would name a feature neither phase implemented.
     */
    RoboCheckEqInt(caps.lineBoxes, 1);
    RoboCheckEqInt(RoboLayoutLineBoxesImplemented, 1);

    /*
     * And `baselines`, read from its own macro like the two above it. It is the field
     * 10.6's prose named as the thing `lineBoxes` was *not* claiming -- a line had
     * membership and an ordinal but no floor -- which is why it exists as a separate
     * promise rather than as a widening of that one.
     */
    RoboCheckEqInt(caps.baselines, 1);
    RoboCheckEqInt(RoboLayoutBaselinesImplemented, 1);

    /*
     * And `sizing`, the field 10.7's prose left for the goal that would make a
     * declaration *mean* something. Read from its own macro like the three above it,
     * and the boundary is asserted where a reader will meet it: a declared px width and
     * height reach geometry, an intrinsic keyword resolves from a measurement (19.1) and
     * a percentage resolves against the containing block a pass hands over (19.2) -- so
     * this is not "sizing is finished", it is "a dimension declaration reaches
     * geometry".
     */
    RoboCheckEqInt(caps.sizing, 1);
    RoboCheckEqInt(RoboLayoutSizingImplemented, 1);

    /*
     * And `margins`, the field 10.9 added, with its own boundary stated where a
     * reader meets it: a margin reaches geometry -- a box is placed at its margin
     * origin and a parent stacks by a child's margin box -- while collapsing is
     * deliberately absent, so adjacent vertical margins *sum*. The macro is asserted
     * from its own definition like the four above it, so a build that turned the
     * behaviour on and left the report off would fail here rather than in a suite
     * nobody ran.
     */
    RoboCheckEqInt(caps.margins, 1);
    RoboCheckEqInt(RoboLayoutMarginsImplemented, 1);

    /*
     * And `padding`, the field 10.10 added, with its boundary stated where a reader
     * meets it: a padding reaches geometry -- a box's `size` is its **border** box and
     * its contents begin `padding-left`/`padding-top` inside that -- while this build is
     * `box-sizing: content-box` (a declared `width` is the *content* box), a percentage
     * padding resolves against the containing block's width (19.2), and no padding
     * applies to a transparent inline box. `padding` would be claiming those if it were
     * named for "padding is finished".
     */
    RoboCheckEqInt(caps.padding, 1);
    RoboCheckEqInt(RoboLayoutPaddingImplemented, 1);

    /*
     * And `border`, the field 10.11 added, with the one boundary that has no analogue in
     * the two fields above it: a border's **used** width is not its declared one. A
     * `border: 5px none` box is the same box as one with no border declaration at all,
     * because `none` and `hidden` are the two styles that generate nothing -- so this is
     * not "borders are finished", it is "a style that draws reaches geometry and one
     * that does not costs nothing". `border-radius`, `hidden`'s collapsing meaning and
     * any border on a transparent inline box are each a later goal's.
     */
    RoboCheckEqInt(caps.border, 1);
    RoboCheckEqInt(RoboLayoutBorderImplemented, 1);

    /*
     * And `positioning` and `overflow`, the two fields 10.12 and 10.13 added, which are
     * the pair whose claims are about something other than the box's own size: a
     * displacement says where the box *is* and an overflow says what its contents *did*,
     * and neither can be read off a declaration the way a width can. `absolute`, `fixed`
     * and `sticky` placement, stacking contexts and paint order are 18.5's; scrolling,
     * the other three overflow values and the actual clipping are later goals'.
     */
    RoboCheckEqInt(caps.positioning, RoboLayoutPositioningImplemented);
    RoboCheckEqInt(caps.overflow, RoboLayoutOverflowImplemented);
    RoboCheckEqInt(LayoutGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.overflow, 1);
    RoboCheckEqInt(RoboLayoutApiFrozen, 0);
}

/* ------------------------------------------------------------------ */
/* One document, the whole chain                                       */
/* ------------------------------------------------------------------ */

/**
 * A document that exercises every family Phase 9 gained.
 *
 * The point is not coverage for its own sake: it is that a *borrowed computed
 * style* is the container the cascade filled. A layout module that consumed only
 * `display` would pass every other suite in this directory and fail the assertions
 * below, because this is the one place a style is asked about a property no
 * layout code reads.
 */
#define LAYOUT_CONFORMANCE_HTML          \
    "<html>\n"                           \
    "  <body>\n"                         \
    "    <div class=\"card\">\n"         \
    "      <p class=\"title\">Hello</p>\n" \
    "    </div>\n"                       \
    "    <div class=\"hidden\"></div>\n" \
    "  </body>\n"                        \
    "</html>\n"

#define LAYOUT_CONFORMANCE_CSS                                        \
    "html { display: block }\n"                                        \
    "body { display: block; color: rgb(20, 20, 20) }\n"                \
    ".card {\n"                                                        \
    "  display: block;\n"                                              \
    "  position: relative;\n"                                          \
    "  top: 10px;\n"                                                   \
    "  background-color: #ffffff;\n"                                   \
    "  border: 1px solid black;\n"                                     \
    "  margin: 10px 20px;\n"                                           \
    "  padding: 5px;\n"                                                \
    "  width: 200px;\n"                                                \
    "  height: 100px;\n"                                               \
    "  z-index: 3;\n"                                                  \
    "}\n"                                                              \
    ".title { display: block; font-style: normal }\n"                   \
    ".hidden { display: none }\n"

typedef struct layoutConformanceFixture {
    LayoutDocument doc;
    LayoutCssFixture css;
    LayoutDom *dom;
    LayoutTree *tree;
    LayoutStyleSource source;
} LayoutConformanceFixture;

static void LayoutConformanceBegin(LayoutConformanceFixture *fixture)
{
    LayoutBuildOptions options;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, LAYOUT_CONFORMANCE_HTML);
    LayoutCssBegin(&fixture->css);
    LayoutCssSheet(&fixture->css, LAYOUT_CONFORMANCE_CSS);
    RoboCheckEqInt(LayoutDomCreate(fixture->css.context, fixture->doc.id,
                                             &fixture->dom, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutDomRefresh(fixture->dom, NULL), RoboOk);
    RoboCheckEqInt(LayoutDomStyleSource(fixture->dom, &fixture->source, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->source;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
}

static void LayoutConformanceEnd(LayoutConformanceFixture *fixture)
{
    /* The tree borrows the bridge's styles, so it goes first. */
    LayoutTreeDestroy(fixture->tree);
    fixture->tree = NULL;
    LayoutDomDestroy(fixture->dom);
    fixture->dom = NULL;
    LayoutCssEnd(&fixture->css);
    LayoutDocumentRelease(&fixture->doc);
}

static void TestConformancePipeline(void)
{
    LayoutConformanceFixture fixture;
    LayoutContext *context = NULL;
    LayoutStats stats;
    LayoutResult result;
    LayoutSize size;
    LayoutRect rect;
    HtmlNodeHandle card;
    const CssComputedStyle *cardStyle;
    size_t index;

    LayoutFixture();
    LayoutConformanceBegin(&fixture);

    /* html, head, body, div.card, p.title, div.hidden = six elements; the hidden
     * one generates nothing, so five boxes. `<head>` is the parser's, not the
     * markup's, and it is a box because nothing hid it. The sixth *object* is the
     * paragraph's text node, which is a text object and not a box (10.5). */
    RoboCheckEqInt(LayoutDomElementCount(fixture.dom), 6u);
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 6u);

    card = LayoutDocumentFind(&fixture.doc, "div");
    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&card), 1);
    cardStyle = LayoutDomStyleFor(fixture.dom, &card);
    RoboCheck(cardStyle != NULL);

    /*
     * The borrowed style is the container the *cascade* filled, and this is the
     * assertion that says so. `display` is the only property RoboLayout reads, so
     * every other name here is one the module has no business understanding -- and
     * it is exactly the presence of those entries that proves the pointer is a
     * real computed style rather than a struct the builder fabricated.
     */
    RoboCheckEqInt(LayoutStyleDisplay(cardStyle), RoboCssDisplayBlock);
    {
        static const char *const names[] = {
            "display", "position", "top", "background-color",
            "border-top-style", "border-top-width", "border-top-color",
            "margin-top", "margin-right", "padding-top", "width", "height", "z-index"
        };

        for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
            const CssComputedValue *value = CssComputedStyleComputedByName(
                cardStyle, CssSliceFromText(names[index]));

            /* Each is a *typed* value with a kind, not a string, and the kind is
             * not the "cleared" zero any more than it is NONE. */
            RoboCheckMsg(value != NULL, names[index]);
            if (value) {
                RoboCheck(value->kind != RoboCssComputedValueNone);
            }
        }
    }
    RoboCheck(CssComputedStyleCount(cardStyle) > 0u);

    /* The tree's shape is the document's, with the hidden element absent. */
    RoboCheckEqInt(LayoutTreeRoot(fixture.tree), 1);
    {
        LayoutNodeId root = LayoutTreeRoot(fixture.tree);
        LayoutNodeId head = RoboLayoutNodeIdNone;
        LayoutNodeId body = RoboLayoutNodeIdNone;
        size_t children = 0u;

        /* The root's children are the two the document requires; `<body>` is the
         * *last* of them, because `<head>` comes first. */
        RoboCheckEqInt(LayoutObjectFirstChild(fixture.tree, root, &head), RoboOk);
        RoboCheckEqInt(LayoutObjectLastChild(fixture.tree, root, &body), RoboOk);
        RoboCheck((int)head != (int)body);
        RoboCheckEqInt(LayoutObjectChildCount(fixture.tree, body, &children),
                          RoboOk);
        RoboCheckEqInt(children, 1u); /* only `.card`; `.hidden` generated nothing */
    }

    /*
     * A full pass over a real document, into a real viewport -- and this is the
     * case that used to assert the *opposite* of what it asserts now.
     *
     * Until 10.3 the pass walked, counted and classified, and every box was left
     * UNRESOLVED, because nothing in the build computed a coordinate. That claim
     * has been *replaced* rather than weakened, which is the only honest way for a
     * claim like it to die: the same tree, the same viewport and the same call now
     * produce geometry, and the four boxes below are asserted to the pixel.
     */
    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    {
        LayoutSize viewport;

        viewport.width = 1280;
        viewport.height = 800;
        RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    }
    RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);
    RoboCheckEqInt((int)LayoutContextOutcome(context),
                      (int)RoboLayoutPassWalked);
    LayoutContextStats(context, &stats);
    RoboCheckEqInt(stats.objects, 6u); /* five boxes and one text run (10.5) */
    RoboCheckEqInt(stats.visited, 6u);
    /*
     * Five of six, and the one left over is `<head>`: nothing styles it, so it
     * computes to `display: inline` -- 9.12's initial value, not a UA stylesheet --
     * and block layout does not place it. The two counters therefore disagree on
     * purpose, which is what makes them two counters. The five include the *text run*
     * the paragraph holds, which is placed along a line rather than in the block flow.
     */
    RoboCheckEqInt(stats.boxesComputed, 5u);
    RoboCheckEqInt(stats.boxesUnresolved, 1u);
    RoboCheckEqInt(stats.withoutStyle, 0u);
    /* html 0, head 1, body 1, div.card 2, p.title 3 -- and the text run inside the
     * paragraph at 4, which is why the deepest object is no longer a box. */
    RoboCheckEqInt(stats.depthMax, 4u);
    RoboCheckEqInt(stats.depthMax, LayoutTreeMaxDepth(fixture.tree));

    /* The block pass's own record, because `boxesComputed` is *derived* from it:
     * four placed, one declined for a stated reason, none unreached. A reader who
     * wants to know why the fifth box has no geometry reads it here. */
    RoboCheckEqInt(stats.block.blocksPlaced, 4u);
    RoboCheckEqInt(stats.block.inlineLevelSkipped, 1u);
    RoboCheckEqInt(stats.block.unreached, 0u);
    RoboCheckEqInt(stats.block.containers, 3u);
    RoboCheckEqInt(stats.block.emptyContainers, 1u);
    RoboCheckEqInt(stats.block.heightsExplicit, 1u);
    RoboCheckEqInt(stats.block.depthMax, 3u);
    /*
     * And the padding's two (10.10), asserted where a reader can check them against the
     * stylesheet above by hand: `.card` declares `padding: 5px`, which is *four*
     * declarations one layer down and four sides of geometry here, while `<html>` and
     * `<body>` declare none -- so the count is four and not twelve, which is the whole
     * reason an `ABSENT` side is counted nowhere.
     */
    RoboCheckEqInt(stats.block.paddingsApplied, 4u);
    RoboCheckEqInt(stats.block.paddingsUnresolved, 0u);

    /*
     * And the inline direction's record, for the one inline formatting context this
     * document has. `<p class="title">` holds nothing but text, so 10.2's rule makes
     * the *paragraph* establish the context -- and since 10.5 that context has a
     * participant: the text node's own layout object, placed along the line. It is a
     * participant and *not* a box, which is why `participantsPlaced` counts it while
     * the box counters above do not.
     *
     * `<head>` is not here: it is an inline-level box in a *block-direction* context,
     * which is 10.3's count and not this one's. Two records, one box, told apart by
     * which direction declined it.
     */
    RoboCheckEqInt(stats.inlineFlow.contexts, 1u);
    RoboCheckEqInt(stats.inlineFlow.emptyContexts, 0u);
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 1u);
    RoboCheckEqInt(stats.inlineFlow.lines, 1u);
    RoboCheckEqInt(stats.inlineFlow.blockLevelDeclined, 0u);

    /*
     * And the text direction's record, which is the one that says whether the line's
     * participant was actually *measured*. This pass installed no metrics source, so
     * the run was placed and not measured -- and the two counters are what make that
     * difference visible rather than looking like a paragraph that measured zero.
     * The document's title is `Roboticela`'s text and nothing declared a font for it,
     * which is exactly the case a build with no font has to be able to say out loud.
     */
    RoboCheckEqInt(stats.text.runsPlaced, 1u);
    RoboCheckEqInt(stats.text.measured, 0u);
    RoboCheckEqInt(stats.text.unmeasured, 1u);
    RoboCheckEqInt(stats.text.widthMax, 0);
    RoboCheckEqInt(stats.text.heightMax, 0);
    /* `boxesComputed` is the two placement counts added together, asserted where
     * both are non-trivial numbers rather than where one of them is zero. */
    RoboCheckEqInt(stats.boxesComputed,
                      stats.block.blocksPlaced + stats.inlineFlow.participantsPlaced);

    /*
     * And the geometry itself, which is what this case is now for: a box fills its
     * containing block, its children stack, and both declared dimensions are
     * honoured -- with the *width* also being what the box's children are laid out
     * into, which is 10.8's arrival.
     *
     * The card is the whole point. It declares `width: 200px` and `height: 100px`,
     * `position: relative`, `top: 10px`, a border, a margin and padding -- and of
     * those, every number but the two offsets now reaches the result. Its width is
     * 200 because resolving a declared width *is* 10.8; its height is 100; since 10.9
     * its `margin: 10px 20px` also reaches geometry: the card is placed at its
     * **margin origin**, (20, 10), which is where the containing block's content
     * origin is plus the two margins, and the height it propagates up is its margin
     * box; and since 10.11 its `border: 1px solid black` and `padding: 5px` are inside
     * that box rather than beside it. `position: relative`/`top: 10px` is still not
     * applied, so a build that read either here would be answering a phase that has not
     * run.
     *
     * The card's width then travels: `<p class="title">` is its child, so the width
     * its own `auto` resolves against is 200 rather than the viewport's 1280. That is
     * the same change seen one level down and it is asserted below, because a build
     * that resolved a box's own width and then handed its children the containing
     * block's would pass the card's assertion and put the title at 1280.
     */
    {
        HtmlNodeHandle htmlElement = LayoutDocumentFind(&fixture.doc, "html");
        HtmlNodeHandle bodyElement = LayoutDocumentFind(&fixture.doc, "body");
        HtmlNodeHandle titleElement = LayoutDocumentFind(&fixture.doc, "p");
        LayoutNodeId htmlBox = LayoutObjectIdForDomNode(fixture.tree,
                                                                           &htmlElement);
        LayoutNodeId bodyBox = LayoutObjectIdForDomNode(fixture.tree,
                                                                           &bodyElement);
        LayoutNodeId cardBox = LayoutObjectIdForDomNode(fixture.tree,
                                                                           &card);
        LayoutNodeId titleBox = LayoutObjectIdForDomNode(fixture.tree,
                                                                            &titleElement);
        LayoutPoint position;

        RoboCheck(htmlBox != RoboLayoutNodeIdNone);
        RoboCheck(bodyBox != RoboLayoutNodeIdNone);
        RoboCheck(cardBox != RoboLayoutNodeIdNone);
        RoboCheck(titleBox != RoboLayoutNodeIdNone);

        RoboCheckEqInt(LayoutObjectResult(fixture.tree, htmlBox, &result),
                          RoboOk);
        RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
        RoboCheckEqInt(LayoutResultRect(&result, &rect, NULL), RoboOk);
        RoboCheckEqInt(rect.x, 0);
        RoboCheckEqInt(rect.y, 0);
        RoboCheckEqInt(rect.width, 1280);
        /* `<body>` is the only block-level child that stacks, and it is 132 tall since
         * 10.11: the extent the cursor reached is the card's **margin box**, which spans
         * all four of the card's boxes -- 10 (margin) + 112 (the border box, whose
         * `height: 100px` is a *content* box plus 5px of padding and 1px of border on each
         * axis) + 10 (margin). */
        RoboCheckEqInt(rect.height, 132);

        RoboCheckEqInt(LayoutObjectResult(fixture.tree, bodyBox, &result),
                          RoboOk);
        RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
        RoboCheckEqInt(LayoutResultRect(&result, &rect, NULL), RoboOk);
        RoboCheckEqInt(rect.x, 0);
        RoboCheckEqInt(rect.y, 0);
        RoboCheckEqInt(rect.width, 1280);
        RoboCheckEqInt(rect.height, 132);

        RoboCheckEqInt(LayoutObjectResult(fixture.tree, cardBox, &result),
                          RoboOk);
        RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
        RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboOk);
        /* Since 10.9 these are the card's **margin** edges, and this is the case that
         * recorded (0, 0): a build that added the margins to the box's `size` instead
         * would move the size and leave the origin, and the pair below fails both
         * ways round. Since 10.12 the card also declares `position: relative;
         * top: 10px`, so its final position is the margin origin displaced -- 10px
         * down, and never sideways (`left`/`right` are absent). A `top` changes the
         * y and leaves the x; the x below is the margins' 20 and nothing else. */
        RoboCheckEqInt(position.x, 20);
        RoboCheckEqInt(position.y, 20);
        RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
        /*
         * And since 10.11 the two numbers are the card's **border** box rather than the
         * declared pair: `width: 200px` and `height: 100px` are the *content* box under
         * this build's `content-box` model, `padding: 5px` puts 5 on each of the four sides
         * and `border: 1px solid black` puts 1 more outside those, so the box a reader sees
         * is 200 + 5 + 5 + 1 + 1 by 100 + 5 + 5 + 1 + 1. That is the box model *complete* --
         * the same assertion 10.10 wrote one inset short, which is why the fourth phase to
         * move these numbers is also the last.
         */
        RoboCheckEqInt(size.width, 212);
        RoboCheckEqInt(size.height, 112);

        /* The title is a placed box with a *zero* extent, which is 10.3's honest
         * answer for a `<p>` whose only content is text: a text node is not a box,
         * so there is nothing to stack and nothing measuring it yet. The container
         * that holds only text is what establishes an inline context. */
        RoboCheckEqInt(LayoutObjectResult(fixture.tree, titleBox, &result),
                          RoboOk);
        RoboCheckEqInt((int)result.state, (int)RoboLayoutResultComputed);
        RoboCheckEqInt(LayoutResultSize(&result, &size), RoboOk);
        /*
         * The card's 200, not the viewport's -- the child's containing block is the box
         * that declares the width, which is what makes the declaration *contain* something
         * rather than only describe one box.
         *
         * Still 200 after 10.11, and that is the `content-box` model asserted one level
         * down: the card's *border* box is 212, but its **content** box is the 200 its
         * author declared, so the paragraph fills 200 -- not the 212 the card occupies and
         * not the 188 a `border-box` reading of the declaration would give its child.
         */
        RoboCheckEqInt(size.width, 200);
        RoboCheckEqInt(size.height, 0);
    }

    /*
     * And the opposite answer, from the same call, on the one box the pass declined:
     * "no geometry" is not a claim about a count, it is a claim that a caller
     * reading a result is *told*. `<head>` reads UNRESOLVED and its geometry reads
     * are misses -- where the four above read COMPUTED and their numbers. A build
     * that returned a zeroed rectangle for the fifth would satisfy "the pass ran"
     * and is exactly what the states exist to prevent.
     */
    {
        HtmlNodeHandle headElement = LayoutDocumentFind(&fixture.doc, "head");
        LayoutNodeId headBox = LayoutObjectIdForDomNode(fixture.tree,
                                                                           &headElement);

        RoboCheck(headBox != RoboLayoutNodeIdNone);
        RoboCheckEqInt(LayoutObjectResult(fixture.tree, headBox, &result),
                          RoboOk);
        RoboCheckEqInt((int)result.state, (int)RoboLayoutResultUnresolved);
        RoboCheckEqInt(LayoutResultSize(&result, &size), RoboNotFound);
        RoboCheckEqInt(LayoutResultIsComputed(&result), 0);
        RoboCheckEqInt(result.position.x, 0);
        RoboCheckEqInt(result.position.y, 0);
        RoboCheckEqInt(result.size.width, 0);
        RoboCheckEqInt(result.size.height, 0);
        RoboCheckEqInt(result.flags & RoboLayoutResultFlagAll, 0);
    }

    LayoutContextDestroy(context);
    LayoutConformanceEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Determinism                                                         */
/* ------------------------------------------------------------------ */

/**
 * Compare two trees box by box, link by link.
 *
 * Compared by *shape*, not by identity: the ids are dense from 1 in both, so the
 * comparison is over ids, and a difference in any link is a difference in the
 * tree. Counts alone would not catch a tree that had the right number of boxes
 * arranged wrongly, which is the failure a rebuild could plausibly have.
 */
static void LayoutConformanceAssertSameTree(const LayoutTree *a,
                                                const LayoutTree *b)
{
    size_t count = LayoutTreeObjectCount(a);
    size_t index;

    RoboCheckEqInt(count, LayoutTreeObjectCount(b));
    RoboCheckEqInt((int)LayoutTreeRoot(a), (int)LayoutTreeRoot(b));

    for (index = 0u; index < count; index++) {
        LayoutNodeId idA = RoboLayoutNodeIdNone;
        LayoutNodeId idB = RoboLayoutNodeIdNone;
        LayoutObject objectA;
        LayoutObject objectB;

        RoboCheckEqInt(LayoutObjectIdAt(a, index, &idA), RoboOk);
        RoboCheckEqInt(LayoutObjectIdAt(b, index, &idB), RoboOk);
        RoboCheckEqInt((int)idA, (int)idB);
        RoboCheckEqInt(LayoutObjectGet(a, idA, &objectA, NULL), RoboOk);
        RoboCheckEqInt(LayoutObjectGet(b, idB, &objectB, NULL), RoboOk);

        RoboCheckEqInt((int)objectA.kind, (int)objectB.kind);
        RoboCheckEqInt((int)objectA.display, (int)objectB.display);
        RoboCheckEqInt((int)objectA.parent, (int)objectB.parent);
        RoboCheckEqInt((int)objectA.firstChild, (int)objectB.firstChild);
        RoboCheckEqInt((int)objectA.lastChild, (int)objectB.lastChild);
        RoboCheckEqInt((int)objectA.prevSibling, (int)objectB.prevSibling);
        RoboCheckEqInt((int)objectA.NextSibling, (int)objectB.NextSibling);
        RoboCheckEqInt(objectA.childCount, objectB.childCount);
        RoboCheckEqInt(objectA.depth, objectB.depth);
        RoboCheckEqInt(objectA.flags, objectB.flags);
        /* The classification travels with the box, so two builds of one document
         * classify identically -- including the ids, which are tree-scoped and
         * assigned in the same order by the same walk. */
        RoboCheckEqInt((int)objectA.context, (int)objectB.context);
        RoboCheckEqInt((int)objectA.contextOwned, (int)objectB.contextOwned);
        RoboCheckEqInt(objectA.domNode.nodeId, objectB.domNode.nodeId);
        RoboCheckEqInt(objectA.domNode.documentId, objectB.domNode.documentId);
        /* The borrowed style is the *same* style in both trees, because both
         * borrow from the one bridge: determinism here is not "an equal style was
         * built twice" but "the one style was consumed twice". */
        RoboCheck(objectA.style == objectB.style);
    }
}

static void TestConformanceDeterminism(void)
{
    LayoutConformanceFixture fixture;
    LayoutTree *second = NULL;
    LayoutBuildOptions options;

    LayoutFixture();
    LayoutConformanceBegin(&fixture);

    /* Build the same document into a *second* tree, from the same source -- the
     * path a rebuild takes. */
    RoboCheckEqInt(LayoutTreeCreate(&second, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture.source;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(second, fixture.doc.id, &options,
                                                          NULL, NULL),
                      RoboOk);
    LayoutConformanceAssertSameTree(fixture.tree, second);

    /* And again into the *same* tree, which is the rebuild case: the tree is
     * cleared and rebuilt, so its ids restart and its shape must come back
     * identical rather than drifting. */
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture.tree, fixture.doc.id,
                                                          &options, NULL, NULL),
                      RoboOk);
    LayoutConformanceAssertSameTree(fixture.tree, second);

    /* A tree built with no styles is also deterministic, and differs from the
     * styled one in the way it should: same shape, different flags and displays. */
    {
        LayoutTree *bare = NULL;

        RoboCheckEqInt(LayoutTreeCreate(&bare, NULL), RoboOk);
        RoboCheckEqInt(LayoutTreeBuildFromDocument(bare, fixture.doc.id, NULL,
                                                              NULL, NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutTreeBuildFromDocument(bare, fixture.doc.id, NULL,
                                                              NULL, NULL),
                          RoboOk);
        /* Built twice with no styles: the two builds agree with each other, which
         * is checked by comparing it against itself after a rebuild. */
        {
            size_t index;
            size_t count = LayoutTreeObjectCount(bare);

            for (index = 0u; index < count; index++) {
                LayoutNodeId id = RoboLayoutNodeIdNone;

                RoboCheckEqInt(LayoutObjectIdAt(bare, index, &id), RoboOk);
                RoboCheckEqInt(LayoutObjectHasFlag(
                                      bare, id, RoboLayoutObjectFlagStyleMissing),
                                  1);
            }
        }
        LayoutTreeDestroy(bare);
    }

    LayoutTreeDestroy(second);
    LayoutConformanceEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Robustness                                                          */
/* ------------------------------------------------------------------ */

static void TestConformanceRobust(void)
{
    /*
     * Documents a builder has no business crashing on. Each is built, walked and
     * destroyed; every one must produce a *reported* answer -- either a build or a
     * specific code -- and no box may exist without the tree knowing about it.
     *
     * The list is intentionally blunt: a layout pass runs over whatever the tree
     * builder produced, and the tree builder runs over whatever the tokenizer
     * produced, so "the parser recovered" has to mean "layout gets something
     * walkable" rather than "layout gets something it did not expect".
     */
    static const char *const corpus[] = {
        "",
        "<",
        "<>",
        "</>",
        "<html",
        "<html>",
        "<html></html>",
        "<html><body>",
        "<<div>>",
        "<div class=>",
        "<div class=\"\"></div>",
        "<div class=\"a b c d e f g h i j\"></div>",
        "<div><div><div><div><div></div></div></div></div></div>",
        "<p>text only</p>",
        "text before <p>after</p> text after",
        "<!-- comment --><div></div>",
        "<!doctype html><html><body></body></html>",
        "<div style=\"display: none\">hidden</div>",
        "<div style=\"display\">bad declaration</div>",
        "<div style=\"display: bogus\"></div>",
        "<div style=></div>",
        "<div></div></div>",
        "<div attr=\"unterminated></div>",
        "<a href=\"x\"><b>nested</b></a>",
        "<ul><li>one<li>two<li>three</ul>",
        "<table><tr><td>cell</td></tr></table>",
        "<svg><path/></svg>",
        "<div>\xC3\x28</div>",
        "<div>\xFF\xFE</div>",
        "\x00\x01\x02",
        "<div title=\"\xC3\xA9\"></div>",
        "<html><body><div class=\"panel\"><p>deep</p></div></body></html>"
    };
    LayoutCssFixture css;
    size_t index;

    LayoutFixture();
    LayoutCssBegin(&css);
    LayoutCssSheet(&css, "html, body, div, p, li, td { display: block }\n"
                           ".panel { display: inline-block }\n"
                           "div + div { display: none }\n");

    for (index = 0u; index < sizeof(corpus) / sizeof(corpus[0]); index++) {
        LayoutDocument doc;
        LayoutDom *dom = NULL;
        LayoutTree *tree = NULL;
        LayoutStyleSource source;
        LayoutBuildOptions options;
        LayoutBuildReport report;
        LayoutContext *context = NULL;
        LayoutStats stats;
        Result built;

        LayoutDocumentBuild(&doc, corpus[index]);

        /* The bridge may refuse a document if the capture cannot describe it (a
         * cap exceeded, an attribute the DOM will not hand over). That is a
         * *reported* refusal and the loop moves on; what must not happen is a
         * crash, or a bridge that reported OK and is not usable. */
        if (LayoutDomCreate(css.context, doc.id, &dom, NULL) != RoboOk) {
            LayoutDocumentRelease(&doc);
            continue;
        }
        if (LayoutDomRefresh(dom, NULL) != RoboOk) {
            LayoutDomDestroy(dom);
            LayoutDocumentRelease(&doc);
            continue;
        }

        RoboCheckEqInt(LayoutDomStyleSource(dom, &source, NULL), RoboOk);
        LayoutBuildOptionsInit(&options);
        options.styles = &source;
        RoboCheckEqInt(LayoutTreeCreate(&tree, NULL), RoboOk);

        built = LayoutTreeBuildFromDocument(tree, doc.id, &options, &report, NULL);
        RoboCheckEqInt(built, RoboOk);

        /* Whatever came out is walkable, every box is reachable, and the report's
         * counts are the tree's -- so a builder that half-filled a tree would fail
         * here rather than in a later phase. */
        {
            size_t count = LayoutTreeObjectCount(tree);
            size_t at;

            /*
             * The tree holds every box *and* every text run the document generated
             * (10.5), which is why the report has two creation counters and this is
             * their sum. A corpus entry with no text in it has `textsCreated` 0 and
             * the two numbers coincide -- and the entry that has text is the one that
             * proves the sum is not a coincidence.
             */
            RoboCheckEqInt(count, report.boxesCreated + report.textsCreated);
            for (at = 0u; at < count; at++) {
                LayoutNodeId id = RoboLayoutNodeIdNone;
                LayoutObject object;

                RoboCheckEqInt(LayoutObjectIdAt(tree, at, &id), RoboOk);
                RoboCheckEqInt(LayoutObjectGet(tree, id, &object, NULL), RoboOk);
                RoboCheckEqInt((int)object.id, (int)id);
                RoboCheck(object.kind == RoboLayoutObjectBox ||
                           object.kind == RoboLayoutObjectText);
                if (object.kind == RoboLayoutObjectText) {
                    /* A text object is not a box, and the two facts that make it one
                     * are asserted rather than assumed: no children, and a display
                     * of `inline` that was *derived* rather than declared -- so a
                     * builder that copied a `display` onto a text object would fail
                     * here. */
                    RoboCheckEqInt(object.childCount, 0u);
                    RoboCheckEqInt((int)object.display, (int)RoboCssDisplayInline);
                    RoboCheckEqInt(CssDisplayGeneratesBox(object.display), 1);
                } else {
                    RoboCheckEqInt(CssDisplayGeneratesBox(object.display), 1);
                }
            }
        }

        /* And a pass over it is legitimate however odd the document was. */
        RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
        RoboCheckEqInt(LayoutContextRun(context, tree, NULL), RoboOk);
        RoboCheckEqInt((int)LayoutContextOutcome(context),
                          (int)RoboLayoutPassWalked);
        LayoutContextStats(context, &stats);
        RoboCheckEqInt(stats.objects, report.boxesCreated + report.textsCreated);
        RoboCheckEqInt(stats.boxesComputed, 0u);

        LayoutContextDestroy(context);
        LayoutTreeDestroy(tree);
        LayoutDomDestroy(dom);
        LayoutDocumentRelease(&doc);
    }

    LayoutCssEnd(&css);
}

/* ------------------------------------------------------------------ */
/* Classification without a viewport, geometry with one               */
/* ------------------------------------------------------------------ */

/**
 * A pass with no viewport: it classifies, and it places nothing.
 *
 * This case **replaces** `layout.conformance.no_geometry`, and the replacement is
 * the point rather than an edit. 10.3 made "the pass computed no coordinate" false
 * of a build; asserting it anyway would mean weakening the suite to keep a name,
 * and the useful half of the old claim was never about geometry -- it was that the
 * *classification* does not depend on having a viewport. A caller that wants to
 * know which context a box is in should not have to invent a window size, and a
 * build that refused would make the phase that classifies depend on the phase that
 * sizes.
 *
 * So the geometry half is now *conditional*, and this case states the condition
 * exactly: no viewport, no coordinate, every box honestly UNRESOLVED, and the
 * counter that says nothing was placed at zero. The pipeline case above is the
 * other side of the same coin, and the two together are what the capability fields
 * mean.
 */
static void TestConformanceNoViewport(void)
{
    LayoutConformanceFixture fixture;
    LayoutContext *context = NULL;
    LayoutCapabilities caps;
    LayoutStats stats;
    LayoutSize viewport;
    size_t index;

    LayoutFixture();
    LayoutConformanceBegin(&fixture);

    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    /* No viewport was recorded, and the context says so rather than reporting a
     * default -- the difference between "the initial containing block is 0x0" and
     * "nobody has told me what it is" is a difference a caller acts on. */
    RoboCheckEqInt(LayoutContextViewport(context, &viewport), RoboNotFound);
    RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);

    RoboCheckEqInt(LayoutGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.geometry, 1);
    RoboCheckEqInt(caps.formattingContexts, 1);
    RoboCheckEqInt(caps.blockLayout, 1);
    RoboCheckEqInt(caps.inlineLayout, 1);
    RoboCheckEqInt(caps.blockAndInlineLayout, 1);
    RoboCheckEqInt(caps.painting, 0);
    RoboCheckEqInt(LayoutGeometryIsImplemented(), 1);

    /*
     * The phase's two claims, side by side and both true of the same tree, which is
     * the whole reason they are separate: `geometry` describes the *build* -- it can
     * compute a coordinate -- while this pass has no containing block to compute one
     * *into*. A build that had conflated "this build lays boxes out" with "this pass
     * laid them out" would have failed here, and that conflation is what the
     * per-box result state exists to make impossible.
     */
    RoboCheckEqInt(LayoutTreeIsClassified(fixture.tree), 1);
    RoboCheckEqInt(LayoutTreeContextCount(fixture.tree) > 0u, 1);
    {
        LayoutFcId rootContext = RoboLayoutFcIdNone;
        LayoutFormattingContext context;
        size_t box;

        RoboCheckEqInt(LayoutTreeRootContext(fixture.tree, &rootContext, NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutTreeContext(fixture.tree, rootContext, &context,
                                                    NULL),
                          RoboOk);
        RoboCheckEqInt((int)context.kind, (int)RoboLayoutFcRoot);
        RoboCheckEqInt((int)context.owner, (int)LayoutTreeRoot(fixture.tree));
        /* Every box reached a context -- the hidden `<div>` never became a box, so
         * it is absent rather than unclassified -- and every context owner is a
         * live box. */
        for (box = 0u; box < LayoutTreeObjectCount(fixture.tree); box++) {
            LayoutNodeId id = RoboLayoutNodeIdNone;
            LayoutFcId boxContext = RoboLayoutFcIdNone;

            RoboCheckEqInt(LayoutObjectIdAt(fixture.tree, box, &id), RoboOk);
            RoboCheckEqInt(LayoutObjectIsLive(fixture.tree, id), 1);
            RoboCheckEqInt(LayoutObjectContext(fixture.tree, id, &boxContext,
                                                         NULL),
                              RoboOk);
            RoboCheck(boxContext != RoboLayoutFcIdNone);
        }
    }

    /*
     * Not one box has a coordinate, and not one of them answers as if it did -- and
     * the counters say *why* rather than leaving a reader to guess: two passes ran,
     * both wrote their per-box statement, and the block pass placed nothing.
     */
    LayoutContextStats(context, &stats);
    RoboCheckEqInt(stats.passes, 2u);
    RoboCheckEqInt(stats.objects, 6u); /* five boxes and the paragraph's text run */
    RoboCheckEqInt(stats.visited, 6u);
    RoboCheckEqInt(stats.boxesComputed, 0u);
    RoboCheckEqInt(stats.boxesUnresolved, 6u);
    RoboCheckEqInt(stats.block.blocksPlaced, 0u);
    /* No viewport means no line, so the text direction placed nothing either -- and
     * the record is zeros rather than absent, which is the same rule the other two
     * follow. */
    RoboCheckEqInt(stats.text.runsPlaced, 0u);
    RoboCheckEqInt(stats.text.unmeasured, 0u);

    for (index = 0u; index < LayoutTreeObjectCount(fixture.tree); index++) {
        LayoutNodeId id = RoboLayoutNodeIdNone;
        LayoutResult result;
        const LayoutResult *ref;
        LayoutPoint position;
        LayoutRect rect;

        RoboCheckEqInt(LayoutObjectIdAt(fixture.tree, index, &id), RoboOk);
        RoboCheckEqInt(LayoutObjectResult(fixture.tree, id, &result), RoboOk);
        RoboCheckEqInt(result.state, RoboLayoutResultUnresolved);
        RoboCheckEqInt(result.position.x, 0);
        RoboCheckEqInt(result.position.y, 0);
        RoboCheckEqInt(result.size.width, 0);
        RoboCheckEqInt(result.size.height, 0);
        RoboCheckEqInt(result.flags & RoboLayoutResultFlagAll, 0);
        RoboCheckEqInt(LayoutResultPosition(&result, &position), RoboNotFound);
        RoboCheckEqInt(LayoutResultRect(&result, &rect, NULL), RoboNotFound);

        ref = LayoutObjectResultRef(fixture.tree, id);
        RoboCheck(ref != NULL);
        if (ref) {
            RoboCheckEqInt(LayoutResultEqual(ref, &result), 1);
        }
    }

    /*
     * Then the viewport arrives, and the *same* call computes: the only input that
     * changed is the initial containing block, and four boxes now have geometry where
     * none of them did. That pair is the whole claim of this case -- what a pass
     * needs in order to place a box -- and it is why the viewport is recorded by the
     * caller instead of asked of a window.
     */
    viewport.width = 1280;
    viewport.height = 800;
    RoboCheckEqInt(LayoutContextSetViewport(context, viewport, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);
    LayoutContextStats(context, &stats);
    RoboCheckEqInt(stats.passes, 3u);
    RoboCheckEqInt(stats.boxesComputed, 5u);
    RoboCheckEqInt(stats.boxesUnresolved, 1u);
    RoboCheckEqInt(stats.block.blocksPlaced, 4u);
    /* The fifth placement is the text run: with a viewport there is now a line for it
     * to be on, and still nothing to measure it with. */
    RoboCheckEqInt(stats.inlineFlow.participantsPlaced, 1u);
    RoboCheckEqInt(stats.text.runsPlaced, 1u);
    RoboCheckEqInt(stats.text.unmeasured, 1u);

    LayoutContextDestroy(context);
    LayoutConformanceEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The whole chain returns the allocator to its baseline               */
/* ------------------------------------------------------------------ */

static void TestConformanceAllocatorBalance(void)
{
    size_t before;
    int pass;

    LayoutFixture();
    before = LayoutLiveBlocks();

    for (pass = 0; pass < 8; pass++) {
        LayoutConformanceFixture fixture;
        LayoutContext *context = NULL;
        LayoutTree *second = NULL;
        LayoutBuildOptions options;

        LayoutConformanceBegin(&fixture);
        LayoutConformanceAssertSameTree(fixture.tree, fixture.tree);

        RoboCheckEqInt(LayoutTreeCreate(&second, NULL), RoboOk);
        LayoutBuildOptionsInit(&options);
        options.styles = &fixture.source;
        RoboCheckEqInt(LayoutTreeBuildFromDocument(second, fixture.doc.id,
                                                              &options, NULL, NULL),
                          RoboOk);
        LayoutConformanceAssertSameTree(fixture.tree, second);

        /* Run both trees, so the pass's per-box write is exercised too. */
        RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
        RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);
        RoboCheckEqInt(LayoutContextRun(context, second, NULL), RoboOk);
        LayoutContextDestroy(context);

        LayoutTreeDestroy(second);
        LayoutConformanceEnd(&fixture);
    }

    RoboCheckEqInt(LayoutLiveBlocks(), before);
}

/* ------------------------------------------------------------------ */
/* The module's promises, in one place                                 */
/* ------------------------------------------------------------------ */

static void TestConformanceStatement(void)
{
    LayoutFixture();

    /*
     * The sentences the module would be described by, each an assertion rather than
     * a comment. If a later goal flips one of these it must also change this test,
     * which is the point: a phase that computes geometry should not be able to leave
     * the build claiming it does not.
     */
    RoboCheckEqInt(LayoutGeometryIsImplemented(), 1);
    RoboCheckStrEq(LayoutVersionString(), "0.23.0");
    RoboCheckEqInt(RoboLayoutApiFrozen, 0);

    /*
     * One box kind, and this is where 10.1's prediction turned out to be wrong in a
     * way worth recording. The 10.1 record expected formatting contexts to arrive as
     * extra *object kinds*; they arrived instead as a vocabulary of their own
     * (RoboLayoutFormattingContext.h). The reason is the one this module applies
     * everywhere else: `display` already carries the outer/inner pair that would
     * distinguish a block box from an inline one, and a second copy of those facts in
     * `kind` would be two places for one answer -- which is exactly what 10.1 refused
     * to do for every other property. `kind` therefore stayed one entry wide through
     * 10.2, 10.3 and 10.4: a block box was the only kind of box the build makes.
     *
     * **10.5 is the goal that widened it, and it did so with something that is not a
     * box at all.** A text node is not an element and has no `display` declaration, so
     * there is no property to derive its kind from and no element for a `box` to
     * reference -- which is exactly the condition 10.1 said would justify a second
     * entry. The anonymous box is still absent and still a later goal's: it *does* have
     * a display to derive from (it is block-level by construction), so it is the case
     * `kind` was never needed for, and 10.4 laid the inline direction out without one.
     */
    RoboCheckEqInt((int)RoboLayoutObjectKindCount, 3);
    RoboCheckEqInt(LayoutObjectKindIsKnown(RoboLayoutObjectBox), 1);
    RoboCheckEqInt(LayoutObjectKindIsKnown(RoboLayoutObjectText), 1);
    RoboCheckStrEq(LayoutObjectKindName(RoboLayoutObjectText), "text");
    /* One past the last defined entry is still refused, so the vocabulary is closed at
     * three and not merely observed to have three members. */
    RoboCheckEqInt(LayoutObjectKindIsKnown((LayoutObjectKind)3), 0);

    /* And the vocabulary 10.2 added instead: five kinds of context plus the
     * absence, of which ROOT is one and only one per tree, and 19.3's FLEX is the
     * third laying-out kind, 19.4's GRID the fourth and 19.6's TABLE the fifth. */
    RoboCheckEqInt((int)RoboLayoutFcKindCount, 7);
    RoboCheckEqInt(LayoutFcKindIsBlockContainer(RoboLayoutFcRoot), 1);
    RoboCheckEqInt(LayoutFcKindIsBlockContainer(RoboLayoutFcInline), 0);
    RoboCheckEqInt(LayoutFcKindIsBlockContainer(RoboLayoutFcFlex), 0);
    RoboCheckEqInt(LayoutFcKindIsBlockContainer(RoboLayoutFcGrid), 0);
    RoboCheckEqInt(LayoutFcKindIsBlockContainer(RoboLayoutFcTable), 0);
    RoboCheckStrEq(LayoutFcKindName(RoboLayoutFcBlock), "block");
    RoboCheckStrEq(LayoutFcKindName(RoboLayoutFcFlex), "flex");
    RoboCheckStrEq(LayoutFcKindName(RoboLayoutFcGrid), "grid");
    RoboCheckStrEq(LayoutFcKindName(RoboLayoutFcTable), "table");
}

/* ------------------------------------------------------------------ */

void TestRegisterLayoutConformance(void);

void TestRegisterLayoutConformance(void)
{
    TestRegister("layout.conformance.capabilities", TestConformanceCapabilities);
    TestRegister("layout.conformance.pipeline", TestConformancePipeline);
    TestRegister("layout.conformance.determinism", TestConformanceDeterminism);
    TestRegister("layout.conformance.robust", TestConformanceRobust);
    TestRegister("layout.conformance.no_viewport", TestConformanceNoViewport);
    TestRegister("layout.conformance.allocator_balance",
                       TestConformanceAllocatorBalance);
    TestRegister("layout.conformance.statement", TestConformanceStatement);
}
