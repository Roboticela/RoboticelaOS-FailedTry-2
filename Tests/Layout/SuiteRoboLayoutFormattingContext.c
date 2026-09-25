/*
 * RoboLayout formatting-context suite (Phase 10.2) — layout.fc.*
 *
 * Two halves, and they are tested differently on purpose.
 *
 * The *classification* is a pure function of a display value, one boolean and one
 * flag, so it is asserted as a **table**: every row of every column, including the
 * rows that answer "nothing". A rule tested only through a built document is a
 * rule whose table cannot be seen, and every row of this table is a claim about
 * CSS rather than a claim about the code.
 *
 * The *tree* is tested through real documents, because participation is not a
 * property of a box — it is a property of a box *in a tree*, and the interesting
 * cases are exactly the ones a table cannot express: a transparent container whose
 * children reach past it, an atomic inline that participates in one context and
 * establishes another, and a `display: none` element that never becomes a box and
 * therefore never reaches any context at all.
 *
 * The counts are asserted exactly rather than as "greater than zero". A
 * classification is arithmetic over a document, and a suite that could not say how
 * many contexts a four-element document has would not notice the day it produced
 * two of them.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Fixtures                                                            */
/* ------------------------------------------------------------------ */

typedef struct layoutFcFixture {
    LayoutDocument doc;
    LayoutTree *tree;
    LayoutFakeSource fake;
    LayoutStyleSource styles;
    LayoutNodeId root;
} LayoutFcFixture;

/**
 * One scripted display answers: an element with this tag computes to this.
 *
 * A named type rather than an anonymous struct repeated at each call site,
 * because C's type compatibility is per-declaration: two anonymous structs with
 * identical members are two different types, and the second would not compile.
 */
typedef struct layoutFcDisplayRule {
    const char *tag;
    CssDisplay display;
} LayoutFcDisplayRule;

/**
 * Build @p html into a tree whose styles are @p displays, keyed by tag.
 *
 * Every tag named in @p displays is answered for, and every other element keeps
 * the initial `inline` -- which is what makes a document's classification depend
 * on the declarations the test wrote rather than on the parser's synthesized
 * elements.
 */
static void LayoutFcBegin(LayoutFcFixture *fixture,
                            const char *html,
                            const LayoutFcDisplayRule *displays,
                            size_t pairs)
{
    LayoutBuildOptions options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    LayoutDocumentBuild(&fixture->doc, html);
    LayoutFakeInit(&fixture->fake);
    for (index = 0u; index < pairs; index++) {
        LayoutFakeAdd(&fixture->fake, displays[index].tag, displays[index].display);
    }
    fixture->styles = LayoutFakeSourceOf(&fixture->fake);
    RoboCheckEqInt(LayoutTreeCreate(&fixture->tree, NULL), RoboOk);
    LayoutBuildOptionsInit(&options);
    options.styles = &fixture->styles;
    RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture->tree, fixture->doc.id,
                                                           &options, NULL, NULL),
                      RoboOk);
    fixture->root = LayoutTreeRoot(fixture->tree);
}

static void LayoutFcEnd(LayoutFcFixture *fixture)
{
    LayoutTreeDestroy(fixture->tree);
    fixture->tree = NULL;
    LayoutFakeDispose(&fixture->fake);
    LayoutDocumentRelease(&fixture->doc);
}

/** Run one pass, which is what classifies the tree. */
static void LayoutFcClassify(LayoutFcFixture *fixture, LayoutStats *outStats)
{
    LayoutContext *context = NULL;
    LayoutStats stats;

    RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
    RoboCheckEqInt(LayoutContextRun(context, fixture->tree, NULL), RoboOk);
    LayoutContextStats(context, &stats);
    RoboCheckEqInt(LayoutTreeIsClassified(fixture->tree), 1);
    LayoutContextDestroy(context);
    if (outStats) {
        *outStats = stats;
    }
}

/** The box built for the element @p tag, or a miss. */
static LayoutNodeId LayoutFcBoxOf(LayoutFcFixture *fixture, const char *tag)
{
    HtmlNodeHandle element = LayoutDocumentFind(&fixture->doc, tag);

    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&element), 1);
    return LayoutObjectIdForDomNode(fixture->tree, &element);
}

/* ------------------------------------------------------------------ */
/* The kind vocabulary                                                 */
/* ------------------------------------------------------------------ */

static void TestFcKinds(void)
{
    size_t index;
    size_t seen[RoboLayoutFcKindCount];

    LayoutFixture();

    RoboCheckEqInt((int)RoboLayoutFcKindCount, 4);
    RoboCheckEqInt((int)RoboLayoutFcNone, 0);
    RoboCheckEqInt((int)RoboLayoutFcRoot, 1);
    RoboCheckEqInt((int)RoboLayoutFcBlock, 2);
    RoboCheckEqInt((int)RoboLayoutFcInline, 3);

    RoboCheckEqInt(LayoutFcKindIsKnown(RoboLayoutFcNone), 1);
    RoboCheckEqInt(LayoutFcKindIsKnown(RoboLayoutFcRoot), 1);
    RoboCheckEqInt(LayoutFcKindIsKnown(RoboLayoutFcBlock), 1);
    RoboCheckEqInt(LayoutFcKindIsKnown(RoboLayoutFcInline), 1);
    RoboCheckEqInt(LayoutFcKindIsKnown((LayoutFcKind)4), 0);
    RoboCheckEqInt(LayoutFcKindIsKnown((LayoutFcKind)99), 0);

    /* Walked in enumerator order: a row whose table entry had drifted would still
     * answer IsKnown for every value and would show only as a name printing as
     * another's keyword. Every name is non-NULL and no two agree. */
    for (index = 0u; index < RoboLayoutFcKindCount; index++) {
        LayoutFcKind kind = (LayoutFcKind)index;
        const char *name = LayoutFcKindName(kind);
        size_t other;

        RoboCheckEqInt(LayoutFcKindIsKnown(kind), 1);
        RoboCheck(name != NULL);
        if (!name) {
            continue;
        }
        seen[index] = strlen(name);
        RoboCheck(seen[index] > 0u);
        for (other = 0u; other < index; other++) {
            RoboCheck(strcmp(name, LayoutFcKindName(
                                   (LayoutFcKind)other)) != 0);
        }
    }
    RoboCheckStrEq(LayoutFcKindName(RoboLayoutFcNone), "none");
    RoboCheckStrEq(LayoutFcKindName(RoboLayoutFcRoot), "root");
    RoboCheckStrEq(LayoutFcKindName(RoboLayoutFcBlock), "block");
    RoboCheckStrEq(LayoutFcKindName(RoboLayoutFcInline), "inline");
    RoboCheckStrEq(LayoutFcKindName((LayoutFcKind)40), "unknown");
    RoboCheck(LayoutFcKindName((LayoutFcKind)40) != NULL);

    /* The one predicate with a right answer a reader could get wrong: ROOT *is* a
     * block formatting context -- the initial one -- so a block pass that
     * special-cased it would stack the root's children in the wrong direction
     * while every other test passed. */
    RoboCheckEqInt(LayoutFcKindIsBlockContainer(RoboLayoutFcRoot), 1);
    RoboCheckEqInt(LayoutFcKindIsBlockContainer(RoboLayoutFcBlock), 1);
    RoboCheckEqInt(LayoutFcKindIsBlockContainer(RoboLayoutFcInline), 0);
    RoboCheckEqInt(LayoutFcKindIsBlockContainer(RoboLayoutFcNone), 0);
    RoboCheckEqInt(LayoutFcKindIsBlockContainer((LayoutFcKind)70), 0);
}

/* ------------------------------------------------------------------ */
/* The classifier, as a table                                          */
/* ------------------------------------------------------------------ */

static void TestFcClassifyEstablishes(void)
{
    LayoutFixture();

    /*
     * The table, row by row, and every row is a claim about CSS.
     *
     * `hasBlockLevelChild` is meaningful only for a block container, and it is
     * passed for the inline rows as well so that the *absence* of an effect is
     * asserted rather than assumed: a build that let an inline box with a
     * block-level child establish something would be a build that had turned
     * inline boxes into block containers.
     */
    struct {
        CssDisplay display;
        int hasBlockLevelChild;
        int isRoot;
        int isOutOfFlow;
        LayoutFcKind expected;
        const char *why;
    } table[] = {
        /* The root wins over every display question, including `none`: a document
         * whose root element generates no box has no root box at all, which is a
         * different case, so this row is about a root that *does* generate one. */
        { RoboCssDisplayBlock, 1, 1, 0, RoboLayoutFcRoot, "root over block" },
        { RoboCssDisplayBlock, 0, 1, 0, RoboLayoutFcRoot, "root over block, leaf" },
        { RoboCssDisplayInline, 0, 1, 0, RoboLayoutFcRoot, "root over inline" },
        { RoboCssDisplayInlineBlock, 0, 1, 0, RoboLayoutFcRoot,
          "root over inline-block" },
        { RoboCssDisplayNone, 0, 1, 0, RoboLayoutFcNone, "no box, no contents" },

        /* `flow-root` inner: a new block formatting context by definition, which is
         * what makes `inline-block` establish one without being named. */
        { RoboCssDisplayInlineBlock, 0, 0, 0, RoboLayoutFcBlock, "atomic inline" },
        { RoboCssDisplayInlineBlock, 1, 0, 0, RoboLayoutFcBlock,
          "atomic inline, block child" },

        /* A block container with no block-level child establishes an inline
         * context; with one, it is transparent. */
        { RoboCssDisplayBlock, 0, 0, 0, RoboLayoutFcInline, "block, inline content" },
        { RoboCssDisplayBlock, 1, 0, 0, RoboLayoutFcNone, "block, transparent" },

        /*
         * Phase 14.0: an **out-of-flow** block container establishes a block context of its own,
         * and the row that matters is the one with a block-level child — that is, the row where
         * transparency would otherwise apply. A positioned box's children are laid out inside it
         * rather than in its parent's flow, so they need a context of their own; a build that
         * answered with the transparency row refused every document whose absolutely positioned
         * container held a block (which is every window the desktop shell puts in a slot).
         */
        { RoboCssDisplayBlock, 1, 0, 1, RoboLayoutFcBlock,
          "out-of-flow block, block child" },
        { RoboCssDisplayBlock, 0, 0, 1, RoboLayoutFcBlock,
          "out-of-flow block, inline content" },
        { RoboCssDisplayInlineBlock, 1, 0, 1, RoboLayoutFcBlock,
          "out-of-flow atomic inline" },
        /* And the out-of-flow bit does not turn an inline box into a container. */
        { RoboCssDisplayInline, 1, 0, 1, RoboLayoutFcNone, "out-of-flow inline box" },

        /* An inline box establishes nothing, whatever it contains. */
        { RoboCssDisplayInline, 0, 0, 0, RoboLayoutFcNone, "inline box" },
        { RoboCssDisplayInline, 1, 0, 0, RoboLayoutFcNone, "inline box, block child" },

        /* No box: nothing to format. */
        { RoboCssDisplayNone, 0, 0, 0, RoboLayoutFcNone, "none" },
        { RoboCssDisplayNone, 1, 0, 0, RoboLayoutFcNone, "none, irrelevant child" }
    };
    size_t index;

    for (index = 0u; index < sizeof(table) / sizeof(table[0]); index++) {
        LayoutFcKind actual = LayoutFcEstablishes(
            table[index].display, table[index].hasBlockLevelChild, table[index].isRoot,
            table[index].isOutOfFlow);

        RoboCheckMsg(actual == table[index].expected, table[index].why);
    }
}

static void TestFcClassifyDisplay(void)
{
    LayoutFixture();

    /*
     * "Can this display *ever* establish a context?" and "is this display a block
     * container?" are different questions, and the difference is the whole reason
     * both doors exist: an `inline` box answers 0 to both for reasons that will
     * never change, while a block answers 1 and *then* answers NONE for a
     * particular child composition.
     */
    struct {
        CssDisplay display;
        int canEstablish;
        int isBlockContainer;
    } table[] = {
        { RoboCssDisplayNone, 0, 0 },
        { RoboCssDisplayBlock, 1, 1 },
        { RoboCssDisplayInline, 0, 0 },
        { RoboCssDisplayInlineBlock, 1, 1 }
    };
    size_t index;

    for (index = 0u; index < sizeof(table) / sizeof(table[0]); index++) {
        RoboCheckEqInt(LayoutFcDisplayCanEstablish(table[index].display),
                          table[index].canEstablish);
        RoboCheckEqInt(LayoutFcDisplayIsBlockContainer(table[index].display),
                          table[index].isBlockContainer);
    }

    /* An undefined enumerator answers no to both rather than falling through a
     * table: a display this build does not know is not a container, and it is
     * certainly not something that establishes anything. */
    RoboCheckEqInt(LayoutFcDisplayCanEstablish((CssDisplay)88), 0);
    RoboCheckEqInt(LayoutFcDisplayIsBlockContainer((CssDisplay)88), 0);

    /* And the two doors agree wherever both have an opinion: anything that can
     * establish does. Stated as a walk so it stays true as rows are added. */
    for (index = 0u; index < RoboCssDisplayCount; index++) {
        CssDisplay display = (CssDisplay)index;

        if (LayoutFcDisplayCanEstablish(display)) {
            RoboCheckEqInt(LayoutFcEstablishes(display, 0, 0, 0) !=
                                  RoboLayoutFcNone,
                              1);
        }
        if (!CssDisplayGeneratesBox(display)) {
            RoboCheckEqInt(LayoutFcDisplayCanEstablish(display), 0);
            RoboCheckEqInt(LayoutFcEstablishes(display, 0, 0, 0), RoboLayoutFcNone);
            RoboCheckEqInt(LayoutFcEstablishes(display, 0, 1, 0), RoboLayoutFcNone);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

static void TestFcRecord(void)
{
    LayoutFormattingContext context;
    LayoutFormattingContext other;

    LayoutFixture();

    memset(&context, 0xAB, sizeof(context));
    LayoutFormattingContextInit(&context);
    RoboCheckEqInt((int)context.id, (int)RoboLayoutFcIdNone);
    RoboCheckEqInt((int)context.kind, (int)RoboLayoutFcNone);
    RoboCheckEqInt((int)context.owner, (int)RoboLayoutNodeIdNone);
    RoboCheckEqInt((int)context.parent, (int)RoboLayoutFcIdNone);
    RoboCheckEqInt(context.participants, 0u);
    RoboCheckEqInt(context.depth, 0u);

    LayoutFormattingContextInit(NULL);

    other = context;
    RoboCheckEqInt(LayoutFormattingContextEqual(&context, &other), 1);

    /* Identity is part of equality, deliberately: two contexts with the same kind,
     * owner and parent are still two contexts, and a comparison that ignored the
     * id would report a classification rebuilt into a different tree as
     * unchanged. */
    other.id = 7u;
    RoboCheckEqInt(LayoutFormattingContextEqual(&context, &other), 0);
    other = context;
    other.kind = RoboLayoutFcBlock;
    RoboCheckEqInt(LayoutFormattingContextEqual(&context, &other), 0);
    other = context;
    other.owner = 3u;
    RoboCheckEqInt(LayoutFormattingContextEqual(&context, &other), 0);
    other = context;
    other.parent = 5u;
    RoboCheckEqInt(LayoutFormattingContextEqual(&context, &other), 0);
    other = context;
    other.participants = 9u;
    RoboCheckEqInt(LayoutFormattingContextEqual(&context, &other), 0);
    other = context;
    other.depth = 2u;
    RoboCheckEqInt(LayoutFormattingContextEqual(&context, &other), 0);

    /* Two NULls are equal and one is not: the same rule the other records use, so
     * a caller can compare an optional record without a second guard. */
    RoboCheckEqInt(LayoutFormattingContextEqual(NULL, NULL), 1);
    RoboCheckEqInt(LayoutFormattingContextEqual(&context, NULL), 0);
    RoboCheckEqInt(LayoutFormattingContextEqual(NULL, &context), 0);
}

/* ------------------------------------------------------------------ */
/* A document classification, counted                                  */
/* ------------------------------------------------------------------ */

/**
 * Eight elements, and between them every rule the classifier has.
 *
 * `<div>`/`<section>` nest two transparent block containers, so `<p>`'s context is
 * reached *through* two boxes that establish nothing -- which is the case a
 * one-fact-per-box model cannot express and the case margin collapsing will
 * depend on. `<span>` is an inline box that establishes nothing and `<i>` is an
 * atomic inline that establishes a block context while participating in its
 * parent's.
 */
static const LayoutFcDisplayRule layoutFcDisplays[] = {
    { "html", RoboCssDisplayBlock },
    { "head", RoboCssDisplayBlock },
    { "body", RoboCssDisplayBlock },
    { "div", RoboCssDisplayBlock },
    { "section", RoboCssDisplayBlock },
    { "p", RoboCssDisplayBlock },
    { "span", RoboCssDisplayInline },
    { "i", RoboCssDisplayInlineBlock }
};

#define LAYOUT_FC_HTML                      \
    "<html>\n"                              \
    "  <body>\n"                            \
    "    <div>\n"                           \
    "      <section>\n"                     \
    "        <p>one</p>\n"                  \
    "      </section>\n"                    \
    "    </div>\n"                          \
    "    <span>two</span>\n"                \
    "    <i>three</i>\n"                    \
    "  </body>\n"                           \
    "</html>\n"

#define LAYOUT_FC_PAIRS (sizeof(layoutFcDisplays) / sizeof(layoutFcDisplays[0]))

static void TestFcTreeRoot(void)
{
    LayoutFcFixture fixture;
    LayoutStats stats;
    LayoutFcId rootContext = RoboLayoutFcIdNone;
    LayoutFormattingContext context;
    LayoutFcId owned = RoboLayoutFcIdNone;

    LayoutFixture();
    LayoutFcBegin(&fixture, LAYOUT_FC_HTML, layoutFcDisplays, LAYOUT_FC_PAIRS);

    /* Nothing is classified before a pass, and the two facts are separate: the
     * tree has boxes and it has no contexts. */
    RoboCheckEqInt(LayoutTreeIsClassified(fixture.tree), 0);
    RoboCheckEqInt(LayoutTreeContextCount(fixture.tree), 0u);
    /* Eight boxes and the three text runs their text nodes contributed (10.5): a
     * text object is an object, so it is in the count the classification walks. */
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 11u);

    LayoutFcClassify(&fixture, &stats);

    /* Exactly one initial context, owned by the root box, with no parent and at
     * depth 0. */
    RoboCheckEqInt(stats.contextsByKind[RoboLayoutFcRoot], 1u);
    RoboCheckEqInt(LayoutTreeRootContext(fixture.tree, &rootContext, NULL),
                      RoboOk);
    RoboCheckEqInt((int)rootContext, 1);
    RoboCheckEqInt(LayoutTreeContext(fixture.tree, rootContext, &context, NULL),
                      RoboOk);
    RoboCheckEqInt((int)context.kind, (int)RoboLayoutFcRoot);
    RoboCheckEqInt((int)context.owner, (int)fixture.root);
    RoboCheckEqInt((int)context.parent, (int)RoboLayoutFcIdNone);
    RoboCheckEqInt(context.depth, 0u);
    RoboCheckEqInt(LayoutFcKindIsBlockContainer(context.kind), 1);

    /* The root box participates in the context it establishes -- there is nothing
     * above it -- so "every live box has a context once the tree is classified"
     * holds without an exception. */
    RoboCheckEqInt(LayoutObjectContext(fixture.tree, fixture.root, &owned, NULL),
                      RoboOk);
    RoboCheckEqInt((int)owned, (int)rootContext);
    RoboCheckEqInt(LayoutObjectContextOwned(fixture.tree, fixture.root, &owned,
                                                       NULL),
                      RoboOk);
    RoboCheckEqInt((int)owned, (int)rootContext);

    /* Every box reaches a context, and the reach is real rather than a default. */
    {
        size_t index;

        for (index = 0u; index < LayoutTreeObjectCount(fixture.tree); index++) {
            LayoutNodeId id = RoboLayoutNodeIdNone;

            RoboCheckEqInt(LayoutObjectIdAt(fixture.tree, index, &id), RoboOk);
            RoboCheckEqInt(LayoutObjectContext(fixture.tree, id, &owned, NULL),
                              RoboOk);
            RoboCheckEqInt(owned != RoboLayoutFcIdNone, 1);
        }
    }

    LayoutFcEnd(&fixture);
}

static void TestFcTreeTransparent(void)
{
    LayoutFcFixture fixture;
    LayoutStats stats;
    LayoutFcId divContext = RoboLayoutFcIdNone;
    LayoutFcId pContext = RoboLayoutFcIdNone;
    LayoutFcId pOwned = RoboLayoutFcIdNone;
    LayoutFcId rootContext = RoboLayoutFcIdNone;
    LayoutFormattingContext context;

    LayoutFixture();
    LayoutFcBegin(&fixture, LAYOUT_FC_HTML, layoutFcDisplays, LAYOUT_FC_PAIRS);
    LayoutFcClassify(&fixture, &stats);

    /* Four contexts: the root, the two inline contexts (`<head>` and `<p>`) and the
     * one block context `<i>` establishes. The two transparent block containers
     * establish nothing, and that is the count that says so. */
    RoboCheckEqInt(stats.contexts, 4u);
    RoboCheckEqInt(stats.contextsByKind[RoboLayoutFcNone], 0u);
    RoboCheckEqInt(stats.contextsByKind[RoboLayoutFcRoot], 1u);
    RoboCheckEqInt(stats.contextsByKind[RoboLayoutFcBlock], 1u);
    RoboCheckEqInt(stats.contextsByKind[RoboLayoutFcInline], 2u);
    RoboCheckEqInt(LayoutTreeContextCount(fixture.tree), 4u);
    RoboCheckEqInt(stats.boxesEstablishing, 4u);
    /*
     * Seven of eleven participate without establishing: `<body>`, `<div>` and
     * `<section>` are transparent (each has a block-level child), `<span>` is inline,
     * and the three **text runs** take the context of whichever box encloses
     * them (10.5). The identity below is the one that has to hold, and it does: every
     * object either establishes a context or participates in one, and none does
     * neither.
     */
    RoboCheckEqInt(stats.boxesParticipatingOnly, 7u);
    RoboCheckEqInt(stats.boxesEstablishing + stats.boxesParticipatingOnly,
                      stats.objects);
    /* html 0, head 1, body 1, div 2, section 3, p 4 -- so `<p>`'s context nests one
     * level below the root's. */
    RoboCheckEqInt(stats.contextDepthMax, 1u);

    RoboCheckEqInt(LayoutTreeRootContext(fixture.tree, &rootContext, NULL),
                      RoboOk);

    /*
     * `<div>` and `<section>` are both transparent: each has a block-level child.
     * Their own layout participates in the root context, they establish nothing,
     * and -- this is the assertion the model exists for -- `<p>` reaches the root
     * context *through* both of them rather than each being a barrier.
     */
    {
        LayoutNodeId div = LayoutFcBoxOf(&fixture, "div");
        LayoutNodeId section = LayoutFcBoxOf(&fixture, "section");
        LayoutNodeId p = LayoutFcBoxOf(&fixture, "p");

        RoboCheckEqInt(LayoutObjectEstablishesContext(fixture.tree, div), 0);
        RoboCheckEqInt(LayoutObjectEstablishesContext(fixture.tree, section), 0);
        RoboCheckEqInt(LayoutObjectContextOwned(fixture.tree, div, &divContext,
                                                           NULL),
                          RoboNotFound);
        RoboCheckEqInt(LayoutObjectContext(fixture.tree, div, &divContext, NULL),
                          RoboOk);
        RoboCheckEqInt((int)divContext, (int)rootContext);
        RoboCheckEqInt(LayoutObjectContext(fixture.tree, section, &divContext,
                                                     NULL),
                          RoboOk);
        RoboCheckEqInt((int)divContext, (int)rootContext);
        RoboCheckEqInt(LayoutObjectContext(fixture.tree, p, &pContext, NULL),
                          RoboOk);
        RoboCheckEqInt((int)pContext, (int)rootContext);

        /* `<p>` holds only text, and a text node is not a box, so it has no
         * block-level child and therefore establishes an inline context -- which is
         * where 10.6's line box for that text will live. */
        RoboCheckEqInt(LayoutObjectEstablishesContext(fixture.tree, p), 1);
        RoboCheckEqInt(LayoutObjectContextOwned(fixture.tree, p, &pOwned, NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutTreeContext(fixture.tree, pOwned, &context, NULL),
                          RoboOk);
        RoboCheckEqInt((int)context.kind, (int)RoboLayoutFcInline);
        RoboCheckEqInt((int)context.owner, (int)p);
        RoboCheckEqInt((int)context.parent, (int)rootContext);
        RoboCheckEqInt(context.depth, 1u);
        /*
         * One participant, and it is the phase's arrival: 10.4 left this at zero with
         * the note that inline content that is text was not a box, and 10.5 is the
         * phase that changed it. The participant is `<p>`'s own text run -- a layout
         * object, not a box, which is why the *box* counters above do not see it.
         */
        RoboCheckEqInt(context.participants, 1u);
    }

    /* The initial context coordinates every box, because nothing in this document
     * establishes a context that encloses another box. */
    RoboCheckEqInt(LayoutTreeContext(fixture.tree, rootContext, &context, NULL),
                      RoboOk);
    /*
     * Every box, and every text run whose nearest enclosing context is the root's --
     * which is one of the three: `<p>` and `<i>` each establish a context of their
     * own, so the runs inside them belong to those and not here. Written as the
     * object count minus two rather than as a bare 9, so that the *reason* is in the
     * expression: a text run is only the root's when nothing nearer owns it.
     */
    RoboCheckEqInt(context.participants, 9u);
    RoboCheckEqInt(context.participants,
                      LayoutTreeObjectCount(fixture.tree) - 2u);

    LayoutFcEnd(&fixture);
}

static void TestFcTreeInlineBoxes(void)
{
    /* `<p>` holds an inline box and an atomic inline, so its inline context has box
     * participants -- and the atomic inline both participates in that context and
     * establishes a block context of its own. */
    LayoutFcFixture fixture;
    LayoutStats stats;
    LayoutFcId pOwned = RoboLayoutFcIdNone;
    LayoutFcId spanContext = RoboLayoutFcIdNone;
    LayoutFcId iContext = RoboLayoutFcIdNone;
    LayoutFcId iOwned = RoboLayoutFcIdNone;
    LayoutFormattingContext context;

    LayoutFixture();
    LayoutFcBegin(&fixture, "<html><body><p><span>a</span><i>b</i></p></body></html>",
                    layoutFcDisplays, LAYOUT_FC_PAIRS);
    /* html, head, body, p, span, i -- and the two text runs `a` and `b`. */
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 8u);
    LayoutFcClassify(&fixture, &stats);

    RoboCheckEqInt(stats.contexts, 4u);
    RoboCheckEqInt(stats.contextsByKind[RoboLayoutFcRoot], 1u);
    RoboCheckEqInt(stats.contextsByKind[RoboLayoutFcBlock], 1u);
    RoboCheckEqInt(stats.contextsByKind[RoboLayoutFcInline], 2u);
    RoboCheckEqInt(stats.boxesEstablishing, 4u);
    /* `<body>` and `<span>` participate without establishing, and so do the two text
     * runs -- four of eight. */
    RoboCheckEqInt(stats.boxesParticipatingOnly, 4u);
    /* Two, not one: `<i>`'s block context nests *inside* `<p>`'s inline one, so the
     * deepest context is an atomic inline's, one level below its parent's. */
    RoboCheckEqInt(stats.contextDepthMax, 2u);

    {
        LayoutNodeId p = LayoutFcBoxOf(&fixture, "p");
        LayoutNodeId span = LayoutFcBoxOf(&fixture, "span");
        LayoutNodeId i = LayoutFcBoxOf(&fixture, "i");

        RoboCheckEqInt(LayoutObjectContextOwned(fixture.tree, p, &pOwned, NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutTreeContext(fixture.tree, pOwned, &context, NULL),
                          RoboOk);
        RoboCheckEqInt((int)context.kind, (int)RoboLayoutFcInline);
        /* `<span>`, `<i>`, and the text run inside `<span>`: a transparent inline box
         * is transparent to the *line* as well as to the context, so its text is a
         * participant of `<p>`'s context and not of a context of its own. */
        RoboCheckEqInt(context.participants, 3u);

        /* An inline box participates; it establishes nothing. */
        RoboCheckEqInt(LayoutObjectContext(fixture.tree, span, &spanContext,
                                                     NULL),
                          RoboOk);
        RoboCheckEqInt((int)spanContext, (int)pOwned);
        RoboCheckEqInt(LayoutObjectEstablishesContext(fixture.tree, span), 0);
        RoboCheckEqInt(LayoutObjectContextOwned(fixture.tree, span,
                                                           &spanContext, NULL),
                          RoboNotFound);

        /*
         * The atomic inline: it participates in its parent's *inline* context and
         * establishes a *block* context for its own contents. Both ids are real and
         * they are different, which is the single assertion that would fail if the
         * two relationships were ever collapsed into one field.
         */
        RoboCheckEqInt(LayoutObjectContext(fixture.tree, i, &iContext, NULL),
                          RoboOk);
        RoboCheckEqInt((int)iContext, (int)pOwned);
        RoboCheckEqInt(LayoutObjectContextOwned(fixture.tree, i, &iOwned, NULL),
                          RoboOk);
        RoboCheck(iContext != iOwned);
        RoboCheckEqInt(LayoutTreeContext(fixture.tree, iOwned, &context, NULL),
                          RoboOk);
        RoboCheckEqInt((int)context.kind, (int)RoboLayoutFcBlock);
        RoboCheckEqInt((int)context.owner, (int)i);
        RoboCheckEqInt((int)context.parent, (int)pOwned);
        RoboCheckEqInt(context.depth, 2u);
        RoboCheckEqInt(LayoutFcKindIsBlockContainer(context.kind), 1);
        /* And it coordinates one thing: the text run inside `<i>`, which is the block
         * context's own because `<i>` is atomic and owns its contents. */
        RoboCheckEqInt(context.participants, 1u);
    }

    /* The nesting is real: a context two levels down names the one above it, so the
     * contexts form a tree rather than a flat list with a depth column. */
    {
        LayoutFormattingContext parent;
        LayoutFcId walk = iOwned;
        size_t steps = 0u;

        while (walk != RoboLayoutFcIdNone) {
            RoboCheckEqInt(LayoutTreeContext(fixture.tree, walk, &parent, NULL),
                              RoboOk);
            walk = parent.parent;
            steps++;
            if (steps > 8u) {
                break;
            }
        }
        RoboCheckEqInt(steps, 3u); /* block -> inline -> root, then NONE */
    }

    LayoutFcEnd(&fixture);
}

static void TestFcTreeDisplayNone(void)
{
    static const LayoutFcDisplayRule displays[] = {
        { "html", RoboCssDisplayBlock },
        { "head", RoboCssDisplayBlock },
        { "body", RoboCssDisplayBlock },
        { "div", RoboCssDisplayBlock },
        { "aside", RoboCssDisplayNone },
        { "span", RoboCssDisplayBlock }
    };
    LayoutFcFixture fixture;
    LayoutStats stats;
    HtmlNodeHandle aside;
    size_t index;

    LayoutFixture();
    LayoutFcBegin(&fixture, "<html><body><div></div><aside><span>x</span></aside></body></html>",
                    displays, sizeof(displays) / sizeof(displays[0]));

    /* `<aside>` generates no box and its `<span>` is never reached, so four boxes:
     * html, head, body, div. */
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 4u);
    LayoutFcClassify(&fixture, &stats);

    /* Three contexts. The `display: none` element has no box, so it is not a
     * participant and it owns nothing -- and the DOM still has it, which is what
     * makes "no context" different from "the element is gone". */
    RoboCheckEqInt(stats.contexts, 3u);
    RoboCheckEqInt(stats.contextsByKind[RoboLayoutFcRoot], 1u);
    RoboCheckEqInt(stats.contextsByKind[RoboLayoutFcInline], 2u);
    RoboCheckEqInt(stats.contextsByKind[RoboLayoutFcBlock], 0u);

    aside = LayoutDocumentFind(&fixture.doc, "aside");
    RoboCheckEqInt(HtmlNodeHandleIsWellformed(&aside), 1);
    RoboCheckEqInt((int)LayoutObjectIdForDomNode(fixture.tree, &aside),
                      (int)RoboLayoutNodeIdNone);
    /* Asking the DOM the other way confirms the element is still there. */
    RoboCheckStrEq(HtmlElementName(&aside, NULL), "aside");

    /* No context anywhere claims either element: an owner is always a live box, and
     * neither the hidden element nor the one inside it has one. */
    for (index = 0u; index < LayoutTreeContextCount(fixture.tree); index++) {
        LayoutFormattingContext context;

        RoboCheckEqInt(LayoutTreeContextAt(fixture.tree, index, &context, NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutObjectIsLive(fixture.tree, context.owner), 1);
        RoboCheckEqInt((int)context.id, (int)(index + 1u));
    }

    LayoutFcEnd(&fixture);
}

static void TestFcTreeRootVariants(void)
{
    /* The root box establishes the initial context whatever it computes to. */
    static const LayoutFcDisplayRule inlineRoot[] = {
        { "html", RoboCssDisplayInline },
        { "body", RoboCssDisplayBlock }
    };
    static const LayoutFcDisplayRule hiddenRoot[] = {
        { "html", RoboCssDisplayNone }
    };
    LayoutFcFixture fixture;
    LayoutStats stats;
    LayoutFcId rootContext = RoboLayoutFcIdNone;
    LayoutFormattingContext context;

    LayoutFixture();

    LayoutFcBegin(&fixture, "<html><body></body></html>", inlineRoot,
                    sizeof(inlineRoot) / sizeof(inlineRoot[0]));
    LayoutFcClassify(&fixture, &stats);
    RoboCheckEqInt(LayoutTreeRootContext(fixture.tree, &rootContext, NULL),
                      RoboOk);
    RoboCheckEqInt(LayoutTreeContext(fixture.tree, rootContext, &context, NULL),
                      RoboOk);
    RoboCheckEqInt((int)context.kind, (int)RoboLayoutFcRoot);
    RoboCheckEqInt((int)context.owner, (int)fixture.root);
    RoboCheckEqInt(LayoutFcKindIsBlockContainer(context.kind), 1);
    /*
     * Two: the initial context and `<body>`'s. `<body>` is a block container with
     * no children, so it establishes an inline context -- and the root element is
     * `display: inline`, which proves the root rule is asked before any display
     * question rather than after it.
     *
     * `<head>` does *not* establish one, and the reason is a nice accident of the
     * fixture rather than an oversight: the style source answers only for the tags
     * it was told about, and `head` was not one of them -- so the parser's
     * synthesized `<head>` is an inline box, and an inline box establishes nothing.
     * That is 10.1's rule ("a display nothing declared is the initial value")
     * reaching all the way through the classification, asserted here rather than
     * assumed.
     */
    RoboCheckEqInt(stats.contexts, 2u);
    RoboCheckEqInt(stats.contextsByKind[RoboLayoutFcRoot], 1u);
    RoboCheckEqInt(stats.contextsByKind[RoboLayoutFcInline], 1u);
    RoboCheckEqInt(stats.contextsByKind[RoboLayoutFcBlock], 0u);
    {
        LayoutNodeId head = LayoutFcBoxOf(&fixture, "head");
        LayoutFcId headContext = RoboLayoutFcIdNone;

        RoboCheckEqInt((int)LayoutObjectDisplay(fixture.tree, head),
                          (int)RoboCssDisplayInline);
        RoboCheckEqInt(LayoutObjectEstablishesContext(fixture.tree, head), 0);
        RoboCheckEqInt(LayoutObjectContext(fixture.tree, head, &headContext,
                                                     NULL),
                          RoboOk);
        RoboCheckEqInt((int)headContext, (int)rootContext);
    }
    LayoutFcEnd(&fixture);

    /*
     * A `display: none` root generates no boxes at all, and the result is a tree
     * that is *classified* with no contexts -- which is a different statement from
     * a tree nobody has looked at, and the reason `is_classified` exists.
     */
    LayoutFcBegin(&fixture, "<html><body><p>x</p></body></html>", hiddenRoot,
                    sizeof(hiddenRoot) / sizeof(hiddenRoot[0]));
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 0u);
    LayoutFcClassify(&fixture, &stats);
    RoboCheckEqInt(LayoutTreeIsClassified(fixture.tree), 1);
    RoboCheckEqInt(LayoutTreeContextCount(fixture.tree), 0u);
    RoboCheckEqInt(stats.contexts, 0u);
    RoboCheckEqInt(stats.boxesEstablishing, 0u);
    RoboCheckEqInt(stats.boxesParticipatingOnly, 0u);
    RoboCheckEqInt(stats.contextDepthMax, 0u);
    /* No root box, so no initial context -- reported rather than invented. */
    RoboCheckEqInt(LayoutTreeRootContext(fixture.tree, &rootContext, NULL),
                      RoboNotFound);
    LayoutFcEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Lifecycle, invalidation and determinism                             */
/* ------------------------------------------------------------------ */

/** The classification of @p tree, as a list of (kind, owner, parent, depth). */
typedef struct layoutFcShape {
    LayoutFcKind kind[32];
    LayoutNodeId owner[32];
    LayoutFcId parent[32];
    uint32_t depth[32];
    size_t count;
} LayoutFcShape;

static void LayoutFcCapture(const LayoutTree *tree, LayoutFcShape *shape)
{
    size_t index;

    memset(shape, 0, sizeof(*shape));
    shape->count = LayoutTreeContextCount(tree);
    RoboCheck(shape->count <= 32u);
    for (index = 0u; index < shape->count; index++) {
        LayoutFormattingContext context;

        RoboCheckEqInt(LayoutTreeContextAt(tree, index, &context, NULL), RoboOk);
        shape->kind[index] = context.kind;
        shape->owner[index] = context.owner;
        shape->parent[index] = context.parent;
        shape->depth[index] = context.depth;
    }
}

static void TestFcLifecycle(void)
{
    LayoutFcFixture fixture;
    LayoutStats stats;
    LayoutFcShape first;
    LayoutFcShape second;
    LayoutFcId rootContext = RoboLayoutFcIdNone;
    size_t index;

    LayoutFixture();
    LayoutFcBegin(&fixture, LAYOUT_FC_HTML, layoutFcDisplays, LAYOUT_FC_PAIRS);
    LayoutFcClassify(&fixture, &stats);
    LayoutFcCapture(fixture.tree, &first);
    RoboCheckEqInt(first.count, 4u);

    /*
     * A second pass over the same tree replaces the classification rather than
     * refining it, and the result is identical -- contexts, owners, parents and
     * depths. That is what makes the classification *deterministic*, and it is the
     * property a rebuild relies on.
     */
    LayoutFcClassify(&fixture, &stats);
    LayoutFcCapture(fixture.tree, &second);
    RoboCheckEqInt(second.count, first.count);
    for (index = 0u; index < first.count; index++) {
        RoboCheckEqInt((int)second.kind[index], (int)first.kind[index]);
        RoboCheckEqInt((int)second.owner[index], (int)first.owner[index]);
        RoboCheckEqInt((int)second.parent[index], (int)first.parent[index]);
        RoboCheckEqInt(second.depth[index], first.depth[index]);
    }
    /* And the boxes still agree about which context they are in, so the arena was
     * not replaced under them. */
    RoboCheckEqInt(LayoutObjectContext(fixture.tree, fixture.root, &rootContext,
                                                 NULL),
                      RoboOk);
    RoboCheckEqInt((int)rootContext, 1);

    /* Invalidating releases the contexts and leaves every box exactly where it was:
     * a classification is derived state and the boxes are not. */
    LayoutTreeInvalidateContexts(fixture.tree);
    RoboCheckEqInt(LayoutTreeIsClassified(fixture.tree), 0);
    RoboCheckEqInt(LayoutTreeContextCount(fixture.tree), 0u);
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 11u);
    RoboCheckEqInt(LayoutTreeRoot(fixture.tree), fixture.root);
    /*
     * And the box's own stale id must *not* be readable. This is the assertion that
     * found a real bug: the ids live on the boxes and the arena was released, so a
     * version of this module that trusted the field handed back an id that named
     * nothing -- a stale answer a caller could not tell from a live one. The doors
     * validate the id against the arena they are reading from, which is why this is
     * a miss rather than a number.
     */
    RoboCheckEqInt(LayoutObjectContext(fixture.tree, fixture.root, &rootContext,
                                                 NULL),
                      RoboNotFound);
    RoboCheckEqInt(LayoutObjectContextOwned(fixture.tree, fixture.root,
                                                       &rootContext, NULL),
                      RoboNotFound);
    RoboCheckEqInt(LayoutObjectEstablishesContext(fixture.tree, fixture.root), 0);
    /* Idempotent, because "make sure nothing is classified" is a legitimate thing
     * to want twice. */
    LayoutTreeInvalidateContexts(fixture.tree);
    RoboCheckEqInt(LayoutTreeContextCount(fixture.tree), 0u);
    LayoutTreeInvalidateContexts(NULL);

    /* Re-classifying produces the same shape, which is what makes invalidation
     * safe rather than merely available. */
    LayoutFcClassify(&fixture, &stats);
    LayoutFcCapture(fixture.tree, &second);
    RoboCheckEqInt(second.count, first.count);
    RoboCheckEqInt((int)second.kind[0], (int)first.kind[0]);
    RoboCheckEqInt((int)second.owner[3], (int)first.owner[3]);

    /* Clearing a tree releases the classification with the boxes -- the contexts
     * describe a box set, and the box set is gone. Clearing is what a build does
     * first, so a rebuild cannot inherit one. */
    LayoutTreeClear(fixture.tree);
    RoboCheckEqInt(LayoutTreeIsClassified(fixture.tree), 0);
    RoboCheckEqInt(LayoutTreeContextCount(fixture.tree), 0u);
    RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 0u);

    /* A build into a cleared tree classifies nothing, because no pass has run on
     * the new boxes. */
    {
        LayoutBuildOptions options;

        LayoutBuildOptionsInit(&options);
        options.styles = &fixture.styles;
        RoboCheckEqInt(LayoutTreeBuildFromDocument(fixture.tree, fixture.doc.id,
                                                               &options, NULL, NULL),
                          RoboOk);
        RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 11u);
        RoboCheckEqInt(LayoutTreeIsClassified(fixture.tree), 0);
    }

    LayoutFcEnd(&fixture);
}

static void TestFcTreeAddInvalidates(void)
{
    LayoutFcFixture fixture;
    LayoutStats stats;
    LayoutNodeId added = RoboLayoutNodeIdNone;

    LayoutFixture();
    LayoutFcBegin(&fixture, LAYOUT_FC_HTML, layoutFcDisplays, LAYOUT_FC_PAIRS);
    LayoutFcClassify(&fixture, &stats);
    RoboCheckEqInt(LayoutTreeContextCount(fixture.tree), 4u);

    /*
     * An add invalidates. A participant count and a child-composition
     * classification are both statements about a box *set*, and the set has just
     * changed -- so leaving the old classification standing would be a
     * half-truth a caller could not tell from a correct answer. The boxes are
     * untouched and the refused add below leaves the classification alone, which is
     * the other half of the rule: only a change invalidates.
     */
    {
        LayoutObjectInfo info;
        HtmlNodeHandle span = LayoutDocumentFind(&fixture.doc, "span");
        LayoutNodeId parent = LayoutFcBoxOf(&fixture, "body");

        LayoutObjectInfoInit(&info);
        info.parent = parent;
        info.domNode = span;
        info.display = RoboCssDisplayBlock;
        /* A second box for an element that already has one is *not* refused -- the
         * tree is a box set and not a mirror of the DOM -- so this add succeeds and
         * the classification goes with it. */
        RoboCheckEqInt(LayoutTreeAddObject(fixture.tree, &info, &added, NULL),
                          RoboOk);
        RoboCheck(added != RoboLayoutNodeIdNone);
        RoboCheckEqInt(LayoutTreeObjectCount(fixture.tree), 12u);
        RoboCheckEqInt(LayoutTreeIsClassified(fixture.tree), 0);
        RoboCheckEqInt(LayoutTreeContextCount(fixture.tree), 0u);
    }

    /* A *refused* add changes nothing, so it must not invalidate either. */
    LayoutFcClassify(&fixture, &stats);
    RoboCheckEqInt(LayoutTreeContextCount(fixture.tree) > 0u, 1);
    {
        LayoutObjectInfo info;
        LayoutNodeId ignored = RoboLayoutNodeIdNone;
        HtmlNodeHandle span = LayoutDocumentFind(&fixture.doc, "span");
        LayoutNodeId parent = LayoutFcBoxOf(&fixture, "body");

        LayoutObjectInfoInit(&info);
        info.parent = parent;
        info.domNode = span;
        info.display = RoboCssDisplayNone; /* generates no box: refused */
        RoboCheckEqInt(LayoutTreeAddObject(fixture.tree, &info, &ignored, NULL),
                          RoboInvalidArgument);
        RoboCheckEqInt(LayoutTreeIsClassified(fixture.tree), 1);
        RoboCheckEqInt(LayoutTreeContextCount(fixture.tree) > 0u, 1);

        /* And an unknown parent id is refused for the same reason. */
        LayoutObjectInfoInit(&info);
        info.parent = (LayoutNodeId)9999u;
        info.domNode = span;
        info.display = RoboCssDisplayBlock;
        RoboCheckEqInt(LayoutTreeAddObject(fixture.tree, &info, &ignored, NULL),
                          RoboNotFound);
        RoboCheckEqInt(LayoutTreeIsClassified(fixture.tree), 1);
    }

    LayoutFcEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

static void TestFcValidation(void)
{
    LayoutFcFixture fixture;
    LayoutFormattingContext context;
    LayoutFcId id = RoboLayoutFcIdNone;
    LayoutDiag diag = RoboLayoutCleared;
    LayoutStats stats;

    LayoutFixture();
    LayoutFcBegin(&fixture, LAYOUT_FC_HTML, layoutFcDisplays, LAYOUT_FC_PAIRS);
    LayoutFcClassify(&fixture, &stats);

    /* Reading a context does not write an output, which is what makes a failure
     * safe to test by pre-filling the record. */
    memset(&context, 0xAB, sizeof(context));
    RoboCheckEqInt(LayoutTreeContext(fixture.tree, 1u, &context, NULL), RoboOk);
    memset(&context, 0xAB, sizeof(context));
    RoboCheckEqInt(LayoutTreeContext(fixture.tree, 99u, &context, &diag),
                      RoboNotFound);
    RoboCheckEqInt(diag.code, RoboNotFound);
    RoboCheckEqInt((int)diag.domain, (int)RoboLayoutDomainFormattingContext);
    RoboCheckEqInt((int)context.id, (int)RoboLayoutFcIdNone);
    RoboCheckEqInt((int)context.kind, (int)RoboLayoutFcNone);

    RoboCheckEqInt(LayoutTreeContext(fixture.tree, RoboLayoutFcIdNone,
                                               &context, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTreeContext(NULL, 1u, &context, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTreeContext(fixture.tree, 1u, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTreeRootContext(fixture.tree, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTreeRootContext(NULL, &id, NULL),
                      RoboInvalidArgument);

    /* An index at the count is past the end, not the last one. */
    RoboCheckEqInt(LayoutTreeContextAt(fixture.tree, 4u, &context, NULL),
                      RoboNotFound);
    RoboCheckEqInt(LayoutTreeContextAt(fixture.tree, 3u, &context, NULL), RoboOk);
    RoboCheckEqInt(LayoutTreeContextAt(fixture.tree, 0u, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutTreeContextAt(NULL, 0u, &context, NULL),
                      RoboInvalidArgument);

    /* The box doors refuse an id that names nothing, and a NONE id, and a NULL
     * tree -- three answers rather than one crash. */
    RoboCheckEqInt(LayoutObjectContext(fixture.tree, RoboLayoutNodeIdNone, &id,
                                                 NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectContext(fixture.tree, 99u, &id, NULL),
                      RoboNotFound);
    RoboCheckEqInt(LayoutObjectContext(NULL, fixture.root, &id, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectContext(fixture.tree, fixture.root, NULL, NULL),
                      RoboInvalidArgument);
    RoboCheckEqInt(LayoutObjectContextOwned(fixture.tree, 99u, &id, NULL),
                      RoboNotFound);
    RoboCheckEqInt(LayoutObjectContextOwned(fixture.tree, RoboLayoutNodeIdNone,
                                                       &id, NULL),
                      RoboInvalidArgument);

    /* A box in a tree nobody has classified answers NOT_FOUND rather than NONE,
     * because NONE would read as "no context" for a box that will certainly have
     * one. */
    {
        LayoutFcFixture unclassified;

        LayoutFcBegin(&unclassified, LAYOUT_FC_HTML, layoutFcDisplays, LAYOUT_FC_PAIRS);
        RoboCheckEqInt(LayoutTreeIsClassified(unclassified.tree), 0);
        RoboCheckEqInt(LayoutObjectContext(unclassified.tree, unclassified.root,
                                                     &id, NULL),
                          RoboNotFound);
        RoboCheckEqInt(LayoutObjectContextOwned(unclassified.tree,
                                                           unclassified.root, &id, NULL),
                          RoboNotFound);
        RoboCheckEqInt(LayoutObjectEstablishesContext(unclassified.tree,
                                                                 unclassified.root),
                          0);
        LayoutFcEnd(&unclassified);
    }

    /* A context cannot be created for an owner that names no box, and a non-root
     * context cannot be created without an enclosing one. Both are reached through
     * the tree's own door rather than through a caller -- the door is internal to
     * the module and the pass is its only caller -- so they are asserted through
     * the invariants a caller *can* see: every owner is a live box, and every
     * non-root context names a parent that exists. */
    {
        size_t index;

        for (index = 0u; index < LayoutTreeContextCount(fixture.tree); index++) {
            LayoutFormattingContext one;

            RoboCheckEqInt(LayoutTreeContextAt(fixture.tree, index, &one, NULL),
                              RoboOk);
            RoboCheckEqInt(LayoutObjectIsLive(fixture.tree, one.owner), 1);
            if (one.kind == RoboLayoutFcRoot) {
                RoboCheckEqInt((int)one.parent, (int)RoboLayoutFcIdNone);
                RoboCheckEqInt(one.depth, 0u);
            } else {
                LayoutFormattingContext parent;

                RoboCheck(one.parent != RoboLayoutFcIdNone);
                RoboCheckEqInt(LayoutTreeContext(fixture.tree, one.parent, &parent,
                                                           NULL),
                                  RoboOk);
                RoboCheckEqInt(one.depth, parent.depth + 1u);
            }
        }
    }

    LayoutFcEnd(&fixture);
}

/* ------------------------------------------------------------------ */
/* The allocator returns to its baseline                               */
/* ------------------------------------------------------------------ */

static void TestFcAllocatorBalance(void)
{
    size_t before;
    int round;

    LayoutFixture();
    before = LayoutLiveBlocks();

    for (round = 0; round < 16; round++) {
        LayoutFcFixture fixture;
        LayoutContext *context = NULL;
        int pass;

        LayoutFcBegin(&fixture, LAYOUT_FC_HTML, layoutFcDisplays, LAYOUT_FC_PAIRS);
        RoboCheckEqInt(LayoutContextCreate(&context, NULL), RoboOk);
        /* Eight passes, each releasing the previous classification and building a
         * new one -- so the arena is grown, released and rebuilt eight times, which
         * is the path a leak would hide in. */
        for (pass = 0; pass < 8; pass++) {
            RoboCheckEqInt(LayoutContextRun(context, fixture.tree, NULL), RoboOk);
        }
        LayoutContextDestroy(context);
        /* And an explicit invalidation, which is the other release path. */
        LayoutTreeInvalidateContexts(fixture.tree);
        LayoutFcEnd(&fixture);
    }

    RoboCheckEqInt(LayoutLiveBlocks(), before);
}

/* ------------------------------------------------------------------ */

void TestRegisterLayoutFormattingContext(void);

void TestRegisterLayoutFormattingContext(void)
{
    TestRegister("layout.fc.kinds", TestFcKinds);
    TestRegister("layout.fc.classify.establishes", TestFcClassifyEstablishes);
    TestRegister("layout.fc.classify.display", TestFcClassifyDisplay);
    TestRegister("layout.fc.record", TestFcRecord);
    TestRegister("layout.fc.tree.root", TestFcTreeRoot);
    TestRegister("layout.fc.tree.transparent", TestFcTreeTransparent);
    TestRegister("layout.fc.tree.inline_boxes", TestFcTreeInlineBoxes);
    TestRegister("layout.fc.tree.display_none", TestFcTreeDisplayNone);
    TestRegister("layout.fc.tree.root_variants", TestFcTreeRootVariants);
    TestRegister("layout.fc.lifecycle", TestFcLifecycle);
    TestRegister("layout.fc.tree.add_invalidates", TestFcTreeAddInvalidates);
    TestRegister("layout.fc.validation", TestFcValidation);
    TestRegister("layout.fc.allocator_balance", TestFcAllocatorBalance);
}
