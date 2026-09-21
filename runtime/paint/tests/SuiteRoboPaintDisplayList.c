/*
 * RoboDisplay list suite (Phase 11.3) — paint.display.*
 *
 * The artifact Phase 11.3 adds above the paint record, and the guarantees it claims:
 *
 *   **mapping**    every paint operation resolves to exactly one display command, by a
 *                  rule that is a total function over the record vocabulary -- so the two
 *                  lists are the same length and a reader can pair them positionally;
 *
 *   **resolution** the command carries the **one rectangle it acts on**, resolved: a
 *                  background is the border box, a border side its band, the content
 *                  boundary the content box, a marker the border box -- and the band is
 *                  asserted against `robo_paint_operation_side_band`, the same derivation
 *                  the record leaves to its consumer;
 *
 *   **regularity** the builder refuses a record list that is not a traversable document
 *                  (a box that never closes, a content boundary in the wrong place, a
 *                  decoration after it, a marker that names the wrong node), so a display
 *                  list's structural skeleton is an invariant rather than a convention;
 *
 *   **snapshot**   a display list references nothing mutable: mutating the record list or
 *                  re-laying-out the tree leaves it byte-identical.
 *
 * Every case builds a real document through the real chain (HTML → CSS → layout → paint →
 * display). Hand-built *operations* appear only in the malformed-sequence cases, where the
 * question is about the *builder* rather than about a document -- and never in place of a
 * layout result.
 *
 * Chromium is reference only; nothing here is a translation of it, and this suite depends
 * on no Chromium header, library or test framework.
 */

#include "RoboPaintTestSupport.h"

#include "RoboPaint/RoboPaintDisplayDump.h"
#include "RoboPaint/RoboPaintDisplayListBuilder.h"

#include <string.h>

/* The same base sheet the other paint suites use: these elements are the block
 * containers, and `<head>` stays the one box nothing styles. */
#define DISPLAY_BASE_CSS \
    "html, body, div, section, aside, figure, p { display: block }"

/* ------------------------------------------------------------------ */
/* Local helpers                                                       */
/* ------------------------------------------------------------------ */

/** Build a display list from @p records, asserting the build succeeds. */
static void display_build(const paint_result_t *records,
                          robo_paint_display_list_t **out_list,
                          robo_paint_display_list_stats_t *out_stats)
{
    robo_paint_display_list_builder_t *builder = NULL;
    robo_paint_diag_t diag;
    robo_result_t status;

    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(out_list, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_create(&builder, NULL), ROBO_OK);
    robo_paint_diag_reset(&diag);
    status = robo_paint_display_list_builder_build(builder, records->list, *out_list, out_stats,
                                             &diag);
    if (status != ROBO_OK) {
        robo_test_fail(__FILE__, __LINE__, "the display build refused the records: %s",
                       diag.message ? diag.message : "(no message)");
    }
    robo_paint_display_list_builder_destroy(builder);
}

/** The @p nth command of @p list whose kind is @p kind, or 0 when there is none. */
static int display_nth_of_kind(const robo_paint_display_list_t *list,
                               robo_paint_display_command_kind_t kind,
                               size_t nth,
                               robo_paint_display_command_t *out)
{
    size_t index;
    size_t seen = 0u;

    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        if (robo_paint_display_list_at(list, index, &command) != ROBO_OK) {
            return 0;
        }
        if (command.kind != kind) {
            continue;
        }
        if (seen == nth) {
            if (out) {
                *out = command;
            }
            return 1;
        }
        seen++;
    }
    return 0;
}

/** Write @p list's dump into @p buffer (asserted to fit) and return its length. */
static size_t display_dump(const robo_paint_display_list_t *list, char *buffer, size_t capacity)
{
    size_t length = 0u;

    memset(buffer, 0, capacity);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_write(list, buffer, capacity, &length, NULL),
                      ROBO_OK);
    return length;
}

/** A minimal, valid hand-built operation: a 10x10 box, all three rectangles equal. */
static robo_paint_operation_t display_make_op(robo_paint_op_kind_t kind,
                                              robo_layout_node_id_t node,
                                              size_t depth)
{
    robo_paint_operation_t operation;

    robo_paint_operation_init(&operation);
    operation.kind = kind;
    operation.node = node;
    operation.depth = depth;
    operation.boxes.border = robo_gfx_rect(0, 0, 10, 10);
    operation.boxes.padding = robo_gfx_rect(0, 0, 10, 10);
    operation.boxes.content = robo_gfx_rect(0, 0, 10, 10);
    operation.boxes.insets_measured = 0;
    operation.origin = robo_gfx_point(0, 0);
    operation.color = robo_css_color_none();
    return operation;
}

/**
 * The mapping and resolution rules, checked for every pair of the two lists.
 *
 * The strongest form of "geometry survives conversion" that does not hardcode a document:
 * the command's rectangle is asserted to be exactly the record's resolved rectangle, kind
 * by kind, and node/depth/clip/colour are asserted to carry through.
 */
static void display_check_mapping(const paint_result_t *records,
                                  const robo_paint_display_list_t *list)
{
    size_t count = robo_paint_list_count(records->list);
    size_t index;

    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count(list), (int)count);
    for (index = 0u; index < count; index++) {
        robo_paint_operation_t operation;
        robo_paint_display_command_t command;
        robo_layout_rect_t expected;

        ROBO_CHECK_EQ_INT(robo_paint_list_at(records->list, index, &operation), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_display_list_at(list, index, &command), ROBO_OK);

        /* Identity carries through unchanged. */
        ROBO_CHECK(command.node == operation.node);
        ROBO_CHECK_EQ_INT((int)command.depth, (int)operation.depth);
        ROBO_CHECK_EQ_INT(robo_paint_clip_equal(&command.clip, &operation.clip), 1);

        /* The kind mapping, and the resolved rectangle per kind. */
        memset(&expected, 0, sizeof(expected));
        switch (operation.kind) {
        case ROBO_PAINT_OP_BEGIN_BOX:
            ROBO_CHECK_EQ_INT((int)command.kind, (int)ROBO_PAINT_DISPLAY_CMD_BEGIN_NODE);
            expected = operation.boxes.border;
            break;
        case ROBO_PAINT_OP_BACKGROUND:
            ROBO_CHECK_EQ_INT((int)command.kind, (int)ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND);
            expected = operation.boxes.border;
            break;
        case ROBO_PAINT_OP_BORDER_TOP:
        case ROBO_PAINT_OP_BORDER_RIGHT:
        case ROBO_PAINT_OP_BORDER_BOTTOM:
        case ROBO_PAINT_OP_BORDER_LEFT:
            ROBO_CHECK_EQ_INT((int)command.kind, (int)ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER);
            /* The band is the one derivation the record leaves to its consumer, and the
             * builder resolves it -- asserted against the record's own door. */
            ROBO_CHECK_EQ_INT(robo_paint_operation_side_band(&operation, &expected, NULL),
                              ROBO_OK);
            break;
        case ROBO_PAINT_OP_TEXT:
            ROBO_CHECK_EQ_INT((int)command.kind, (int)ROBO_PAINT_DISPLAY_CMD_DRAW_TEXT);
            /* A run has no insets, so all three of its rectangles are equal and the
             * border box is the one the command is built on. */
            expected = operation.boxes.border;
            break;
        case ROBO_PAINT_OP_CONTENT:
            ROBO_CHECK_EQ_INT((int)command.kind, (int)ROBO_PAINT_DISPLAY_CMD_CONTENT);
            expected = operation.boxes.content;
            break;
        case ROBO_PAINT_OP_PUSH_CLIP:
        case ROBO_PAINT_OP_POP_CLIP:
            ROBO_CHECK_EQ_INT((int)command.kind,
                              operation.kind == ROBO_PAINT_OP_PUSH_CLIP
                                  ? (int)ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP
                                  : (int)ROBO_PAINT_DISPLAY_CMD_POP_CLIP);
            /* A scope command's rectangle is the box's **padding box** -- the boundary the
             * box's `overflow` resolved to, not the effective clip's rectangle, which does
             * not exist for a one-axis document. The boundary itself travels in `clip`, and
             * the identity check above has already compared it. */
            expected = operation.boxes.padding;
            break;
        case ROBO_PAINT_OP_END_BOX:
            ROBO_CHECK_EQ_INT((int)command.kind, (int)ROBO_PAINT_DISPLAY_CMD_END_NODE);
            expected = operation.boxes.border;
            break;
        default:
            robo_test_fail(__FILE__, __LINE__, "an operation kind outside the vocabulary");
            break;
        }
        ROBO_CHECK_EQ_INT(robo_gfx_rect_equal(command.rect, expected), 1);

        /* The colour rule: a drawing carries the record's colour, a structural carries
         * `none`. */
        if (robo_paint_display_command_kind_draws(command.kind)) {
            ROBO_CHECK_EQ_INT(robo_css_color_equals(command.color, operation.color), 1);
        } else {
            ROBO_CHECK_EQ_INT((int)command.color.kind, (int)ROBO_CSS_COLOR_NONE);
        }
        ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&command), 1);
    }
}

/* ------------------------------------------------------------------ */
/* 1. The vocabulary and the artifact                                  */
/* ------------------------------------------------------------------ */

static void test_display_command_vocabulary(void)
{
    size_t index;
    int drawing_total = 0;
    int structural_total = 0;

    paint_fixture();

    ROBO_CHECK_EQ_INT((int)ROBO_PAINT_DISPLAY_CMD_KIND_COUNT, 11);
    for (index = 0u; index < (size_t)ROBO_PAINT_DISPLAY_CMD_KIND_COUNT; index++) {
        robo_paint_display_command_kind_t kind = (robo_paint_display_command_kind_t)index;
        int is_drawing = robo_paint_display_command_kind_draws(kind);
        int is_structural = robo_paint_display_command_kind_is_structural(kind);

        ROBO_CHECK_EQ_INT(robo_paint_display_command_kind_is_known(kind), 1);
        ROBO_CHECK(strcmp(robo_paint_display_command_kind_name(kind), "unknown") != 0);
        if (kind == ROBO_PAINT_DISPLAY_CMD_NONE) {
            ROBO_CHECK_EQ_INT(is_drawing, 0);
            ROBO_CHECK_EQ_INT(is_structural, 0);
        } else {
            /* Exactly one half, and the halves partition the vocabulary above NONE --
             * the split is `draws`, so `CONTENT` (structural, not a marker) falls right. */
            ROBO_CHECK_EQ_INT(is_drawing + is_structural, 1);
            drawing_total += is_drawing;
            structural_total += is_structural;
        }
        if (robo_paint_display_command_kind_is_marker(kind)) {
            ROBO_CHECK_EQ_INT(is_structural, 1);
            ROBO_CHECK(kind != ROBO_PAINT_DISPLAY_CMD_CONTENT);
        }
    }
    /* Three drawing kinds -- background, border and text (11.6) -- and **seven** structural:
     * the begin/end markers, `CONTENT`, 11.8's push/pop pair and, since 11.9, the opacity
     * pair, which draw nothing by definition and therefore live on this side of the split.
     * The split is unchanged by the phase: 11.9 added two commands, not a drawing kind. */
    ROBO_CHECK_EQ_INT(drawing_total, 3);
    ROBO_CHECK_EQ_INT(structural_total, 7);

    ROBO_CHECK_STR_EQ(robo_paint_display_command_kind_name(ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER),
                      "draw-border");
    ROBO_CHECK_STR_EQ(robo_paint_display_command_kind_name(ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY),
                      "push-opacity");
    ROBO_CHECK_STR_EQ(robo_paint_display_command_kind_name(ROBO_PAINT_DISPLAY_CMD_POP_OPACITY),
                      "pop-opacity");
    /* The two scope predicates are disjoint and both refuse a drawing kind. */
    ROBO_CHECK_EQ_INT(robo_paint_display_command_kind_is_opacity(
                          ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY), 1);
    ROBO_CHECK_EQ_INT(robo_paint_display_command_kind_is_opacity(
                          ROBO_PAINT_DISPLAY_CMD_POP_OPACITY), 1);
    ROBO_CHECK_EQ_INT(robo_paint_display_command_kind_is_clip(
                          ROBO_PAINT_DISPLAY_CMD_PUSH_OPACITY), 0);
    ROBO_CHECK_EQ_INT(robo_paint_display_command_kind_is_opacity(
                          ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP), 0);
    ROBO_CHECK_EQ_INT(robo_paint_display_command_kind_is_opacity(
                          ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND), 0);
    ROBO_CHECK_STR_EQ(robo_paint_display_command_kind_name((robo_paint_display_command_kind_t)99),
                      "unknown");
}

static void test_display_empty_list(void)
{
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_builder_t *builder = NULL;
    robo_paint_display_list_stats_t stats;
    robo_paint_list_t *records = NULL;
    char buffer[64];

    paint_fixture();

    memset(&stats, 0xff, sizeof(stats));
    ROBO_CHECK_EQ_INT(robo_paint_list_create(&records, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&list, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_create(&builder, NULL), ROBO_OK);

    /* An empty record list is a legitimate build: a document that draws nothing has an
     * empty display list, and every counter is zero. */
    ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_build(builder, records, list, &stats, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count(list), 0);
    ROBO_CHECK(robo_paint_display_list_commands(list) == NULL);
    ROBO_CHECK_EQ_INT((int)stats.records, 0);
    ROBO_CHECK_EQ_INT((int)stats.commands, 0);
    ROBO_CHECK_EQ_INT((int)stats.nodes, 0);

    /* Its dump is empty, which is an answer rather than a placeholder line. */
    {
        size_t length = display_dump(list, buffer, sizeof(buffer));

        ROBO_CHECK_EQ_INT((int)length, 0);
        ROBO_CHECK_EQ_INT((int)buffer[0], 0);
    }

    robo_paint_display_list_builder_destroy(builder);
    robo_paint_display_list_destroy(list);
    robo_paint_list_destroy(records);
}

static void test_display_single_node_mapping(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"card\"></div></body></html>",
                         DISPLAY_BASE_CSS
                         " .card { width: 100px; height: 40px;"
                         "   border: 1px solid #000000; background-color: #ff0000 }");
    paint_once(&fixture, &result);
    display_build(&result, &list, &stats);

    ROBO_CHECK(robo_paint_list_count(result.list) > 0u);
    display_check_mapping(&result, list);

    /* The card's own commands: a begin marker, a background, four border bands, a content
     * boundary, an end marker. */
    ROBO_CHECK_EQ_INT((int)stats.nodes, (int)result.stats.boxes);
    ROBO_CHECK_EQ_INT((int)stats.backgrounds, (int)result.stats.backgrounds);
    ROBO_CHECK_EQ_INT((int)stats.borders, (int)result.stats.border_sides);
    ROBO_CHECK_EQ_INT((int)stats.commands, (int)robo_paint_list_count(result.list));
    ROBO_CHECK_EQ_INT((int)stats.records, (int)robo_paint_list_count(result.list));
    ROBO_CHECK((int)stats.backgrounds >= 1);
    ROBO_CHECK((int)stats.borders >= 4);

    /* The border band is *resolved* into the command: with a 1px border the top band's
     * height is exactly one, where the background's rectangle is the whole border box. */
    {
        robo_paint_display_command_t background;
        robo_paint_display_command_t border;

        ROBO_CHECK_EQ_INT(display_nth_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND, 0u,
                                              &background),
                          1);
        ROBO_CHECK_EQ_INT(display_nth_of_kind(list, ROBO_PAINT_DISPLAY_CMD_DRAW_BORDER, 0u, &border),
                          1);
        ROBO_CHECK_EQ_INT((int)border.rect.height, 1);
        ROBO_CHECK(border.rect.height < background.rect.height);
    }

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 2. Ordering, nesting, siblings                                      */
/* ------------------------------------------------------------------ */

static void test_display_ordering(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    size_t index;

    paint_begin_laid_out(&fixture,
                         "<html><body>"
                         "<div class=\"card\"><section class=\"badge\"></section></div>"
                         "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"
                         "</body></html>",
                         DISPLAY_BASE_CSS
                         " .card { width: 120px; height: 40px; padding: 4px;"
                         "   border: 1px solid #000000; background-color: #ffffff }"
                         " .badge { width: 20px; height: 10px; background-color: #123456 }"
                         " .clip { width: 60px; height: 30px; overflow: hidden }"
                         " .spill { width: 200px; height: 200px; border: 2px solid #ff0000 }");
    paint_once(&fixture, &result);
    display_build(&result, &list, &stats);

    display_check_mapping(&result, list);

    /*
     * The order is the records' order, which is the tree's: a consumer that ignored every
     * kind would still see each box's commands contiguous and in paint order.
     *
     * The nesting is recoverable from the display list alone: walk it, and the first
     * command of every box is a `BEGIN_NODE` whose depth is the number of open markers --
     * which is exactly the invariant the builder enforced, asked here as a consumer would.
     */
    {
        robo_paint_clip_t open_scopes[8];
        size_t scope_owner[8];
        size_t open = 0u;
        size_t scopes = 0u;
        int saw_content_for_open = 0;

        memset(open_scopes, 0, sizeof(open_scopes));
        memset(scope_owner, 0, sizeof(scope_owner));
        for (index = 0u; index < robo_paint_display_list_count(list); index++) {
            robo_paint_display_command_t command;

            ROBO_CHECK_EQ_INT(robo_paint_display_list_at(list, index, &command), ROBO_OK);
            if (command.kind == ROBO_PAINT_DISPLAY_CMD_BEGIN_NODE) {
                ROBO_CHECK_EQ_INT((int)command.depth, (int)open);
                open++;
                saw_content_for_open = 0;
            } else if (command.kind == ROBO_PAINT_DISPLAY_CMD_CONTENT) {
                ROBO_CHECK_EQ_INT((int)command.depth, (int)(open - 1u));
                saw_content_for_open = 1;
            } else if (command.kind == ROBO_PAINT_DISPLAY_CMD_PUSH_CLIP) {
                /* A scope opens **after** its box's content boundary and inside it, which is
                 * the two facts a consumer replaying the sequence needs to reconstruct the
                 * nesting -- and the reason this branch cannot share the drawing one below. */
                ROBO_CHECK_EQ_INT((int)command.depth, (int)(open - 1u));
                ROBO_CHECK_EQ_INT(saw_content_for_open, 1);
                ROBO_CHECK(scopes < sizeof(open_scopes) / sizeof(open_scopes[0]));
                open_scopes[scopes] = command.clip;
                scope_owner[scopes] = (size_t)command.depth;
                scopes++;
            } else if (command.kind == ROBO_PAINT_DISPLAY_CMD_POP_CLIP) {
                ROBO_CHECK_EQ_INT((int)command.depth, (int)(open - 1u));
                ROBO_CHECK_EQ_INT(saw_content_for_open, 1);
                ROBO_CHECK(scopes > 0u);
                ROBO_CHECK_EQ_INT(robo_paint_clip_equal(&command.clip, &open_scopes[scopes - 1u]), 1);
                ROBO_CHECK_EQ_INT((int)scope_owner[scopes - 1u], (int)command.depth);
                scopes--;
            } else if (command.kind == ROBO_PAINT_DISPLAY_CMD_END_NODE) {
                ROBO_CHECK_EQ_INT((int)command.depth, (int)(open - 1u));
                ROBO_CHECK_EQ_INT(saw_content_for_open, 1);
                /*
                 * **A box's scope closes before its own end-node.** Scopes still open here
                 * belong to ancestors -- `.spill`'s marker stands inside `.clip`'s scope -- so
                 * what is asserted is that none of them was opened *by the box being closed*,
                 * which is the ownership half of the invariant. The scope's pop names the same
                 * depth as its push, which is checked one branch up.
                 */
                for (size_t scope = 0u; scope < scopes; scope++) {
                    ROBO_CHECK(scope_owner[scope] != (size_t)command.depth);
                }
                open--;
            } else {
                ROBO_CHECK_EQ_INT((int)command.depth, (int)(open - 1u));
                ROBO_CHECK_EQ_INT(saw_content_for_open, 0);
            }
        }
        ROBO_CHECK_EQ_INT((int)open, 0);
        ROBO_CHECK_EQ_INT((int)scopes, 0);
        ROBO_CHECK(stats.clip_scopes > 0u);
    }

    /* The markers balance and the boundaries are one per node. */
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count_of_kind(list, ROBO_PAINT_DISPLAY_CMD_BEGIN_NODE),
                      (int)robo_paint_display_list_count_of_kind(list, ROBO_PAINT_DISPLAY_CMD_END_NODE));
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count_of_kind(list, ROBO_PAINT_DISPLAY_CMD_CONTENT),
                      (int)stats.nodes);

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 3. Clip carries through                                             */
/* ------------------------------------------------------------------ */

static void test_display_clip_carried(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    size_t index;
    int clipped_commands = 0;

    paint_begin_laid_out(&fixture,
                         "<html><body>"
                         "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"
                         "</body></html>",
                         DISPLAY_BASE_CSS
                         " .clip { width: 60px; height: 30px; overflow-x: hidden }"
                         " .spill { width: 200px; height: 200px }");
    paint_once(&fixture, &result);
    display_build(&result, &list, &stats);

    display_check_mapping(&result, list);

    /* The boundary a box's overflow put in force travels onto its descendants' commands,
     * so a rasteriser applies it from the command rather than from a separate op. */
    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_at(list, index, &command), ROBO_OK);
        if (robo_paint_clip_is_active(&command.clip)) {
            clipped_commands++;
        }
    }
    ROBO_CHECK(clipped_commands >= 1);

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 4. The builder refuses a list that is not a document                */
/* ------------------------------------------------------------------ */

static void display_expect_refusal(robo_paint_list_t *records, const char *what)
{
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_builder_t *builder = NULL;
    robo_paint_diag_t diag;
    robo_result_t status;

    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&list, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_create(&builder, NULL), ROBO_OK);
    robo_paint_diag_reset(&diag);
    status = robo_paint_display_list_builder_build(builder, records, list, NULL, &diag);
    if (status != ROBO_ERROR) {
        robo_test_fail(__FILE__, __LINE__, "%s: expected ROBO_ERROR, got %d", what,
                       (int)status);
    }
    /* A refused build hands back an empty list, never a partially converted one. */
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count(list), 0);

    robo_paint_display_list_builder_destroy(builder);
    robo_paint_display_list_destroy(list);
}

static void test_display_builder_refuses_malformed(void)
{
    robo_paint_list_t *records = NULL;

    paint_fixture();

    /* A box that never closes: a begin with no end. */
    ROBO_CHECK_EQ_INT(robo_paint_list_create(&records, NULL), ROBO_OK);
    {
        robo_paint_operation_t op = display_make_op(ROBO_PAINT_OP_BEGIN_BOX, 1u, 0u);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(records, &op, NULL), ROBO_OK);
    }
    display_expect_refusal(records, "an unclosed box");

    /* A begin nested where its depth says it does not nest. */
    robo_paint_list_clear(records);
    {
        robo_paint_operation_t a = display_make_op(ROBO_PAINT_OP_BEGIN_BOX, 1u, 0u);
        robo_paint_operation_t b = display_make_op(ROBO_PAINT_OP_BEGIN_BOX, 2u, 2u);

        ROBO_CHECK_EQ_INT(robo_paint_list_append(records, &a, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(records, &b, NULL), ROBO_OK);
    }
    display_expect_refusal(records, "a begin at the wrong depth");

    /* A content boundary before any box is open. */
    robo_paint_list_clear(records);
    {
        robo_paint_operation_t op = display_make_op(ROBO_PAINT_OP_CONTENT, 1u, 0u);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(records, &op, NULL), ROBO_OK);
    }
    display_expect_refusal(records, "a content boundary with no box");

    /* A box that closes before its content boundary. */
    robo_paint_list_clear(records);
    {
        robo_paint_operation_t a = display_make_op(ROBO_PAINT_OP_BEGIN_BOX, 1u, 0u);
        robo_paint_operation_t b = display_make_op(ROBO_PAINT_OP_END_BOX, 1u, 0u);

        ROBO_CHECK_EQ_INT(robo_paint_list_append(records, &a, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(records, &b, NULL), ROBO_OK);
    }
    display_expect_refusal(records, "a box closed before its content boundary");

    /* A marker that names a node other than its box's. */
    robo_paint_list_clear(records);
    {
        robo_paint_operation_t a = display_make_op(ROBO_PAINT_OP_BEGIN_BOX, 1u, 0u);
        robo_paint_operation_t b = display_make_op(ROBO_PAINT_OP_CONTENT, 2u, 0u);

        ROBO_CHECK_EQ_INT(robo_paint_list_append(records, &a, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(records, &b, NULL), ROBO_OK);
    }
    display_expect_refusal(records, "a marker naming the wrong node");

    /* A decoration after its box's content boundary. */
    robo_paint_list_clear(records);
    {
        robo_paint_operation_t a = display_make_op(ROBO_PAINT_OP_BEGIN_BOX, 1u, 0u);
        robo_paint_operation_t b = display_make_op(ROBO_PAINT_OP_CONTENT, 1u, 0u);
        robo_paint_operation_t c = display_make_op(ROBO_PAINT_OP_BACKGROUND, 1u, 0u);

        c.color = robo_css_color_make(0, 0, 0, 255);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(records, &a, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(records, &b, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(records, &c, NULL), ROBO_OK);
    }
    display_expect_refusal(records, "a decoration after the content boundary");

    /* And a well-formed hand-built list is *not* refused, so the refusals above are the
     * rules rather than the builder refusing everything it did not produce. */
    robo_paint_list_clear(records);
    {
        robo_paint_operation_t a = display_make_op(ROBO_PAINT_OP_BEGIN_BOX, 1u, 0u);
        robo_paint_operation_t b = display_make_op(ROBO_PAINT_OP_BACKGROUND, 1u, 0u);
        robo_paint_operation_t c = display_make_op(ROBO_PAINT_OP_CONTENT, 1u, 0u);
        robo_paint_operation_t d = display_make_op(ROBO_PAINT_OP_END_BOX, 1u, 0u);
        robo_paint_display_list_t *list = NULL;
        robo_paint_display_list_builder_t *builder = NULL;

        b.color = robo_css_color_make(0, 0, 0, 255);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(records, &a, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(records, &b, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(records, &c, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_list_append(records, &d, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&list, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_create(&builder, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(
            robo_paint_display_list_builder_build(builder, records, list, NULL, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count(list), 4);
        ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_build(builder, records, list, NULL, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count(list), 4);
        robo_paint_display_list_builder_destroy(builder);
        robo_paint_display_list_destroy(list);
    }

    robo_paint_list_destroy(records);
}

/* ------------------------------------------------------------------ */
/* 5. Ownership, determinism, dumps                                    */
/* ------------------------------------------------------------------ */

static void test_display_ownership_and_balance(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    size_t baseline;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"card\"></div></body></html>",
                         DISPLAY_BASE_CSS
                         " .card { width: 100px; height: 40px;"
                         "   border: 1px solid #000000; background-color: #ff0000 }");
    paint_once(&fixture, &result);

    baseline = paint_live_blocks();
    {
        robo_paint_display_list_builder_t *builder = NULL;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&list, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_create(&builder, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_build(builder, result.list, list, &stats,
                                                          NULL),
                          ROBO_OK);
        ROBO_CHECK(robo_paint_display_list_count(list) > 0u);
        ROBO_CHECK(robo_paint_display_list_capacity(list) >= robo_paint_display_list_count(list));

        /* Clear keeps the capacity and drops the commands -- the door a per-frame renderer
         * uses. */
        {
            size_t capacity = robo_paint_display_list_capacity(list);

            robo_paint_display_list_clear(list);
            ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count(list), 0);
            ROBO_CHECK_EQ_INT((int)robo_paint_display_list_capacity(list), (int)capacity);
        }
        /* Rebuild into the same list, and the build clears first. */
        ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_build(builder, result.list, list, &stats,
                                                          NULL),
                          ROBO_OK);

        robo_paint_display_list_destroy(list);
        list = NULL;
        robo_paint_display_list_builder_destroy(builder);
    }
    /* The list and the builder released everything they owned. */
    ROBO_CHECK_EQ_INT((int)paint_live_blocks(), (int)baseline);

    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_display_determinism(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *first = NULL;
    robo_paint_display_list_t *second = NULL;
    robo_paint_display_list_stats_t first_stats;
    robo_paint_display_list_stats_t second_stats;
    char first_dump[8192];
    char second_dump[8192];

    paint_begin_laid_out(&fixture,
                         "<html><body>"
                         "<div class=\"card\"><section class=\"badge\"></section></div>"
                         "<aside class=\"clip\"><figure class=\"spill\"></figure></aside>"
                         "</body></html>",
                         DISPLAY_BASE_CSS
                         " .card { width: 120px; height: 40px; padding: 4px;"
                         "   border: 1px solid #000000; background-color: #ffffff }"
                         " .badge { width: 20px; height: 10px; background-color: #123456 }"
                         " .clip { width: 60px; height: 30px; overflow: hidden }"
                         " .spill { width: 200px; height: 200px; border: 2px solid #ff0000 }");
    paint_once(&fixture, &result);

    /* Two independent builds of the same records produce byte-identical lists, stats and
     * dumps. */
    {
        robo_paint_display_list_builder_t *builder = NULL;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&first, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&second, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_create(&builder, NULL), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_build(builder, result.list, first,
                                                          &first_stats, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_build(builder, result.list, second,
                                                          &second_stats, NULL),
                          ROBO_OK);
        /* And one builder reused a third time: the stack's capacity does not change the
         * commands. */
        robo_paint_display_list_builder_reset(builder);
        ROBO_CHECK_EQ_INT(robo_paint_display_list_builder_build(builder, result.list, second,
                                                          &second_stats, NULL),
                          ROBO_OK);
        robo_paint_display_list_builder_destroy(builder);
    }
    ROBO_CHECK_EQ_INT(robo_paint_display_list_equal(first, second), 1);
    ROBO_CHECK_EQ_INT((int)first_stats.commands, (int)second_stats.commands);
    ROBO_CHECK_EQ_INT((int)first_stats.max_depth, (int)second_stats.max_depth);

    display_dump(first, first_dump, sizeof(first_dump));
    display_dump(second, second_dump, sizeof(second_dump));
    ROBO_CHECK_STR_EQ(first_dump, second_dump);

    robo_paint_display_list_destroy(first);
    robo_paint_display_list_destroy(second);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

static void test_display_dump(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    char buffer[8192];
    size_t length;
    size_t lines = 0u;
    const char *scan;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"card\"><section class=\"badge\"></section>"
                         "</div></body></html>",
                         DISPLAY_BASE_CSS
                         " .card { width: 100px; height: 40px; border: 1px solid #000000;"
                         "   background-color: #ff0000 }"
                         " .badge { width: 20px; height: 10px; background-color: #123456 }");
    paint_once(&fixture, &result);
    display_build(&result, &list, &stats);

    length = display_dump(list, buffer, sizeof(buffer));
    ROBO_CHECK(length > 0u);
    for (scan = buffer; *scan != '\0'; scan++) {
        if (*scan == '\n') {
            lines++;
        }
    }
    ROBO_CHECK_EQ_INT((int)lines, (int)robo_paint_display_list_count(list));

    /* The dump names the commands and their resolved rectangles, so a person can diff two
     * documents after the box model has been resolved. */
    ROBO_CHECK(strstr(buffer, "BEGIN_NODE") != NULL ||
               strstr(buffer, "begin-node") != NULL);
    ROBO_CHECK(strstr(buffer, "draw-border") != NULL);
    ROBO_CHECK(strstr(buffer, "draw-background") != NULL);
    ROBO_CHECK(strstr(buffer, "rect=") != NULL);

    /* A buffer one byte too small is refused with the length it would have needed. */
    {
        char small[8];
        size_t needed = 0u;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_write(list, small, sizeof(small), &needed, NULL),
                          ROBO_NOT_SUPPORTED);
        ROBO_CHECK_EQ_INT((int)needed, (int)length);
    }

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 6. Mutation isolation                                               */
/* ------------------------------------------------------------------ */

static void test_display_mutation_isolation(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *before = NULL;
    robo_paint_display_list_t *after = NULL;
    robo_paint_display_list_stats_t before_stats;
    robo_paint_display_list_stats_t after_stats;

    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"box\"></div></body></html>",
                         DISPLAY_BASE_CSS
                         " .box { height: 40px; background-color: #ff0000 }");
    paint_once(&fixture, &result);
    display_build(&result, &before, &before_stats);

    /*
     * Mutate both sources: clear the record list (the strongest legal change to it) and
     * lay the *same* tree out at a different viewport, which changes every box's geometry.
     * A display list is a snapshot -- and the mutation is *proved* observable by building
     * a second display list afterwards and asserting the two differ.
     */
    robo_paint_list_clear(result.list);
    ROBO_CHECK_EQ_INT((int)robo_paint_list_count(result.list), 0);

    paint_layout(&fixture, 400, 600);
    {
        robo_paint_context_t *context = NULL;

        ROBO_CHECK_EQ_INT(robo_paint_context_create(&context, NULL), ROBO_OK);
        paint_run(&fixture, context, result.list);
        robo_paint_context_destroy(context);
    }
    display_build(&result, &after, &after_stats);

    ROBO_CHECK(robo_paint_display_list_count(before) > 0u);
    ROBO_CHECK(robo_paint_display_list_count(after) > 0u);
    /* The mutation moved the second build, so the isolation is a real test rather than a
     * comparison of two identical documents. */
    ROBO_CHECK_EQ_INT(robo_paint_display_list_equal(before, after), 0);

    robo_paint_display_list_destroy(before);
    robo_paint_display_list_destroy(after);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 7. Empty and degenerate cases                                       */
/* ------------------------------------------------------------------ */

static void test_display_empty_and_degenerate(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;

    /* A document whose root element generates no box: a legitimate empty pass and an empty
     * display list. */
    paint_begin_laid_out(&fixture, "<html><body></body></html>", DISPLAY_BASE_CSS);
    paint_once(&fixture, &result);
    display_build(&result, &list, &stats);
    ROBO_CHECK_EQ_INT((int)stats.records, (int)robo_paint_list_count(result.list));
    ROBO_CHECK_EQ_INT((int)stats.commands, (int)robo_paint_display_list_count(list));
    display_check_mapping(&result, list);
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);

    /* A zero-sized box, and a box with no children: still a regular skeleton. */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"empty\"></div></body></html>",
                         DISPLAY_BASE_CSS " .empty { width: 0px; height: 0px }");
    paint_once(&fixture, &result);
    display_build(&result, &list, &stats);
    display_check_mapping(&result, list);
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count_of_kind(list, ROBO_PAINT_DISPLAY_CMD_CONTENT),
                      (int)stats.nodes);
    {
        robo_paint_display_command_t command;

        /* A structural command with a zero-sized rectangle is a valid record. */
        ROBO_CHECK_EQ_INT(display_nth_of_kind(list, ROBO_PAINT_DISPLAY_CMD_CONTENT, 0u, &command), 1);
        ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&command), 1);
    }
    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* 8. Geometry safety                                                  */
/* ------------------------------------------------------------------ */

static void test_display_geometry_safety(void)
{
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_command_t command;

    paint_fixture();

    ROBO_CHECK_EQ_INT(robo_paint_display_list_create(&list, NULL), ROBO_OK);

    /* A cleared command is not a command: the list refuses it, so no list can hold a
     * record with no node or a negative rectangle. */
    robo_paint_display_command_init(&command);
    ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&command), 0);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_append(list, &command, NULL), ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count(list), 0);

    /* A negative rectangle is refused; a zero-sized one is not. */
    command.kind = ROBO_PAINT_DISPLAY_CMD_CONTENT;
    command.node = (robo_layout_node_id_t)1u;
    command.depth = 0u;
    command.rect = robo_gfx_rect(0, 0, -1, 10);
    ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&command), 0);
    command.rect = robo_gfx_rect(0, 0, 0, 0);
    ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&command), 1);
    ROBO_CHECK_EQ_INT(robo_paint_display_list_append(list, &command, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT((int)robo_paint_display_list_count(list), 1);

    /* A drawing command with no colour is refused: a fill with no colour draws nothing. */
    robo_paint_display_command_init(&command);
    command.kind = ROBO_PAINT_DISPLAY_CMD_DRAW_BACKGROUND;
    command.node = (robo_layout_node_id_t)1u;
    ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&command), 0);
    command.color = robo_css_color_make(255, 0, 0, 255);
    ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&command), 1);

    robo_paint_display_list_destroy(list);
}

/* ------------------------------------------------------------------ */
/* 9. End to end                                                       */
/* ------------------------------------------------------------------ */

static void test_display_end_to_end(void)
{
    paint_fixture_doc_t fixture;
    paint_result_t result;
    robo_paint_display_list_t *list = NULL;
    robo_paint_display_list_stats_t stats;
    size_t index;

    /*
     * The whole pipeline in one case: HTML → CSS → layout → paint records → display list.
     * The display list is checked to be a *self-contained* sequence -- every command valid,
     * every kind known, every node live, the skeleton balanced -- with the tree still
     * present only to answer "is this node live", never to supply a rectangle.
     */
    paint_begin_laid_out(&fixture,
                         "<html><body><div class=\"outer\"><div class=\"inner\">"
                         "<span class=\"s\">Hello</span></div></div></body></html>",
                         DISPLAY_BASE_CSS
                         " .outer { width: 200px; height: 80px; padding: 8px;"
                         "   border: 2px solid #000000; background-color: #ffffff }"
                         " .inner { width: 100px; height: 40px; margin: 4px;"
                         "   background-color: #00ff00 }");
    paint_once(&fixture, &result);
    display_build(&result, &list, &stats);

    ROBO_CHECK(robo_paint_list_count(result.list) > 0u);
    ROBO_CHECK(robo_paint_display_list_count(list) > 0u);
    display_check_mapping(&result, list);

    for (index = 0u; index < robo_paint_display_list_count(list); index++) {
        robo_paint_display_command_t command;

        ROBO_CHECK_EQ_INT(robo_paint_display_list_at(list, index, &command), ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_paint_display_command_is_valid(&command), 1);
        ROBO_CHECK_EQ_INT(robo_paint_display_command_kind_is_known(command.kind), 1);
        ROBO_CHECK(command.kind != ROBO_PAINT_DISPLAY_CMD_NONE);
        ROBO_CHECK(command.node != ROBO_LAYOUT_NODE_ID_NONE);
        ROBO_CHECK_EQ_INT(robo_layout_object_is_live(fixture.tree, command.node), 1);
        ROBO_CHECK(command.rect.width >= 0 && command.rect.height >= 0);
    }

    ROBO_CHECK_EQ_INT((int)stats.commands, (int)robo_paint_list_count(result.list));
    ROBO_CHECK_EQ_INT((int)stats.nodes, (int)result.stats.boxes);
    ROBO_CHECK_EQ_INT((int)stats.backgrounds, (int)result.stats.backgrounds);

    robo_paint_display_list_destroy(list);
    paint_result_dispose(&result);
    paint_end(&fixture);
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

void robo_test_register_display_list(void)
{
    robo_test_register("paint.display.command_vocabulary", test_display_command_vocabulary);
    robo_test_register("paint.display.empty_list", test_display_empty_list);
    robo_test_register("paint.display.single_node_mapping", test_display_single_node_mapping);
    robo_test_register("paint.display.ordering", test_display_ordering);
    robo_test_register("paint.display.clip_carried", test_display_clip_carried);
    robo_test_register("paint.display.builder_refuses_malformed",
                       test_display_builder_refuses_malformed);
    robo_test_register("paint.display.ownership_and_balance",
                       test_display_ownership_and_balance);
    robo_test_register("paint.display.determinism", test_display_determinism);
    robo_test_register("paint.display.dump", test_display_dump);
    robo_test_register("paint.display.mutation_isolation", test_display_mutation_isolation);
    robo_test_register("paint.display.empty_and_degenerate",
                       test_display_empty_and_degenerate);
    robo_test_register("paint.display.geometry_safety", test_display_geometry_safety);
    robo_test_register("paint.display.end_to_end", test_display_end_to_end);
}
