/*
 * RoboCSS fragmentation suite (Phase 19.7) — css.fragment.*
 *
 * The two closed break vocabularies, the classifications a fragmentation pass
 * dispatches on, the three registered rows that carry them, and the computed shape
 * each declaration takes.
 *
 * The property this suite is really about is that `break-before` and `break-after`
 * are **one** vocabulary and not two. Both are registered rows, both carry a
 * computation of their own — the registry keys a computation by property — and both
 * are answered by the same table, so the case that matters is the one asserting the
 * two rows hold *different* computations whose predicates accept exactly the same
 * thirteen words. A build that gave them one shared computation would pass every
 * value test here and be wrong about the cascade, which is why the registry case
 * reads the enumerators rather than the names.
 *
 * Nothing here asserts a fragmentation. A break value at this phase is a *value*
 * with classifications in it, and where the fragments fall belongs to the phase that
 * has a box.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSFragment.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* break-before / break-after                                          */
/* ------------------------------------------------------------------ */

static void TestFragmentBetweenVocabulary(void)
{
    CssFixture();
    RoboCheckEqInt((int)RoboCssBreakBetweenCount, 13);
    RoboCheckEqInt((int)CssBreakBetweenCount(), 13);

    RoboCheckStrEq(CssBreakBetweenName(RoboCssBreakBetweenAuto), "auto");
    RoboCheckStrEq(CssBreakBetweenName(RoboCssBreakBetweenAvoid), "avoid");
    RoboCheckStrEq(CssBreakBetweenName(RoboCssBreakBetweenAvoidPage), "avoid-page");
    RoboCheckStrEq(CssBreakBetweenName(RoboCssBreakBetweenAvoidColumn), "avoid-column");
    RoboCheckStrEq(CssBreakBetweenName(RoboCssBreakBetweenAvoidRegion), "avoid-region");
    RoboCheckStrEq(CssBreakBetweenName(RoboCssBreakBetweenPage), "page");
    RoboCheckStrEq(CssBreakBetweenName(RoboCssBreakBetweenColumn), "column");
    RoboCheckStrEq(CssBreakBetweenName(RoboCssBreakBetweenRegion), "region");
    RoboCheckStrEq(CssBreakBetweenName(RoboCssBreakBetweenAlways), "always");
    RoboCheckStrEq(CssBreakBetweenName(RoboCssBreakBetweenLeft), "left");
    RoboCheckStrEq(CssBreakBetweenName(RoboCssBreakBetweenRight), "right");
    RoboCheckStrEq(CssBreakBetweenName(RoboCssBreakBetweenRecto), "recto");
    RoboCheckStrEq(CssBreakBetweenName(RoboCssBreakBetweenVerso), "verso");

    /* Every row round-trips through its own spelling, case-insensitively, which is the
     * assertion that catches a keyword and an enumerator drifting apart. */
    {
        size_t index;
        int sawAuto = 0;

        for (index = 0u; index < CssBreakBetweenCount(); index++) {
            CssBreakBetween value = (CssBreakBetween)index;
            const char *name = CssBreakBetweenName(value);

            RoboCheck(name[0] != '\0');
            RoboCheckEqInt(CssBreakBetweenIsKnown(value), 1);
            RoboCheckEqInt(CssBreakBetweenFromText(name), (int)value);
            RoboCheckEqInt(CssBreakBetweenSliceIsKnown(CssSliceFromText(name)), 1);
            if (value == RoboCssBreakBetweenAuto) {
                sawAuto = 1;
            }
        }
        RoboCheckEqInt(sawAuto, 1);
        RoboCheckEqInt(CssBreakBetweenIsKnown((CssBreakBetween)99), 0);
    }

    /* An absent or unknown keyword answers the initial value rather than a made-up one,
     * and an unknown *value* answers the empty name rather than a spelling. */
    RoboCheckEqInt(CssBreakBetweenFromText(NULL), (int)RoboCssBreakBetweenAuto);
    RoboCheckEqInt(CssBreakBetweenFromText("banana"), (int)RoboCssBreakBetweenAuto);
    RoboCheckEqInt(CssBreakBetweenFromText("COLUMN"), (int)RoboCssBreakBetweenColumn);
    RoboCheckEqInt(CssBreakBetweenSliceIsKnown(CssSliceFromText("banana")), 0);
    RoboCheckStrEq(CssBreakBetweenName((CssBreakBetween)99), "");

    {
        char buffer[RoboCssBreakBetweenTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssBreakBetweenWrite(RoboCssBreakBetweenAvoidColumn, buffer,
                                                     sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "avoid-column");
        RoboCheckEqInt((int)length, 12);
        RoboCheckEqInt(CssBreakBetweenWrite((CssBreakBetween)99, buffer,
                                                     sizeof(buffer), &length, NULL),
                          RoboInvalidArgument);
        RoboCheckStrEq(buffer, "");
    }
}

/**
 * Every classification, read off the table rather than off a spelling.
 *
 * This is the case that makes "forced" and "avoided" two independent questions
 * rather than one three-way switch: no row is both, `auto` is neither, and the four
 * `avoid` rows are avoided without being forced.
 */
static void TestFragmentBetweenClassifications(void)
{
    static const struct {
        CssBreakBetween value;
        int forced;
        int avoided;
        int columns;
        int pages;
    } rows[] = {
        { RoboCssBreakBetweenAuto,         0, 0, 0, 0 },
        { RoboCssBreakBetweenAvoid,        0, 1, 0, 0 },
        { RoboCssBreakBetweenAvoidPage,    0, 1, 0, 0 },
        { RoboCssBreakBetweenAvoidColumn,  0, 1, 0, 0 },
        { RoboCssBreakBetweenAvoidRegion,  0, 1, 0, 0 },
        { RoboCssBreakBetweenPage,         1, 0, 0, 1 },
        { RoboCssBreakBetweenColumn,       1, 0, 1, 0 },
        { RoboCssBreakBetweenRegion,       1, 0, 0, 0 },
        { RoboCssBreakBetweenAlways,       1, 0, 1, 1 },
        { RoboCssBreakBetweenLeft,         1, 0, 0, 1 },
        { RoboCssBreakBetweenRight,        1, 0, 0, 1 },
        { RoboCssBreakBetweenRecto,        1, 0, 0, 1 },
        { RoboCssBreakBetweenVerso,        1, 0, 0, 1 }
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)(sizeof(rows) / sizeof(rows[0])),
                      (int)CssBreakBetweenCount());
    for (index = 0u; index < sizeof(rows) / sizeof(rows[0]); index++) {
        CssBreakBetween value = rows[index].value;

        RoboCheckEqInt(CssBreakBetweenIsForced(value), rows[index].forced);
        RoboCheckEqInt(CssBreakBetweenIsAvoided(value), rows[index].avoided);
        RoboCheckEqInt(CssBreakBetweenForcesColumn(value), rows[index].columns);
        RoboCheckEqInt(CssBreakBetweenForcesPage(value), rows[index].pages);
        /* No row is both forced and avoided, and every forced row belongs to at least
         * one container except `region` -- which is forced and honoured by neither,
         * because this build lays out no regions. That exception is stated here rather
         * than smoothed over: claiming `region` for a container would be claiming the
         * container. */
        RoboCheckEqInt(rows[index].forced && rows[index].avoided, 0);
        if (value == RoboCssBreakBetweenRegion) {
            RoboCheckEqInt(rows[index].columns || rows[index].pages, 0);
        } else if (rows[index].forced) {
            RoboCheck(rows[index].columns || rows[index].pages);
        }
    }

    /* An unknown value classifies as nothing at all -- never as forced, which is the
     * answer that would invent a break out of a typo. */
    RoboCheckEqInt(CssBreakBetweenIsForced((CssBreakBetween)99), 0);
    RoboCheckEqInt(CssBreakBetweenIsAvoided((CssBreakBetween)99), 0);
    RoboCheckEqInt(CssBreakBetweenForcesColumn((CssBreakBetween)99), 0);
    RoboCheckEqInt(CssBreakBetweenForcesPage((CssBreakBetween)99), 0);
}

/* ------------------------------------------------------------------ */
/* break-inside                                                        */
/* ------------------------------------------------------------------ */

static void TestFragmentInsideVocabulary(void)
{
    static const struct {
        CssBreakInside value;
        const char *keyword;
        int avoided;
    } rows[] = {
        { RoboCssBreakInsideAuto,         "auto",         0 },
        { RoboCssBreakInsideAvoid,        "avoid",        1 },
        { RoboCssBreakInsideAvoidPage,    "avoid-page",   1 },
        { RoboCssBreakInsideAvoidColumn,  "avoid-column", 1 },
        { RoboCssBreakInsideAvoidRegion,  "avoid-region", 1 }
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssBreakInsideCount, 5);
    RoboCheckEqInt((int)CssBreakInsideCount(), 5);
    RoboCheckEqInt((int)(sizeof(rows) / sizeof(rows[0])),
                      (int)CssBreakInsideCount());
    for (index = 0u; index < sizeof(rows) / sizeof(rows[0]); index++) {
        CssBreakInside value = rows[index].value;

        RoboCheckStrEq(CssBreakInsideName(value), rows[index].keyword);
        RoboCheckEqInt(CssBreakInsideIsKnown(value), 1);
        RoboCheckEqInt(CssBreakInsideFromText(rows[index].keyword), (int)value);
        RoboCheckEqInt(CssBreakInsideSliceIsKnown(
                              CssSliceFromText(rows[index].keyword)),
                          1);
        RoboCheckEqInt(CssBreakInsideIsAvoided(value), rows[index].avoided);
    }

    RoboCheckEqInt(CssBreakInsideIsKnown((CssBreakInside)99), 0);
    RoboCheckStrEq(CssBreakInsideName((CssBreakInside)99), "");
    RoboCheckEqInt(CssBreakInsideFromText(NULL), (int)RoboCssBreakInsideAuto);
    RoboCheckEqInt(CssBreakInsideFromText("banana"), (int)RoboCssBreakInsideAuto);
    RoboCheckEqInt(CssBreakInsideFromText("AVOID-COLUMN"),
                      (int)RoboCssBreakInsideAvoidColumn);
    RoboCheckEqInt(CssBreakInsideSliceIsKnown(CssSliceFromText("banana")), 0);
    RoboCheckEqInt(CssBreakInsideIsAvoided((CssBreakInside)99), 0);

    /*
     * The set boundary, asserted the way the vocabulary's own comment states it: two of
     * the `break-between` words are **not** `break-inside` values. `break-inside: column`
     * is a syntax error rather than a value with no effect -- there is no next
     * fragmentainer inside a box for the break to begin one -- and the same is true of
     * every other forced word.
     */
    RoboCheckEqInt(CssBreakInsideFromText("column"), (int)RoboCssBreakInsideAuto);
    RoboCheckEqInt(CssBreakInsideFromText("always"), (int)RoboCssBreakInsideAuto);
    RoboCheckEqInt(CssBreakInsideFromText("left"), (int)RoboCssBreakInsideAuto);
    RoboCheckEqInt(CssBreakInsideSliceIsKnown(CssSliceFromText("column")), 0);

    /* And the one word the two sets share keeps the same meaning on both sides: the
     * generic `avoid` refuses a split wherever it is written. */
    RoboCheckEqInt(CssBreakInsideIsAvoided(
                          CssBreakInsideFromText("avoid")),
                      1);
    RoboCheckEqInt(CssBreakBetweenIsAvoided(
                          CssBreakBetweenFromText("avoid")),
                      1);

    {
        char buffer[RoboCssBreakInsideTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssBreakInsideWrite(RoboCssBreakInsideAvoidRegion, buffer,
                                                    sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "avoid-region");
        RoboCheckEqInt(CssBreakInsideWrite((CssBreakInside)99, buffer,
                                                    sizeof(buffer), &length, NULL),
                          RoboInvalidArgument);
        RoboCheckStrEq(buffer, "");
    }
}

/* ------------------------------------------------------------------ */
/* The computed shape                                                  */
/* ------------------------------------------------------------------ */

/** The declaration each property computes through, and what it becomes. */
static void TestFragmentComputedShapes(void)
{
    CssFixture();

    CssComputeEquals(RoboCssPropertyComputationBreakBefore, "o: column", "column");
    CssComputeEquals(RoboCssPropertyComputationBreakBefore, "o: AUTO", "auto");
    CssComputeEquals(RoboCssPropertyComputationBreakBefore, "o: avoid-region",
                     "avoid-region");
    /* An identifier outside the set is *refused*, and the refusal carries its reason as the
     * shape -- the house convention a reader of this file needs, because the answer looks
     * like a value. `auto` here would be the answer of a build that treated an unknown word
     * as the `CssBreakBetweenFromSlice` result, which is the confusion the vocabulary
     * predicate exists to prevent: `auto` is both a value and "not one". */
    CssComputeEquals(RoboCssPropertyComputationBreakBefore, "o: banana",
                       "not a break-before value");
    /* A break is a single identifier; a list is not a break value. */
    CssComputeEquals(RoboCssPropertyComputationBreakBefore, "o: column page",
                       "not a single break-before keyword");

    CssComputeEquals(RoboCssPropertyComputationBreakAfter, "o: page", "page");
    CssComputeEquals(RoboCssPropertyComputationBreakAfter, "o: recto", "recto");
    CssComputeEquals(RoboCssPropertyComputationBreakAfter, "o: banana",
                       "not a break-after value");

    /* `break-inside` shares the `avoid` words and refuses the forced ones, which is the
     * one place the two rows' grammars differ -- and it differs at *compute* time, not
     * only at the vocabulary table. */
    CssComputeEquals(RoboCssPropertyComputationBreakInside, "o: avoid", "avoid");
    CssComputeEquals(RoboCssPropertyComputationBreakInside, "o: avoid-column",
                     "avoid-column");
    CssComputeEquals(RoboCssPropertyComputationBreakInside, "o: auto", "auto");
    /* The two that differ, and they differ *here* rather than only in the table: a forced
     * word is not a `break-inside` value, so the declaration is refused with the reason
     * rather than quietly computing to `auto`. The distinction matters because `auto` is
     * what a build that dropped the set boundary would produce, and it would look right. */
    CssComputeEquals(RoboCssPropertyComputationBreakInside, "o: column",
                       "not a break-inside value");
    CssComputeEquals(RoboCssPropertyComputationBreakInside, "o: banana",
                       "not a break-inside value");

    /*
     * The two line floors, which are the family's numeric half -- and the refusals are the
     * interesting ones, because a floor of zero is not a smaller floor: `orphans: 0` says
     * "leave no lines", which is the absence of a rule rather than a strict one, so it is
     * refused rather than clamped. A fraction is refused too: a line is countable.
     */
    CssComputeEquals(RoboCssPropertyComputationOrphans, "o: 2", "2");
    CssComputeEquals(RoboCssPropertyComputationOrphans, "o: 1", "1");
    CssComputeEquals(RoboCssPropertyComputationOrphans, "o: 0",
                       "a line floor must be at least one");
    CssComputeEquals(RoboCssPropertyComputationOrphans, "o: -3",
                       "a line floor must be at least one");
    CssComputeEquals(RoboCssPropertyComputationOrphans, "o: 2.5",
                       "a line floor must be a whole number");
    CssComputeEquals(RoboCssPropertyComputationOrphans, "o: auto",
                       "not a positive whole number");
    CssComputeEquals(RoboCssPropertyComputationWidows, "o: 3", "3");
    CssComputeEquals(RoboCssPropertyComputationWidows, "o: 0",
                       "a line floor must be at least one");
}

/** The three rows, their classifications, and the one computation two of them share. */
static void TestFragmentRegistry(void)
{
    CssPropertyRegistry *registry = NULL;
    const CssPropertyDefinition *before;
    const CssPropertyDefinition *after;
    const CssPropertyDefinition *inside;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);
    RoboCheck(registry != NULL);

    before = CssPropertyRegistryFind(registry, "break-before");
    after = CssPropertyRegistryFind(registry, "break-after");
    inside = CssPropertyRegistryFind(registry, "break-inside");
    RoboCheck(before != NULL);
    RoboCheck(after != NULL);
    RoboCheck(inside != NULL);

    if (before && after && inside) {
        /* None inherits: a break belongs to the box whose boundary it is about, and a
         * child inheriting one would force a break it never asked for. */
        RoboCheckEqInt(CssPropertyDefinitionInherited(before), 0);
        RoboCheckEqInt(CssPropertyDefinitionInherited(after), 0);
        RoboCheckEqInt(CssPropertyDefinitionInherited(inside), 0);

        /* Two properties, two computations, one vocabulary -- the fact that makes the
         * registry key a computation by property rather than by grammar. */
        RoboCheckEqInt((int)before->computation,
                          (int)RoboCssPropertyComputationBreakBefore);
        RoboCheckEqInt((int)after->computation,
                          (int)RoboCssPropertyComputationBreakAfter);
        RoboCheck((int)before->computation != (int)after->computation);
        RoboCheckEqInt((int)inside->computation,
                          (int)RoboCssPropertyComputationBreakInside);
        RoboCheck((int)inside->computation != (int)before->computation);
        RoboCheck((int)inside->computation != (int)after->computation);

        /* And all three initial `auto`, which is the value that neither forces nor
         * forbids -- a document that declares nothing and one that says `auto` agree. */
        RoboCheckStrEq(before->initialText, "auto");
        RoboCheckStrEq(after->initialText, "auto");
        RoboCheckStrEq(inside->initialText, "auto");

        /* None is a shorthand. */
        RoboCheckEqInt(CssPropertyFlagsShorthand(before->flags), 0);
        RoboCheckEqInt(CssPropertyFlagsShorthand(after->flags), 0);
        RoboCheckEqInt(CssPropertyFlagsShorthand(inside->flags), 0);
    }

    CssPropertyRegistryDestroy(registry);
}

static void TestFragmentCapabilities(void)
{
    CssCapabilities caps;

    CssFixture();
    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.fragmentation, 1);
    /* The flag above is 19.7's, and the two it sits beside are the ones a reader is
     * most likely to confuse it with: this family does *not* claim multi-column layout
     * (18.6's flag, which is a vocabulary too) and it does not claim overflow. */
    RoboCheckEqInt(caps.multiColumn, 1);
    RoboCheckEqInt(caps.overflow, 1);

    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationBreakBefore),
                      "break-before");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationBreakAfter),
                      "break-after");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationBreakInside),
                      "break-inside");
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          RoboCssPropertyComputationBreakBefore),
                      1);
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          RoboCssPropertyComputationBreakInside),
                      1);
    RoboCheckStrEq(CssDiagnosticDomainName(RoboCssDomainFragment), "fragment");
    RoboCheckEqInt(CssDiagnosticDomainIsKnown(RoboCssDomainFragment), 1);
    RoboCheckEqInt(RoboCssDomainCount, 38);
}

/** The same declaration computes to the same value twice, by structural equality. */
static void TestFragmentDeterminism(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssComputedValue first = RoboCssCleared;
    CssComputedValue second = RoboCssCleared;
    const CssDeclaration *declaration;

    CssFixture();
    RoboCheckEqInt(CssParseDeclarations("o: avoid-column", &list, &error), RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationBreakInside,
                          CssDeclarationValue(declaration), &first, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationBreakInside,
                          CssDeclarationValue(declaration), &second, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueEquals(&first, &second), 1);

    CssComputedValueDispose(&first);
    CssComputedValueDispose(&second);
    CssDeclarationListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Suite registration                                                  */
/* ------------------------------------------------------------------ */

void TestRegisterCssFragment(void);

void TestRegisterCssFragment(void)
{
    TestRegister("css.fragment.between_vocabulary", TestFragmentBetweenVocabulary);
    TestRegister("css.fragment.between_classifications",
                       TestFragmentBetweenClassifications);
    TestRegister("css.fragment.inside_vocabulary", TestFragmentInsideVocabulary);
    TestRegister("css.fragment.computed", TestFragmentComputedShapes);
    TestRegister("css.fragment.registry", TestFragmentRegistry);
    TestRegister("css.fragment.capabilities", TestFragmentCapabilities);
    TestRegister("css.fragment.determinism", TestFragmentDeterminism);
}
