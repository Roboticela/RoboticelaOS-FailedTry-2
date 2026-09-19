/*
 * RoboLayout module surface suite (Phase 10.1) — layout.module.*
 *
 * What this build *is*, asserted rather than described. Everything here is a
 * closed vocabulary, a version, a capability table or a type identity, and each
 * one has the same failure mode: it changes and nothing that uses it notices.
 *
 * The two capabilities that are 0 are the phase's whole point, so they are
 * asserted twice from two independent directions -- the struct field and the
 * function that reads the macro -- because a build that flipped one and not the
 * other would have two answers to one question.
 *
 * Since 10.10 the capability table has grown one row per phase, and each row is
 * checked *positionally* against the struct beside it: the table is written out by
 * hand in the struct's own order, so a phase that added a field and forgot a row
 * moves every field after it and fails here rather than at a caller.
 *
 * The geometry test is not decoration either. RoboLayout declares its point,
 * size and rectangle as *aliases* of RoboGraphics', and an alias is exactly the
 * kind of claim that survives a copy-paste: if somebody ever replaced the typedef
 * with a struct, every suite in this directory would still compile and the only
 * symptom would be a conversion at the paint boundary a year later. Sizes and
 * assignment compatibility are what make it a test.
 */

#include "RoboLayoutTestSupport.h"

#include "RoboGraphics/RoboGraphicsGeometry.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Version                                                             */
/* ------------------------------------------------------------------ */

static void test_module_version(void)
{
    robo_i32 major = -1;
    robo_i32 minor = -1;
    robo_i32 patch = -1;

    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_VERSION_MAJOR, 0);
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_VERSION_MINOR, 12);
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_VERSION_PATCH, 0);
    ROBO_CHECK_STR_EQ(ROBO_LAYOUT_VERSION_STRING, "0.12.0");
    ROBO_CHECK_STR_EQ(robo_layout_version_string(), "0.12.0");

    ROBO_CHECK_EQ_INT(robo_layout_get_version(&major, &minor, &patch), ROBO_OK);
    ROBO_CHECK_EQ_INT(major, ROBO_LAYOUT_VERSION_MAJOR);
    ROBO_CHECK_EQ_INT(minor, ROBO_LAYOUT_VERSION_MINOR);
    ROBO_CHECK_EQ_INT(patch, ROBO_LAYOUT_VERSION_PATCH);

    /* Not frozen, and honestly so: Phase 10 keeps growing this surface. */
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_API_FROZEN, 0);

    ROBO_CHECK_EQ_INT(robo_layout_get_version(NULL, &minor, &patch),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_get_version(&major, NULL, &patch),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_get_version(&major, &minor, NULL),
                      ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void test_module_lifecycle(void)
{
    layout_fixture();
    ROBO_CHECK_EQ_INT(robo_layout_is_initialized(), 1);
    /* Already up: the fixture is idempotent and this is the fact it relies on. */
    ROBO_CHECK_EQ_INT(robo_layout_init(), ROBO_ALREADY_EXISTS);

    robo_layout_shutdown();
    ROBO_CHECK_EQ_INT(robo_layout_is_initialized(), 0);
    /* Safe when already down: a module that owns nothing has nothing to refuse. */
    robo_layout_shutdown();
    ROBO_CHECK_EQ_INT(robo_layout_is_initialized(), 0);

    ROBO_CHECK_EQ_INT(robo_layout_init(), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_layout_is_initialized(), 1);
    /* Leave it up: every other suite in this directory assumes the fixture. */
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

/**
 * The flags, in declaration order, with the value this build claims for each.
 *
 * Written out by hand rather than derived: a table generated from the struct
 * would agree with the struct by construction and would therefore assert
 * nothing. The order is the struct's own, so the walk below is positional.
 */
static const struct {
    const char *name;
    int expected;
} layout_capability_flags[] = {
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
    { "position", 1 },
    { "painting", 0 }
};

static void test_module_capabilities(void)
{
    robo_layout_capabilities_t caps;
    const int *fields;
    size_t count = sizeof(layout_capability_flags) / sizeof(layout_capability_flags[0]);
    size_t index;

    ROBO_CHECK_EQ_INT(sizeof(robo_layout_capabilities_t),
                      count * sizeof(int));

    layout_fixture();
    robo_layout_capabilities_init(&caps);
    fields = (const int *)&caps;
    for (index = 0u; index < count; index++) {
        ROBO_CHECK_MSG(fields[index] == 0,
                       "capabilities_init left a field asserted");
    }

    ROBO_CHECK_EQ_INT(robo_layout_get_capabilities(NULL), ROBO_INVALID_ARGUMENT);
    memset(&caps, 0xFF, sizeof(caps));
    ROBO_CHECK_EQ_INT(robo_layout_get_capabilities(&caps), ROBO_OK);
    fields = (const int *)&caps;
    for (index = 0u; index < count; index++) {
        ROBO_CHECK_EQ_INT(fields[index], layout_capability_flags[index].expected);
    }

    /*
     * The fields this build moves are also reachable one at a time, so a caller that
     * asks a single question is not reading a union of fifteen. And they are asserted
     * as a *group* rather than only in the table above, because the group is the
     * interesting fact: this build computes a coordinate, it computes it in *both*
     * directions, and the field that names both is the conjunction of the two -- still
     * an expression rather than a literal, so that the day one direction is switched
     * off the wider field follows it down by itself.
     */
    ROBO_CHECK_EQ_INT(caps.geometry, ROBO_LAYOUT_GEOMETRY_IMPLEMENTED);
    ROBO_CHECK_EQ_INT(caps.formatting_contexts, 1);
    ROBO_CHECK_EQ_INT(caps.block_layout, ROBO_LAYOUT_BLOCK_LAYOUT_IMPLEMENTED);
    ROBO_CHECK_EQ_INT(caps.inline_layout, ROBO_LAYOUT_INLINE_LAYOUT_IMPLEMENTED);
    ROBO_CHECK_EQ_INT(caps.block_and_inline_layout,
                      ROBO_LAYOUT_BLOCK_LAYOUT_IMPLEMENTED &&
                          ROBO_LAYOUT_INLINE_LAYOUT_IMPLEMENTED);
    ROBO_CHECK_EQ_INT(caps.text_layout, ROBO_LAYOUT_TEXT_LAYOUT_IMPLEMENTED);
    /*
     * And `line_boxes`, the same pair for the same reason. Asserted as its own field
     * rather than as a consequence of `text_layout`: producing a line and measuring a
     * run are independent claims, and the build that could do the first and not the
     * second is the one this module was before 10.6.
     */
    ROBO_CHECK_EQ_INT(caps.line_boxes, ROBO_LAYOUT_LINE_BOXES_IMPLEMENTED);
    /*
     * And `baselines`, its own field for the same reason: 10.7's claim is that a line
     * has a floor and its members are placed on it, which is neither the line's
     * *existence* (10.6) nor a run's *measurement* (10.5). A build that produced lines
     * and placed every member at the line's top is exactly what this module was before
     * this phase, and it satisfied both of those fields.
     */
    ROBO_CHECK_EQ_INT(caps.baselines, ROBO_LAYOUT_BASELINES_IMPLEMENTED);
    /*
     * And `sizing`, its own field for the same reason again: 10.8's claim is that a
     * declaration reaches the geometry -- a declared px width *and* the width a box's
     * children are laid out into -- which is neither a line's existence nor a
     * baseline. A build that had a sizing *record* and read nothing through it would
     * have satisfied every field above it.
     */
    ROBO_CHECK_EQ_INT(caps.sizing, ROBO_LAYOUT_SIZING_IMPLEMENTED);
    /*
     * And `margins`, its own field for the same reason one more time: 10.9's claim is
     * that a margin declaration reaches the geometry -- a box is placed at its margin
     * origin, its `auto` sides distribute the leftover beside a declared width, and a
     * parent stacks by its children's margin *boxes* -- which is neither a size nor a
     * baseline. A build with the whole margin vocabulary and no caller would have
     * satisfied every field above it.
     */
    ROBO_CHECK_EQ_INT(caps.margins, ROBO_LAYOUT_MARGINS_IMPLEMENTED);
    /*
     * And `padding`, the field 10.10 added -- the mirror of the one above it, and its
     * own field for the same reason: a padding reaches the geometry as *inside* the
     * box, so it changes a box's size and where its contents begin without ever moving
     * the box. A build with the margin vocabulary and no padding would have satisfied
     * every field above it.
     */
    ROBO_CHECK_EQ_INT(caps.padding, ROBO_LAYOUT_PADDING_IMPLEMENTED);
    /*
     * And `border`, the field 10.11 added -- the third inset and the one whose *used*
     * value is not its declared one: `border: 5px none` is a declared five and a used
     * zero, so this field claims a rule the two fields above it have no analogue of. A
     * build with the padding vocabulary and no border would have satisfied every field
     * above it, which is why it is asserted here rather than left to the table.
     */
    ROBO_CHECK_EQ_INT(caps.border, ROBO_LAYOUT_BORDER_IMPLEMENTED);
    ROBO_CHECK_EQ_INT(caps.positioning, ROBO_LAYOUT_POSITIONING_IMPLEMENTED);
    ROBO_CHECK_EQ_INT(caps.painting, 0);
    /* Both halves of the third row, so a build that set one of the pair without the
     * other is caught here rather than by a caller that trusted the constant. */
    ROBO_CHECK_EQ_INT(caps.geometry, caps.block_layout);
}

static void test_module_geometry_flag(void)
{
    robo_layout_capabilities_t caps;

    layout_fixture();
    /*
     * The macro, the function and the field: three doors, one answer. A build
     * that flipped one of them would have two answers to one question, and this
     * is the question a caller asks before reading a result.
     *
     * 1 since 10.3, and the *field* is still the narrow one: "this build computes
     * coordinates" is true while "this box has one" is a different question, which
     * `robo_layout_result_state` answers per box.
     */
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_GEOMETRY_IMPLEMENTED, 1);
    ROBO_CHECK_EQ_INT(robo_layout_geometry_is_implemented(), 1);
    ROBO_CHECK_EQ_INT(robo_layout_get_capabilities(&caps), ROBO_OK);
    ROBO_CHECK_EQ_INT(caps.geometry, robo_layout_geometry_is_implemented());
    /* And the block direction is where that 1 comes from, which is why the two
     * constants exist rather than one. */
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_BLOCK_LAYOUT_IMPLEMENTED, 1);
    ROBO_CHECK_EQ_INT(caps.block_layout, ROBO_LAYOUT_BLOCK_LAYOUT_IMPLEMENTED);
    /*
     * And the second direction arrives the same way, constant and field together: a
     * macro that says a feature exists and a capability field that says it does not
     * are two answers to one question, and the question is the one a caller asks
     * before trusting a line of geometry.
     */
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_INLINE_LAYOUT_IMPLEMENTED, 1);
    ROBO_CHECK_EQ_INT(caps.inline_layout, ROBO_LAYOUT_INLINE_LAYOUT_IMPLEMENTED);
    ROBO_CHECK_EQ_INT(caps.block_and_inline_layout, 1);
    /*
     * And the *participant* this build can now measure, through the same pair: the
     * macro and the field, so a build whose header promised text measurement and whose
     * capability record denied it would fail rather than pass on the more flattering
     * of the two answers.
     */
    ROBO_CHECK_EQ_INT(ROBO_LAYOUT_TEXT_LAYOUT_IMPLEMENTED, 1);
    ROBO_CHECK_EQ_INT(caps.text_layout, ROBO_LAYOUT_TEXT_LAYOUT_IMPLEMENTED);
}

/* ------------------------------------------------------------------ */
/* Closed vocabularies                                                 */
/* ------------------------------------------------------------------ */

static void test_module_object_kinds(void)
{
    size_t index;

    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_OBJECT_KIND_COUNT, 3);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_OBJECT_NONE, 0);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_OBJECT_BOX, 1);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_OBJECT_TEXT, 2);

    ROBO_CHECK_EQ_INT(robo_layout_object_kind_is_known(ROBO_LAYOUT_OBJECT_NONE), 1);
    ROBO_CHECK_EQ_INT(robo_layout_object_kind_is_known(ROBO_LAYOUT_OBJECT_BOX), 1);
    ROBO_CHECK_EQ_INT(robo_layout_object_kind_is_known(ROBO_LAYOUT_OBJECT_TEXT), 1);
    ROBO_CHECK_EQ_INT(robo_layout_object_kind_is_known((robo_layout_object_kind_t)3), 0);
    ROBO_CHECK_EQ_INT(robo_layout_object_kind_is_known((robo_layout_object_kind_t)99), 0);

    /* Walked in enumerator order: a table whose rows had drifted would still
     * answer is_known for every one and would show only as a name that printed
     * as another's keyword. */
    for (index = 0u; index < ROBO_LAYOUT_OBJECT_KIND_COUNT; index++) {
        robo_layout_object_kind_t kind = (robo_layout_object_kind_t)index;

        ROBO_CHECK_EQ_INT(robo_layout_object_kind_is_known(kind), 1);
        ROBO_CHECK(robo_layout_object_kind_name(kind) != NULL);
    }
    ROBO_CHECK_STR_EQ(robo_layout_object_kind_name(ROBO_LAYOUT_OBJECT_NONE), "none");
    ROBO_CHECK_STR_EQ(robo_layout_object_kind_name(ROBO_LAYOUT_OBJECT_BOX), "box");
    ROBO_CHECK_STR_EQ(robo_layout_object_kind_name(ROBO_LAYOUT_OBJECT_TEXT), "text");
    ROBO_CHECK_STR_EQ(robo_layout_object_kind_name((robo_layout_object_kind_t)77), "unknown");
    ROBO_CHECK(robo_layout_object_kind_name((robo_layout_object_kind_t)77) != NULL);
}

static void test_module_diagnostic_domains(void)
{
    size_t index;
    size_t seen[ROBO_LAYOUT_DOMAIN_COUNT];

    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_DOMAIN_COUNT, 20);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_DOMAIN_NONE, 0);
    ROBO_CHECK_EQ_INT(robo_layout_diagnostic_domain_is_known(ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT), 1);
    ROBO_CHECK_EQ_INT(robo_layout_diagnostic_domain_is_known(ROBO_LAYOUT_DOMAIN_SIZING), 1);
    ROBO_CHECK_EQ_INT(robo_layout_diagnostic_domain_is_known(ROBO_LAYOUT_DOMAIN_MARGINS), 1);
    ROBO_CHECK_EQ_INT(robo_layout_diagnostic_domain_is_known(ROBO_LAYOUT_DOMAIN_PADDING), 1);
    ROBO_CHECK_EQ_INT(robo_layout_diagnostic_domain_is_known(ROBO_LAYOUT_DOMAIN_BORDER), 1);
    ROBO_CHECK_EQ_INT(robo_layout_diagnostic_domain_is_known(ROBO_LAYOUT_DOMAIN_POSITION), 1);
    ROBO_CHECK_EQ_INT(robo_layout_diagnostic_domain_is_known((robo_layout_diagnostic_domain_t)20), 0);
    ROBO_CHECK_EQ_INT(robo_layout_diagnostic_domain_is_known((robo_layout_diagnostic_domain_t)200), 0);

    /* Every enumerator answers, every name is non-NULL, and no two names are the
     * same string -- which is the one thing a copy-pasted switch case breaks
     * while still compiling. */
    for (index = 0u; index < ROBO_LAYOUT_DOMAIN_COUNT; index++) {
        robo_layout_diagnostic_domain_t domain = (robo_layout_diagnostic_domain_t)index;
        const char *name = robo_layout_diagnostic_domain_name(domain);
        size_t other;

        ROBO_CHECK_EQ_INT(robo_layout_diagnostic_domain_is_known(domain), 1);
        ROBO_CHECK(name != NULL);
        if (!name) {
            continue;
        }
        seen[index] = strlen(name);
        ROBO_CHECK(seen[index] > 0u);
        for (other = 0u; other < index; other++) {
            ROBO_CHECK(strcmp(name,
                              robo_layout_diagnostic_domain_name(
                                  (robo_layout_diagnostic_domain_t)other)) != 0);
        }
    }
    ROBO_CHECK_STR_EQ(robo_layout_diagnostic_domain_name(ROBO_LAYOUT_DOMAIN_TREE), "tree");
    /* The two 10.2/10.3 domains, named rather than only walked: a diagnostic that
     * said "domain 11" would be a message nobody could act on. 10.2's formatting
     * contexts are no longer last, and the assertion below says who is: the domain
     * a failure is reported in is the phase that can fail, so "the newest domain is
     * the last enumerator" is a fact about the build and not about the numbering. */
    ROBO_CHECK_STR_EQ(robo_layout_diagnostic_domain_name(
                          ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT),
                      "formatting-context");
    ROBO_CHECK_STR_EQ(robo_layout_diagnostic_domain_name(ROBO_LAYOUT_DOMAIN_BLOCK_LAYOUT),
                      "block-layout");
    /* Each phase's domain, named and numbered, so that "the newest domain is the
     * last enumerator" stays a fact about the build rather than about the numbering:
     * the assertion below is that a failure *lands* in the domain its phase added. */
    ROBO_CHECK_STR_EQ(robo_layout_diagnostic_domain_name(ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT),
                      "inline-layout");
    ROBO_CHECK_STR_EQ(robo_layout_diagnostic_domain_name(ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT),
                      "text-layout");
    /* 10.8's domain, three behind the newest since 10.11. */
    ROBO_CHECK_STR_EQ(robo_layout_diagnostic_domain_name(ROBO_LAYOUT_DOMAIN_SIZING),
                      "sizing");
    ROBO_CHECK_STR_EQ(robo_layout_diagnostic_domain_name(ROBO_LAYOUT_DOMAIN_MARGINS),
                      "margins");
    /* 10.10's, the one that was newest before this phase. */
    ROBO_CHECK_STR_EQ(robo_layout_diagnostic_domain_name(ROBO_LAYOUT_DOMAIN_PADDING),
                      "padding");
    /* 10.11's, and it is the newest: the two derived numbers above a border are the only
     * arithmetic in this layer that can be handed a record it cannot use -- a *negative*
     * side is a record this module's own resolver never produces, so one arriving at a
     * size is a caller bug and the moment to see it is before a size is derived from it. */
    ROBO_CHECK_STR_EQ(robo_layout_diagnostic_domain_name(ROBO_LAYOUT_DOMAIN_BORDER),
                      "border");
    ROBO_CHECK_STR_EQ(robo_layout_diagnostic_domain_name(ROBO_LAYOUT_DOMAIN_POSITION),
                      "position");
    /* 10.12's, and it is the newest: the first domain in this module about *where*
     * a box is rather than about how big it is, which is why it ends the list rather
     * than slotting beside the three insets it displaces. */
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_DOMAIN_POSITION,
                      (int)ROBO_LAYOUT_DOMAIN_COUNT - 1);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_DOMAIN_PADDING,
                      (int)ROBO_LAYOUT_DOMAIN_BORDER - 1);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_DOMAIN_MARGINS,
                      (int)ROBO_LAYOUT_DOMAIN_PADDING - 1);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_DOMAIN_SIZING,
                      (int)ROBO_LAYOUT_DOMAIN_MARGINS - 1);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT,
                      (int)ROBO_LAYOUT_DOMAIN_SIZING - 1);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT,
                      (int)ROBO_LAYOUT_DOMAIN_TEXT_LAYOUT - 1);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_DOMAIN_BLOCK_LAYOUT,
                      (int)ROBO_LAYOUT_DOMAIN_INLINE_LAYOUT - 1);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_DOMAIN_FORMATTING_CONTEXT,
                      (int)ROBO_LAYOUT_DOMAIN_BLOCK_LAYOUT - 1);
    ROBO_CHECK_STR_EQ(robo_layout_diagnostic_domain_name((robo_layout_diagnostic_domain_t)400),
                      "unknown");
}

static void test_module_result_states(void)
{
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_RESULT_STATE_COUNT, 3);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_RESULT_NONE, 0);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_RESULT_UNRESOLVED, 1);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_RESULT_COMPUTED, 2);

    ROBO_CHECK_EQ_INT(robo_layout_result_state_is_known(ROBO_LAYOUT_RESULT_NONE), 1);
    ROBO_CHECK_EQ_INT(robo_layout_result_state_is_known(ROBO_LAYOUT_RESULT_UNRESOLVED), 1);
    ROBO_CHECK_EQ_INT(robo_layout_result_state_is_known(ROBO_LAYOUT_RESULT_COMPUTED), 1);
    ROBO_CHECK_EQ_INT(robo_layout_result_state_is_known((robo_layout_result_state_t)3), 0);

    ROBO_CHECK_STR_EQ(robo_layout_result_state_name(ROBO_LAYOUT_RESULT_NONE), "none");
    ROBO_CHECK_STR_EQ(robo_layout_result_state_name(ROBO_LAYOUT_RESULT_UNRESOLVED), "unresolved");
    ROBO_CHECK_STR_EQ(robo_layout_result_state_name(ROBO_LAYOUT_RESULT_COMPUTED), "computed");
    ROBO_CHECK_STR_EQ(robo_layout_result_state_name((robo_layout_result_state_t)9), "unknown");
}

static void test_module_pass_outcomes(void)
{
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_PASS_OUTCOME_COUNT, 3);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_PASS_NONE, 0);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_PASS_WALKED, 1);
    ROBO_CHECK_EQ_INT((int)ROBO_LAYOUT_PASS_FAILED, 2);

    ROBO_CHECK_EQ_INT(robo_layout_pass_outcome_is_known(ROBO_LAYOUT_PASS_NONE), 1);
    ROBO_CHECK_EQ_INT(robo_layout_pass_outcome_is_known(ROBO_LAYOUT_PASS_WALKED), 1);
    ROBO_CHECK_EQ_INT(robo_layout_pass_outcome_is_known(ROBO_LAYOUT_PASS_FAILED), 1);
    ROBO_CHECK_EQ_INT(robo_layout_pass_outcome_is_known((robo_layout_pass_outcome_t)3), 0);

    ROBO_CHECK_STR_EQ(robo_layout_pass_outcome_name(ROBO_LAYOUT_PASS_NONE), "none");
    ROBO_CHECK_STR_EQ(robo_layout_pass_outcome_name(ROBO_LAYOUT_PASS_WALKED), "walked");
    ROBO_CHECK_STR_EQ(robo_layout_pass_outcome_name(ROBO_LAYOUT_PASS_FAILED), "failed");
    ROBO_CHECK_STR_EQ(robo_layout_pass_outcome_name((robo_layout_pass_outcome_t)55), "unknown");
}

/* ------------------------------------------------------------------ */
/* Geometry is borrowed, not redeclared                                */
/* ------------------------------------------------------------------ */

static void test_module_geometry_aliases(void)
{
    robo_layout_point_t point;
    robo_layout_size_t size;
    robo_layout_rect_t rect;
    robo_layout_rect_f_t rect_f;
    robo_gfx_point_t gfx_point;
    robo_gfx_size_t gfx_size;
    robo_gfx_rect_t gfx_rect;
    robo_gfx_rect_f_t gfx_rect_f;

    /* Same type, by size... */
    ROBO_CHECK_EQ_INT(sizeof(robo_layout_point_t), sizeof(robo_gfx_point_t));
    ROBO_CHECK_EQ_INT(sizeof(robo_layout_size_t), sizeof(robo_gfx_size_t));
    ROBO_CHECK_EQ_INT(sizeof(robo_layout_rect_t), sizeof(robo_gfx_rect_t));
    ROBO_CHECK_EQ_INT(sizeof(robo_layout_rect_f_t), sizeof(robo_gfx_rect_f_t));

    /* ...and by assignment, which is the claim that actually matters: an object
     * built as a layout rectangle is passed to paint as a graphics rectangle
     * with no conversion, and one field write is visible from the other name. */
    gfx_point.x = 7;
    gfx_point.y = -3;
    point = gfx_point;
    ROBO_CHECK_EQ_INT(point.x, 7);
    ROBO_CHECK_EQ_INT(point.y, -3);

    gfx_size.width = 128;
    gfx_size.height = 64;
    size = gfx_size;
    ROBO_CHECK_EQ_INT(size.width, 128);
    ROBO_CHECK_EQ_INT(size.height, 64);

    gfx_rect.x = 1;
    gfx_rect.y = 2;
    gfx_rect.width = 3;
    gfx_rect.height = 4;
    rect = gfx_rect;
    ROBO_CHECK_EQ_INT(rect.x, 1);
    ROBO_CHECK_EQ_INT(rect.width, 3);
    ROBO_CHECK_EQ_INT(rect.height, 4);

    /* The float form is the same type too, so a sub-pixel rectangle travels to
     * paint the same way -- it is *not* implicitly convertible from the integer
     * one, which is a deliberate property of RoboGraphics rather than something
     * layout may paper over. */
    gfx_rect_f.x = 0.5f;
    gfx_rect_f.y = 1.5f;
    gfx_rect_f.width = 2.5f;
    gfx_rect_f.height = 3.5f;
    rect_f = gfx_rect_f;
    ROBO_CHECK_EQ_INT((int)rect_f.width, 2);
    ROBO_CHECK_EQ_INT((int)(rect_f.height * 2.0f), 7);
}

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

static void test_module_diagnostics(void)
{
    robo_layout_diag_t diag = ROBO_LAYOUT_CLEARED;
    robo_result_t code;

    layout_fixture();

    robo_layout_diag_reset(&diag);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_OK);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_LAYOUT_DOMAIN_NONE);
    ROBO_CHECK(diag.message == NULL);
    robo_layout_diag_reset(NULL);

    ROBO_CHECK_EQ_INT(robo_layout_diag_set(&diag, ROBO_NOT_FOUND, ROBO_LAYOUT_DOMAIN_TREE,
                                           "no such box", __FILE__, __LINE__),
                      ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_NOT_FOUND);
    ROBO_CHECK_EQ_INT((int)diag.domain, (int)ROBO_LAYOUT_DOMAIN_TREE);
    ROBO_CHECK_STR_EQ(diag.message, "no such box");
    ROBO_CHECK(diag.file != NULL);
    ROBO_CHECK(diag.line > 0);

    /* A record with no caller-owned home is not a failure: the result code is
     * authoritative and the diag is optional. */
    ROBO_CHECK_EQ_INT(robo_layout_diag_record(NULL, ROBO_INVALID_ARGUMENT,
                                              ROBO_LAYOUT_DOMAIN_OBJECT, "no record",
                                              __FILE__, __LINE__),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_layout_diag_set(NULL, ROBO_ERROR, ROBO_LAYOUT_DOMAIN_MODULE,
                                           "no record", __FILE__, __LINE__),
                      ROBO_INVALID_ARGUMENT);

    /* ROBO_OK clears rather than reports. */
    ROBO_CHECK_EQ_INT(robo_layout_diag_set(&diag, ROBO_OK, ROBO_LAYOUT_DOMAIN_TREE,
                                           "nothing wrong", __FILE__, __LINE__),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_OK);

    /* The macro evaluates to its code with or without a record. */
    code = ROBO_LAYOUT_DIAG(&diag, ROBO_LAYOUT_DOMAIN_CONSTRAINTS, ROBO_INVALID_ARGUMENT,
                            "negative size");
    ROBO_CHECK_EQ_INT(code, ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(diag.code, ROBO_INVALID_ARGUMENT);
    code = ROBO_LAYOUT_DIAG(NULL, ROBO_LAYOUT_DOMAIN_CONSTRAINTS, ROBO_INVALID_ARGUMENT,
                            "negative size");
    ROBO_CHECK_EQ_INT(code, ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* The module owns nothing                                             */
/* ------------------------------------------------------------------ */

static void test_module_allocator_balance(void)
{
    size_t before;
    robo_layout_capabilities_t caps;
    robo_layout_diag_t diag = ROBO_LAYOUT_CLEARED;
    char buffer[32];
    size_t i;

    layout_fixture();
    before = layout_live_blocks();

    /* Every module-level door, called hard: none of them may allocate. That is
     * what "the module owns no global state" means, and it is the reason init
     * cannot fail for an allocation reason. */
    for (i = 0u; i < 64u; i++) {
        ROBO_CHECK_EQ_INT(robo_layout_get_capabilities(&caps), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_layout_get_version(&caps.object_model, &caps.constraints,
                                                  &caps.result_model),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(robo_layout_version_string(), "0.12.0");
        ROBO_CHECK_EQ_INT(robo_layout_geometry_is_implemented(), 1);
        memset(buffer, 0, sizeof(buffer));
        strncpy(buffer, robo_layout_object_kind_name(ROBO_LAYOUT_OBJECT_BOX),
                sizeof(buffer) - 1u);
        strncpy(buffer, robo_layout_diagnostic_domain_name(ROBO_LAYOUT_DOMAIN_RESULT),
                sizeof(buffer) - 1u);
        strncpy(buffer, robo_layout_result_state_name(ROBO_LAYOUT_RESULT_UNRESOLVED),
                sizeof(buffer) - 1u);
        strncpy(buffer, robo_layout_pass_outcome_name(ROBO_LAYOUT_PASS_WALKED),
                sizeof(buffer) - 1u);
        ROBO_CHECK_EQ_INT(robo_layout_diag_record(&diag, ROBO_ERROR,
                                                  ROBO_LAYOUT_DOMAIN_MODULE,
                                                  "guess", __FILE__, __LINE__),
                          ROBO_ERROR);
    }
    robo_layout_diag_reset(&diag);

    ROBO_CHECK_EQ_INT(layout_live_blocks(), before);
}

/* ------------------------------------------------------------------ */

void robo_test_register_layout(void);

void robo_test_register_layout(void)
{
    robo_test_register("layout.module.version", test_module_version);
    robo_test_register("layout.module.lifecycle", test_module_lifecycle);
    robo_test_register("layout.module.capabilities", test_module_capabilities);
    robo_test_register("layout.module.geometry_flag", test_module_geometry_flag);
    robo_test_register("layout.module.object_kinds", test_module_object_kinds);
    robo_test_register("layout.module.diagnostic_domains", test_module_diagnostic_domains);
    robo_test_register("layout.module.result_states", test_module_result_states);
    robo_test_register("layout.module.pass_outcomes", test_module_pass_outcomes);
    robo_test_register("layout.module.geometry_aliases", test_module_geometry_aliases);
    robo_test_register("layout.module.diagnostics", test_module_diagnostics);
    robo_test_register("layout.module.allocator_balance", test_module_allocator_balance);
}
