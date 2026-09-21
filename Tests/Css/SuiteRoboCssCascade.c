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

static void TestCssCascadeLevelTable(void)
{
    /* Every level is reachable, and exactly one (origin, importance) pair
     * reaches it. A row that no pair produces is a level nothing can occupy. */
    static const CssOrigin origins[] = {
        RoboCssOriginRuntime, RoboCssOriginUser,
        RoboCssOriginAuthor, RoboCssOriginInline
    };
    int hits[RoboCssCascadeLevelCount];
    size_t originIndex;
    size_t level;
    int important;

    for (level = 0u; level < RoboCssCascadeLevelCount; level++) {
        hits[level] = 0;
    }
    for (originIndex = 0u; originIndex < sizeof(origins) / sizeof(origins[0]);
         originIndex++) {
        for (important = 0; important <= 1; important++) {
            CssCascadeLevel levelOf =
                CssCascadeLevelOf(origins[originIndex], important);

            RoboCheckEqInt(CssCascadeLevelIsKnown(levelOf), 1);
            RoboCheckEqInt(CssCascadeLevelHolds(levelOf, origins[originIndex],
                                                           important),
                              1);
            hits[(size_t)levelOf]++;
        }
    }
    for (level = 0u; level < RoboCssCascadeLevelCount; level++) {
        RoboCheckEqInt(hits[level], 1);
    }

    /* The spelled-out table, so a "correct by construction" mistake cannot hide
     * behind the loop above. */
    RoboCheckEqInt(CssCascadeLevelOf(RoboCssOriginRuntime, 0),
                      RoboCssCascadeLevelRuntime);
    RoboCheckEqInt(CssCascadeLevelOf(RoboCssOriginUser, 0),
                      RoboCssCascadeLevelUser);
    RoboCheckEqInt(CssCascadeLevelOf(RoboCssOriginAuthor, 0),
                      RoboCssCascadeLevelAuthor);
    RoboCheckEqInt(CssCascadeLevelOf(RoboCssOriginInline, 0),
                      RoboCssCascadeLevelInline);
    RoboCheckEqInt(CssCascadeLevelOf(RoboCssOriginAuthor, 1),
                      RoboCssCascadeLevelAuthorImportant);
    RoboCheckEqInt(CssCascadeLevelOf(RoboCssOriginInline, 1),
                      RoboCssCascadeLevelInlineImportant);
    RoboCheckEqInt(CssCascadeLevelOf(RoboCssOriginUser, 1),
                      RoboCssCascadeLevelUserImportant);
    RoboCheckEqInt(CssCascadeLevelOf(RoboCssOriginRuntime, 1),
                      RoboCssCascadeLevelRuntimeImportant);

    /* Origin NONE is not a level and must not borrow one. An unknown origin is
     * treated the same way rather than being clamped into the table. */
    RoboCheckEqInt(CssCascadeLevelOf(RoboCssOriginNone, 0),
                      RoboCssCascadeLevelNone);
    RoboCheckEqInt(CssCascadeLevelOf(RoboCssOriginNone, 1),
                      RoboCssCascadeLevelNone);
    RoboCheckEqInt(CssCascadeLevelOf((CssOrigin)99, 0),
                      RoboCssCascadeLevelNone);
    RoboCheckEqInt(CssCascadeLevelIsKnown(RoboCssCascadeLevelNone), 0);
    RoboCheckEqInt(CssCascadeLevelIsKnown(
                          (CssCascadeLevel)RoboCssCascadeLevelCount),
                      0);
    RoboCheckEqInt(CssCascadeLevelHolds(RoboCssCascadeLevelNone,
                                                   RoboCssOriginAuthor, 0),
                      0);
}

static void TestCssCascadeLevelOrder(void)
{
    /* The whole table, in one chain, weakest to strongest. */
    static const CssCascadeLevel ascending[] = {
        RoboCssCascadeLevelRuntime,
        RoboCssCascadeLevelUser,
        RoboCssCascadeLevelAuthor,
        RoboCssCascadeLevelInline,
        RoboCssCascadeLevelAuthorImportant,
        RoboCssCascadeLevelInlineImportant,
        RoboCssCascadeLevelUserImportant,
        RoboCssCascadeLevelRuntimeImportant
    };
    size_t i;
    size_t j;

    for (i = 0u; i < sizeof(ascending) / sizeof(ascending[0]); i++) {
        RoboCheckEqInt(CssCascadeLevelCompare(ascending[i], ascending[i]), 0);
        for (j = i + 1u; j < sizeof(ascending) / sizeof(ascending[0]); j++) {
            RoboCheckEqInt(CssCascadeLevelCompare(ascending[i], ascending[j]),
                              -1);
            RoboCheckEqInt(CssCascadeLevelCompare(ascending[j], ascending[i]),
                              1);
        }
    }
    /* An unknown level is weaker than every real one, and two unknowns are
     * equal — there is nothing to rank them by. */
    RoboCheckEqInt(CssCascadeLevelCompare(RoboCssCascadeLevelNone,
                                                     RoboCssCascadeLevelRuntime),
                      -1);
    RoboCheckEqInt(CssCascadeLevelCompare(RoboCssCascadeLevelRuntime,
                                                     RoboCssCascadeLevelNone),
                      1);
    RoboCheckEqInt(CssCascadeLevelCompare(RoboCssCascadeLevelNone,
                                                     (CssCascadeLevel)12),
                      0);
}

static void TestCssCascadeLevelNames(void)
{
    RoboCheckStrEq(CssCascadeLevelName(RoboCssCascadeLevelRuntime),
                      "runtime");
    RoboCheckStrEq(CssCascadeLevelName(RoboCssCascadeLevelUser), "user");
    RoboCheckStrEq(CssCascadeLevelName(RoboCssCascadeLevelAuthor),
                      "author");
    RoboCheckStrEq(CssCascadeLevelName(RoboCssCascadeLevelInline),
                      "inline");
    RoboCheckStrEq(
        CssCascadeLevelName(RoboCssCascadeLevelAuthorImportant),
        "author important");
    RoboCheckStrEq(
        CssCascadeLevelName(RoboCssCascadeLevelInlineImportant),
        "inline important");
    RoboCheckStrEq(
        CssCascadeLevelName(RoboCssCascadeLevelUserImportant),
        "user important");
    RoboCheckStrEq(
        CssCascadeLevelName(RoboCssCascadeLevelRuntimeImportant),
        "runtime important");
    /* Two "no level" names, because they are different facts: NONE is the
     * absence a caller can cause, and anything else is a value nobody wrote. */
    RoboCheckStrEq(CssCascadeLevelName(RoboCssCascadeLevelNone), "none");
    RoboCheckStrEq(CssCascadeLevelName((CssCascadeLevel)42),
                      "unknown");

    RoboCheckEqInt(CssCascadeVerdictIsKnown(RoboCssCascadeVerdictNone),
                      1);
    RoboCheckEqInt(CssCascadeVerdictIsKnown(RoboCssCascadeVerdictDecided),
                      1);
    RoboCheckEqInt(CssCascadeVerdictIsKnown(
                          (CssCascadeVerdict)2),
                      0);
    RoboCheckEqInt(CssCascadeVerdictIsKnown(
                          (CssCascadeVerdict)RoboCssCascadeVerdictCount),
                      0);
    /* 9.5's third verdict is gone: it described a question specificity answers,
     * so a value that used to be the third one is not a verdict any more. */
    RoboCheckEqInt(RoboCssCascadeVerdictCount, 2);
    RoboCheckStrEq(CssCascadeVerdictName(RoboCssCascadeVerdictNone),
                      "none");
    RoboCheckStrEq(CssCascadeVerdictName(RoboCssCascadeVerdictDecided),
                      "decided");

    RoboCheckStrEq(CssCascadeVerdictName((CssCascadeVerdict)77),
                      "unknown");
}

/* ------------------------------------------------------------------ */
/* The two decisions the table encodes                                 */
/* ------------------------------------------------------------------ */

static void TestCssCascadeLevelsInlineIsAuthorLevel(void)
{
    /* Inline normal beats author normal: in CSS a `style` attribute is an
     * author declaration with a specificity no selector reaches, and a level
     * above AUTHOR is how that is modelled without 9.6. */
    CssCascadeLevel author = CssCascadeLevelOf(RoboCssOriginAuthor, 0);
    CssCascadeLevel inlineNormal =
        CssCascadeLevelOf(RoboCssOriginInline, 0);

    RoboCheckEqInt(CssCascadeLevelCompare(inlineNormal, author), 1);
    /* It sits above the *other* origins' normal declarations too, because author
     * normal beats user normal and user normal beats the user agent's. */
    RoboCheckEqInt(CssCascadeLevelCompare(
                          inlineNormal, CssCascadeLevelOf(RoboCssOriginUser, 0)),
                      1);
    RoboCheckEqInt(
        CssCascadeLevelCompare(
            inlineNormal,
            CssCascadeLevelOf(RoboCssOriginRuntime, 0)),
        1);
    /* …and importance still outranks any normal declaration, inline included. */
    RoboCheckEqInt(CssCascadeLevelCompare(
                          inlineNormal,
                          CssCascadeLevelOf(RoboCssOriginAuthor, 1)),
                      -1);
}

static void TestCssCascadeLevelsImportantHalfIsNotAReversal(void)
{
    CssCascadeLevel authorImportant =
        CssCascadeLevelOf(RoboCssOriginAuthor, 1);
    CssCascadeLevel inlineImportant =
        CssCascadeLevelOf(RoboCssOriginInline, 1);
    CssCascadeLevel userImportant =
        CssCascadeLevelOf(RoboCssOriginUser, 1);
    CssCascadeLevel runtimeImportant =
        CssCascadeLevelOf(RoboCssOriginRuntime, 1);

    /* Importance raises a declaration above *every* normal one, its own origin's
     * included. */
    RoboCheckEqInt(
        CssCascadeLevelCompare(authorImportant,
                                       CssCascadeLevelOf(RoboCssOriginInline, 0)),
        1);
    /* Author-level important declarations are ordered author, then inline — the
     * same way round as their normal halves, which is the property a plain
     * "reverse the origins for important" rule would get backwards. */
    RoboCheckEqInt(CssCascadeLevelCompare(inlineImportant,
                                                     authorImportant),
                      1);
    RoboCheckEqInt(CssCascadeLevelCompare(userImportant, inlineImportant), 1);
    RoboCheckEqInt(CssCascadeLevelCompare(runtimeImportant, userImportant),
                      1);
}

void TestRegisterCssCascadeLevels(void);

void TestRegisterCssCascadeLevels(void)
{
    TestRegister("css.cascade.levels.table", TestCssCascadeLevelTable);
    TestRegister("css.cascade.levels.order", TestCssCascadeLevelOrder);
    TestRegister("css.cascade.levels.names", TestCssCascadeLevelNames);
    TestRegister("css.cascade.levels.inline_is_author_level",
                       TestCssCascadeLevelsInlineIsAuthorLevel);
    TestRegister("css.cascade.levels.important_half_is_not_a_reversal",
                       TestCssCascadeLevelsImportantHalfIsNotAReversal);
}
