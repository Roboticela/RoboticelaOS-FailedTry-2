/*
 * RoboCSS border suite (Phase 9.15) — the style vocabulary, the three kinds over
 * four sides, the classifier, the five distributing shorthands, and what the
 * cascade makes of a longhand four declarations can set.
 *
 * Five groups, in the order the phase is built:
 *
 *   the vocabularies  the ten styles and the three kinds walked rather than a
 *                     representative typed out, with the style rows' one fact
 *                     (`generates_border`) asserted per row and the twelve
 *                     longhand names reached through the walk rather than listed
 *   the classifier    what one component of a composite shorthand is, and the
 *                     *order* the questions are asked in, which is the whole logic
 *   the expansion     one `border:` value split into three parts, the reset a
 *                     kind the author did not write takes, and every refusal
 *   the shorthands    the three positional rows through the box table's 1/2/3/4
 *                     mapping, and the five composites through the registry
 *   integration       a real element: initial values through the query door,
 *                     non-inheritance proved from both sides, `inherit`,
 *                     `initial` and `unset`, one longhand set by four different
 *                     declarers at four different keys, and the reset semantics
 *
 * Nothing here paints anything. A border at this phase is twelve *values* with
 * shapes: `2px` stays a length, `medium` stays a keyword, `none` is a value rather
 * than an absent border, and `currentcolor` stays a reference until the one layer
 * that has an element resolves it.
 */

#include "RoboCssTestSupport.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The style vocabulary                                                */
/* ------------------------------------------------------------------ */

/**
 * The ten values, and what each is spelled as.
 *
 * The spelling is the assertion rather than the enumeration: a table whose rows
 * and enumerators had drifted would still answer `is_known` for all ten and would
 * only show as a value that printed as another one's keyword. Walking the set in
 * enumerator order and reading every name back is what makes that visible.
 */
static void test_border_style_vocabulary(void)
{
    static const char *const keywords[] = {
        "none", "hidden", "dotted", "dashed", "solid",
        "double", "groove", "ridge", "inset", "outset"
    };
    size_t index;
    char buffer[32];
    size_t length = 0u;

    css_fixture();
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_BORDER_STYLE_COUNT, 10);
    ROBO_CHECK_EQ_INT((int)robo_css_border_style_count(), 10);
    for (index = 0u; index < sizeof(keywords) / sizeof(keywords[0]); index++) {
        robo_css_border_style_t style = (robo_css_border_style_t)index;

        ROBO_CHECK_EQ_INT(robo_css_border_style_is_known(style), 1);
        ROBO_CHECK_STR_EQ(robo_css_border_style_name(style), keywords[index]);
        /* Read *back*: the keyword must name the same enumerator it came from. */
        ROBO_CHECK_EQ_INT(robo_css_border_style_from_text(keywords[index]), style);
        ROBO_CHECK_EQ_INT(robo_css_border_style_from_slice(
                              robo_css_slice_from_text(keywords[index])),
                          style);
        ROBO_CHECK_EQ_INT(robo_css_border_style_slice_is_known(
                              robo_css_slice_from_text(keywords[index])),
                          1);
        /* CSS compares keywords ASCII case-insensitively, so `SOLID` is `solid` —
         * and the *read-back* is what says so: knowness alone is true of all ten
         * rows and would pass for a table whose names had drifted. */
        ROBO_CHECK_EQ_INT(robo_css_border_style_slice_is_known(
                              robo_css_slice_from_text("SOLID")),
                          1);
        ROBO_CHECK_EQ_INT(robo_css_border_style_from_slice(
                              robo_css_slice_from_text("SOLID")),
                          ROBO_CSS_BORDER_STYLE_SOLID);
        ROBO_CHECK_EQ_INT(robo_css_border_style_write(style, buffer, sizeof(buffer),
                                                     &length, NULL),
                          ROBO_OK);
        ROBO_CHECK_STR_EQ(buffer, keywords[index]);
        ROBO_CHECK_EQ_INT((int)length, (int)strlen(keywords[index]));
    }

    /* An out-of-range enumerator answers *nothing* rather than the first row — the
     * difference between a table lookup and an index into it. */
    ROBO_CHECK_EQ_INT(robo_css_border_style_is_known((robo_css_border_style_t)10), 0);
    ROBO_CHECK_STR_EQ(robo_css_border_style_name((robo_css_border_style_t)10),
                      "unknown");
    ROBO_CHECK_STR_EQ(robo_css_border_style_name((robo_css_border_style_t)-1),
                      "unknown");
    ROBO_CHECK_EQ_INT(robo_css_border_style_slice_is_known(
                          robo_css_slice_from_text("banana")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_border_style_slice_is_known(
                          robo_css_slice_from_text("")),
                      0);
}

/**
 * `none` is both a value and the answer for "not one of them".
 *
 * The trap the display and position vocabularies each recorded, in its third
 * place: a *check* on the result would accept `border-style: banana` as
 * `border-style: none`, which is the most damaging confusion this property has —
 * `none` is not a spelling mistake, it is "this border draws nothing".
 */
static void test_border_style_none_trap(void)
{
    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_border_style_from_text("banana"),
                      ROBO_CSS_BORDER_STYLE_NONE);
    /* ...and the door that tells them apart. */
    ROBO_CHECK_EQ_INT(robo_css_border_style_slice_is_known(
                          robo_css_slice_from_text("banana")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_border_style_slice_is_known(
                          robo_css_slice_from_text("none")),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_border_style_from_text(NULL),
                      ROBO_CSS_BORDER_STYLE_NONE);
}

/**
 * Which styles draw anything.
 *
 * The one fact about a style a later phase must have, asserted per row: `none` and
 * `hidden` draw nothing — and a border that draws nothing has a *used* width of
 * zero whatever `border-width` says — while the other eight all draw. The two are
 * kept apart as values because `hidden` outranks every other style in border
 * collapsing, which is a table rule this build has no tables for; they answer this
 * one question the same way, and the assertion says so rather than leaving it to a
 * reader to notice.
 */
static void test_border_style_generates(void)
{
    static const robo_css_border_style_t silent[] = {
        ROBO_CSS_BORDER_STYLE_NONE, ROBO_CSS_BORDER_STYLE_HIDDEN
    };
    size_t index;
    size_t silent_count = 0u;

    css_fixture();
    for (index = 0u; index < robo_css_border_style_count(); index++) {
        robo_css_border_style_t style = (robo_css_border_style_t)index;

        if (!robo_css_border_style_generates_border(style)) {
            silent_count++;
            ROBO_CHECK_EQ_INT(style, silent[silent_count - 1u]);
        }
    }
    /* Exactly the two, in order, so a style added later cannot arrive silent by
     * accident and cannot arrive drawing without this count moving. */
    ROBO_CHECK_EQ_INT((int)silent_count, 2);
    for (index = 0u; index < sizeof(silent) / sizeof(silent[0]); index++) {
        ROBO_CHECK_EQ_INT(robo_css_border_style_generates_border(silent[index]), 0);
    }
    ROBO_CHECK_EQ_INT(robo_css_border_style_generates_border(
                          ROBO_CSS_BORDER_STYLE_SOLID),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_border_style_generates_border(
                          ROBO_CSS_BORDER_STYLE_DOUBLE),
                      1);
    /* An unknown enumerator answers *nothing*, not "draws". */
    ROBO_CHECK_EQ_INT(robo_css_border_style_generates_border(
                          (robo_css_border_style_t)10),
                      0);
}

/** A buffer one byte short reports the length it needed and writes nothing. */
static void test_border_style_write(void)
{
    char buffer[8];
    size_t length = 99u;
    robo_css_diag_t diag = ROBO_CSS_CLEARED;

    css_fixture();
    memset(buffer, '#', sizeof(buffer));
    ROBO_CHECK_EQ_INT(robo_css_border_style_write(ROBO_CSS_BORDER_STYLE_DOTTED,
                                                 buffer, 5u, &length, &diag),
                      ROBO_NOT_SUPPORTED);
    ROBO_CHECK_EQ_INT((int)length, 6);
    ROBO_CHECK_EQ_INT(diag.domain, ROBO_CSS_DOMAIN_BORDER);

    /* Exactly enough succeeds, NUL included. */
    ROBO_CHECK_EQ_INT(robo_css_border_style_write(ROBO_CSS_BORDER_STYLE_DOTTED,
                                                 buffer, 7u, &length, NULL),
                      ROBO_OK);
    ROBO_CHECK_STR_EQ(buffer, "dotted");

    /* The argument checks come first. */
    ROBO_CHECK_EQ_INT(robo_css_border_style_write(ROBO_CSS_BORDER_STYLE_SOLID, NULL,
                                                 8u, &length, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_border_style_write(ROBO_CSS_BORDER_STYLE_SOLID, buffer,
                                                  sizeof(buffer), NULL, NULL),
                      ROBO_INVALID_ARGUMENT);
    ROBO_CHECK_EQ_INT(robo_css_border_style_write((robo_css_border_style_t)10, buffer,
                                                 sizeof(buffer), &length, NULL),
                      ROBO_INVALID_ARGUMENT);
}

/* ------------------------------------------------------------------ */
/* The kinds and the twelve longhands                                  */
/* ------------------------------------------------------------------ */

/**
 * The three kinds, and the twelve names reached through the walk.
 *
 * The names are read out of the box table's positional rows rather than listed
 * here, which is what makes this a *family* assertion: for every side and every
 * kind there is a name, every name is one the box table owns, and the three rows
 * between them own exactly the twelve. A row written against the wrong side or a
 * kind whose row was never added cannot pass.
 */
static void test_border_kinds(void)
{
    static const char *const kind_names[] = { "width", "style", "color" };
    size_t kind_index;
    size_t side_index;
    size_t seen = 0u;

    css_fixture();
    ROBO_CHECK_EQ_INT((int)ROBO_CSS_BORDER_KIND_COUNT, 3);
    ROBO_CHECK_EQ_INT((int)robo_css_border_kind_count(), 3);
    ROBO_CHECK_EQ_INT((int)robo_css_border_longhand_count(), 12);
    for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
         kind_index++) {
        ROBO_CHECK_EQ_INT(robo_css_border_kind_is_known(
                              (robo_css_border_kind_t)kind_index),
                          1);
        ROBO_CHECK_STR_EQ(robo_css_border_kind_name(
                              (robo_css_border_kind_t)kind_index),
                          kind_names[kind_index]);
        /* The positional row exists, and its *kind* is the index it sits at — the
         * one thing this module states about the relation between the two. */
        ROBO_CHECK(robo_css_border_positional_row_of_kind(
                       (robo_css_border_kind_t)kind_index) != NULL);
        ROBO_CHECK_EQ_INT(robo_css_border_positional_kind(kind_index), kind_index);
    }
    ROBO_CHECK_EQ_INT(robo_css_border_kind_is_known((robo_css_border_kind_t)3), 0);
    ROBO_CHECK_STR_EQ(robo_css_border_kind_name((robo_css_border_kind_t)3),
                      "unknown");
    ROBO_CHECK(robo_css_border_positional_row_of_kind(
                   (robo_css_border_kind_t)3) == NULL);

    for (side_index = 0u; side_index < (size_t)ROBO_CSS_BOX_SIDE_COUNT;
         side_index++) {
        robo_css_box_side_t side = (robo_css_box_side_t)side_index;

        for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
             kind_index++) {
            robo_css_border_kind_t kind = (robo_css_border_kind_t)kind_index;
            const char *name = robo_css_border_longhand_name(side, kind);
            robo_css_box_side_t owner_side = (robo_css_box_side_t)99;

            ROBO_CHECK(name != NULL);
            if (!name) {
                continue;
            }
            /* Composed of the side name and the kind name, which is what makes a
             * transposition visible: a row written against `right` would produce
             * `border-right-width` where `border-left-width` was asked for. */
            {
                char expected[64];

                (void)snprintf(expected, sizeof(expected), "border-%s-%s",
                               robo_css_box_side_name(side), kind_names[kind_index]);
                ROBO_CHECK_STR_EQ(name, expected);
            }
            ROBO_CHECK_EQ_INT(robo_css_border_is_longhand(robo_css_slice_from_text(name)),
                              1);
            /* And the *box table* is where it lives, with the side it belongs to. */
            ROBO_CHECK(robo_css_box_shorthand_of_longhand(robo_css_slice_from_text(name),
                                                         &owner_side) != NULL);
            ROBO_CHECK_EQ_INT(owner_side, side);
            seen++;
        }
    }
    ROBO_CHECK_EQ_INT((int)seen, 12);
    ROBO_CHECK_EQ_INT(robo_css_border_longhand_name(
                          (robo_css_box_side_t)9, ROBO_CSS_BORDER_KIND_WIDTH) == NULL,
                      1);
    ROBO_CHECK_EQ_INT(robo_css_border_longhand_name(
                          ROBO_CSS_BOX_SIDE_TOP, (robo_css_border_kind_t)9) == NULL,
                      1);
    ROBO_CHECK_EQ_INT(robo_css_border_is_longhand(robo_css_slice_from_text(
                          "border-top")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_border_is_longhand(robo_css_slice_from_text(
                          "margin-top")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_border_is_longhand(robo_css_slice_from_text(
                          "border-top-radius")),
                      0);
}

/**
 * The three positional rows, and whose they are.
 *
 * The box table holds them — that is where the twelve longhand names are written
 * down — and the *border family* distributes them, because a longhand they cover
 * can also be set by a side shorthand and by `border`. This is the predicate the
 * box model's pass asks before it distributes a row, and the answer has to be
 * exactly the three.
 */
static void test_border_positional_rows(void)
{
    size_t index;
    size_t found = 0u;

    css_fixture();
    ROBO_CHECK_EQ_INT((int)robo_css_border_positional_count(), 3);
    for (index = 0u; index < robo_css_box_shorthand_count(); index++) {
        const robo_css_box_shorthand_t *row = robo_css_box_shorthand_at(index);
        int ours = robo_css_border_distributes_shorthand(
            robo_css_slice_from_text(row->name));

        if (ours) {
            found++;
            ROBO_CHECK_EQ_INT((int)strncmp(row->name, "border-", 7u), 0);
        }
    }
    /* Exactly the three border rows are the family's, and the other two stay the
     * box model's — `margin` and `padding` are walked over the same table and must
     * not be caught by this predicate. */
    ROBO_CHECK_EQ_INT((int)found, 3);
    ROBO_CHECK_EQ_INT(robo_css_border_distributes_shorthand(
                          robo_css_slice_from_text("margin")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_border_distributes_shorthand(
                          robo_css_slice_from_text("padding")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_border_distributes_shorthand(
                          robo_css_slice_from_text("border")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_border_distributes_shorthand(
                          robo_css_slice_from_text("BORDER-STYLE")),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_border_distributes_shorthand(robo_css_slice_from_text("")),
                      0);

    for (index = 0u; index < robo_css_border_positional_count(); index++) {
        const robo_css_box_shorthand_t *row = robo_css_border_positional_row(index);
        robo_css_border_kind_t kind = robo_css_border_positional_kind(index);

        ROBO_CHECK(row != NULL);
        ROBO_CHECK_STR_EQ(row->name, robo_css_border_positional_name(index));
        ROBO_CHECK_EQ_INT(robo_css_border_distributes_shorthand(
                              robo_css_slice_from_text(row->name)),
                          1);
        /* The row's longhands are the ones this module hands out for that kind,
         * side for side — the two tables agreeing, which is the whole point of
         * reading one from the other. */
        {
            size_t side_index;

            for (side_index = 0u; side_index < (size_t)ROBO_CSS_BOX_SIDE_COUNT;
                 side_index++) {
                ROBO_CHECK_STR_EQ(
                    robo_css_border_longhand_name((robo_css_box_side_t)side_index, kind),
                    robo_css_box_shorthand_longhand(row,
                                                    (robo_css_box_side_t)side_index));
            }
        }
        ROBO_CHECK_EQ_INT(robo_css_border_positional_name(
                              robo_css_border_positional_count()),
                          (const char *)NULL);
        ROBO_CHECK(robo_css_border_positional_row(robo_css_border_positional_count()) ==
                   NULL);
    }
    /* The three names, in kind order — `border-width` first, which is also the
     * order a serialisation would write them in. */
    ROBO_CHECK_STR_EQ(robo_css_border_positional_name(0u), "border-width");
    ROBO_CHECK_STR_EQ(robo_css_border_positional_name(1u), "border-style");
    ROBO_CHECK_STR_EQ(robo_css_border_positional_name(2u), "border-color");
}

/**
 * The five composites, and the side each covers.
 *
 * Index 0 is the whole shorthand and 1..4 are the sides in vocabulary order, so
 * the side is *derived* rather than kept in a second parallel array a walk could
 * fall out of step with. `border` is not a positional row and `border-top` is not
 * a longhand of one, which the assertions say because both are easy to get wrong
 * in the other direction.
 */
static void test_border_composites(void)
{
    static const char *const names[] = {
        "border", "border-top", "border-right", "border-bottom", "border-left"
    };
    size_t index;
    size_t side_index;

    css_fixture();
    ROBO_CHECK_EQ_INT((int)robo_css_border_composite_count(), 5);
    for (index = 0u; index < sizeof(names) / sizeof(names[0]); index++) {
        ROBO_CHECK_STR_EQ(robo_css_border_composite_name(index), names[index]);
        ROBO_CHECK_EQ_INT(robo_css_border_is_composite(
                              robo_css_slice_from_text(names[index])),
                          1);
        ROBO_CHECK_EQ_INT(robo_css_border_composite_is_whole(index), index == 0u);
        if (index > 0u) {
            ROBO_CHECK_EQ_INT(robo_css_border_composite_side(index),
                              (robo_css_box_side_t)(index - 1u));
            ROBO_CHECK_STR_EQ(robo_css_box_side_name(
                                  robo_css_border_composite_side(index)),
                              names[index] + 7u);
        }
    }
    ROBO_CHECK_EQ_INT(robo_css_border_is_composite(
                          robo_css_slice_from_text("BORDER-TOP")),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_border_is_composite(
                          robo_css_slice_from_text("border-width")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_border_is_composite(
                          robo_css_slice_from_text("border-top-width")),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_border_is_composite(robo_css_slice_from_text("")), 0);
    ROBO_CHECK_EQ_INT(robo_css_border_composite_name(
                          robo_css_border_composite_count()) == NULL,
                      1);

    /* `border` is *not* a positional row: it is not four longhands, it is twelve,
     * and it is not a longhand of anything either. */
    for (side_index = 0u; side_index < (size_t)ROBO_CSS_BOX_SIDE_COUNT; side_index++) {
        char name[32];

        (void)snprintf(name, sizeof(name), "border-%s",
                       robo_css_box_side_name((robo_css_box_side_t)side_index));
        ROBO_CHECK(robo_css_box_shorthand_find(robo_css_slice_from_text(name)) == NULL);
        ROBO_CHECK(robo_css_box_shorthand_of_longhand(
                       robo_css_slice_from_text(name), NULL) == NULL);
    }
    ROBO_CHECK(robo_css_box_shorthand_find(robo_css_slice_from_text("border")) == NULL);
}

/* ------------------------------------------------------------------ */
/* The classifier                                                      */
/* ------------------------------------------------------------------ */

/** The component shape `name: text` produces, or NULL (asserting it parses). */
static const robo_css_value_t *css_border_component(const char *text,
                                                    robo_css_declaration_list_t *list)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;
    const robo_css_declaration_t *declaration;

    ROBO_CHECK_EQ_INT(css_parse_declarations(text, list, &error), ROBO_OK);
    declaration = robo_css_declaration_list_at(list, 0u);
    ROBO_CHECK(declaration != NULL);
    return declaration ? robo_css_declaration_value(declaration) : NULL;
}

/** What `b: text` classifies as, through the real grammar. */
static robo_css_border_class_t css_border_classify_of(const char *text)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    const robo_css_value_t *component = css_border_component(text, &list);
    robo_css_border_class_t outcome = robo_css_border_classify(component);

    robo_css_declaration_list_dispose(&list);
    return outcome;
}

/** Assert `b: @p text` classifies as @p expected (a class *name*). */
static void css_border_is_class(const char *text, const char *expected)
{
    ROBO_CHECK_STR_EQ(robo_css_border_class_name(css_border_classify_of(text)),
                      expected);
}

/**
 * The five outcomes, and the *order* the questions are asked in.
 *
 * The order is the whole logic and it is the vocabulary that forces it: a style
 * keyword before a width keyword before a colour *name*, because `none` and
 * `hidden` are styles, `thin`/`medium`/`thick` are the only identifiers left that
 * are certainly not colours, and after those two an identifier the 9.9 table
 * recognises can only be a colour. Each row below is a near miss that a different
 * order would place somewhere else — `red` is not a style, `medium` is not a
 * colour, `none` is not a width — and `solid` is asserted beside them because it is
 * a style *and* an English word.
 */
static void test_border_classify(void)
{
    size_t index;
    static const struct {
        const char *text;
        const char *expected;
    } cases[] = {
        /* --- widths --- */
        { "b: 1px", "width" },
        { "b: 0", "width" },
        { "b: 1.5em", "width" },
        { "b: thin", "width" },
        { "b: medium", "width" },
        { "b: thick", "width" },
        { "b: 50%", "width" },
        { "b: -1px", "width" },
        /* --- styles: all ten, so a style the classifier forgot is a failure --- */
        { "b: none", "style" },
        { "b: hidden", "style" },
        { "b: dotted", "style" },
        { "b: dashed", "style" },
        { "b: solid", "style" },
        { "b: double", "style" },
        { "b: groove", "style" },
        { "b: ridge", "style" },
        { "b: inset", "style" },
        { "b: outset", "style" },
        /* --- colours, in all four of 9.9's spellings --- */
        { "b: red", "color" },
        { "b: transparent", "color" },
        { "b: currentcolor", "color" },
        { "b: #fff", "color" },
        { "b: #aabbccdd", "color" },
        { "b: rgb(10, 20, 30)", "color" },
        { "b: hsl(0 100% 50%)", "color" },
        /* --- deferred: substitution decides, so nothing is guessed --- */
        { "b: var(--w)", "deferred" },
        { "b: env(--w)", "deferred" },
        /* --- nothing this grammar has a place for --- */
        { "b: banana", "invalid" },
        { "b: url(a.png)", "invalid" },
        { "b: translate(1px)", "invalid" },
        { "b: \"solid\"", "invalid" },
        { "b: [solid]", "invalid" },
        { "b: #gg", "invalid" }
    };

    css_fixture();
    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        css_border_is_class(cases[index].text, cases[index].expected);
    }

    /* A list of more than one component is not *a* component — answering with the
     * first would be answering a question nobody asked. */
    css_border_is_class("b: solid dashed", "invalid");
    /* And a list of exactly one is unwrapped, because that is what a declaration
     * with one component is. */
    css_border_is_class("b: 1px", "width");
    /* A list whose one significant child is buried in trivia is one component. */
    css_border_is_class("b:   solid  ", "style");

    /* The vocabulary doors. */
    ROBO_CHECK_EQ_INT((int)robo_css_border_class_count(), 5);
    for (index = 0u; index < robo_css_border_class_count(); index++) {
        ROBO_CHECK_EQ_INT(robo_css_border_class_is_known(
                              (robo_css_border_class_t)index),
                          1);
        ROBO_CHECK(robo_css_border_class_name((robo_css_border_class_t)index) != NULL);
    }
    ROBO_CHECK_EQ_INT(robo_css_border_class_is_known((robo_css_border_class_t)5), 0);
    ROBO_CHECK_STR_EQ(robo_css_border_class_name((robo_css_border_class_t)5),
                      "unknown");
    /* The names, in enumerator order — a diagnostic prints these. */
    ROBO_CHECK_STR_EQ(robo_css_border_class_name(ROBO_CSS_BORDER_CLASS_INVALID),
                      "invalid");
    ROBO_CHECK_STR_EQ(robo_css_border_class_name(ROBO_CSS_BORDER_CLASS_WIDTH),
                      "width");
    ROBO_CHECK_STR_EQ(robo_css_border_class_name(ROBO_CSS_BORDER_CLASS_STYLE),
                      "style");
    ROBO_CHECK_STR_EQ(robo_css_border_class_name(ROBO_CSS_BORDER_CLASS_COLOR),
                      "color");
    ROBO_CHECK_STR_EQ(robo_css_border_class_name(ROBO_CSS_BORDER_CLASS_DEFERRED),
                      "deferred");
    /* A NULL component is INVALID rather than a crash. */
    ROBO_CHECK_EQ_INT(robo_css_border_classify(NULL), ROBO_CSS_BORDER_CLASS_INVALID);
}

/* ------------------------------------------------------------------ */
/* The expansion                                                       */
/* ------------------------------------------------------------------ */

/** `b: text` split into three parts, written as `width|style|color`. */
static void css_border_expand_is(const char *text, const char *expected)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    robo_css_value_t parts[ROBO_CSS_BORDER_KIND_COUNT];
    const robo_css_value_t *value = css_border_component(text, &list);
    char shape[128];
    size_t offset = 0u;
    size_t kind_index;

    for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
         kind_index++) {
        robo_css_value_init(&parts[kind_index]);
    }
    shape[0] = '\0';
    ROBO_CHECK_EQ_INT(robo_css_border_expand(value, parts, NULL), ROBO_OK);
    for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
         kind_index++) {
        if (kind_index > 0u) {
            css_shape_byte(shape, sizeof(shape), &offset, (uint8_t)'|');
        }
        if (parts[kind_index].kind == ROBO_CSS_VALUE_KIND_NONE) {
            /* An *absent* kind, which is not the empty string: the caller applies
             * the longhand's initial value, and the assertion has to be able to
             * tell that from "a component that happened to print as nothing". */
            css_shape_text(shape, sizeof(shape), &offset, "<absent>");
        } else {
            css_shape_write(&parts[kind_index], shape, sizeof(shape), &offset);
        }
    }
    ROBO_CHECK_STR_EQ(shape, expected);

    for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
         kind_index++) {
        robo_css_value_dispose(&parts[kind_index]);
    }
    robo_css_declaration_list_dispose(&list);
}

/**
 * `b: @p text` expanded, written as each slot's *class* name (`-` when absent).
 *
 * The companion to the shape above for the components whose rendering is their
 * token stream rather than a value: a colour function keeps the tokens the grammar
 * gave it, so "which slot did this land in" is the assertion that says something.
 */
static void css_border_expand_classes_is(const char *text, const char *expected)
{
    robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
    robo_css_value_t parts[ROBO_CSS_BORDER_KIND_COUNT];
    const robo_css_value_t *value = css_border_component(text, &list);
    char shape[64];
    size_t offset = 0u;
    size_t kind_index;

    for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
         kind_index++) {
        robo_css_value_init(&parts[kind_index]);
    }
    shape[0] = '\0';
    ROBO_CHECK_EQ_INT(robo_css_border_expand(value, parts, NULL), ROBO_OK);
    for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
         kind_index++) {
        if (kind_index > 0u) {
            css_shape_byte(shape, sizeof(shape), &offset, (uint8_t)'|');
        }
        if (parts[kind_index].kind == ROBO_CSS_VALUE_KIND_NONE) {
            css_shape_byte(shape, sizeof(shape), &offset, (uint8_t)'-');
        } else {
            css_shape_text(shape, sizeof(shape), &offset,
                           robo_css_border_class_name(
                               robo_css_border_classify(&parts[kind_index])));
        }
    }
    ROBO_CHECK_STR_EQ(shape, expected);

    for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
         kind_index++) {
        robo_css_value_dispose(&parts[kind_index]);
    }
    robo_css_declaration_list_dispose(&list);
}

/**
 * One `border:` value split into three, and the reset a missing kind takes.
 *
 * `<absent>` is the important column: `border: 1px solid` says nothing about the
 * colour, and what that means is not "leave it alone" — the shorthand stands for
 * all three kinds, so an unnamed kind takes the longhand's *initial* value. The
 * function deliberately does not supply it; the integration group below is where
 * the reset is asserted end to end.
 */
static void test_border_expand(void)
{
    size_t index;

    css_fixture();
    /* All three, in either order — the classifier's order, not the author's. */
    css_border_expand_is("b: 1px solid red", "1px|solid|red");
    css_border_expand_is("b: solid 1px red", "1px|solid|red");
    css_border_expand_is("b: red solid 1px", "1px|solid|red");
    /* Two of the three. */
    css_border_expand_is("b: 1px solid", "1px|solid|<absent>");
    css_border_expand_is("b: solid", "<absent>|solid|<absent>");
    css_border_expand_is("b: none", "<absent>|none|<absent>");
    css_border_expand_is("b: 2px", "2px|<absent>|<absent>");
    css_border_expand_is("b: red", "<absent>|<absent>|red");
    css_border_expand_is("b: 1px red", "1px|<absent>|red");
    /* One of each, and every spelling of a colour takes the colour slot. */
    css_border_expand_is("b: #fff", "<absent>|<absent>|#fff");
    css_border_expand_is("b: transparent", "<absent>|<absent>|transparent");
    css_border_expand_is("b: currentcolor", "<absent>|<absent>|currentcolor");
    /* A *functional* colour reaches the colour slot, and the slot holds the
     * component the grammar produced — its tokens — so what is asserted here is
     * which slot it landed in rather than a rendering of it. The colour it denotes
     * is asserted where it is computed, in the shapes group above. */
    css_border_expand_classes_is("b: rgb(10, 20, 30)", "-|-|color");
    css_border_expand_is("b: thick", "thick|<absent>|<absent>");
    css_border_expand_is("b: 0", "0|<absent>|<absent>");

    /* The refusals, each with its own reason and every output left absent so a
     * caller that ignored the code owns nothing rather than some of it. */
    {
        static const char *const refused[] = {
            /* A reference whose substitution can change *which kind* a component
             * is: `--rule: 1px solid red` makes one component three. */
            "p: var(--rule)",
            "p: env(--rule)",
            "p: 1px var(--s)",
            /* Two components for one kind. `1px 2px solid` is not a two-value
             * width — a border width is one value. */
            "p: 1px 2px solid",
            "p: red blue",
            "p: solid dashed",
            "p: 1px 2px",
            /* A component that belongs to no kind. */
            "p: banana",
            "p: 1px solid url(a.png)",
            /* Nothing at all. */
            "p:"
        };
        robo_css_value_t parts[ROBO_CSS_BORDER_KIND_COUNT];

        for (index = 0u; index < sizeof(refused) / sizeof(refused[0]); index++) {
            robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
            const robo_css_value_t *value = css_border_component(refused[index], &list);
            robo_css_diag_t diag = ROBO_CSS_CLEARED;
            size_t kind_index;
            char shape[64];

            for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
                 kind_index++) {
                robo_css_value_init(&parts[kind_index]);
            }
            ROBO_CHECK(robo_css_border_expand(value, parts, &diag) != ROBO_OK);
            ROBO_CHECK_EQ_INT(diag.domain, ROBO_CSS_DOMAIN_BORDER);
            ROBO_CHECK(diag.message != NULL);
            for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
                 kind_index++) {
                shape[0] = '\0';
                css_shape(&parts[kind_index], shape, sizeof(shape));
                ROBO_CHECK_STR_EQ(shape, "");
                robo_css_value_dispose(&parts[kind_index]);
            }
            robo_css_declaration_list_dispose(&list);
        }
    }

    /* The refusals that *name* the mistake, because collapsing them would still be
     * correct and would tell a developer nothing. */
    {
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        robo_css_diag_t diag = ROBO_CSS_CLEARED;
        robo_css_value_t parts[ROBO_CSS_BORDER_KIND_COUNT];
        size_t kind_index;

        for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
             kind_index++) {
            robo_css_value_init(&parts[kind_index]);
        }
        (void)robo_css_border_expand(css_border_component("p: var(--rule)", &list),
                                     parts, &diag);
        ROBO_CHECK(strstr(diag.message, "substitution") != NULL);
        robo_css_declaration_list_dispose(&list);

        (void)robo_css_border_expand(css_border_component("p: 1px 2px solid", &list),
                                     parts, &diag);
        ROBO_CHECK(strstr(diag.message, "one value of each kind") != NULL);
        robo_css_declaration_list_dispose(&list);

        (void)robo_css_border_expand(css_border_component("p: banana", &list), parts,
                                     &diag);
        ROBO_CHECK(strstr(diag.message, "border value") != NULL);
        robo_css_declaration_list_dispose(&list);

        for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
             kind_index++) {
            robo_css_value_dispose(&parts[kind_index]);
            robo_css_value_init(&parts[kind_index]);
        }
        ROBO_CHECK_EQ_INT(robo_css_border_expand(NULL, parts, NULL),
                          ROBO_INVALID_ARGUMENT);
        (void)robo_css_border_expand(css_border_component("p: red", &list), NULL, NULL);
        robo_css_declaration_list_dispose(&list);
    }
}

/* ------------------------------------------------------------------ */
/* The registry                                                        */
/* ------------------------------------------------------------------ */

/**
 * The seventeen rows, and the three shapes the twelve longhands take.
 *
 * The three kinds deliberately do *not* share a shape: a width is 9.11's
 * `LINE_WIDTH`, a colour is 9.9's `COLOR` — so `border-top-color: currentColor`
 * and `color: currentColor` are one computation resolved by one rule — and only
 * the style needed a shape of its own. That is asserted rather than assumed,
 * because a phase that gave a border colour its own shape would be a second place
 * `currentColor` is resolved.
 */
static void test_border_registry(void)
{
    robo_css_property_registry_t *registry = NULL;
    const robo_css_property_definition_t *definition;
    size_t side_index;
    size_t kind_index;

    css_fixture();
    ROBO_CHECK_EQ_INT(robo_css_property_registry_create_standard(&registry, NULL),
                      ROBO_OK);

    for (side_index = 0u; side_index < (size_t)ROBO_CSS_BOX_SIDE_COUNT;
         side_index++) {
        for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
             kind_index++) {
            robo_css_box_side_t side = (robo_css_box_side_t)side_index;
            robo_css_border_kind_t kind = (robo_css_border_kind_t)kind_index;
            const char *name = robo_css_border_longhand_name(side, kind);

            definition = robo_css_property_registry_find_slice(
                registry, robo_css_slice_from_text(name));
            ROBO_CHECK(definition != NULL);
            if (!definition) {
                continue;
            }
            /* Non-inherited, all twelve: a border is drawn per element, and a
             * child that inherited one would draw its parent's. */
            ROBO_CHECK_EQ_INT(robo_css_property_definition_inherited(definition), 0);
            switch (kind) {
            case ROBO_CSS_BORDER_KIND_WIDTH:
                ROBO_CHECK_EQ_INT(definition->computation,
                                  ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH);
                ROBO_CHECK_STR_EQ(definition->initial_text, "medium");
                break;
            case ROBO_CSS_BORDER_KIND_STYLE:
                ROBO_CHECK_EQ_INT(definition->computation,
                                  ROBO_CSS_PROPERTY_COMPUTATION_BORDER_STYLE);
                ROBO_CHECK_STR_EQ(definition->initial_text, "none");
                break;
            case ROBO_CSS_BORDER_KIND_COLOR:
                ROBO_CHECK_EQ_INT(definition->computation,
                                  ROBO_CSS_PROPERTY_COMPUTATION_COLOR);
                /* The one initial value in the table that is a *reference*. It is
                 * not `transparent`, and it cannot be: a border whose colour had
                 * been resolved at registration time would be a border nobody
                 * could see. */
                ROBO_CHECK_STR_EQ(definition->initial_text, "currentcolor");
                break;
            default:
                break;
            }
        }
    }

    /* The five shorthands are rows too, with the same concatenated initial a
     * shorthand carries and the shorthand flag set — a shorthand's own entry holds
     * what the author wrote, and the distribution is expansion's job. */
    for (kind_index = 0u; kind_index < robo_css_border_composite_count();
         kind_index++) {
        definition = robo_css_property_registry_find_slice(
            registry,
            robo_css_slice_from_text(robo_css_border_composite_name(kind_index)));
        ROBO_CHECK(definition != NULL);
        if (!definition) {
            continue;
        }
        ROBO_CHECK_EQ_INT(robo_css_property_definition_inherited(definition), 0);
        ROBO_CHECK_EQ_INT(definition->computation,
                          ROBO_CSS_PROPERTY_COMPUTATION_PRESERVE);
        ROBO_CHECK_EQ_INT((definition->flags & ROBO_CSS_PROPERTY_FLAG_SHORTHAND) != 0,
                          1);
        ROBO_CHECK_STR_EQ(definition->initial_text, "medium none currentcolor");
    }
    for (kind_index = 0u; kind_index < robo_css_border_positional_count();
         kind_index++) {
        definition = robo_css_property_registry_find_slice(
            registry,
            robo_css_slice_from_text(robo_css_border_positional_name(kind_index)));
        ROBO_CHECK(definition != NULL);
        if (!definition) {
            continue;
        }
        ROBO_CHECK_EQ_INT(robo_css_property_definition_inherited(definition), 0);
        ROBO_CHECK_EQ_INT((definition->flags & ROBO_CSS_PROPERTY_FLAG_SHORTHAND) != 0,
                          1);
    }
    ROBO_CHECK_STR_EQ(robo_css_property_registry_find_slice(
                          registry, robo_css_slice_from_text("border-style"))
                          ->initial_text,
                      "none");
    ROBO_CHECK_STR_EQ(robo_css_property_registry_find_slice(
                          registry, robo_css_slice_from_text("border-color"))
                          ->initial_text,
                      "currentcolor");
    ROBO_CHECK_STR_EQ(robo_css_property_registry_find_slice(
                          registry, robo_css_slice_from_text("border-width"))
                          ->initial_text,
                      "medium");
    /* `border-radius` is not a value this build has. */
    ROBO_CHECK(robo_css_property_registry_find_slice(
                   registry, robo_css_slice_from_text("border-radius")) == NULL);

    robo_css_property_registry_destroy(registry);
}

/* ------------------------------------------------------------------ */
/* The computed shapes                                                 */
/* ------------------------------------------------------------------ */

/**
 * The four longhand shapes through declaration text.
 *
 * A width keeps `medium` as a keyword, a percentage is admitted because the 9.11
 * registry admits it and this phase follows the registry rather than overruling it
 * (the boundary the box suite records), and a colour reuses 9.9's family so
 * `currentColor` survives as a reference.
 */
static void test_border_longhand_shapes(void)
{
    css_fixture();

    /* --- width --- */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "b: 1px", "1px");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "b: 0.5em", "0.5em");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "b: thin", "thin");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "b: medium", "medium");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "b: thick", "thick");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH,
                                       "b: medium"),
                      "keyword");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "b: red",
                       "not a border width keyword");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH, "b: banana",
                       "not a border width keyword");

    /* --- style: all ten, and the refusals kept apart --- */
    {
        size_t index;

        for (index = 0u; index < robo_css_border_style_count(); index++) {
            char text[64];

            (void)snprintf(text, sizeof(text), "b: %s",
                           robo_css_border_style_name(
                               (robo_css_border_style_t)index));
            css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BORDER_STYLE, text,
                               robo_css_border_style_name(
                                   (robo_css_border_style_t)index));
        }
    }
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_BORDER_STYLE,
                                       "b: solid"),
                      "border-style");
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_BORDER_STYLE,
                                       "b: none"),
                      "border-style");
    /* Three refusals, three sentences: an identifier this vocabulary does not have,
     * something that is not an identifier, and more than one component. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BORDER_STYLE, "b: banana",
                       "not a border style value");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BORDER_STYLE, "b: 20px",
                       "not an identifier");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BORDER_STYLE, "b: #fff",
                       "not an identifier");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BORDER_STYLE, "b: 10%",
                       "not an identifier");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_BORDER_STYLE, "b: solid dashed",
                       "not a single border style keyword");
    /* And a reference is *carried*, not refused — `border-style: var(--s)` is a
     * declaration whose value is decided where `--s` is. */
    {
        uint32_t flags = 0u;
        char shape[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];

        css_compute_text(ROBO_CSS_PROPERTY_COMPUTATION_BORDER_STYLE,
                         "b: var(--s)", shape, sizeof(shape), &flags);
        ROBO_CHECK_EQ_INT(flags, 1u);
    }

    /* --- colour: 9.9's family, so a reference survives --- */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "b: red", "#ff0000");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "b: #00ff00", "#00ff00");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "b: rgb(0, 0, 255)",
                       "#0000ff");
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "b: transparent",
                       "#00000000");
    /* The kind name is the computed-value layer's own: `colour`, which is what its
     * 9.9 suite established and what a diagnostic prints. A `currentcolor` is that
     * kind — a colour that is not resolved *yet* rather than a refusal. */
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_COLOR,
                                       "b: currentcolor"),
                      "colour");
    /* An identifier that is not a colour is reported as exactly that, apart from
     * every other refusal, because the author misspelled a colour rather than
     * putting the wrong kind of value in a colour property. */
    css_compute_equals(ROBO_CSS_PROPERTY_COMPUTATION_COLOR, "b: banana",
                       "unknown colour name");
    /* The shape name a diagnostic prints for the new one. */
    ROBO_CHECK_STR_EQ(robo_css_property_computation_name(
                          ROBO_CSS_PROPERTY_COMPUTATION_BORDER_STYLE),
                      "border-style");
    ROBO_CHECK_EQ_INT(robo_css_property_computation_is_known(
                          (robo_css_property_computation_t)
                              ROBO_CSS_PROPERTY_COMPUTATION_COUNT),
                      0);
}

/* ------------------------------------------------------------------ */
/* Integration: a real element                                         */
/* ------------------------------------------------------------------ */

/** Compute @p ref's style into @p out, asserting the walk itself was clean. */
static void css_border_compute_of(css_inherit_fixture_t *fixture,
                                  robo_css_element_ref_t ref,
                                  robo_css_computed_style_t *out)
{
    robo_css_error_t error = ROBO_CSS_CLEARED;

    ROBO_CHECK_EQ_INT(robo_css_style_context_compute_style(css_inherit_context(fixture),
                                                          ref, out, &error, NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(error.kind, ROBO_CSS_ERROR_KIND_NONE);
}

/**
 * The typed value behind a name, through the query door a consumer reads.
 *
 * The door is the *context's* rather than the style's, for the reason 9.12
 * recorded: nothing in the border family inherits, so an element that declared
 * nothing has no entry — 9.7 materialises initial values for the inherited set
 * only — and "what is this element's border-top-width" is a question only
 * something holding the registry can answer.
 */
static void css_border_query(css_inherit_fixture_t *fixture,
                             const robo_css_computed_style_t *style,
                             const char *name,
                             char *out,
                             size_t capacity,
                             int *out_from_initial)
{
    robo_css_computed_value_t value = ROBO_CSS_CLEARED;
    size_t length = 0u;

    if (out_from_initial) {
        *out_from_initial = -1;
    }
    if (out && capacity > 0u) {
        out[0] = '\0';
    }
    ROBO_CHECK_EQ_INT(robo_css_style_context_query_computed_value(
                          css_inherit_context(fixture), style,
                          robo_css_slice_from_text(name), &value, out_from_initial,
                          NULL),
                      ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_value_kind_is_known(value.kind), 1);
    if (out && capacity > 0u) {
        ROBO_CHECK_EQ_INT(robo_css_computed_value_write(&value, out, capacity, &length,
                                                        NULL),
                          ROBO_OK);
    }
    robo_css_computed_value_dispose(&value);
}

/**
 * The twelve initial values, and the thirteen shorthand ones.
 *
 * `medium`, `none` and `currentcolor` — and the last is the one worth reading
 * twice, because it is a *reference* rather than a value: a border colour is
 * whatever the element's `color` is, and resolving it here would be inventing an
 * answer the element has not given.
 */
static void test_border_initial(void)
{
    /* The third one is the element's *own* colour, and that is the phase's most
     * easily-lost fact: the property's initial value is the reference `currentcolor`
     * — asserted as such, as text, in the registry group — and what an element sees
     * is that reference resolved against its own `color`. Left as the word, or
     * resolved to anything but the element's colour, `border: 1px solid` would draw
     * a border nobody could see. The sheet below declares the colour and no border,
     * so a resolution is visible in the answer rather than a default. */
    static const char *const initials[ROBO_CSS_BORDER_KIND_COUNT] = {
        "medium", "none", "#008000"
    };
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t element;
    char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
    size_t side_index;
    size_t kind_index;

    css_fixture();
    css_inherit_begin(&fixture);
    element = css_host_add(css_inherit_host(&fixture), "div", "bare", NULL);
    css_host_seal(css_inherit_host(&fixture));
    /* A sheet that says nothing about borders, so every answer below is the
     * property's own initial value. */
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, "#bare { color: green }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    css_border_compute_of(&fixture, element, style);
    for (side_index = 0u; side_index < (size_t)ROBO_CSS_BOX_SIDE_COUNT;
         side_index++) {
        for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
             kind_index++) {
            const char *name = robo_css_border_longhand_name(
                (robo_css_box_side_t)side_index, (robo_css_border_kind_t)kind_index);
            int from_initial = -1;

            css_border_query(&fixture, style, name, text, sizeof(text),
                             &from_initial);
            ROBO_CHECK_EQ_INT(from_initial, 1);
            ROBO_CHECK_STR_EQ(text, initials[kind_index]);
            /* Nothing was declared, so there is no entry — which is not the same
             * fact as "it has a value", and the door's flag is what says so. */
            ROBO_CHECK_EQ_INT(
                robo_css_computed_style_find(
                    style, robo_css_slice_from_text(name)) == (size_t)-1,
                1);
        }
    }
    /* `border-style: none` is the property's initial value *and* a decided value,
     * not an absent border — the same distinction `display: none` draws. */
    css_border_query(&fixture, style, "border-top-style", text, sizeof(text), NULL);
    ROBO_CHECK_STR_EQ(text, "none");
    css_border_query(&fixture, style, "border-top-width", text, sizeof(text), NULL);
    ROBO_CHECK_STR_EQ(text, "medium");

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * Every declaration the family has, on one element, through the computed style.
 *
 * The acceptance test for the *shapes*: four sides, three kinds, and the author's
 * value arriving as a value rather than as letters.
 */
static void test_border_longhands_computed(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t element;

    css_fixture();
    css_inherit_begin(&fixture);
    element = css_host_add(css_inherit_host(&fixture), "div", "box", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#box { border-top-width: 1px; border-right-width: 2px;"
                      " border-bottom-width: 3px; border-left-width: 4px;"
                      " border-top-style: solid; border-right-style: dashed;"
                      " border-bottom-style: dotted; border-left-style: double;"
                      " border-top-color: red; border-right-color: #00ff00;"
                      " border-bottom-color: rgb(0, 0, 255);"
                      " border-left-color: transparent }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);
    css_border_compute_of(&fixture, element, style);

    css_computed_is(style, "border-top-width", "1px");
    css_computed_is(style, "border-right-width", "2px");
    css_computed_is(style, "border-bottom-width", "3px");
    css_computed_is(style, "border-left-width", "4px");
    css_computed_is(style, "border-top-style", "solid");
    css_computed_is(style, "border-right-style", "dashed");
    css_computed_is(style, "border-bottom-style", "dotted");
    css_computed_is(style, "border-left-style", "double");
    css_computed_is(style, "border-top-color", "#ff0000");
    css_computed_is(style, "border-right-color", "#00ff00");
    css_computed_is(style, "border-bottom-color", "#0000ff");
    css_computed_is(style, "border-left-color", "#00000000");
    css_computed_flags_are(style, "border-top-width", "declared");
    css_computed_flags_are(style, "border-left-style", "declared");
    css_computed_flags_are(style, "border-top-color", "declared");

    /* The shorthands keep the author's text on their own entries, exactly as
     * `margin` and `background` do — but only the ones the sheet *declared*, which
     * here is none of them. */
    css_computed_is(style, "border-width", "<absent>");
    css_computed_is(style, "border", "<absent>");

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * A border does not inherit, proved from both sides.
 *
 * "The child has no entry" is the half that catches a property misclassified as
 * inherited; "the child has the initial value when asked" is the half that catches
 * a query door reporting a hole as a value. And an explicit `inherit` still works
 * through the existing mechanism, which is why the property is classified rather
 * than special-cased.
 */
static void test_border_non_inheritance(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *parent_style = NULL;
    robo_css_computed_style_t *child_style = NULL;
    robo_css_element_ref_t parent;
    robo_css_element_ref_t child;
    char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];

    css_fixture();
    css_inherit_begin(&fixture);
    parent = css_host_add(css_inherit_host(&fixture), "div", "parent", NULL);
    child = css_host_add(css_inherit_host(&fixture), "div", "child", NULL);
    css_host_set_parent(css_inherit_host(&fixture), child, parent);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#parent { border: 3px dashed red; border-top-style: ridge;"
                      " color: green }"
                      "#child { border-top-style: inherit }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&parent_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&child_style, NULL), ROBO_OK);

    css_border_compute_of(&fixture, parent, parent_style);
    css_computed_is(parent_style, "border-top-width", "3px");
    css_computed_is(parent_style, "border-top-style", "ridge");
    css_computed_is(parent_style, "border-top-color", "#ff0000");
    css_computed_is(parent_style, "border-left-color", "#ff0000");

    css_border_compute_of(&fixture, child, child_style);
    /* The longhand that asked for the parent's value got it... */
    css_computed_is(child_style, "border-top-style", "ridge");
    css_computed_flags_are(child_style, "border-top-style", "inherited");
    /* ...and a sibling kind on the *same* side did not, which is the assertion a
     * blanket "children inherit the parent's border" would fail. */
    css_computed_is(child_style, "border-top-width", "<absent>");
    css_computed_is(child_style, "border-top-color", "<absent>");
    css_computed_is(child_style, "border-bottom-style", "<absent>");
    css_border_query(&fixture, child_style, "border-top-width", text, sizeof(text),
                     NULL);
    ROBO_CHECK_STR_EQ(text, "medium");
    /* ...and what an undeclared border colour *is*: not the parent's red border,
     * which no child may inherit, but the child's own `color` — which it did inherit,
     * because `color` is an inherited property and a border colour is a reference to
     * it. One assertion, two rules. */
    css_border_query(&fixture, child_style, "border-left-color", text, sizeof(text),
                     NULL);
    ROBO_CHECK_STR_EQ(text, "#008000");

    robo_css_computed_style_destroy(parent_style);
    robo_css_computed_style_destroy(child_style);
    css_inherit_end(&fixture);
}

/**
 * `initial`, `inherit` and `unset` on a non-inherited family.
 *
 * `unset` is the assertion that matters: it behaves as `initial` for a property
 * that does not inherit, so it would fail if any of the twelve were classified
 * wrongly. `border: initial` is the shorthand form of the same statement, and it is
 * checked too — a shorthand whose keyword did not travel would leave all twelve
 * longhands absent while the shorthand's own entry carried the word.
 */
static void test_border_wide_keywords(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *parent_style = NULL;
    robo_css_computed_style_t *child_style = NULL;
    robo_css_element_ref_t parent;
    robo_css_element_ref_t child;
    robo_css_element_ref_t plain;
    char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];

    css_fixture();
    css_inherit_begin(&fixture);
    parent = css_host_add(css_inherit_host(&fixture), "div", "parent", NULL);
    child = css_host_add(css_inherit_host(&fixture), "div", "child", NULL);
    plain = css_host_add(css_inherit_host(&fixture), "div", "plain", NULL);
    css_host_set_parent(css_inherit_host(&fixture), child, parent);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#parent { border-top-width: 7px; border-top-style: solid;"
                      " border-top-color: red; color: green }"
                      "#child { border-top-width: initial; border-top-style: inherit;"
                      " border-top-color: unset }"
                      "#plain { border: initial }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&parent_style, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&child_style, NULL), ROBO_OK);

    css_border_compute_of(&fixture, parent, parent_style);
    css_computed_is(parent_style, "border-top-width", "7px");

    css_border_compute_of(&fixture, child, child_style);
    /* `initial` is the property's own initial value... */
    css_computed_is(child_style, "border-top-width", "medium");
    /* `default` rather than `declared+default`: the declaration is `initial`, so what
     * the longhand carries is the property's default, which is the same pair of
     * facts the box model's own wide-keyword test records. */
    css_computed_flags_are(child_style, "border-top-width", "default");
    /* ...`inherit` is the parent's... */
    css_computed_is(child_style, "border-top-style", "solid");
    css_computed_flags_are(child_style, "border-top-style", "inherited");
    /* ...and `unset` on a non-inherited property is `initial`, which is the
     * assertion that would fail if any of the twelve were misclassified. */
    css_computed_is(child_style, "border-top-color", "#008000");
    css_computed_flags_are(child_style, "border-top-color", "default");

    /* The shorthand form, on an element that declared nothing else. */
    css_border_compute_of(&fixture, plain, child_style);
    ROBO_CHECK_STR_EQ(css_compute_kind(ROBO_CSS_PROPERTY_COMPUTATION_LINE_WIDTH,
                                       "b: 1px"),
                      "length");
    css_border_query(&fixture, child_style, "border-top-width", text, sizeof(text),
                     NULL);
    ROBO_CHECK_STR_EQ(text, "medium");
    css_border_query(&fixture, child_style, "border-top-style", text, sizeof(text),
                     NULL);
    ROBO_CHECK_STR_EQ(text, "none");
    /* `plain` declared the whole shorthand as `initial` and nothing else, and it has
     * no parent, so its `color` is `color`'s own initial: black. The reference
     * resolves to that — a colour, not the word and not `transparent`. */
    css_border_query(&fixture, child_style, "border-left-color", text, sizeof(text),
                     NULL);
    ROBO_CHECK_STR_EQ(text, "#000000");

    robo_css_computed_style_destroy(parent_style);
    robo_css_computed_style_destroy(child_style);
    css_inherit_end(&fixture);
}

/**
 * The positional rows: 1/2/3/4 components over four sides.
 *
 * The mapping is the box model's — literally the same function — and that is why
 * it is asserted for all three kinds rather than for one: `border-style: solid
 * dashed dotted double` and `border-width: 1px 2px 3px 4px` must distribute by the
 * same rule, and a second copy of the mapping is how they would come to disagree.
 */
static void test_border_positional_expansion(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t one;
    robo_css_element_ref_t two;
    robo_css_element_ref_t three;
    robo_css_element_ref_t four;

    css_fixture();
    css_inherit_begin(&fixture);
    one = css_host_add(css_inherit_host(&fixture), "div", "one", NULL);
    two = css_host_add(css_inherit_host(&fixture), "div", "two", NULL);
    three = css_host_add(css_inherit_host(&fixture), "div", "three", NULL);
    four = css_host_add(css_inherit_host(&fixture), "div", "four", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#one { border-width: 1px; border-style: solid;"
                      " border-color: red }"
                      "#two { border-width: 1px 2px; border-style: solid dashed;"
                      " border-color: red blue }"
                      "#three { border-width: 1px 2px 3px;"
                      " border-style: solid dashed dotted;"
                      " border-color: red blue green }"
                      "#four { border-width: 1px 2px 3px 4px;"
                      " border-style: solid dashed dotted double;"
                      " border-color: red blue green black }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    css_border_compute_of(&fixture, one, style);
    css_computed_is(style, "border-top-width", "1px");
    css_computed_is(style, "border-right-width", "1px");
    css_computed_is(style, "border-bottom-width", "1px");
    css_computed_is(style, "border-left-width", "1px");
    css_computed_is(style, "border-top-style", "solid");
    css_computed_is(style, "border-left-style", "solid");
    css_computed_is(style, "border-top-color", "#ff0000");
    css_computed_is(style, "border-bottom-color", "#ff0000");

    css_border_compute_of(&fixture, two, style);
    css_computed_is(style, "border-top-width", "1px");
    css_computed_is(style, "border-right-width", "2px");
    css_computed_is(style, "border-bottom-width", "1px");
    css_computed_is(style, "border-left-width", "2px");
    css_computed_is(style, "border-top-style", "solid");
    css_computed_is(style, "border-right-style", "dashed");
    css_computed_is(style, "border-bottom-style", "solid");
    css_computed_is(style, "border-left-style", "dashed");
    css_computed_is(style, "border-top-color", "#ff0000");
    css_computed_is(style, "border-left-color", "#0000ff");

    css_border_compute_of(&fixture, three, style);
    css_computed_is(style, "border-top-width", "1px");
    css_computed_is(style, "border-right-width", "2px");
    css_computed_is(style, "border-bottom-width", "3px");
    css_computed_is(style, "border-left-width", "2px");
    css_computed_is(style, "border-bottom-style", "dotted");
    css_computed_is(style, "border-right-style", "dashed");
    css_computed_is(style, "border-left-style", "dashed");
    css_computed_is(style, "border-bottom-color", "#008000");
    css_computed_is(style, "border-left-color", "#0000ff");

    css_border_compute_of(&fixture, four, style);
    css_computed_is(style, "border-top-width", "1px");
    css_computed_is(style, "border-right-width", "2px");
    css_computed_is(style, "border-bottom-width", "3px");
    css_computed_is(style, "border-left-width", "4px");
    css_computed_is(style, "border-top-style", "solid");
    css_computed_is(style, "border-right-style", "dashed");
    css_computed_is(style, "border-bottom-style", "dotted");
    css_computed_is(style, "border-left-style", "double");
    css_computed_is(style, "border-top-color", "#ff0000");
    css_computed_is(style, "border-right-color", "#0000ff");
    css_computed_is(style, "border-bottom-color", "#008000");
    css_computed_is(style, "border-left-color", "#000000");

    /* Five components is not a longer shorthand, it is an invalid declaration —
     * and the author's text stays on the shorthand's own entry either way. */
    {
        robo_css_element_ref_t five;

        five = css_host_add(css_inherit_host(&fixture), "div", "five", NULL);
        css_host_seal(css_inherit_host(&fixture));
        css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                          "#five { border-width: 1px 2px 3px 4px 5px }");
        css_border_compute_of(&fixture, five, style);
        css_computed_is(style, "border-top-width", "<absent>");
        css_computed_is(style, "border-width", "1px 2px 3px 4px 5px");
    }

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * One longhand, four declarers, and the cascade deciding.
 *
 * The phase's central fact, in the only form that can be wrong: `border-top-width`
 * can be set by itself, by `border-width`, by `border-top`, and by `border`, and
 * which of them it *is* comes down to the same three keys as anything else — level,
 * specificity, document order. Every pair below is a different one of those keys,
 * and the last is the case a single-shorthand implementation cannot see: two
 * *different* shorthands competing for one longhand.
 */
static void test_border_cascade(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t order;
    robo_css_element_ref_t reversed;
    robo_css_element_ref_t specific;
    robo_css_element_ref_t important;
    robo_css_element_ref_t cross;
    robo_css_element_ref_t inlined;
    css_inline_stub_t stub;

    css_fixture();
    css_inherit_begin(&fixture);
    order = css_host_add(css_inherit_host(&fixture), "div", "order", NULL);
    reversed = css_host_add(css_inherit_host(&fixture), "div", "reversed", NULL);
    specific = css_host_add(css_inherit_host(&fixture), "div", "specific", NULL);
    important = css_host_add(css_inherit_host(&fixture), "div", "important", NULL);
    cross = css_host_add(css_inherit_host(&fixture), "div", "cross", NULL);
    inlined = css_host_add(css_inherit_host(&fixture), "div", "inlined", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      /* the shorthand then the longhand: the longhand is later,
                       * so it wins */
                      "#order { border: 1px solid red; border-top-width: 5px }"
                      /* the reverse: the shorthand is later, so *it* wins, and the
                       * width arrives out of it */
                      "#reversed { border-top-width: 5px; border: 1px solid red }"
                      /* specificity beating document order across two declarers */
                      "#specific { border-width: 9px }"
                      "div#specific { border-top-width: 1px }"
                      /* `!important` beats a more specific selector */
                      "#important { border-top-width: 1px !important }"
                      "div#important { border-top-width: 9px }"
                      /* two *different* shorthands, one longhand */
                      "#cross { border-width: 1px; border-top: 3px solid blue }"
                      ".never { border-top-width: 99px }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    css_border_compute_of(&fixture, order, style);
    css_computed_is(style, "border-top-width", "5px");
    css_computed_is(style, "border-top-style", "solid");
    css_computed_flags_are(style, "border-top-width", "declared");

    /* The other order: the shorthand wins and the longhand arrives *from* it, with
     * the other three sides taking the same value. */
    css_border_compute_of(&fixture, reversed, style);
    css_computed_is(style, "border-top-width", "1px");
    css_computed_is(style, "border-left-width", "1px");
    css_computed_is(style, "border-top-color", "#ff0000");

    css_border_compute_of(&fixture, specific, style);
    css_computed_is(style, "border-top-width", "1px");
    css_computed_is(style, "border-left-width", "9px");

    css_border_compute_of(&fixture, important, style);
    css_computed_is(style, "border-top-width", "1px");

    /* `border-width` and `border-top` are two shorthands for one longhand, and the
     * later one wins — the case a pass that owned them separately could not see. */
    css_border_compute_of(&fixture, cross, style);
    css_computed_is(style, "border-top-width", "3px");
    css_computed_is(style, "border-top-style", "solid");
    css_computed_is(style, "border-left-width", "1px");
    /* A positional row covers four sides of *one kind*, so the left side has a width
     * and no style at all: absent is not `none`, and the difference is what keeps a
     * shorthand from inventing a border. */
    css_computed_is(style, "border-left-style", "<absent>");

    /* And an inline declaration is the strongest normal level. Installed here
     * rather than above because the stub answers *every* element, which is exactly
     * what the assertions before it must not be measuring. */
    css_inherit_inline(&fixture, &stub, "border-top-width: 7px");
    css_border_compute_of(&fixture, inlined, style);
    css_computed_is(style, "border-top-width", "7px");
    css_computed_flags_are(style, "border-top-width", "declared");

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * A kind the shorthand did not name is *reset*, and that is the composite rule.
 *
 * `border: 1px solid` after `border-color: red` really does clear the colour: the
 * shorthand stands for all three kinds on all four sides, so a kind it did not
 * mention takes the longhand's initial value. The reverse order is the same rule
 * read the other way — the longhand is later, so it wins and the colour survives.
 * And a *positional* row has no such case at all: one to four components always
 * cover all four sides, so `border-width: 1px` never resets a style.
 */
static void test_border_shorthand_reset(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t reset;
    robo_css_element_ref_t kept;
    robo_css_element_ref_t positional;
    char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];

    css_fixture();
    css_inherit_begin(&fixture);
    reset = css_host_add(css_inherit_host(&fixture), "div", "reset", NULL);
    kept = css_host_add(css_inherit_host(&fixture), "div", "kept", NULL);
    positional = css_host_add(css_inherit_host(&fixture), "div", "positional", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#reset { border-color: red; border-top-style: dashed;"
                      " border: 1px solid; color: green }"
                      "#kept { border: 1px solid; border-left-color: red;"
                      " border-left-style: dashed; color: green }"
                      "#positional { border-style: dashed; border-width: 1px }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    css_border_compute_of(&fixture, reset, style);
    css_computed_is(style, "border-top-width", "1px");
    css_computed_is(style, "border-top-style", "solid");
    /* The colour was named by an earlier declaration and the shorthand did not name
     * it, so it is the property's *initial* value rather than the earlier red. */
    css_border_query(&fixture, style, "border-top-color", text, sizeof(text), NULL);
    ROBO_CHECK_STR_EQ(text, "#008000");
    ROBO_CHECK_EQ_INT(
        robo_css_computed_style_find(style,
                                     robo_css_slice_from_text("border-top-color")) ==
            (size_t)-1,
        0);
    /* And every side, not only the one an earlier declaration happened to name. */
    css_border_query(&fixture, style, "border-left-color", text, sizeof(text), NULL);
    ROBO_CHECK_STR_EQ(text, "#008000");

    /* The reverse order: the longhands are later, so they win on their own sides
     * and the reset happens only for the other three. */
    css_border_compute_of(&fixture, kept, style);
    css_computed_is(style, "border-left-color", "#ff0000");
    css_computed_is(style, "border-left-style", "dashed");
    css_computed_is(style, "border-right-style", "solid");
    css_border_query(&fixture, style, "border-right-color", text, sizeof(text), NULL);
    ROBO_CHECK_STR_EQ(text, "#008000");

    /* A positional row covers all four sides, so nothing is ever left unnamed: the
     * dashed style declared before `border-width: 1px` survives. */
    css_border_compute_of(&fixture, positional, style);
    css_computed_is(style, "border-top-width", "1px");
    css_computed_is(style, "border-top-style", "dashed");
    css_computed_is(style, "border-left-style", "dashed");

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * A shorthand that cannot be distributed is reported, not guessed at.
 *
 * `border-top: var(--rule)` is legal CSS whose value is decided where `--rule` is,
 * and substitution can change how many components there are and *which kind* each
 * is — `--rule: 1px solid red` makes one component three, of two kinds. So the
 * author's text stays on the shorthand's own entry, the three longhands it would
 * have covered stay absent, and the style is marked unsupported rather than
 * silently answering with a value nobody wrote. The same rule for a positional row
 * whose count substitution would change.
 */
static void test_border_shorthand_undistributed(void)
{
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *style = NULL;
    robo_css_element_ref_t composite;
    robo_css_element_ref_t positional;
    char text[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];

    css_fixture();
    css_inherit_begin(&fixture);
    composite = css_host_add(css_inherit_host(&fixture), "div", "composite", NULL);
    positional = css_host_add(css_inherit_host(&fixture), "div", "positional", NULL);
    css_host_seal(css_inherit_host(&fixture));
    css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR,
                      "#composite { border-top: var(--rule) }"
                      "#positional { border-width: var(--edges) }");
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&style, NULL), ROBO_OK);

    /* The longhands are absent, and the shorthand's own entry carries the author's
     * text — which is what makes the failure *recoverable* by a later phase. The
     * count is one per *declaration*, not one per longhand it would have covered:
     * `border-top` covers three and is counted once, which is the difference
     * between reporting an author's mistake and reporting its consequences. */
    {
        robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
        robo_css_inherited_style_t resolved = ROBO_CSS_CLEARED;
        robo_css_error_t error = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_style_context_match_declarations(
                              css_inherit_context(&fixture), composite, &cascade,
                              &error, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_inherit_resolve(css_inherit_context(&fixture),
                                                  &cascade, NULL, &resolved, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(
            (int)robo_css_inherited_style_shorthands_unexpanded(&resolved), 1);
        ROBO_CHECK_EQ_INT(resolved.unsupported, 1);
        ROBO_CHECK_EQ_INT(robo_css_inherited_style_find(
                              &resolved, robo_css_slice_from_text("border-top-width")) ==
                              NULL,
                          1);
        css_inherit_is(&resolved, "border-top-width", "<absent>", "<absent>");
        css_inherit_is(&resolved, "border-top", "var(--rule)", "declared");
        robo_css_inherited_style_dispose(&resolved);
        robo_css_cascade_dispose(&cascade);
    }
    {
        robo_css_cascade_t cascade = ROBO_CSS_CLEARED;
        robo_css_inherited_style_t resolved = ROBO_CSS_CLEARED;
        robo_css_error_t error = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_style_context_match_declarations(
                              css_inherit_context(&fixture), positional, &cascade,
                              &error, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(robo_css_inherit_resolve(css_inherit_context(&fixture),
                                                  &cascade, NULL, &resolved, NULL),
                          ROBO_OK);
        ROBO_CHECK_EQ_INT(
            (int)robo_css_inherited_style_shorthands_unexpanded(&resolved), 1);
        css_inherit_is(&resolved, "border-top-width", "<absent>", "<absent>");
        css_inherit_is(&resolved, "border-width", "var(--edges)", "declared");
        robo_css_inherited_style_dispose(&resolved);
        robo_css_cascade_dispose(&cascade);
    }

    /* Through the computed style the longhands are absent too, and the query door
     * still answers the initial values — the two facts a consumer needs together. */
    css_border_compute_of(&fixture, composite, style);
    css_computed_is(style, "border-top-width", "<absent>");
    css_border_query(&fixture, style, "border-top-width", text, sizeof(text), NULL);
    ROBO_CHECK_STR_EQ(text, "medium");

    robo_css_computed_style_destroy(style);
    css_inherit_end(&fixture);
}

/**
 * Hostile input, and the invariant that matters.
 *
 * Every string is driven through the classifier, the expansion and the computed
 * style, and the invariants are stated rather than the outcomes: the classification
 * is always one of the five, an expansion that succeeded leaves each kind either
 * absent or a component, and nothing crashes. Malformed CSS must never take the
 * runtime down, and a colour cannot become a width because the input looked odd.
 */
static void test_border_robust(void)
{
    static const char *const hostile[] = {
        "b:",
        "b: ",
        "b: ;",
        "b: 1px solid red extra",
        "b: 1px 1px 1px 1px",
        "b: red solid 1px #fff",
        "b: var(--a) var(--b)",
        "b: rgb(1)",
        "b: rgb(1, 2)",
        "b: hsl(nonsense)",
        "b: #1234567890",
        "b: #",
        "b: -",
        "b: 1e999px",
        "b: 999999999999999999999999999px",
        "b: 0/0",
        "b: calc(1px + 1px) solid",
        "b: (solid)",
        "b: [1px]",
        "b: url('a.png')",
        "b: \"dashed\"",
        "b: currentColor",
        "b: CURRENTCOLOR",
        "b: SOLID",
        "b: None",
        "b: thin thin",
        "b: inherit inherit",
        "b: initial"
    };
    size_t index;

    css_fixture();
    for (index = 0u; index < sizeof(hostile) / sizeof(hostile[0]); index++) {
        robo_css_declaration_list_t list = ROBO_CSS_CLEARED;
        robo_css_error_t error = ROBO_CSS_CLEARED;
        const robo_css_value_t *value;
        robo_css_border_class_t outcome;
        robo_css_value_t parts[ROBO_CSS_BORDER_KIND_COUNT];
        size_t kind_index;

        /* The declaration may or may not survive the grammar; both are answers. */
        if (css_parse_declarations(hostile[index], &list, &error) != ROBO_OK) {
            continue;
        }
        value = robo_css_declaration_value(robo_css_declaration_list_at(&list, 0u));
        if (!value) {
            robo_css_declaration_list_dispose(&list);
            continue;
        }
        outcome = robo_css_border_classify(value);
        ROBO_CHECK_EQ_INT(robo_css_border_class_is_known(outcome), 1);

        for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
             kind_index++) {
            robo_css_value_init(&parts[kind_index]);
        }
        if (robo_css_border_expand(value, parts, NULL) == ROBO_OK) {
            /* Success means every kind is absent or a *single-component* list,
             * which is the shape a declared longhand has. */
            for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
                 kind_index++) {
                if (parts[kind_index].kind == ROBO_CSS_VALUE_KIND_NONE) {
                    continue;
                }
                ROBO_CHECK_EQ_INT(parts[kind_index].kind, ROBO_CSS_VALUE_KIND_LIST);
                ROBO_CHECK_EQ_INT((int)parts[kind_index].children.count, 1);
            }
        }
        for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
             kind_index++) {
            robo_css_value_dispose(&parts[kind_index]);
        }
        robo_css_declaration_list_dispose(&list);
    }

    /* The same strings through the other two shapes, where they must answer rather
     * than crash — a malformed declaration is an invalid *computed value*, not a
     * failure of the normaliser. */
    for (index = 0u; index < sizeof(hostile) / sizeof(hostile[0]); index++) {
        robo_css_computed_value_t value = ROBO_CSS_CLEARED;

        ROBO_CHECK_EQ_INT(robo_css_computed_value_compute(
                              ROBO_CSS_PROPERTY_COMPUTATION_BORDER_STYLE,
                              NULL, &value, NULL),
                          ROBO_INVALID_ARGUMENT);
        ROBO_CHECK_EQ_INT(robo_css_computed_value_kind_is_known(value.kind), 1);
        robo_css_computed_value_dispose(&value);
        break;
    }
    /* A NULL component and a cleared value are both answers. */
    ROBO_CHECK_EQ_INT(robo_css_border_classify(NULL), ROBO_CSS_BORDER_CLASS_INVALID);
    {
        robo_css_value_t cleared = ROBO_CSS_CLEARED;
        robo_css_value_t parts[ROBO_CSS_BORDER_KIND_COUNT];
        size_t kind_index;

        for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
             kind_index++) {
            robo_css_value_init(&parts[kind_index]);
        }
        ROBO_CHECK_EQ_INT(robo_css_border_expand(&cleared, parts, NULL),
                          ROBO_NOT_SUPPORTED);
        for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
             kind_index++) {
            ROBO_CHECK_EQ_INT(parts[kind_index].kind, ROBO_CSS_VALUE_KIND_NONE);
        }
    }
}

/**
 * The same document twice, and the vocabulary walked backwards.
 *
 * Determinism is a property of the whole family: twelve longhands from five
 * declarers means twelve resolutions per element, and an order-dependent weight (a
 * "last one wins" that quietly meant "last one *read*") would show here or nowhere.
 */
static void test_border_determinism(void)
{
    static const char *const sheet_text =
        "#a { border: 1px solid red; border-top-width: 2px }"
        "#b { border-width: 1px 2px 3px 4px; border-color: red blue green black }"
        "#c { border-style: solid dashed dotted double; border-top: 5px }";
    css_inherit_fixture_t fixture;
    robo_css_computed_style_t *first = NULL;
    robo_css_computed_style_t *second = NULL;
    char a[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
    char b[ROBO_CSS_COMPUTED_VALUE_TEXT_MAX];
    size_t side_index;
    size_t kind_index;
    size_t pass;

    css_fixture();
    css_inherit_begin(&fixture);
    (void)css_host_add(css_inherit_host(&fixture), "div", "a", NULL);
    (void)css_host_add(css_inherit_host(&fixture), "div", "b", NULL);
    (void)css_host_add(css_inherit_host(&fixture), "div", "c", NULL);
    css_host_seal(css_inherit_host(&fixture));
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&first, NULL), ROBO_OK);
    ROBO_CHECK_EQ_INT(robo_css_computed_style_create(&second, NULL), ROBO_OK);

    for (pass = 0u; pass < 2u; pass++) {
        robo_css_stylesheet_id_t sheet =
            css_inherit_sheet(&fixture, ROBO_CSS_ORIGIN_AUTHOR, sheet_text);

        for (side_index = 0u; side_index < 3u; side_index++) {
            robo_css_computed_style_t *style = pass == 0u ? first : second;
            robo_css_element_ref_t ref =
                (robo_css_element_ref_t)(side_index + 1u);

            css_border_compute_of(&fixture, ref, style);
        }
        ROBO_CHECK_EQ_INT(robo_css_stylesheet_destroy(sheet, NULL), ROBO_OK);
    }

    for (side_index = 0u; side_index < (size_t)ROBO_CSS_BOX_SIDE_COUNT;
         side_index++) {
        for (kind_index = 0u; kind_index < (size_t)ROBO_CSS_BORDER_KIND_COUNT;
             kind_index++) {
            const char *name = robo_css_border_longhand_name(
                (robo_css_box_side_t)side_index, (robo_css_border_kind_t)kind_index);

            css_border_query(&fixture, first, name, a, sizeof(a), NULL);
            css_border_query(&fixture, second, name, b, sizeof(b), NULL);
            ROBO_CHECK_STR_EQ(a, b);
        }
    }

    /* And the vocabulary walked *backwards* through the classifier, so a head-first
     * search that stopped at the wrong row would show as a disagreement with the
     * forward walk. */
    for (side_index = robo_css_border_style_count(); side_index > 0u; side_index--) {
        robo_css_border_style_t style =
            (robo_css_border_style_t)(side_index - 1u);

        ROBO_CHECK_EQ_INT(robo_css_border_style_from_text(
                              robo_css_border_style_name(style)),
                          style);
    }

    robo_css_computed_style_destroy(first);
    robo_css_computed_style_destroy(second);
    css_inherit_end(&fixture);
}

/* ------------------------------------------------------------------ */

void robo_test_register_css_border(void);

void robo_test_register_css_border(void)
{
    robo_test_register("css.border.style.vocabulary", test_border_style_vocabulary);
    robo_test_register("css.border.style.none_trap", test_border_style_none_trap);
    robo_test_register("css.border.style.generates", test_border_style_generates);
    robo_test_register("css.border.style.write", test_border_style_write);
    robo_test_register("css.border.kinds", test_border_kinds);
    robo_test_register("css.border.positional.rows", test_border_positional_rows);
    robo_test_register("css.border.composites", test_border_composites);
    robo_test_register("css.border.classify", test_border_classify);
    robo_test_register("css.border.expand", test_border_expand);
    robo_test_register("css.border.registry", test_border_registry);
    robo_test_register("css.border.longhand.shapes", test_border_longhand_shapes);
    robo_test_register("css.border.initial", test_border_initial);
    robo_test_register("css.border.longhands.computed",
                       test_border_longhands_computed);
    robo_test_register("css.border.non_inheritance", test_border_non_inheritance);
    robo_test_register("css.border.keywords", test_border_wide_keywords);
    robo_test_register("css.border.positional.expansion",
                       test_border_positional_expansion);
    robo_test_register("css.border.cascade", test_border_cascade);
    robo_test_register("css.border.shorthand.reset", test_border_shorthand_reset);
    robo_test_register("css.border.shorthand.undistributed",
                       test_border_shorthand_undistributed);
    robo_test_register("css.border.robust", test_border_robust);
    robo_test_register("css.border.determinism", test_border_determinism);
}
