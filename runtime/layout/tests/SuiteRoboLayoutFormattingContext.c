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

typedef struct layout_fc_fixture {
    layout_document_t doc;
    robo_layout_tree_t *tree;
    layout_fake_source_t fake;
    robo_layout_style_source_t styles;
    robo_layout_node_id_t root;
} layout_fc_fixture_t;

/**
 * One scripted display answers: an element with this tag computes to this.
 *
 * A named type rather than an anonymous struct repeated at each call site,
 * because C's type compatibility is per-declaration: two anonymous structs with
 * identical members are two different types, and the second would not compile.
 */
typedef struct layout_fc_display_rule {
    const char *tag;
    robo_css_display_t display;
} layout_fc_display_rule_t;

/**
 * Build @p html into a tree whose styles are @p displays, keyed by tag.
 *
 * Every tag named in @p displays is answered for, and every other element keeps
 * the initial `inline` -- which is what makes a document's classification depend
 * on the declarations the test wrote rather than on the parser's synthesized
 * elements.
 */
static void layout_fc_begin(layout_fc_fixture_t *fixture,
                            const char *html,
                            const layout_fc_display_rule_t *displays,
                            size_t pairs)
{
    robo_layout_build_options_t options;
    size_t index;

    memset(fixture, 0, sizeof(*fixture));
    layout_document_build(&fixture->doc, html);
    layout_fake_init(&fixture->fake);
    for (index = 0u; index < pairs; index++) {
        layout_fake_add(&fixture->fake, displays[index].tag, displays[index].display);
    }
    fixture->styles = layout_fake_source_of(&fixture->fake);
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture->tree, NULL), ROBO_OK);
    robo_layout_build_options_init(&options);
    options.styles = &fixture->styles;
    ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture->tree, fixture->doc.id,
                                                           &options, NULL, NULL),
                      ROBO_OK);
    fixture->root = robo_layout_tree_root(fixture->tree);
}

static void layout_fc_end(layout_fc_fixture_t *fixture)
{
    robo_layout_tree_destroy(fixture->tree);
    fixture->tree = NULL;
    layout_fake_dispose(&fixture->fake);
    layout_document_release(&fixture->doc);
}

/** Run one pass, which is what classifies the tree. */
static void layout_fc_classify(layout_fc_fixture_t *fixture, robo_layout_stats_t *out_stats)
{
    robo_layout_context_t *context = NULL;
    robo_layout_stats_t stats;

    ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture->tree, NULL), ROBO_OK);
    robo_layout_context_stats(context, &stats);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(fixture->tree), 1);
    robo_layout_context_destroy(context);
    if (out_stats) {
        *out_stats = stats;
    }
}

/** The box built for the element @p tag, or a miss. */
static robo_layout_node_id_t layout_fc_box_of(layout_fc_fixture_t *fixture, const char *tag)
{
    robo_html_node_handle_t element = layout_document_find(&fixture->doc, tag);

    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&element), 1);
    return robo_layout_object_id_for_dom_node(fixture->tree, &element);
}

/* ------------------------------------------------------------------ */
/* The kind vocabulary                                                 */
/* ------------------------------------------------------------------ */

static void test_fc_kinds(void)
{
    size_t index;
    size_t seen[ROBO_LAYOUT_FC_KIND_COUNT];

    layout_fixture();

    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_FC_KIND_COUNT, 4);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_FC_NONE, 0);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_FC_ROOT, 1);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_FC_BLOCK, 2);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_FC_INLINE, 3);

    ROBO_CHECK_EQ_INT(robo_layout_fc_kind_is_known(ROBO_LAYOUT_FC_NONE), 1);
    ROBO_CHECK_EQ_INT(robo_layout_fc_kind_is_known(ROBO_LAYOUT_FC_ROOT), 1);
    ROBO_CHECK_EQ_INT(robo_layout_fc_kind_is_known(ROBO_LAYOUT_FC_BLOCK), 1);
    ROBO_CHECK_EQ_INT(robo_layout_fc_kind_is_known(ROBO_LAYOUT_FC_INLINE), 1);
    ROBO_CHECK_EQ_INT(robo_layout_fc_kind_is_known((robo_layout_fc_kind_t)4), 0);
    ROBO_CHECK_EQ_INT(robo_layout_fc_kind_is_known((robo_layout_fc_kind_t)99), 0);

    /* Walked in enumerator order: a row whose table entry had drifted would still
     * answer is_known for every value and would show only as a name printing as
     * another's keyword. Every name is non-NULL and no two agree. */
    for (index = 0u; index < ROBO_LAYOUT_FC_KIND_COUNT; index++) {
        robo_layout_fc_kind_t kind = (robo_layout_fc_kind_t)index;
        const char *name = robo_layout_fc_kind_name(kind);
        size_t other;

        ROBO_CHECK_EQ_INT(robo_layout_fc_kind_is_known(kind), 1);
        ROBO_CHECK(name != NULL);
        if (!name) {
            continue;
        }
        seen[index] = strlen(name);
        ROBO_CHECK(seen[index] > 0u);
        for (other = 0u; other < index; other++) {
            ROBO_CHECK(strcmp(name, robo_layout_fc_kind_name(
                                   (robo_layout_fc_kind_t)other)) != 0);
        }
    }
    ROBO_CHECK_STR_EQ(robo_layout_fc_kind_name(ROBO_LAYOUT_FC_NONE), "none");
    ROBO_CHECK_STR_EQ(robo_layout_fc_kind_name(ROBO_LAYOUT_FC_ROOT), "root");
    ROBO_CHECK_STR_EQ(robo_layout_fc_kind_name(ROBO_LAYOUT_FC_BLOCK), "block");
    ROBO_CHECK_STR_EQ(robo_layout_fc_kind_name(ROBO_LAYOUT_FC_INLINE), "inline");
    ROBO_CHECK_STR_EQ(robo_layout_fc_kind_name((robo_layout_fc_kind_t)40), "unknown");
    ROBO_CHECK(robo_layout_fc_kind_name((robo_layout_fc_kind_t)40) != NULL);

    /* The one predicate with a right answer a reader could get wrong: ROOT *is* a
     * block formatting context -- the initial one -- so a block pass that
     * special-cased it would stack the root's children in the wrong direction
     * while every other test passed. */
    ROBO_CHECK_EQ_INT(robo_layout_fc_kind_is_block_container(ROBO_LAYOUT_FC_ROOT), 1);
    ROBO_CHECK_EQ_INT(robo_layout_fc_kind_is_block_container(ROBO_LAYOUT_FC_BLOCK), 1);
    ROBO_CHECK_EQ_INT(robo_layout_fc_kind_is_block_container(ROBO_LAYOUT_FC_INLINE), 0);
    ROBO_CHECK_EQ_INT(robo_layout_fc_kind_is_block_container(ROBO_LAYOUT_FC_NONE), 0);
    ROBO_CHECK_EQ_INT(robo_layout_fc_kind_is_block_container((robo_layout_fc_kind_t)70), 0);
}

/* ------------------------------------------------------------------ */
/* The classifier, as a table                                          */
/* ------------------------------------------------------------------ */

static void test_fc_classify_establishes(void)
{
    layout_fixture();

    /*
     * The table, row by row, and every row is a claim about CSS.
     *
     * `has_block_level_child` is meaningful only for a block container, and it is
     * passed for the inline rows as well so that the *absence* of an effect is
     * asserted rather than assumed: a build that let an inline box with a
     * block-level child establish something would be a build that had turned
     * inline boxes into block containers.
     */
    struct {
        robo_css_display_t display;
        int has_block_level_child;
        int is_root;
        robo_layout_fc_kind_t expected;
        const char *why;
    } table[] = {
        /* The root wins over every display question, including `none`: a document
         * whose root element generates no box has no root box at all, which is a
         * different case, so this row is about a root that *does* generate one. */
        { ROBO_CSS_DISPLAY_BLOCK, 1, 1, ROBO_LAYOUT_FC_ROOT, "root over block" },
        { ROBO_CSS_DISPLAY_BLOCK, 0, 1, ROBO_LAYOUT_FC_ROOT, "root over block, leaf" },
        { ROBO_CSS_DISPLAY_INLINE, 0, 1, ROBO_LAYOUT_FC_ROOT, "root over inline" },
        { ROBO_CSS_DISPLAY_INLINE_BLOCK, 0, 1, ROBO_LAYOUT_FC_ROOT,
          "root over inline-block" },
        { ROBO_CSS_DISPLAY_NONE, 0, 1, ROBO_LAYOUT_FC_NONE, "no box, no contents" },

        /* `flow-root` inner: a new block formatting context by definition, which is
         * what makes `inline-block` establish one without being named. */
        { ROBO_CSS_DISPLAY_INLINE_BLOCK, 0, 0, ROBO_LAYOUT_FC_BLOCK, "atomic inline" },
        { ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 0, ROBO_LAYOUT_FC_BLOCK,
          "atomic inline, block child" },

        /* A block container with no block-level child establishes an inline
         * context; with one, it is transparent. */
        { ROBO_CSS_DISPLAY_BLOCK, 0, 0, ROBO_LAYOUT_FC_INLINE, "block, inline content" },
        { ROBO_CSS_DISPLAY_BLOCK, 1, 0, ROBO_LAYOUT_FC_NONE, "block, transparent" },

        /* An inline box establishes nothing, whatever it contains. */
        { ROBO_CSS_DISPLAY_INLINE, 0, 0, ROBO_LAYOUT_FC_NONE, "inline box" },
        { ROBO_CSS_DISPLAY_INLINE, 1, 0, ROBO_LAYOUT_FC_NONE, "inline box, block child" },

        /* No box: nothing to format. */
        { ROBO_CSS_DISPLAY_NONE, 0, 0, ROBO_LAYOUT_FC_NONE, "none" },
        { ROBO_CSS_DISPLAY_NONE, 1, 0, ROBO_LAYOUT_FC_NONE, "none, irrelevant child" }
    };
    size_t index;

    for (index = 0u; index < sizeof(table) / sizeof(table[0]); index++) {
        robo_layout_fc_kind_t actual = robo_layout_fc_establishes(
            table[index].display, table[index].has_block_level_child, table[index].is_root);

        ROBO_CHECK_MSG(actual == table[index].expected, table[index].why);
    }
}

static void test_fc_classify_display(void)
{
    layout_fixture();

    /*
     * "Can this display *ever* establish a context?" and "is this display a block
     * container?" are different questions, and the difference is the whole reason
     * both doors exist: an `inline` box answers 0 to both for reasons that will
     * never change, while a block answers 1 and *then* answers NONE for a
     * particular child composition.
     */
    struct {
        robo_css_display_t display;
        int can_establish;
        int is_block_container;
    } table[] = {
        { ROBO_CSS_DISPLAY_NONE, 0, 0 },
        { ROBO_CSS_DISPLAY_BLOCK, 1, 1 },
        { ROBO_CSS_DISPLAY_INLINE, 0, 0 },
        { ROBO_CSS_DISPLAY_INLINE_BLOCK, 1, 1 }
    };
    size_t index;

    for (index = 0u; index < sizeof(table) / sizeof(table[0]); index++) {
        ROBO_CHECK_EQ_INT(robo_layout_fc_display_can_establish(table[index].display),
                          table[index].can_establish);
        ROBO_CHECK_EQ_INT(robo_layout_fc_display_is_block_container(table[index].display),
                          table[index].is_block_container);
    }

    /* An undefined enumerator answers no to both rather than falling through a
     * table: a display this build does not know is not a container, and it is
     * certainly not something that establishes anything. */
    ROBO_CHECK_EQ_INT(robo_layout_fc_display_can_establish((robo_css_display_t)88), 0);
    ROBO_CHECK_EQ_INT(robo_layout_fc_display_is_block_container((robo_css_display_t)88), 0);

    /* And the two doors agree wherever both have an opinion: anything that can
     * establish does. Stated as a walk so it stays true as rows are added. */
    for (index = 0u; index < ROBO_CSS_DISPLAY_COUNT; index++) {
        robo_css_display_t display = (robo_css_display_t)index;

        if (robo_layout_fc_display_can_establish(display)) {
            ROBO_CHECK_EQ_INT(robo_layout_fc_establishes(display, 0, 0) !=
                                  ROBO_LAYOUT_FC_NONE,
                              1);
        }
        if (!robo_css_display_generates_box(display)) {
            ROBO_CHECK_EQ_INT(robo_layout_fc_display_can_establish(display), 0);
            ROBO_CHECK_EQ_INT(robo_layout_fc_establishes(display, 0, 0), ROBO_LAYOUT_FC_NONE);
            ROBO_CHECK_EQ_INT(robo_layout_fc_establishes(display, 0, 1), ROBO_LAYOUT_FC_NONE);
        }
    }
}

/* ------------------------------------------------------------------ */
/* The record                                                          */
/* ------------------------------------------------------------------ */

static void test_fc_record(void)
{
    robo_layout_formatting_context_t context;
    robo_layout_formatting_context_t other;

    layout_fixture();

    memset(&context, 0xAB, sizeof(context));
    robo_layout_formatting_context_init(&context);
    ROBO_CHECK_EQ_INT((int)context.id, (int)ROBO_LAYOUT_FC_ID_NONE);
    ROBO_CHECK_EQ_INT((int)context.kind, (int)ROBO_LAYOUT_FC_NONE);
    ROBO_CHECK_EQ_INT((int)context.owner, (int)ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT((int)context.parent, (int)ROBO_LAYOUT_FC_ID_NONE);
    ROBO_CHECK_EQ_INT(context.participants, 0u);
    ROBO_CHECK_EQ_INT(context.depth, 0u);

    robo_layout_formatting_context_init(NULL);

    other = context;
    ROBO_CHECK_EQ_INT(robo_layout_formatting_context_equal(&context, &other), 1);

    /* Identity is part of equality, deliberately: two contexts with the same kind,
     * owner and parent are still two contexts, and a comparison that ignored the
     * id would report a classification rebuilt into a different tree as
     * unchanged. */
    other.id = 7u;
    ROBO_CHECK_EQ_INT(robo_layout_formatting_context_equal(&context, &other), 0);
    other = context;
    other.kind = ROBO_LAYOUT_FC_BLOCK;
    ROBO_CHECK_EQ_INT(robo_layout_formatting_context_equal(&context, &other), 0);
    other = context;
    other.owner = 3u;
    ROBO_CHECK_EQ_INT(robo_layout_formatting_context_equal(&context, &other), 0);
    other = context;
    other.parent = 5u;
    ROBO_CHECK_EQ_INT(robo_layout_formatting_context_equal(&context, &other), 0);
    other = context;
    other.participants = 9u;
    ROBO_CHECK_EQ_INT(robo_layout_formatting_context_equal(&context, &other), 0);
    other = context;
    other.depth = 2u;
    ROBO_CHECK_EQ_INT(robo_layout_formatting_context_equal(&context, &other), 0);

    /* Two NULls are equal and one is not: the same rule the other records use, so
     * a caller can compare an optional record without a second guard. */
    ROBO_CHECK_EQ_INT(robo_layout_formatting_context_equal(NULL, NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_formatting_context_equal(&context, NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_formatting_context_equal(NULL, &context), 0);
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
static const layout_fc_display_rule_t layout_fc_displays[] = {
    { "html", ROBO_CSS_DISPLAY_BLOCK },
    { "head", ROBO_CSS_DISPLAY_BLOCK },
    { "body", ROBO_CSS_DISPLAY_BLOCK },
    { "div", ROBO_CSS_DISPLAY_BLOCK },
    { "section", ROBO_CSS_DISPLAY_BLOCK },
    { "p", ROBO_CSS_DISPLAY_BLOCK },
    { "span", ROBO_CSS_DISPLAY_INLINE },
    { "i", ROBO_CSS_DISPLAY_INLINE_BLOCK }
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

#define LAYOUT_FC_PAIRS (sizeof(layout_fc_displays) / sizeof(layout_fc_displays[0]))

static void test_fc_tree_root(void)
{
    layout_fc_fixture_t fixture;
    robo_layout_stats_t stats;
    robo_layout_fc_id_t root_context = ROBO_LAYOUT_FC_ID_NONE;
    robo_layout_formatting_context_t context;
    robo_layout_fc_id_t owned = ROBO_LAYOUT_FC_ID_NONE;

    layout_fixture();
    layout_fc_begin(&fixture, LAYOUT_FC_HTML, layout_fc_displays, LAYOUT_FC_PAIRS);

    /* Nothing is classified before a pass, and the two facts are separate: the
     * tree has boxes and it has no contexts. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(fixture.tree), 0);
    ROBO_CHECK_EQ_INT(robo_layout_tree_context_count(fixture.tree), 0u);
    /* Eight boxes and the three text runs their text nodes contributed (10.5): a
     * text object is an object, so it is in the count the classification walks. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 11u);

    layout_fc_classify(&fixture, &stats);

    /* Exactly one initial context, owned by the root box, with no parent and at
     * depth 0. */
    ROBO_CHECK_EQ_INT(stats.contexts_by_kind[ROBO_LAYOUT_FC_ROOT], 1u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_root_context(fixture.tree, &root_context, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)root_context, 1);
    ROBO_CHECK_EQ_INT(robo_layout_tree_context(fixture.tree, root_context, &context, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)context.kind, (int)ROBO_LAYOUT_FC_ROOT);
    ROBO_CHECK_EQ_INT((int)context.owner, (int)fixture.root);
    ROBO_CHECK_EQ_INT((int)context.parent, (int)ROBO_LAYOUT_FC_ID_NONE);
    ROBO_CHECK_EQ_INT(context.depth, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_fc_kind_is_block_container(context.kind), 1);

    /* The root box participates in the context it establishes -- there is nothing
     * above it -- so "every live box has a context once the tree is classified"
     * holds without an exception. */
    ROBO_CHECK_EQ_INT(robo_layout_object_context(fixture.tree, fixture.root, &owned, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)owned, (int)root_context);
    ROBO_CHECK_EQ_INT(robo_layout_object_context_owned(fixture.tree, fixture.root, &owned,
                                                       NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)owned, (int)root_context);

    /* Every box reaches a context, and the reach is real rather than a default. */
    {
        size_t index;

        for (index = 0u; index < robo_layout_tree_object_count(fixture.tree); index++) {
            robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;

            ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture.tree, index, &id), ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_layout_object_context(fixture.tree, id, &owned, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(owned != ROBO_LAYOUT_FC_ID_NONE, 1);
        }
    }

    layout_fc_end(&fixture);
}

static void test_fc_tree_transparent(void)
{
    layout_fc_fixture_t fixture;
    robo_layout_stats_t stats;
    robo_layout_fc_id_t div_context = ROBO_LAYOUT_FC_ID_NONE;
    robo_layout_fc_id_t p_context = ROBO_LAYOUT_FC_ID_NONE;
    robo_layout_fc_id_t p_owned = ROBO_LAYOUT_FC_ID_NONE;
    robo_layout_fc_id_t root_context = ROBO_LAYOUT_FC_ID_NONE;
    robo_layout_formatting_context_t context;

    layout_fixture();
    layout_fc_begin(&fixture, LAYOUT_FC_HTML, layout_fc_displays, LAYOUT_FC_PAIRS);
    layout_fc_classify(&fixture, &stats);

    /* Four contexts: the root, the two inline contexts (`<head>` and `<p>`) and the
     * one block context `<i>` establishes. The two transparent block containers
     * establish nothing, and that is the count that says so. */
    ROBO_CHECK_EQ_INT(stats.contexts, 4u);
    ROBO_CHECK_EQ_INT(stats.contexts_by_kind[ROBO_LAYOUT_FC_NONE], 0u);
    ROBO_CHECK_EQ_INT(stats.contexts_by_kind[ROBO_LAYOUT_FC_ROOT], 1u);
    ROBO_CHECK_EQ_INT(stats.contexts_by_kind[ROBO_LAYOUT_FC_BLOCK], 1u);
    ROBO_CHECK_EQ_INT(stats.contexts_by_kind[ROBO_LAYOUT_FC_INLINE], 2u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_context_count(fixture.tree), 4u);
    ROBO_CHECK_EQ_INT(stats.boxes_establishing, 4u);
    /*
     * Seven of eleven participate without establishing: `<body>`, `<div>` and
     * `<section>` are transparent (each has a block-level child), `<span>` is inline,
     * and the three **text runs** take the context of whichever box encloses
     * them (10.5). The identity below is the one that has to hold, and it does: every
     * object either establishes a context or participates in one, and none does
     * neither.
     */
    ROBO_CHECK_EQ_INT(stats.boxes_participating_only, 7u);
    ROBO_CHECK_EQ_INT(stats.boxes_establishing + stats.boxes_participating_only,
                      stats.objects);
    /* html 0, head 1, body 1, div 2, section 3, p 4 -- so `<p>`'s context nests one
     * level below the root's. */
    ROBO_CHECK_EQ_INT(stats.context_depth_max, 1u);

    ROBO_CHECK_EQ_INT(robo_layout_tree_root_context(fixture.tree, &root_context, NULL),
                      ROBO_OK);

    /*
     * `<div>` and `<section>` are both transparent: each has a block-level child.
     * Their own layout participates in the root context, they establish nothing,
     * and -- this is the assertion the model exists for -- `<p>` reaches the root
     * context *through* both of them rather than each being a barrier.
     */
    {
        robo_layout_node_id_t div = layout_fc_box_of(&fixture, "div");
        robo_layout_node_id_t section = layout_fc_box_of(&fixture, "section");
        robo_layout_node_id_t p = layout_fc_box_of(&fixture, "p");

        ROBO_CHECK_EQ_INT(robo_layout_object_establishes_context(fixture.tree, div), 0);
        ROBO_CHECK_EQ_INT(robo_layout_object_establishes_context(fixture.tree, section), 0);
        ROBO_CHECK_EQ_INT(robo_layout_object_context_owned(fixture.tree, div, &div_context,
                                                           NULL),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_layout_object_context(fixture.tree, div, &div_context, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)div_context, (int)root_context);
        ROBO_CHECK_EQ_INT(robo_layout_object_context(fixture.tree, section, &div_context,
                                                     NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)div_context, (int)root_context);
        ROBO_CHECK_EQ_INT(robo_layout_object_context(fixture.tree, p, &p_context, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)p_context, (int)root_context);

        /* `<p>` holds only text, and a text node is not a box, so it has no
         * block-level child and therefore establishes an inline context -- which is
         * where 10.6's line box for that text will live. */
        ROBO_CHECK_EQ_INT(robo_layout_object_establishes_context(fixture.tree, p), 1);
        ROBO_CHECK_EQ_INT(robo_layout_object_context_owned(fixture.tree, p, &p_owned, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_context(fixture.tree, p_owned, &context, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)context.kind, (int)ROBO_LAYOUT_FC_INLINE);
        ROBO_CHECK_EQ_INT((int)context.owner, (int)p);
        ROBO_CHECK_EQ_INT((int)context.parent, (int)root_context);
        ROBO_CHECK_EQ_INT(context.depth, 1u);
        /*
         * One participant, and it is the phase's arrival: 10.4 left this at zero with
         * the note that inline content that is text was not a box, and 10.5 is the
         * phase that changed it. The participant is `<p>`'s own text run -- a layout
         * object, not a box, which is why the *box* counters above do not see it.
         */
        ROBO_CHECK_EQ_INT(context.participants, 1u);
    }

    /* The initial context coordinates every box, because nothing in this document
     * establishes a context that encloses another box. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_context(fixture.tree, root_context, &context, NULL),
                      ROBO_OK);
    /*
     * Every box, and every text run whose nearest enclosing context is the root's --
     * which is one of the three: `<p>` and `<i>` each establish a context of their
     * own, so the runs inside them belong to those and not here. Written as the
     * object count minus two rather than as a bare 9, so that the *reason* is in the
     * expression: a text run is only the root's when nothing nearer owns it.
     */
    ROBO_CHECK_EQ_INT(context.participants, 9u);
    ROBO_CHECK_EQ_INT(context.participants,
                      robo_layout_tree_object_count(fixture.tree) - 2u);

    layout_fc_end(&fixture);
}

static void test_fc_tree_inline_boxes(void)
{
    /* `<p>` holds an inline box and an atomic inline, so its inline context has box
     * participants -- and the atomic inline both participates in that context and
     * establishes a block context of its own. */
    layout_fc_fixture_t fixture;
    robo_layout_stats_t stats;
    robo_layout_fc_id_t p_owned = ROBO_LAYOUT_FC_ID_NONE;
    robo_layout_fc_id_t span_context = ROBO_LAYOUT_FC_ID_NONE;
    robo_layout_fc_id_t i_context = ROBO_LAYOUT_FC_ID_NONE;
    robo_layout_fc_id_t i_owned = ROBO_LAYOUT_FC_ID_NONE;
    robo_layout_formatting_context_t context;

    layout_fixture();
    layout_fc_begin(&fixture, "<html><body><p><span>a</span><i>b</i></p></body></html>",
                    layout_fc_displays, LAYOUT_FC_PAIRS);
    /* html, head, body, p, span, i -- and the two text runs `a` and `b`. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 8u);
    layout_fc_classify(&fixture, &stats);

    ROBO_CHECK_EQ_INT(stats.contexts, 4u);
    ROBO_CHECK_EQ_INT(stats.contexts_by_kind[ROBO_LAYOUT_FC_ROOT], 1u);
    ROBO_CHECK_EQ_INT(stats.contexts_by_kind[ROBO_LAYOUT_FC_BLOCK], 1u);
    ROBO_CHECK_EQ_INT(stats.contexts_by_kind[ROBO_LAYOUT_FC_INLINE], 2u);
    ROBO_CHECK_EQ_INT(stats.boxes_establishing, 4u);
    /* `<body>` and `<span>` participate without establishing, and so do the two text
     * runs -- four of eight. */
    ROBO_CHECK_EQ_INT(stats.boxes_participating_only, 4u);
    /* Two, not one: `<i>`'s block context nests *inside* `<p>`'s inline one, so the
     * deepest context is an atomic inline's, one level below its parent's. */
    ROBO_CHECK_EQ_INT(stats.context_depth_max, 2u);

    {
        robo_layout_node_id_t p = layout_fc_box_of(&fixture, "p");
        robo_layout_node_id_t span = layout_fc_box_of(&fixture, "span");
        robo_layout_node_id_t i = layout_fc_box_of(&fixture, "i");

        ROBO_CHECK_EQ_INT(robo_layout_object_context_owned(fixture.tree, p, &p_owned, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_context(fixture.tree, p_owned, &context, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)context.kind, (int)ROBO_LAYOUT_FC_INLINE);
        /* `<span>`, `<i>`, and the text run inside `<span>`: a transparent inline box
         * is transparent to the *line* as well as to the context, so its text is a
         * participant of `<p>`'s context and not of a context of its own. */
        ROBO_CHECK_EQ_INT(context.participants, 3u);

        /* An inline box participates; it establishes nothing. */
        ROBO_CHECK_EQ_INT(robo_layout_object_context(fixture.tree, span, &span_context,
                                                     NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)span_context, (int)p_owned);
        ROBO_CHECK_EQ_INT(robo_layout_object_establishes_context(fixture.tree, span), 0);
        ROBO_CHECK_EQ_INT(robo_layout_object_context_owned(fixture.tree, span,
                                                           &span_context, NULL),
                          ROBO_NOT_FOUND);

        /*
         * The atomic inline: it participates in its parent's *inline* context and
         * establishes a *block* context for its own contents. Both ids are real and
         * they are different, which is the single assertion that would fail if the
         * two relationships were ever collapsed into one field.
         */
        ROBO_CHECK_EQ_INT(robo_layout_object_context(fixture.tree, i, &i_context, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)i_context, (int)p_owned);
        ROBO_CHECK_EQ_INT(robo_layout_object_context_owned(fixture.tree, i, &i_owned, NULL),
                          ROBO_OK);
        ROBO_CHECK(i_context != i_owned);
        ROBO_CHECK_EQ_INT(robo_layout_tree_context(fixture.tree, i_owned, &context, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)context.kind, (int)ROBO_LAYOUT_FC_BLOCK);
        ROBO_CHECK_EQ_INT((int)context.owner, (int)i);
        ROBO_CHECK_EQ_INT((int)context.parent, (int)p_owned);
        ROBO_CHECK_EQ_INT(context.depth, 2u);
        ROBO_CHECK_EQ_INT(robo_layout_fc_kind_is_block_container(context.kind), 1);
        /* And it coordinates one thing: the text run inside `<i>`, which is the block
         * context's own because `<i>` is atomic and owns its contents. */
        ROBO_CHECK_EQ_INT(context.participants, 1u);
    }

    /* The nesting is real: a context two levels down names the one above it, so the
     * contexts form a tree rather than a flat list with a depth column. */
    {
        robo_layout_formatting_context_t parent;
        robo_layout_fc_id_t walk = i_owned;
        size_t steps = 0u;

        while (walk != ROBO_LAYOUT_FC_ID_NONE) {
            ROBO_CHECK_EQ_INT(robo_layout_tree_context(fixture.tree, walk, &parent, NULL),
                              ROBO_OK);
            walk = parent.parent;
            steps++;
            if (steps > 8u) {
                break;
            }
        }
        ROBO_CHECK_EQ_INT(steps, 3u); /* block -> inline -> root, then NONE */
    }

    layout_fc_end(&fixture);
}

static void test_fc_tree_display_none(void)
{
    static const layout_fc_display_rule_t displays[] = {
        { "html", ROBO_CSS_DISPLAY_BLOCK },
        { "head", ROBO_CSS_DISPLAY_BLOCK },
        { "body", ROBO_CSS_DISPLAY_BLOCK },
        { "div", ROBO_CSS_DISPLAY_BLOCK },
        { "aside", ROBO_CSS_DISPLAY_NONE },
        { "span", ROBO_CSS_DISPLAY_BLOCK }
    };
    layout_fc_fixture_t fixture;
    robo_layout_stats_t stats;
    robo_html_node_handle_t aside;
    size_t index;

    layout_fixture();
    layout_fc_begin(&fixture, "<html><body><div></div><aside><span>x</span></aside></body></html>",
                    displays, sizeof(displays) / sizeof(displays[0]));

    /* `<aside>` generates no box and its `<span>` is never reached, so four boxes:
     * html, head, body, div. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 4u);
    layout_fc_classify(&fixture, &stats);

    /* Three contexts. The `display: none` element has no box, so it is not a
     * participant and it owns nothing -- and the DOM still has it, which is what
     * makes "no context" different from "the element is gone". */
    ROBO_CHECK_EQ_INT(stats.contexts, 3u);
    ROBO_CHECK_EQ_INT(stats.contexts_by_kind[ROBO_LAYOUT_FC_ROOT], 1u);
    ROBO_CHECK_EQ_INT(stats.contexts_by_kind[ROBO_LAYOUT_FC_INLINE], 2u);
    ROBO_CHECK_EQ_INT(stats.contexts_by_kind[ROBO_LAYOUT_FC_BLOCK], 0u);

    aside = layout_document_find(&fixture.doc, "aside");
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&aside), 1);
    ROBO_CHECK_EQ_INT((int)robo_layout_object_id_for_dom_node(fixture.tree, &aside),
                      (int)ROBO_LAYOUT_NODE_ID_NONE);
    /* Asking the DOM the other way confirms the element is still there. */
    ROBO_CHECK_STR_EQ(robo_html_element_name(&aside, NULL), "aside");

    /* No context anywhere claims either element: an owner is always a live box, and
     * neither the hidden element nor the one inside it has one. */
    for (index = 0u; index < robo_layout_tree_context_count(fixture.tree); index++) {
        robo_layout_formatting_context_t context;

        ROBO_CHECK_EQ_INT(robo_layout_tree_context_at(fixture.tree, index, &context, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_object_is_live(fixture.tree, context.owner), 1);
        ROBO_CHECK_EQ_INT((int)context.id, (int)(index + 1u));
    }

    layout_fc_end(&fixture);
}

static void test_fc_tree_root_variants(void)
{
    /* The root box establishes the initial context whatever it computes to. */
    static const layout_fc_display_rule_t inline_root[] = {
        { "html", ROBO_CSS_DISPLAY_INLINE },
        { "body", ROBO_CSS_DISPLAY_BLOCK }
    };
    static const layout_fc_display_rule_t hidden_root[] = {
        { "html", ROBO_CSS_DISPLAY_NONE }
    };
    layout_fc_fixture_t fixture;
    robo_layout_stats_t stats;
    robo_layout_fc_id_t root_context = ROBO_LAYOUT_FC_ID_NONE;
    robo_layout_formatting_context_t context;

    layout_fixture();

    layout_fc_begin(&fixture, "<html><body></body></html>", inline_root,
                    sizeof(inline_root) / sizeof(inline_root[0]));
    layout_fc_classify(&fixture, &stats);
    ROBO_CHECK_EQ_INT(robo_layout_tree_root_context(fixture.tree, &root_context, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_context(fixture.tree, root_context, &context, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)context.kind, (int)ROBO_LAYOUT_FC_ROOT);
    ROBO_CHECK_EQ_INT((int)context.owner, (int)fixture.root);
    ROBO_CHECK_EQ_INT(robo_layout_fc_kind_is_block_container(context.kind), 1);
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
    ROBO_CHECK_EQ_INT(stats.contexts, 2u);
    ROBO_CHECK_EQ_INT(stats.contexts_by_kind[ROBO_LAYOUT_FC_ROOT], 1u);
    ROBO_CHECK_EQ_INT(stats.contexts_by_kind[ROBO_LAYOUT_FC_INLINE], 1u);
    ROBO_CHECK_EQ_INT(stats.contexts_by_kind[ROBO_LAYOUT_FC_BLOCK], 0u);
    {
        robo_layout_node_id_t head = layout_fc_box_of(&fixture, "head");
        robo_layout_fc_id_t head_context = ROBO_LAYOUT_FC_ID_NONE;

        ROBO_CHECK_EQ_INT((int)robo_layout_object_display(fixture.tree, head),
                          (int)ROBO_CSS_DISPLAY_INLINE);
        ROBO_CHECK_EQ_INT(robo_layout_object_establishes_context(fixture.tree, head), 0);
        ROBO_CHECK_EQ_INT(robo_layout_object_context(fixture.tree, head, &head_context,
                                                     NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)head_context, (int)root_context);
    }
    layout_fc_end(&fixture);

    /*
     * A `display: none` root generates no boxes at all, and the result is a tree
     * that is *classified* with no contexts -- which is a different statement from
     * a tree nobody has looked at, and the reason `is_classified` exists.
     */
    layout_fc_begin(&fixture, "<html><body><p>x</p></body></html>", hidden_root,
                    sizeof(hidden_root) / sizeof(hidden_root[0]));
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 0u);
    layout_fc_classify(&fixture, &stats);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(fixture.tree), 1);
    ROBO_CHECK_EQ_INT(robo_layout_tree_context_count(fixture.tree), 0u);
    ROBO_CHECK_EQ_INT(stats.contexts, 0u);
    ROBO_CHECK_EQ_INT(stats.boxes_establishing, 0u);
    ROBO_CHECK_EQ_INT(stats.boxes_participating_only, 0u);
    ROBO_CHECK_EQ_INT(stats.context_depth_max, 0u);
    /* No root box, so no initial context -- reported rather than invented. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_root_context(fixture.tree, &root_context, NULL),
                      ROBO_NOT_FOUND);
    layout_fc_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Lifecycle, invalidation and determinism                             */
/* ------------------------------------------------------------------ */

/** The classification of @p tree, as a list of (kind, owner, parent, depth). */
typedef struct layout_fc_shape {
    robo_layout_fc_kind_t kind[32];
    robo_layout_node_id_t owner[32];
    robo_layout_fc_id_t parent[32];
    uint32_t depth[32];
    size_t count;
} layout_fc_shape_t;

static void layout_fc_capture(const robo_layout_tree_t *tree, layout_fc_shape_t *shape)
{
    size_t index;

    memset(shape, 0, sizeof(*shape));
    shape->count = robo_layout_tree_context_count(tree);
    ROBO_CHECK(shape->count <= 32u);
    for (index = 0u; index < shape->count; index++) {
        robo_layout_formatting_context_t context;

        ROBO_CHECK_EQ_INT(robo_layout_tree_context_at(tree, index, &context, NULL), ROBO_OK);
        shape->kind[index] = context.kind;
        shape->owner[index] = context.owner;
        shape->parent[index] = context.parent;
        shape->depth[index] = context.depth;
    }
}

static void test_fc_lifecycle(void)
{
    layout_fc_fixture_t fixture;
    robo_layout_stats_t stats;
    layout_fc_shape_t first;
    layout_fc_shape_t second;
    robo_layout_fc_id_t root_context = ROBO_LAYOUT_FC_ID_NONE;
    size_t index;

    layout_fixture();
    layout_fc_begin(&fixture, LAYOUT_FC_HTML, layout_fc_displays, LAYOUT_FC_PAIRS);
    layout_fc_classify(&fixture, &stats);
    layout_fc_capture(fixture.tree, &first);
    ROBO_CHECK_EQ_INT(first.count, 4u);

    /*
     * A second pass over the same tree replaces the classification rather than
     * refining it, and the result is identical -- contexts, owners, parents and
     * depths. That is what makes the classification *deterministic*, and it is the
     * property a rebuild relies on.
     */
    layout_fc_classify(&fixture, &stats);
    layout_fc_capture(fixture.tree, &second);
    ROBO_CHECK_EQ_INT(second.count, first.count);
    for (index = 0u; index < first.count; index++) {
        ROBO_CHECK_EQ_INT((int)second.kind[index], (int)first.kind[index]);
        ROBO_CHECK_EQ_INT((int)second.owner[index], (int)first.owner[index]);
        ROBO_CHECK_EQ_INT((int)second.parent[index], (int)first.parent[index]);
        ROBO_CHECK_EQ_INT(second.depth[index], first.depth[index]);
    }
    /* And the boxes still agree about which context they are in, so the arena was
     * not replaced under them. */
    ROBO_CHECK_EQ_INT(robo_layout_object_context(fixture.tree, fixture.root, &root_context,
                                                 NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)root_context, 1);

    /* Invalidating releases the contexts and leaves every box exactly where it was:
     * a classification is derived state and the boxes are not. */
    robo_layout_tree_invalidate_contexts(fixture.tree);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(fixture.tree), 0);
    ROBO_CHECK_EQ_INT(robo_layout_tree_context_count(fixture.tree), 0u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 11u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_root(fixture.tree), fixture.root);
    /*
     * And the box's own stale id must *not* be readable. This is the assertion that
     * found a real bug: the ids live on the boxes and the arena was released, so a
     * version of this module that trusted the field handed back an id that named
     * nothing -- a stale answer a caller could not tell from a live one. The doors
     * validate the id against the arena they are reading from, which is why this is
     * a miss rather than a number.
     */
    ROBO_CHECK_EQ_INT(robo_layout_object_context(fixture.tree, fixture.root, &root_context,
                                                 NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_object_context_owned(fixture.tree, fixture.root,
                                                       &root_context, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_object_establishes_context(fixture.tree, fixture.root), 0);
    /* Idempotent, because "make sure nothing is classified" is a legitimate thing
     * to want twice. */
    robo_layout_tree_invalidate_contexts(fixture.tree);
    ROBO_CHECK_EQ_INT(robo_layout_tree_context_count(fixture.tree), 0u);
    robo_layout_tree_invalidate_contexts(NULL);

    /* Re-classifying produces the same shape, which is what makes invalidation
     * safe rather than merely available. */
    layout_fc_classify(&fixture, &stats);
    layout_fc_capture(fixture.tree, &second);
    ROBO_CHECK_EQ_INT(second.count, first.count);
    ROBO_CHECK_EQ_INT((int)second.kind[0], (int)first.kind[0]);
    ROBO_CHECK_EQ_INT((int)second.owner[3], (int)first.owner[3]);

    /* Clearing a tree releases the classification with the boxes -- the contexts
     * describe a box set, and the box set is gone. Clearing is what a build does
     * first, so a rebuild cannot inherit one. */
    robo_layout_tree_clear(fixture.tree);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(fixture.tree), 0);
    ROBO_CHECK_EQ_INT(robo_layout_tree_context_count(fixture.tree), 0u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 0u);

    /* A build into a cleared tree classifies nothing, because no pass has run on
     * the new boxes. */
    {
        robo_layout_build_options_t options;

        robo_layout_build_options_init(&options);
        options.styles = &fixture.styles;
        ROBO_CHECK_EQ_INT(robo_layout_tree_build_from_document(fixture.tree, fixture.doc.id,
                                                               &options, NULL, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 11u);
        ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(fixture.tree), 0);
    }

    layout_fc_end(&fixture);
}

static void test_fc_tree_add_invalidates(void)
{
    layout_fc_fixture_t fixture;
    robo_layout_stats_t stats;
    robo_layout_node_id_t added = ROBO_LAYOUT_NODE_ID_NONE;

    layout_fixture();
    layout_fc_begin(&fixture, LAYOUT_FC_HTML, layout_fc_displays, LAYOUT_FC_PAIRS);
    layout_fc_classify(&fixture, &stats);
    ROBO_CHECK_EQ_INT(robo_layout_tree_context_count(fixture.tree), 4u);

    /*
     * An add invalidates. A participant count and a child-composition
     * classification are both statements about a box *set*, and the set has just
     * changed -- so leaving the old classification standing would be a
     * half-truth a caller could not tell from a correct answer. The boxes are
     * untouched and the refused add below leaves the classification alone, which is
     * the other half of the rule: only a change invalidates.
     */
    {
        robo_layout_object_info_t info;
        robo_html_node_handle_t span = layout_document_find(&fixture.doc, "span");
        robo_layout_node_id_t parent = layout_fc_box_of(&fixture, "body");

        robo_layout_object_info_init(&info);
        info.parent = parent;
        info.dom_node = span;
        info.display = ROBO_CSS_DISPLAY_BLOCK;
        /* A second box for an element that already has one is *not* refused -- the
         * tree is a box set and not a mirror of the DOM -- so this add succeeds and
         * the classification goes with it. */
        ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(fixture.tree, &info, &added, NULL),
                          ROBO_OK);
        ROBO_CHECK(added != ROBO_LAYOUT_NODE_ID_NONE);
        ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 12u);
        ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(fixture.tree), 0);
        ROBO_CHECK_EQ_INT(robo_layout_tree_context_count(fixture.tree), 0u);
    }

    /* A *refused* add changes nothing, so it must not invalidate either. */
    layout_fc_classify(&fixture, &stats);
    ROBO_CHECK_EQ_INT(robo_layout_tree_context_count(fixture.tree) > 0u, 1);
    {
        robo_layout_object_info_t info;
        robo_layout_node_id_t ignored = ROBO_LAYOUT_NODE_ID_NONE;
        robo_html_node_handle_t span = layout_document_find(&fixture.doc, "span");
        robo_layout_node_id_t parent = layout_fc_box_of(&fixture, "body");

        robo_layout_object_info_init(&info);
        info.parent = parent;
        info.dom_node = span;
        info.display = ROBO_CSS_DISPLAY_NONE; /* generates no box: refused */
        ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(fixture.tree, &info, &ignored, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(fixture.tree), 1);
        ROBO_CHECK_EQ_INT(robo_layout_tree_context_count(fixture.tree) > 0u, 1);

        /* And an unknown parent id is refused for the same reason. */
        robo_layout_object_info_init(&info);
        info.parent = (robo_layout_node_id_t)9999u;
        info.dom_node = span;
        info.display = ROBO_CSS_DISPLAY_BLOCK;
        ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(fixture.tree, &info, &ignored, NULL),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(fixture.tree), 1);
    }

    layout_fc_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

static void test_fc_validation(void)
{
    layout_fc_fixture_t fixture;
    robo_layout_formatting_context_t context;
    robo_layout_fc_id_t id = ROBO_LAYOUT_FC_ID_NONE;
    robo_layout_diag_t diag = ROBO_LAYOUT_CLEARED;
    robo_layout_stats_t stats;

    layout_fixture();
    layout_fc_begin(&fixture, LAYOUT_FC_HTML, layout_fc_displays, LAYOUT_FC_PAIRS);
    layout_fc_classify(&fixture, &stats);

    /* Reading a context does not write an output, which is what makes a failure
     * safe to test by pre-filling the record. */
    memset(&context, 0xAB, sizeof(context));
    ROBO_CHECK_EQ_INT(robo_layout_tree_context(fixture.tree, 1u, &context, NULL), ROBO_OK);
    memset(&context, 0xAB, sizeof(context));
    ROBO_CHECK_EQ_INT(robo_layout_tree_context(fixture.tree, 99u, &context, &diag),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT);
    ROBO_CHECK_EQ_INT((int)context.id, (int)ROBO_LAYOUT_FC_ID_NONE);
    ROBO_CHECK_EQ_INT((int)context.kind, (int)ROBO_LAYOUT_FC_NONE);

    ROBO_CHECK_EQ_INT(robo_layout_tree_context(fixture.tree, ROBO_LAYOUT_FC_ID_NONE,
                                               &context, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_tree_context(NULL, 1u, &context, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_tree_context(fixture.tree, 1u, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_tree_root_context(fixture.tree, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_tree_root_context(NULL, &id, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* An index at the count is past the end, not the last one. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_context_at(fixture.tree, 4u, &context, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_tree_context_at(fixture.tree, 3u, &context, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_tree_context_at(fixture.tree, 0u, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_tree_context_at(NULL, 0u, &context, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* The box doors refuse an id that names nothing, and a NONE id, and a NULL
     * tree -- three answers rather than one crash. */
    ROBO_CHECK_EQ_INT(robo_layout_object_context(fixture.tree, ROBO_LAYOUT_NODE_ID_NONE, &id,
                                                 NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_context(fixture.tree, 99u, &id, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_object_context(NULL, fixture.root, &id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_context(fixture.tree, fixture.root, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_context_owned(fixture.tree, 99u, &id, NULL),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_object_context_owned(fixture.tree, ROBO_LAYOUT_NODE_ID_NONE,
                                                       &id, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* A box in a tree nobody has classified answers NOT_FOUND rather than NONE,
     * because NONE would read as "no context" for a box that will certainly have
     * one. */
    {
        layout_fc_fixture_t unclassified;

        layout_fc_begin(&unclassified, LAYOUT_FC_HTML, layout_fc_displays, LAYOUT_FC_PAIRS);
        ROBO_CHECK_EQ_INT(robo_layout_tree_is_classified(unclassified.tree), 0);
        ROBO_CHECK_EQ_INT(robo_layout_object_context(unclassified.tree, unclassified.root,
                                                     &id, NULL),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_layout_object_context_owned(unclassified.tree,
                                                           unclassified.root, &id, NULL),
                          ROBO_NOT_FOUND);
        ROBO_CHECK_EQ_INT(robo_layout_object_establishes_context(unclassified.tree,
                                                                 unclassified.root),
                          0);
        layout_fc_end(&unclassified);
    }

    /* A context cannot be created for an owner that names no box, and a non-root
     * context cannot be created without an enclosing one. Both are reached through
     * the tree's own door rather than through a caller -- the door is internal to
     * the module and the pass is its only caller -- so they are asserted through
     * the invariants a caller *can* see: every owner is a live box, and every
     * non-root context names a parent that exists. */
    {
        size_t index;

        for (index = 0u; index < robo_layout_tree_context_count(fixture.tree); index++) {
            robo_layout_formatting_context_t one;

            ROBO_CHECK_EQ_INT(robo_layout_tree_context_at(fixture.tree, index, &one, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_layout_object_is_live(fixture.tree, one.owner), 1);
            if (one.kind == ROBO_LAYOUT_FC_ROOT) {
                ROBO_CHECK_EQ_INT((int)one.parent, (int)ROBO_LAYOUT_FC_ID_NONE);
                ROBO_CHECK_EQ_INT(one.depth, 0u);
            } else {
                robo_layout_formatting_context_t parent;

                ROBO_CHECK(one.parent != ROBO_LAYOUT_FC_ID_NONE);
                ROBO_CHECK_EQ_INT(robo_layout_tree_context(fixture.tree, one.parent, &parent,
                                                           NULL),
                                  ROBO_OK);
                ROBO_CHECK_EQ_INT(one.depth, parent.depth + 1u);
            }
        }
    }

    layout_fc_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The allocator returns to its baseline                               */
/* ------------------------------------------------------------------ */

static void test_fc_allocator_balance(void)
{
    size_t before;
    int round;

    layout_fixture();
    before = layout_live_blocks();

    for (round = 0; round < 16; round++) {
        layout_fc_fixture_t fixture;
        robo_layout_context_t *context = NULL;
        int pass;

        layout_fc_begin(&fixture, LAYOUT_FC_HTML, layout_fc_displays, LAYOUT_FC_PAIRS);
        ROBO_CHECK_EQ_INT(robo_layout_context_create(&context, NULL), ROBO_OK);
        /* Eight passes, each releasing the previous classification and building a
         * new one -- so the arena is grown, released and rebuilt eight times, which
         * is the path a leak would hide in. */
        for (pass = 0; pass < 8; pass++) {
            ROBO_CHECK_EQ_INT(robo_layout_context_run(context, fixture.tree, NULL), ROBO_OK);
        }
        robo_layout_context_destroy(context);
        /* And an explicit invalidation, which is the other release path. */
        robo_layout_tree_invalidate_contexts(fixture.tree);
        layout_fc_end(&fixture);
    }

    ROBO_CHECK_EQ_INT(layout_live_blocks(), before);
}

/* ------------------------------------------------------------------ */

void robo_test_register_layout_formatting_context(void);

void robo_test_register_layout_formatting_context(void)
{
    robo_test_register("layout.fc.kinds", test_fc_kinds);
    robo_test_register("layout.fc.classify.establishes", test_fc_classify_establishes);
    robo_test_register("layout.fc.classify.display", test_fc_classify_display);
    robo_test_register("layout.fc.record", test_fc_record);
    robo_test_register("layout.fc.tree.root", test_fc_tree_root);
    robo_test_register("layout.fc.tree.transparent", test_fc_tree_transparent);
    robo_test_register("layout.fc.tree.inline_boxes", test_fc_tree_inline_boxes);
    robo_test_register("layout.fc.tree.display_none", test_fc_tree_display_none);
    robo_test_register("layout.fc.tree.root_variants", test_fc_tree_root_variants);
    robo_test_register("layout.fc.lifecycle", test_fc_lifecycle);
    robo_test_register("layout.fc.tree.add_invalidates", test_fc_tree_add_invalidates);
    robo_test_register("layout.fc.validation", test_fc_validation);
    robo_test_register("layout.fc.allocator_balance", test_fc_allocator_balance);
}
