/*
 * RoboCSS cascade levels (Phase 9.5) — css.cascade.levels.*
 *
 * The level table is the one part of the cascade that is complete and final, so
 * it is the part worth pinning hardest. Everything here is a statement about
 * (origin, importance) → level, and about the order of the levels themselves.
 *
 * The assertions that matter are not "the function returned 4". They are the
 * *comparisons*: a table with the inline and important rows shuffled would still
 * return a level for every input, and only `user !important` losing to
 * `runtime !important` while beating `author !important` shows it up. The two
 * decisions the header calls out — inline sits above the origin it belongs to,
 * and the important half is not a plain reversal — each get a test of their own.
 */

#include "RoboCssTestSupport.h"

/* ------------------------------------------------------------------ */
/* The table itself                                                    */
/* ------------------------------------------------------------------ */

static void test_css_cascade_level_table(void)
{
    /* Every level is reachable, and exactly one (origin, importance) pair
     * reaches it. A row that no pair produces is a level nothing can occupy. */
    static const robo_css_origin_t origins[] = {
        ROBO_CSS_ORIGIN_RUNTIME, ROBO_CSS_ORIGIN_USER,
        ROBO_CSS_ORIGIN_AUTHOR, ROBO_CSS_ORIGIN_INLINE
    };
    int hits[ROBO_CSS_CASCADE_LEVEL_COUNT];
    size_t origin_index;
    size_t level;
    int important;

    for (level = 0u; level < ROBO_CSS_CASCADE_LEVEL_COUNT; level++) {
        hits[level] = 0;
    }
    for (origin_index = 0u; origin_index < sizeof(origins) / sizeof(origins[0]);
         origin_index++) {
        for (important = 0; important <= 1; important++) {
            robo_css_cascade_level_t level_of =
                robo_css_cascade_level_of(origins[origin_index], important);

            ROBO_CHECK_EQ_INT(robo_css_cascade_level_is_known(level_of), 1);
            ROBO_CHECK_EQ_INT(robo_css_cascade_level_holds(level_of, origins[origin_index],
                                                           important),
                              1);
            hits[(size_t)level_of]++;
        }
    }
    for (level = 0u; level < ROBO_CSS_CASCADE_LEVEL_COUNT; level++) {
        ROBO_CHECK_EQ_INT(hits[level], 1);
    }

    /* The spelled-out table, so a "correct by construction" mistake cannot hide
     * behind the loop above. */
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_of(ROBO_CSS_ORIGIN_RUNTIME, 0),
                      ROBO_CSS_CASCADE_LEVEL_RUNTIME);
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_of(ROBO_CSS_ORIGIN_USER, 0),
                      ROBO_CSS_CASCADE_LEVEL_USER);
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_of(ROBO_CSS_ORIGIN_AUTHOR, 0),
                      ROBO_CSS_CASCADE_LEVEL_AUTHOR);
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_of(ROBO_CSS_ORIGIN_INLINE, 0),
                      ROBO_CSS_CASCADE_LEVEL_INLINE);
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_of(ROBO_CSS_ORIGIN_AUTHOR, 1),
                      ROBO_CSS_CASCADE_LEVEL_AUTHOR_IMPORTANT);
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_of(ROBO_CSS_ORIGIN_INLINE, 1),
                      ROBO_CSS_CASCADE_LEVEL_INLINE_IMPORTANT);
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_of(ROBO_CSS_ORIGIN_USER, 1),
                      ROBO_CSS_CASCADE_LEVEL_USER_IMPORTANT);
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_of(ROBO_CSS_ORIGIN_RUNTIME, 1),
                      ROBO_CSS_CASCADE_LEVEL_RUNTIME_IMPORTANT);

    /* Origin NONE is not a level and must not borrow one. An unknown origin is
     * treated the same way rather than being clamped into the table. */
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_of(ROBO_CSS_ORIGIN_NONE, 0),
                      ROBO_CSS_CASCADE_LEVEL_NONE);
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_of(ROBO_CSS_ORIGIN_NONE, 1),
                      ROBO_CSS_CASCADE_LEVEL_NONE);
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_of((robo_css_origin_t)99, 0),
                      ROBO_CSS_CASCADE_LEVEL_NONE);
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_is_known(ROBO_CSS_CASCADE_LEVEL_NONE), 0);
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_is_known(
                          (robo_css_cascade_level_t)ROBO_CSS_CASCADE_LEVEL_COUNT),
                      0);
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_holds(ROBO_CSS_CASCADE_LEVEL_NONE,
                                                   ROBO_CSS_ORIGIN_AUTHOR, 0),
                      0);
}

static void test_css_cascade_level_order(void)
{
    /* The whole table, in one chain, weakest to strongest. */
    static const robo_css_cascade_level_t ascending[] = {
        ROBO_CSS_CASCADE_LEVEL_RUNTIME,
        ROBO_CSS_CASCADE_LEVEL_USER,
        ROBO_CSS_CASCADE_LEVEL_AUTHOR,
        ROBO_CSS_CASCADE_LEVEL_INLINE,
        ROBO_CSS_CASCADE_LEVEL_AUTHOR_IMPORTANT,
        ROBO_CSS_CASCADE_LEVEL_INLINE_IMPORTANT,
        ROBO_CSS_CASCADE_LEVEL_USER_IMPORTANT,
        ROBO_CSS_CASCADE_LEVEL_RUNTIME_IMPORTANT
    };
    size_t i;
    size_t j;

    for (i = 0u; i < sizeof(ascending) / sizeof(ascending[0]); i++) {
        ROBO_CHECK_EQ_INT(robo_css_cascade_level_compare(ascending[i], ascending[i]), 0);
        for (j = i + 1u; j < sizeof(ascending) / sizeof(ascending[0]); j++) {
            ROBO_CHECK_EQ_INT(robo_css_cascade_level_compare(ascending[i], ascending[j]),
                              -1);
            ROBO_CHECK_EQ_INT(robo_css_cascade_level_compare(ascending[j], ascending[i]),
                              1);
        }
    }
    /* An unknown level is weaker than every real one, and two unknowns are
     * equal — there is nothing to rank them by. */
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_compare(ROBO_CSS_CASCADE_LEVEL_NONE,
                                                     ROBO_CSS_CASCADE_LEVEL_RUNTIME),
                      -1);
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_compare(ROBO_CSS_CASCADE_LEVEL_RUNTIME,
                                                     ROBO_CSS_CASCADE_LEVEL_NONE),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_compare(ROBO_CSS_CASCADE_LEVEL_NONE,
                                                     (robo_css_cascade_level_t)12),
                      0);
}

static void test_css_cascade_level_names(void)
{
    ROBO_CHECK_STR_EQ(robo_css_cascade_level_name(ROBO_CSS_CASCADE_LEVEL_RUNTIME),
                      "runtime");
    ROBO_CHECK_STR_EQ(robo_css_cascade_level_name(ROBO_CSS_CASCADE_LEVEL_USER), "user");
    ROBO_CHECK_STR_EQ(robo_css_cascade_level_name(ROBO_CSS_CASCADE_LEVEL_AUTHOR),
                      "author");
    ROBO_CHECK_STR_EQ(robo_css_cascade_level_name(ROBO_CSS_CASCADE_LEVEL_INLINE),
                      "inline");
    ROBO_CHECK_STR_EQ(
        robo_css_cascade_level_name(ROBO_CSS_CASCADE_LEVEL_AUTHOR_IMPORTANT),
        "author important");
    ROBO_CHECK_STR_EQ(
        robo_css_cascade_level_name(ROBO_CSS_CASCADE_LEVEL_INLINE_IMPORTANT),
        "inline important");
    ROBO_CHECK_STR_EQ(
        robo_css_cascade_level_name(ROBO_CSS_CASCADE_LEVEL_USER_IMPORTANT),
        "user important");
    ROBO_CHECK_STR_EQ(
        robo_css_cascade_level_name(ROBO_CSS_CASCADE_LEVEL_RUNTIME_IMPORTANT),
        "runtime important");
    /* Two "no level" names, because they are different facts: NONE is the
     * absence a caller can cause, and anything else is a value nobody wrote. */
    ROBO_CHECK_STR_EQ(robo_css_cascade_level_name(ROBO_CSS_CASCADE_LEVEL_NONE), "none");
    ROBO_CHECK_STR_EQ(robo_css_cascade_level_name((robo_css_cascade_level_t)42),
                      "unknown");

    ROBO_CHECK_EQ_INT(robo_css_cascade_verdict_is_known(ROBO_CSS_CASCADE_VERDICT_NONE),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_cascade_verdict_is_known(ROBO_CSS_CASCADE_VERDICT_DECIDED),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_cascade_verdict_is_known(
                          ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_cascade_verdict_is_known(
                          (robo_css_cascade_verdict_t)ROBO_CSS_CASCADE_VERDICT_COUNT),
                      0);
    ROBO_CHECK_STR_EQ(robo_css_cascade_verdict_name(ROBO_CSS_CASCADE_VERDICT_NONE),
                      "none");
    ROBO_CHECK_STR_EQ(robo_css_cascade_verdict_name(ROBO_CSS_CASCADE_VERDICT_DECIDED),
                      "decided");
    ROBO_CHECK_STR_EQ(
        robo_css_cascade_verdict_name(ROBO_CSS_CASCADE_VERDICT_UNDECIDED_SPECIFICITY),
        "undecided-specificity");
    ROBO_CHECK_STR_EQ(robo_css_cascade_verdict_name((robo_css_cascade_verdict_t)77),
                      "unknown");
}

/* ------------------------------------------------------------------ */
/* The two decisions the table encodes                                 */
/* ------------------------------------------------------------------ */

static void test_css_cascade_levels_inline_is_author_level(void)
{
    /* Inline normal beats author normal: in CSS a `style` attribute is an
     * author declaration with a specificity no selector reaches, and a level
     * above AUTHOR is how that is modelled without 9.6. */
    robo_css_cascade_level_t author = robo_css_cascade_level_of(ROBO_CSS_ORIGIN_AUTHOR, 0);
    robo_css_cascade_level_t inline_normal =
        robo_css_cascade_level_of(ROBO_CSS_ORIGIN_INLINE, 0);

    ROBO_CHECK_EQ_INT(robo_css_cascade_level_compare(inline_normal, author), 1);
    /* It sits above the *other* origins' normal declarations too, because author
     * normal beats user normal and user normal beats the user agent's. */
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_compare(
                          inline_normal, robo_css_cascade_level_of(ROBO_CSS_ORIGIN_USER, 0)),
                      1);
    ROBO_CHECK_EQ_INT(
        robo_css_cascade_level_compare(
            inline_normal,
            robo_css_cascade_level_of(ROBO_CSS_ORIGIN_RUNTIME, 0)),
        1);
    /* …and importance still outranks any normal declaration, inline included. */
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_compare(
                          inline_normal,
                          robo_css_cascade_level_of(ROBO_CSS_ORIGIN_AUTHOR, 1)),
                      -1);
}

static void test_css_cascade_levels_important_half_is_not_a_reversal(void)
{
    robo_css_cascade_level_t author_important =
        robo_css_cascade_level_of(ROBO_CSS_ORIGIN_AUTHOR, 1);
    robo_css_cascade_level_t inline_important =
        robo_css_cascade_level_of(ROBO_CSS_ORIGIN_INLINE, 1);
    robo_css_cascade_level_t user_important =
        robo_css_cascade_level_of(ROBO_CSS_ORIGIN_USER, 1);
    robo_css_cascade_level_t runtime_important =
        robo_css_cascade_level_of(ROBO_CSS_ORIGIN_RUNTIME, 1);

    /* Importance raises a declaration above *every* normal one, its own origin's
     * included. */
    ROBO_CHECK_EQ_INT(
        robo_css_cascade_level_compare(author_important,
                                       robo_css_cascade_level_of(ROBO_CSS_ORIGIN_INLINE, 0)),
        1);
    /* Author-level important declarations are ordered author, then inline — the
     * same way round as their normal halves, which is the property a plain
     * "reverse the origins for important" rule would get backwards. */
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_compare(inline_important,
                                                     author_important),
                      1);
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_compare(user_important, inline_important), 1);
    ROBO_CHECK_EQ_INT(robo_css_cascade_level_compare(runtime_important, user_important),
                      1);
}

void robo_test_register_css_cascade_levels(void);

void robo_test_register_css_cascade_levels(void)
{
    robo_test_register("css.cascade.levels.table", test_css_cascade_level_table);
    robo_test_register("css.cascade.levels.order", test_css_cascade_level_order);
    robo_test_register("css.cascade.levels.names", test_css_cascade_level_names);
    robo_test_register("css.cascade.levels.inline_is_author_level",
                       test_css_cascade_levels_inline_is_author_level);
    robo_test_register("css.cascade.levels.important_half_is_not_a_reversal",
                       test_css_cascade_levels_important_half_is_not_a_reversal);
}
