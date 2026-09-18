/*
 * RoboLayout tree suite (Phase 10.1) — layout.tree.*
 *
 * The container and the one box it holds. Everything asserted here is a rule a
 * later goal would otherwise re-check while walking, so each test is written as
 * the question that rule answers rather than as a field comparison:
 *
 *   can a second root exist?              no, and the call says ALREADY_EXISTS
 *   can a `display: none` box exist?      no, and the call says so rather than
 *                                         silently filtering it later
 *   is a box's id dense?                  id == index + 1 until the tree clears
 *   does a miss look like a hit?          no: NOT_FOUND, and the output record is
 *                                         cleared first so a caller cannot read a
 *                                         half-filled copy
 *
 * The structural fixtures are built *by hand*, one add per box, rather than from
 * a document: this suite is about the container, and a builder that happened to
 * produce the right shape would make a container that could not hold one look
 * correct.
 */

#include "RoboLayoutTestSupport.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* A hand-built fixture                                                */
/* ------------------------------------------------------------------ */

/*
 * The shape every structural test below asserts:
 *
 *   html                 id 1, depth 0, the root
 *   `- body              id 2, depth 1
 *      |- section        id 3, depth 2
 *      |  `- p           id 4, depth 3
 *      `- div            id 5, depth 2
 *
 * Four levels, so depth is asserted somewhere other than 0 and 1, and two
 * siblings under `body` with a *grandchild* under the first, so first/last/prev
 * and next are each distinguishable from the only-child case.
 */
typedef struct layout_tree_fixture {
    layout_document_t doc;
    robo_layout_tree_t *tree;
    robo_html_node_handle_t html;
    robo_html_node_handle_t body;
    robo_html_node_handle_t section;
    robo_html_node_handle_t p;
    robo_html_node_handle_t div;
} layout_tree_fixture_t;

#define LAYOUT_TREE_FIXTURE_HTML \
    "<html>\n"                    \
    "  <body>\n"                  \
    "    <section>\n"             \
    "      <p>deep</p>\n"         \
    "    </section>\n"            \
    "    <div></div>\n"           \
    "  </body>\n"                 \
    "</html>\n"

static robo_result_t layout_tree_add(layout_tree_fixture_t *fixture,
                                     robo_layout_node_id_t parent,
                                     const robo_html_node_handle_t *element,
                                     const robo_css_computed_style_t *style,
                                     robo_layout_node_id_t *out)
{
    robo_layout_object_info_t info;

    robo_layout_object_info_init(&info);
    info.parent = parent;
    info.dom_node = *element;
    info.style = style;
    info.display = ROBO_CSS_DISPLAY_BLOCK;
    return robo_layout_tree_add_object(fixture->tree, &info, out, NULL);
}

static void layout_tree_fixture_begin(layout_tree_fixture_t *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    layout_document_build(&fixture->doc, LAYOUT_TREE_FIXTURE_HTML);
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&fixture->tree, NULL), ROBO_OK);
    fixture->html = layout_document_find(&fixture->doc, "html");
    fixture->body = layout_document_find(&fixture->doc, "body");
    fixture->section = layout_document_find(&fixture->doc, "section");
    fixture->p = layout_document_find(&fixture->doc, "p");
    fixture->div = layout_document_find(&fixture->doc, "div");
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&fixture->html), 1);
    ROBO_CHECK_EQ_INT(robo_html_node_handle_is_wellformed(&fixture->div), 1);
}

static void layout_tree_fixture_end(layout_tree_fixture_t *fixture)
{
    robo_layout_tree_destroy(fixture->tree);
    fixture->tree = NULL;
    layout_document_release(&fixture->doc);
}

/** Add all five boxes and check every add succeeded. */
static void layout_tree_fixture_build(layout_tree_fixture_t *fixture,
                                      robo_layout_node_id_t *out_ids)
{
    layout_tree_fixture_begin(fixture);
    ROBO_CHECK_EQ_INT(layout_tree_add(fixture, ROBO_LAYOUT_NODE_ID_NONE, &fixture->html,
                                      NULL, &out_ids[0]),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(layout_tree_add(fixture, out_ids[0], &fixture->body, NULL,
                                      &out_ids[1]),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(layout_tree_add(fixture, out_ids[1], &fixture->section, NULL,
                                      &out_ids[2]),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(layout_tree_add(fixture, out_ids[2], &fixture->p, NULL,
                                      &out_ids[3]),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(layout_tree_add(fixture, out_ids[1], &fixture->div, NULL,
                                      &out_ids[4]),
                      ROBO_OK);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void test_tree_lifecycle(void)
{
    robo_layout_tree_t *tree = NULL;
    layout_document_t doc;
    robo_layout_object_info_t info;
    robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;

    layout_fixture();

    ROBO_CHECK_EQ_INT(robo_layout_tree_create(NULL, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);
    ROBO_CHECK(tree != NULL);

    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(tree), 0);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_empty(tree), 1);
    /* Capacity is the arena's, not the tree's contents: a fresh tree need not be
     * a zero-capacity one. What must hold is the invariant the header states. */
    ROBO_CHECK(robo_layout_tree_capacity(tree) >= robo_layout_tree_object_count(tree));
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_root(tree), (int)ROBO_LAYOUT_NODE_ID_NONE);

    /* Every read door answers sanely before anything exists. */
    ROBO_CHECK(robo_layout_object(tree, (robo_layout_node_id_t)1u) == NULL);
    ROBO_CHECK_EQ_INT((int)robo_layout_object_kind(tree, (robo_layout_node_id_t)1u),
                      (int)ROBO_LAYOUT_OBJECT_NONE);
    ROBO_CHECK(robo_layout_object_style(tree, (robo_layout_node_id_t)1u) == NULL);
    ROBO_CHECK_EQ_INT(robo_layout_object_is_live(tree, (robo_layout_node_id_t)1u), 0);
    ROBO_CHECK_EQ_INT(robo_layout_object_is_root(tree, (robo_layout_node_id_t)1u), 0);
    ROBO_CHECK_EQ_INT(robo_layout_object_has_children(tree, (robo_layout_node_id_t)1u), 0);
    ROBO_CHECK_EQ_INT(robo_layout_object_flags(tree, (robo_layout_node_id_t)1u), 0);
    ROBO_CHECK(robo_layout_object_result_ref(tree, (robo_layout_node_id_t)1u) == NULL);

    /* An empty tree behaves like one; a NULL tree behaves like an empty one too,
     * because "no tree" and "no boxes" are the same answer to every read. */
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(NULL), 0);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_empty(NULL), 1);
    ROBO_CHECK_EQ_INT(robo_layout_object_is_live(NULL, (robo_layout_node_id_t)1u), 0);
    robo_layout_tree_clear(NULL);
    robo_layout_tree_destroy(NULL);

    /* An add on a NULL tree is refused without touching the output. */
    layout_document_build(&doc, "<html><body></body></html>");
    robo_layout_object_info_init(&info);
    info.dom_node = layout_document_element(&doc);
    info.display = ROBO_CSS_DISPLAY_BLOCK;
    id = (robo_layout_node_id_t)9u;
    ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(NULL, &info, &id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)id, (int)ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(tree, NULL, &id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(tree, &info, NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    robo_layout_tree_destroy(tree);
    layout_document_release(&doc);
}

/* ------------------------------------------------------------------ */
/* Adding the root                                                     */
/* ------------------------------------------------------------------ */

static void test_tree_add_root(void)
{
    layout_tree_fixture_t fixture;
    robo_layout_node_id_t root = ROBO_LAYOUT_NODE_ID_NONE;
    const robo_layout_object_t *object;
    robo_layout_node_id_t parent = (robo_layout_node_id_t)4u;
    uint32_t depth = 99u;
    size_t children = 99u;

    layout_fixture();
    layout_tree_fixture_begin(&fixture);
    ROBO_CHECK_EQ_INT(layout_tree_add(&fixture, ROBO_LAYOUT_NODE_ID_NONE, &fixture.html,
                                      NULL, &root),
                      ROBO_OK);

    /* Ids are 1-based and dense: the root is 1, and it is the tree's root. */
    ROBO_CHECK_EQ_INT((int)root, 1);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 1);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_empty(fixture.tree), 0);
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_root(fixture.tree), 1);
    ROBO_CHECK(robo_layout_tree_capacity(fixture.tree) >= 1u);

    object = robo_layout_object(fixture.tree, root);
    ROBO_CHECK(object != NULL);
    if (object) {
        ROBO_CHECK_EQ_INT((int)object->kind, (int)ROBO_LAYOUT_OBJECT_BOX);
        ROBO_CHECK_EQ_INT((int)object->parent, (int)ROBO_LAYOUT_NODE_ID_NONE);
        ROBO_CHECK_EQ_INT(object->child_count, 0u);
        ROBO_CHECK_EQ_INT(object->depth, 0u);
        /* A NULL style is a fact, not an accident, and it is *stated*. */
        ROBO_CHECK_EQ_INT(object->flags, ROBO_LAYOUT_OBJECT_FLAG_STYLE_MISSING);
        ROBO_CHECK(object->style == NULL);
        /* No pass has spoken about this box yet, so its result is NONE rather
         * than UNRESOLVED: "the tree was built" and "a pass saw it" stay two
         * different facts. */
        ROBO_CHECK_EQ_INT((int)object->result.state, (int)ROBO_LAYOUT_RESULT_NONE);
        /* The element is the one the DOM was asked about, by handle equality. */
        ROBO_CHECK_EQ_INT(robo_html_node_handle_equal(&object->dom_node, &fixture.html), 1);
    }

    ROBO_CHECK_EQ_INT(robo_layout_object_is_root(fixture.tree, root), 1);
    ROBO_CHECK_EQ_INT(robo_layout_object_is_live(fixture.tree, root), 1);
    ROBO_CHECK_EQ_INT(robo_layout_object_has_children(fixture.tree, root), 0);
    ROBO_CHECK_EQ_INT((int)robo_layout_object_kind(fixture.tree, root),
                      (int)ROBO_LAYOUT_OBJECT_BOX);
    ROBO_CHECK_EQ_INT(robo_layout_object_depth(fixture.tree, root, &depth), ROBO_OK);
    ROBO_CHECK_EQ_INT(depth, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_object_child_count(fixture.tree, root, &children), ROBO_OK);
    ROBO_CHECK_EQ_INT(children, 0u);

    /* The root has no parent, and that is a *miss* rather than a NONE answer --
     * the caller distinguishes it with robo_layout_object_is_root. */
    ROBO_CHECK_EQ_INT(robo_layout_object_parent(fixture.tree, root, &parent), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_object_first_child(fixture.tree, root, &parent),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_object_last_child(fixture.tree, root, &parent),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_object_next_sibling(fixture.tree, root, &parent),
                      ROBO_NOT_FOUND);

    layout_tree_fixture_end(&fixture);
}

static void test_tree_add_second_root_refused(void)
{
    layout_tree_fixture_t fixture;
    robo_layout_node_id_t ids[2];
    robo_layout_node_id_t id = (robo_layout_node_id_t)7u;

    layout_fixture();
    layout_tree_fixture_begin(&fixture);
    ROBO_CHECK_EQ_INT(layout_tree_add(&fixture, ROBO_LAYOUT_NODE_ID_NONE, &fixture.html,
                                      NULL, &ids[0]),
                      ROBO_OK);
    /* A tree has one root. A second is refused rather than re-parented, because
     * "which one is the root" would otherwise depend on insertion order. */
    ROBO_CHECK_EQ_INT(layout_tree_add(&fixture, ROBO_LAYOUT_NODE_ID_NONE, &fixture.body,
                                      NULL, &id),
                      ROBO_ALREADY_EXISTS);
    ROBO_CHECK_EQ_INT((int)id, (int)ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 1);

    /* And an id that names nothing is NOT_FOUND, distinct from the refused root. */
    id = (robo_layout_node_id_t)7u;
    ROBO_CHECK_EQ_INT(layout_tree_add(&fixture, (robo_layout_node_id_t)42u, &fixture.body,
                                      NULL, &id),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)id, (int)ROBO_LAYOUT_NODE_ID_NONE);

    layout_tree_fixture_end(&fixture);
}

static void test_tree_add_validation(void)
{
    layout_tree_fixture_t fixture;
    robo_layout_object_info_t info;
    robo_layout_node_id_t id = (robo_layout_node_id_t)1u;
    robo_html_node_handle_t unformed;

    layout_fixture();
    layout_tree_fixture_begin(&fixture);

    /* An element handle that is not well formed: a box always came from
     * something the DOM can be asked about. */
    robo_html_node_handle_clear(&unformed);
    robo_layout_object_info_init(&info);
    info.dom_node = unformed;
    info.display = ROBO_CSS_DISPLAY_BLOCK;
    ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(fixture.tree, &info, &id, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* `display: none` generates no box. Accepting one would make this tree a
     * copy of the DOM's element list, and every pass would re-filter what the
     * container had already failed to. */
    robo_layout_object_info_init(&info);
    info.dom_node = fixture.html;
    info.display = ROBO_CSS_DISPLAY_NONE;
    ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(fixture.tree, &info, &id, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 0);

    /* An undefined flag bit is refused rather than stored, so a typo cannot
     * travel through the pipeline looking like a laid-out fact. */
    info.display = ROBO_CSS_DISPLAY_BLOCK;
    info.flags = 0x80000000u;
    ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(fixture.tree, &info, &id, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* The defined bit is accepted and travels. */
    info.flags = ROBO_LAYOUT_OBJECT_FLAG_STYLE_MISSING;
    ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(fixture.tree, &info, &id, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_object_flags(fixture.tree, id),
                      ROBO_LAYOUT_OBJECT_FLAG_STYLE_MISSING);
    ROBO_CHECK_EQ_INT(robo_layout_object_has_flag(fixture.tree, id,
                                                  ROBO_LAYOUT_OBJECT_FLAG_STYLE_MISSING),
                      1);

    /* Every display that generates a box is accepted, and none of them arrive as
     * a false STYLE_MISSING. */
    {
        robo_css_display_t displays[] = { ROBO_CSS_DISPLAY_BLOCK, ROBO_CSS_DISPLAY_INLINE,
                                          ROBO_CSS_DISPLAY_INLINE_BLOCK };
        size_t i;

        robo_layout_tree_clear(fixture.tree);
        for (i = 0u; i < sizeof(displays) / sizeof(displays[0]); i++) {
            robo_layout_object_info_init(&info);
            info.parent = (i == 0u) ? ROBO_LAYOUT_NODE_ID_NONE : (robo_layout_node_id_t)i;
            info.dom_node = (i == 0u) ? fixture.html
                                      : ((i == 1u) ? fixture.body : fixture.section);
            info.display = displays[i];
            ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(fixture.tree, &info, &id, NULL),
                              ROBO_OK);
            ROBO_CHECK_EQ_INT(robo_layout_object_display(fixture.tree, id),
                              (int)displays[i]);
        }
    }

    layout_tree_fixture_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Relationships                                                       */
/* ------------------------------------------------------------------ */

static void test_tree_relationships(void)
{
    layout_tree_fixture_t fixture;
    robo_layout_node_id_t ids[5];
    robo_layout_node_id_t out = ROBO_LAYOUT_NODE_ID_NONE;
    size_t count = 0u;
    uint32_t depth = 0u;
    size_t i;

    layout_fixture();
    layout_tree_fixture_build(&fixture, ids);

    /* Dense and in creation order -- the property every walk in the module and
     * in the suites relies on. */
    for (i = 0u; i < 5u; i++) {
        robo_layout_node_id_t at = ROBO_LAYOUT_NODE_ID_NONE;

        ROBO_CHECK_EQ_INT(ids[i], (robo_layout_node_id_t)(i + 1u));
        ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture.tree, i, &at), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)at, (int)ids[i]);
    }
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 5u);

    /* html: one child. */
    ROBO_CHECK_EQ_INT(robo_layout_object_first_child(fixture.tree, ids[0], &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out, (int)ids[1]);
    ROBO_CHECK_EQ_INT(robo_layout_object_last_child(fixture.tree, ids[0], &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out, (int)ids[1]);
    ROBO_CHECK_EQ_INT(robo_layout_object_child_count(fixture.tree, ids[0], &count), ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 1u);
    ROBO_CHECK_EQ_INT(robo_layout_object_has_children(fixture.tree, ids[0]), 1);

    /* body: two children, so first and last differ and prev/next are real. */
    ROBO_CHECK_EQ_INT(robo_layout_object_first_child(fixture.tree, ids[1], &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out, (int)ids[2]);
    ROBO_CHECK_EQ_INT(robo_layout_object_last_child(fixture.tree, ids[1], &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out, (int)ids[4]);
    ROBO_CHECK_EQ_INT(robo_layout_object_child_count(fixture.tree, ids[1], &count), ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 2u);

    ROBO_CHECK_EQ_INT(robo_layout_object_next_sibling(fixture.tree, ids[2], &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out, (int)ids[4]);
    ROBO_CHECK_EQ_INT(robo_layout_object_previous_sibling(fixture.tree, ids[4], &out),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out, (int)ids[2]);

    /* The first child has no previous sibling and the last has no next: a miss,
     * not a NONE answer. */
    ROBO_CHECK_EQ_INT(robo_layout_object_previous_sibling(fixture.tree, ids[2], &out),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_object_next_sibling(fixture.tree, ids[4], &out),
                      ROBO_NOT_FOUND);

    /* `p` is a box under `section`, *not* under `body`: the grandchild depth is
     * what separates "linked to the right parent" from "linked to the nearest
     * one". */
    ROBO_CHECK_EQ_INT(robo_layout_object_parent(fixture.tree, ids[3], &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out, (int)ids[2]);
    ROBO_CHECK_EQ_INT(robo_layout_object_depth(fixture.tree, ids[3], &depth), ROBO_OK);
    ROBO_CHECK_EQ_INT(depth, 3u);

    /* Depth is 0 at the root and increments by exactly one per level. */
    for (i = 0u; i < 4u; i++) {
        ROBO_CHECK_EQ_INT(robo_layout_object_depth(fixture.tree, ids[i], &depth), ROBO_OK);
        ROBO_CHECK_EQ_INT(depth, (uint32_t)i);
    }
    ROBO_CHECK_EQ_INT(robo_layout_object_is_root(fixture.tree, ids[0]), 1);
    ROBO_CHECK_EQ_INT(robo_layout_object_is_root(fixture.tree, ids[1]), 0);

    /* A leaf has no children and is nobody's parent. */
    ROBO_CHECK_EQ_INT(robo_layout_object_child_count(fixture.tree, ids[3], &count), ROBO_OK);
    ROBO_CHECK_EQ_INT(count, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_object_has_children(fixture.tree, ids[3]), 0);
    ROBO_CHECK_EQ_INT(robo_layout_object_first_child(fixture.tree, ids[3], &out),
                      ROBO_NOT_FOUND);

    /* And both children of `body` agree about the link, from either end. */
    ROBO_CHECK_EQ_INT(robo_layout_object_parent(fixture.tree, ids[4], &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out, (int)ids[1]);
    ROBO_CHECK_EQ_INT(robo_layout_object_parent(fixture.tree, ids[2], &out), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)out, (int)ids[1]);

    layout_tree_fixture_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Misses                                                              */
/* ------------------------------------------------------------------ */

static void test_tree_misses(void)
{
    layout_tree_fixture_t fixture;
    robo_layout_node_id_t ids[5];
    robo_layout_node_id_t id = (robo_layout_node_id_t)99u;
    robo_layout_node_id_t out = (robo_layout_node_id_t)88u;
    robo_layout_object_t object;
    robo_layout_result_t result = ROBO_LAYOUT_CLEARED;
    robo_html_node_handle_t handle;
    robo_layout_diag_t diag = ROBO_LAYOUT_CLEARED;
    size_t count = 7u;
    uint32_t depth = 7u;

    layout_fixture();
    layout_tree_fixture_build(&fixture, ids);

    /* An id past the end names nothing, and the answer is a reported miss rather
     * than a read of whatever now occupies that slot. */
    memset(&object, 0xAB, sizeof(object));
    ROBO_CHECK_EQ_INT(robo_layout_object_get(fixture.tree, id, &object, &diag), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_NOT_FOUND);
    /* Out is cleared first, so a failure never leaves a half-filled copy. */
    ROBO_CHECK_EQ_INT(object.id, 0u);
    ROBO_CHECK_EQ_INT(object.kind, ROBO_LAYOUT_OBJECT_NONE);

    /* A NONE id is a caller bug rather than a miss. */
    ROBO_CHECK_EQ_INT(robo_layout_object_get(fixture.tree, ROBO_LAYOUT_NODE_ID_NONE,
                                             &object, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_get(NULL, ids[0], &object, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_get(fixture.tree, ids[0], NULL, NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Every navigation door joins the two regimes. */
    ROBO_CHECK_EQ_INT(robo_layout_object_dom_node(fixture.tree, id, &handle), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_object_dom_node(fixture.tree, ROBO_LAYOUT_NODE_ID_NONE,
                                                  &handle),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_dom_node(NULL, ids[0], &handle),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_dom_node(fixture.tree, ids[0], NULL),
                      ROBO_INVALID_ARGUMENT);

    /* Each navigation door takes its own output, so a miss on one cannot be
     * confused with a hit on the next: a door that cleared its caller's *id* into
     * no box would make every assert after it a test of the wrong thing. */
    ROBO_CHECK_EQ_INT(robo_layout_object_parent(fixture.tree, id, &out), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)out, (int)ROBO_LAYOUT_NODE_ID_NONE);
    /* A miss *clears* the output rather than leaving it, so a caller that ignored
     * the result code cannot read whatever its variable happened to hold as if it
     * were a fact. */
    ROBO_CHECK_EQ_INT(robo_layout_object_child_count(fixture.tree, id, &count), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(count, 0u);
    ROBO_CHECK_EQ_INT(robo_layout_object_depth(fixture.tree, id, &depth), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(depth, 0u);

    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, id, &result), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, ids[0], &result), ROBO_OK);

    /* The id_at door is the walk's own, so its two failures are the two a walk
     * can make. */
    ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture.tree, 5u, &id), ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(robo_layout_object_id_at(fixture.tree, 0u, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_object_id_at(NULL, 0u, &id), ROBO_INVALID_ARGUMENT);

    layout_tree_fixture_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Reverse lookup                                                      */
/* ------------------------------------------------------------------ */

static void test_tree_reverse_lookup(void)
{
    layout_tree_fixture_t fixture;
    robo_layout_node_id_t ids[5];
    robo_html_node_handle_t unformed;

    layout_fixture();
    layout_tree_fixture_build(&fixture, ids);

    /* Every box is reachable from its element, and the box that comes back is
     * the one built for it rather than the first box in the tree. */
    ROBO_CHECK_EQ_INT((int)robo_layout_object_id_for_dom_node(fixture.tree, &fixture.html),
                      (int)ids[0]);
    ROBO_CHECK_EQ_INT((int)robo_layout_object_id_for_dom_node(fixture.tree, &fixture.body),
                      (int)ids[1]);
    ROBO_CHECK_EQ_INT((int)robo_layout_object_id_for_dom_node(fixture.tree, &fixture.section),
                      (int)ids[2]);
    ROBO_CHECK_EQ_INT((int)robo_layout_object_id_for_dom_node(fixture.tree, &fixture.p),
                      (int)ids[3]);
    ROBO_CHECK_EQ_INT((int)robo_layout_object_id_for_dom_node(fixture.tree, &fixture.div),
                      (int)ids[4]);

    /* An element no box was built for answers NONE -- which is also what a
     * `display: none` element answers, which is why the builder's report is the
     * door that tells the two apart (RoboLayoutBuilder.h). */
    robo_html_node_handle_clear(&unformed);
    ROBO_CHECK_EQ_INT((int)robo_layout_object_id_for_dom_node(fixture.tree, &unformed),
                      (int)ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT((int)robo_layout_object_id_for_dom_node(NULL, &fixture.html),
                      (int)ROBO_LAYOUT_NODE_ID_NONE);
    ROBO_CHECK_EQ_INT((int)robo_layout_object_id_for_dom_node(fixture.tree, NULL),
                      (int)ROBO_LAYOUT_NODE_ID_NONE);

    layout_tree_fixture_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Clearing and rebuilding                                             */
/* ------------------------------------------------------------------ */

static void test_tree_clear(void)
{
    layout_tree_fixture_t fixture;
    robo_layout_node_id_t ids[5];
    robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;

    layout_fixture();
    layout_tree_fixture_build(&fixture, ids);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 5u);

    robo_layout_tree_clear(fixture.tree);
    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), 0u);
    ROBO_CHECK_EQ_INT(robo_layout_tree_is_empty(fixture.tree), 1);
    ROBO_CHECK_EQ_INT((int)robo_layout_tree_root(fixture.tree), (int)ROBO_LAYOUT_NODE_ID_NONE);
    /* The old ids name nothing now: a stale id must not alias whatever occupies
     * the slot next. */
    ROBO_CHECK_EQ_INT(robo_layout_object_is_live(fixture.tree, ids[0]), 0);
    ROBO_CHECK(robo_layout_object(fixture.tree, ids[0]) == NULL);
    ROBO_CHECK_EQ_INT(robo_layout_object_kind(fixture.tree, ids[4]),
                      ROBO_LAYOUT_OBJECT_NONE);

    /* Ids restart at 1, which is what makes a rebuild produce the same tree
     * rather than a tree that grows every time. */
    ROBO_CHECK_EQ_INT(layout_tree_add(&fixture, ROBO_LAYOUT_NODE_ID_NONE, &fixture.html,
                                      NULL, &id),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)id, 1);
    ROBO_CHECK_EQ_INT(robo_layout_object_is_live(fixture.tree, id), 1);

    layout_tree_fixture_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Borrowed styles                                                     */
/* ------------------------------------------------------------------ */

static void test_tree_object_style(void)
{
    layout_tree_fixture_t fixture;
    layout_fake_source_t fake;
    robo_layout_node_id_t with_style = ROBO_LAYOUT_NODE_ID_NONE;
    robo_layout_node_id_t without_style = ROBO_LAYOUT_NODE_ID_NONE;
    robo_layout_object_info_t info;
    robo_layout_object_t object;

    layout_fixture();
    layout_tree_fixture_begin(&fixture);
    layout_fake_init(&fake);
    layout_fake_add(&fake, "html", ROBO_CSS_DISPLAY_BLOCK);

    ROBO_CHECK_EQ_INT(fake.count, 1u);

    /* A box built *with* a style returns that exact pointer -- not a copy, which
     * is what makes "the tree borrows the styles" checkable rather than a claim
     * in a comment. */
    robo_layout_object_info_init(&info);
    info.dom_node = fixture.html;
    info.style = fake.rules[0].style;
    info.display = ROBO_CSS_DISPLAY_BLOCK;
    ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(fixture.tree, &info, &with_style, NULL),
                      ROBO_OK);
    ROBO_CHECK(fake.rules[0].style == robo_layout_object_style(fixture.tree, with_style));
    ROBO_CHECK_EQ_INT(robo_layout_object_has_flag(fixture.tree, with_style,
                                                  ROBO_LAYOUT_OBJECT_FLAG_STYLE_MISSING),
                      0);
    ROBO_CHECK_EQ_INT(robo_layout_object_flags(fixture.tree, with_style), 0u);
    /* And the element it is for comes back by handle equality. */
    {
        robo_html_node_handle_t handle;

        ROBO_CHECK_EQ_INT(robo_layout_object_dom_node(fixture.tree, with_style, &handle),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_html_node_handle_equal(&handle, &fixture.html), 1);
    }

    /* A box built with no style keeps a NULL style and the flag, because the
     * absence is a property of the build rather than an accident. */
    robo_layout_object_info_init(&info);
    info.parent = with_style;
    info.dom_node = fixture.body;
    info.display = ROBO_CSS_DISPLAY_BLOCK;
    ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(fixture.tree, &info, &without_style, NULL),
                      ROBO_OK);
    ROBO_CHECK(robo_layout_object_style(fixture.tree, without_style) == NULL);
    ROBO_CHECK_EQ_INT(robo_layout_object_has_flag(fixture.tree, without_style,
                                                  ROBO_LAYOUT_OBJECT_FLAG_STYLE_MISSING),
                      1);
    /* The read side agrees with the doors: a box nothing styled carries a NULL
     * style and *states* it with the flag rather than leaving a formatting context
     * to infer it from the pointer. */
    ROBO_CHECK_EQ_INT(robo_layout_object_get(fixture.tree, without_style, &object, NULL),
                      ROBO_OK);
    ROBO_CHECK(object.style == NULL);
    ROBO_CHECK_EQ_INT(object.flags, ROBO_LAYOUT_OBJECT_FLAG_STYLE_MISSING);
    ROBO_CHECK_EQ_INT(object.child_count, 0u);
    /* And the parent's side of the link, so the box is where the add put it. */
    ROBO_CHECK_EQ_INT((int)object.parent, (int)with_style);
    ROBO_CHECK_EQ_INT(robo_layout_object_get(fixture.tree, with_style, &object, NULL),
                      ROBO_OK);
    ROBO_CHECK(object.style != NULL);
    ROBO_CHECK_EQ_INT(object.flags, 0u);
    ROBO_CHECK_EQ_INT(object.child_count, 1u);
    ROBO_CHECK_EQ_INT((int)object.first_child, (int)without_style);

    layout_fake_dispose(&fake);
    layout_tree_fixture_end(&fixture);
}

static void test_tree_object_display(void)
{
    layout_tree_fixture_t fixture;
    robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;
    robo_layout_object_info_t info;

    layout_fixture();
    layout_tree_fixture_begin(&fixture);

    /* The display is *copied*, and it is the one CSS value that is: the box
     * would not exist without it, and the first question a formatting context
     * asks is what kind of box it has. */
    robo_layout_object_info_init(&info);
    info.dom_node = fixture.html;
    info.display = ROBO_CSS_DISPLAY_INLINE_BLOCK;
    ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(fixture.tree, &info, &id, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_layout_object_display(fixture.tree, id),
                      (int)ROBO_CSS_DISPLAY_INLINE_BLOCK);
    {
        const robo_layout_object_t *object = robo_layout_object(fixture.tree, id);

        ROBO_CHECK(object != NULL);
        if (object) {
            ROBO_CHECK_EQ_INT((int)object->display, (int)ROBO_CSS_DISPLAY_INLINE_BLOCK);
        }
    }
    /* A miss answers the display of no box. */
    ROBO_CHECK_EQ_INT((int)robo_layout_object_display(fixture.tree,
                                                      (robo_layout_node_id_t)44u),
                      (int)ROBO_CSS_DISPLAY_NONE);

    layout_tree_fixture_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Growth                                                              */
/* ------------------------------------------------------------------ */

static void test_tree_growth(void)
{
    layout_tree_fixture_t fixture;
    robo_layout_node_id_t root = ROBO_LAYOUT_NODE_ID_NONE;
    robo_layout_object_info_t info;
    size_t initial_capacity;
    size_t i;
    const size_t wanted = 64u;

    layout_fixture();
    layout_tree_fixture_begin(&fixture);

    robo_layout_object_info_init(&info);
    info.dom_node = fixture.html;
    info.display = ROBO_CSS_DISPLAY_BLOCK;
    ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(fixture.tree, &info, &root, NULL), ROBO_OK);
    initial_capacity = robo_layout_tree_capacity(fixture.tree);

    /*
     * A *chain* rather than a fan, so every add after the first must resolve a
     * parent that the previous add created, and the depth of the last box is the
     * number of adds. The ids are dense from 1, so the box at index `i + 1` has id
     * `i + 2` -- which makes the parent of the i-th add the id the *previous* add
     * returned. A tree that lost a link when the arena moved fails at the growth
     * boundary, not at the first add.
     */
    for (i = 0u; i < wanted; i++) {
        robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;
        robo_layout_node_id_t parent_of_this = (i == 0u) ? root
                                                          : (robo_layout_node_id_t)(i + 1u);

        robo_layout_object_info_init(&info);
        info.parent = parent_of_this;
        info.dom_node = fixture.body;
        info.display = ROBO_CSS_DISPLAY_BLOCK;
        ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(fixture.tree, &info, &id, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)id, (int)(i + 2u));
    }

    ROBO_CHECK_EQ_INT(robo_layout_tree_object_count(fixture.tree), wanted + 1u);
    ROBO_CHECK(robo_layout_tree_capacity(fixture.tree) > initial_capacity);
    ROBO_CHECK(robo_layout_tree_capacity(fixture.tree) >=
               robo_layout_tree_object_count(fixture.tree));

    /* Every link survived the growth: the arena moved, the ids did not. */
    {
        uint32_t depth = 0u;

        /* The last box's depth is the number of links above it, which is the
         * number of adds: a chain that was broken anywhere would be shorter. */
        ROBO_CHECK_EQ_INT(robo_layout_object_depth(fixture.tree,
                                                   (robo_layout_node_id_t)(wanted + 1u),
                                                   &depth),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(depth, (uint32_t)wanted);
        ROBO_CHECK_EQ_INT(robo_layout_object_is_root(fixture.tree, root), 1);
    }

    /* And the whole chain is walkable from the root, one child per level. */
    {
        robo_layout_node_id_t walker = root;
        size_t steps = 0u;

        for (;;) {
            robo_layout_node_id_t next = ROBO_LAYOUT_NODE_ID_NONE;
            uint32_t depth = 0u;

            ROBO_CHECK_EQ_INT(robo_layout_object_depth(fixture.tree, walker, &depth), ROBO_OK);
            ROBO_CHECK_EQ_INT(depth, (uint32_t)steps);
            if (robo_layout_object_first_child(fixture.tree, walker, &next) != ROBO_OK) {
                break;
            }
            walker = next;
            steps++;
        }
        ROBO_CHECK_EQ_INT(steps, wanted);
    }

    layout_tree_fixture_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The borrowed-pointer contract                                       */
/* ------------------------------------------------------------------ */

static void test_tree_borrowed_pointer(void)
{
    layout_tree_fixture_t fixture;
    robo_layout_node_id_t ids[5];
    const robo_layout_object_t *borrowed;

    layout_fixture();
    layout_tree_fixture_build(&fixture, ids);

    /* The whole struct in one call, valid until the next add -- which is why the
     * supported way to hold on to a box is its id. This test asserts the contract
     * that a caller can rely on: *before* an add, the pointer is the object. */
    borrowed = robo_layout_object(fixture.tree, ids[2]);
    ROBO_CHECK(borrowed != NULL);
    if (borrowed) {
        ROBO_CHECK_EQ_INT((int)borrowed->id, (int)ids[2]);
        ROBO_CHECK_EQ_INT((int)borrowed->parent, (int)ids[1]);
        ROBO_CHECK_EQ_INT((int)borrowed->first_child, (int)ids[3]);
        /* `<section>`'s only child is `<p>`, and it is also the last child. */
        ROBO_CHECK_EQ_INT((int)borrowed->last_child, (int)ids[3]);
        ROBO_CHECK_EQ_INT((int)borrowed->next_sibling, (int)ids[4]);
        ROBO_CHECK_EQ_INT((int)borrowed->prev_sibling, (int)ROBO_LAYOUT_NODE_ID_NONE);
        ROBO_CHECK_EQ_INT(borrowed->child_count, 1u);
        ROBO_CHECK_EQ_INT(borrowed->depth, 2u);
    }

    /* The read side is the same data by value, which is the door that survives a
     * growth. */
    {
        robo_layout_object_t copy;

        ROBO_CHECK_EQ_INT(robo_layout_object_get(fixture.tree, ids[2], &copy, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)copy.id, (int)ids[2]);
        ROBO_CHECK_EQ_INT(robo_layout_object_get(fixture.tree, ids[3], &copy, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)copy.parent, (int)ids[2]);
        ROBO_CHECK_EQ_INT(copy.child_count, 0u);
        ROBO_CHECK_EQ_INT(copy.depth, 3u);
    }

    layout_tree_fixture_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Results before a pass                                               */
/* ------------------------------------------------------------------ */

static void test_tree_result_before_pass(void)
{
    layout_tree_fixture_t fixture;
    robo_layout_node_id_t ids[5];
    robo_layout_result_t result;
    const robo_layout_result_t *ref;
    size_t i;

    layout_fixture();
    layout_tree_fixture_build(&fixture, ids);

    for (i = 0u; i < 5u; i++) {
        /* A box that exists always has a result *record*; what varies is its
         * state. Building a tree is not a pass, so the state is NONE and the
         * geometry is zeroed rather than meaningful. */
        ROBO_CHECK_EQ_INT(robo_layout_object_result(fixture.tree, ids[i], &result), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)result.state, (int)ROBO_LAYOUT_RESULT_NONE);
        ROBO_CHECK_EQ_INT(result.flags, 0u);
        ROBO_CHECK_EQ_INT(result.position.x, 0);
        ROBO_CHECK_EQ_INT(result.position.y, 0);
        ROBO_CHECK_EQ_INT(result.size.width, 0);
        ROBO_CHECK_EQ_INT(result.size.height, 0);
        ROBO_CHECK_EQ_INT(robo_layout_result_is_computed(&result), 0);

        ref = robo_layout_object_result_ref(fixture.tree, ids[i]);
        ROBO_CHECK(ref != NULL);
        if (ref) {
            ROBO_CHECK_EQ_INT((int)ref->state, (int)ROBO_LAYOUT_RESULT_NONE);
        }
        /* And reading geometry that nobody computed is a miss, not a zero. */
        {
            robo_layout_point_t position;
            robo_layout_size_t size;
            robo_layout_rect_t rect;

            ROBO_CHECK_EQ_INT(robo_layout_result_position(&result, &position), ROBO_NOT_FOUND);
            ROBO_CHECK_EQ_INT(robo_layout_result_size(&result, &size), ROBO_NOT_FOUND);
            ROBO_CHECK_EQ_INT(robo_layout_result_rect(&result, &rect, NULL), ROBO_NOT_FOUND);
        }
    }

    layout_tree_fixture_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* The allocator returns to its baseline                               */
/* ------------------------------------------------------------------ */

static void test_tree_allocator_balance(void)
{
    size_t before;
    layout_document_t doc;
    robo_layout_tree_t *tree = NULL;
    robo_html_node_handle_t html;
    robo_layout_object_info_t info;
    int pass;

    layout_fixture();
    before = layout_live_blocks();
    layout_document_build(&doc, LAYOUT_TREE_FIXTURE_HTML);
    html = layout_document_element(&doc);

    for (pass = 0; pass < 8; pass++) {
        size_t i;

        ROBO_CHECK_EQ_INT(robo_layout_tree_create(&tree, NULL), ROBO_OK);
        for (i = 0u; i < 32u; i++) {
            robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;

            robo_layout_object_info_init(&info);
            info.parent = (i == 0u) ? ROBO_LAYOUT_NODE_ID_NONE : (robo_layout_node_id_t)i;
            info.dom_node = html;
            info.display = ROBO_CSS_DISPLAY_BLOCK;
            ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(tree, &info, &id, NULL), ROBO_OK);
        }
        /* A failed add must not leak either: it is the path a suite hits least. */
        robo_layout_object_info_init(&info);
        info.parent = (robo_layout_node_id_t)999u;
        info.dom_node = html;
        info.display = ROBO_CSS_DISPLAY_BLOCK;
        {
            robo_layout_node_id_t id = ROBO_LAYOUT_NODE_ID_NONE;

            ROBO_CHECK_EQ_INT(robo_layout_tree_add_object(tree, &info, &id, NULL),
                              ROBO_NOT_FOUND);
        }
        robo_layout_tree_destroy(tree);
        tree = NULL;
    }

    layout_document_release(&doc);
    ROBO_CHECK_EQ_INT(layout_live_blocks(), before);
}

/* ------------------------------------------------------------------ */

void robo_test_register_layout_tree(void);

void robo_test_register_layout_tree(void)
{
    robo_test_register("layout.tree.lifecycle", test_tree_lifecycle);
    robo_test_register("layout.tree.add.root", test_tree_add_root);
    robo_test_register("layout.tree.add.second_root", test_tree_add_second_root_refused);
    robo_test_register("layout.tree.add.validation", test_tree_add_validation);
    robo_test_register("layout.tree.relationships", test_tree_relationships);
    robo_test_register("layout.tree.misses", test_tree_misses);
    robo_test_register("layout.tree.reverse_lookup", test_tree_reverse_lookup);
    robo_test_register("layout.tree.clear", test_tree_clear);
    robo_test_register("layout.tree.object.style", test_tree_object_style);
    robo_test_register("layout.tree.object.display", test_tree_object_display);
    robo_test_register("layout.tree.growth", test_tree_growth);
    robo_test_register("layout.tree.borrowed_pointer", test_tree_borrowed_pointer);
    robo_test_register("layout.tree.result.before_pass", test_tree_result_before_pass);
    robo_test_register("layout.tree.allocator_balance", test_tree_allocator_balance);
}
