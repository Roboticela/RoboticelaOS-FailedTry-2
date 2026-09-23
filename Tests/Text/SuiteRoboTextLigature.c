/*
 * RoboText's ligature layer (Phase 12.12) — `text.ligature.*`.
 *
 * ## What this suite can prove, and with which font
 *
 * Two halves, and they need two different faces.
 *
 * The **policy** half is about data: five tags, the stylesheet's vocabulary, and the rule that an explicit
 * setting outranks a default. None of that needs a font with ligatures in it, so it runs against the shipped
 * pack like every other Text suite does — and the wiring half (`options.ligatures` reaching a run's feature
 * set) is checked there too, because that is where a policy either arrives or does not.
 *
 * The **font's answer** half needs a face that actually substitutes, and here the shipped pack is
 * interestingly honest: Inter ships `calt`, `ccmp` and `dlig` but **no `liga`**. So the standard-ligature
 * behaviour is 12.10's own fixture (`Tests/Text/SuiteRoboTextGsub.c`, where `f f i` is a glyph because the
 * *font bytes* say so), and what this suite takes from the real pack is the one discretionary ligature Inter
 * does carry: `!?` → `‽` (U+203D) and `!!`/`??` → `⁉` (U+2E18).
 *
 * That makes `!?` the sharpest test in the file, because it is real end-to-end evidence for the whole claim
 * rather than a fixture agreeing with itself:
 *
 *   - with the default policy `dlig` is **off** and the run is two glyphs;
 *   - with `RoboTextLigatureVariantDiscretionary` it is **one** glyph whose source range covers both
 *     scalars, whose component count is two, and whose advance is the substitute's own `hmtx` advance —
 *     which the suite checks by measuring the interrobang on its own and getting the same width;
 *   - and the source text is untouched: both scalars still map to that one glyph.
 *
 * ## Why the source mapping is asserted here rather than in a later editing phase
 *
 * A ligature is the first thing in the pipeline that makes "one glyph" and "one character" disagree, and
 * 12.12 §"ligature cluster semantics" is explicit that the data a caret will need must survive even though
 * caret behaviour itself is later work. So the suite checks the two facts a caret will be built on: scalar 0
 * and scalar 1 of `!?` both answer *the ligature glyph*, and that glyph answers `[0..2)`.
 */

#include "RoboTextTestSupport.h"

#include "RoboText/RoboTextLigature.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static RoboTextView View(const char *text)
{
    return RoboTextViewMake(text, strlen(text));
}

/** Shape @p text with @p options, asserting success. */
static RoboTextShapeResult *ShapeWith(const TextFixture *fixture,
                                      const RoboTextShapeOptions *options,
                                      const char *text)
{
    RoboTextShapeResult *result = NULL;

    RoboCheckEqInt((int)RoboTextShape(View(text), &fixture->font, options, &result, NULL), (int)RoboOk);
    RoboCheckMsg(result != NULL, "shaping produced no result");
    return result;
}

/** The single glyph run of a text this suite knows is one run, or NULL. */
static const RoboTextGlyphRun *OnlyRun(const RoboTextShapeResult *result)
{
    RoboCheckEqInt((int)RoboTextShapeResultGlyphRunCount(result), 1);
    return RoboTextShapeResultGlyphRun(result, 0u);
}

/** The interrobang, UTF-8: the glyph Inter's `dlig` substitutes for `!?`. */
#define TextInterrobang "\xE2\x80\xBD"

/* ------------------------------------------------------------------ */
/* The vocabulary                                                      */
/* ------------------------------------------------------------------ */

static void TestLigatureVocabulary(void)
{
    RoboTextLigatureKind kind = RoboTextLigatureKindCount;

    RoboCheckEqInt((int)RoboTextLigatureKindCount, 5);

    /* Every kind has a name and a tag, and the tag maps back to the same kind. */
    {
        static const char *const kTags[RoboTextLigatureKindCount] = {"liga", "clig", "rlig", "dlig", "hlig"};
        static const char *const kNames[RoboTextLigatureKindCount] = {"standard", "contextual", "required",
                                                                      "discretionary", "historical"};

        for (size_t index = 0u; index < RoboTextLigatureKindCount; index++) {
            RoboTextLigatureKind back = RoboTextLigatureKindCount;

            RoboCheck(RoboTextLigatureKindIsKnown((RoboTextLigatureKind)index));
            RoboCheckStrEq(RoboTextLigatureKindTag((RoboTextLigatureKind)index), kTags[index]);
            RoboCheckStrEq(RoboTextLigatureKindName((RoboTextLigatureKind)index), kNames[index]);
            RoboCheck(RoboTextLigatureKindForTag(kTags[index], &back));
            RoboCheckEqInt((int)back, (int)index);
        }
    }

    /* The five tags are the five kinds, and nothing else is a ligature kind: `kern` is not, and neither is
     * a tag that is the right length but the wrong name. */
    RoboCheck(!RoboTextLigatureKindForTag("kern", &kind));
    RoboCheck(!RoboTextLigatureKindForTag("LIGA", &kind));
    RoboCheck(!RoboTextLigatureKindForTag("liga!", &kind));
    RoboCheck(!RoboTextLigatureKindForTag("", &kind));
    RoboCheck(!RoboTextLigatureKindForTag(NULL, &kind));
    RoboCheckEqInt((int)kind, (int)RoboTextLigatureKindCount); /* Untouched. */
    /* A caller that only wants the answer may pass no out. */
    RoboCheck(RoboTextLigatureKindForTag("rlig", NULL));

    RoboCheck(!RoboTextLigatureKindIsKnown(RoboTextLigatureKindCount));
    RoboCheckStrEq(RoboTextLigatureKindTag(RoboTextLigatureKindCount), "");
    RoboCheckStrEq(RoboTextLigatureKindName(RoboTextLigatureKindCount), "?");

    /* Variants: eight, each with its CSS spelling, and the spelling is the `font-variant-ligatures` value. */
    {
        static const char *const kVariants[RoboTextLigatureVariantCount] = {
            "normal", "none", "common-ligatures", "no-common-ligatures",
            "contextual", "no-contextual", "discretionary-ligatures", "historical-ligatures"};

        RoboCheckEqInt((int)RoboTextLigatureVariantCount, 8);
        for (size_t index = 0u; index < RoboTextLigatureVariantCount; index++) {
            RoboCheck(RoboTextLigatureVariantIsKnown((RoboTextLigatureVariant)index));
            RoboCheckStrEq(RoboTextLigatureVariantName((RoboTextLigatureVariant)index), kVariants[index]);
        }
    }
    RoboCheck(!RoboTextLigatureVariantIsKnown(RoboTextLigatureVariantCount));
    RoboCheckStrEq(RoboTextLigatureVariantName(RoboTextLigatureVariantCount), "?");
}

/* ------------------------------------------------------------------ */
/* The policy                                                          */
/* ------------------------------------------------------------------ */

static void TestLigaturePolicy(void)
{
    RoboTextLigaturePolicy policy;
    RoboTextLigaturePolicy other;
    char line[128];
    size_t length = 0u;

    /* `Init` states nothing at all, which is not the same as turning everything off. */
    RoboTextLigaturePolicyInit(&policy);
    for (size_t index = 0u; index < RoboTextLigatureKindCount; index++) {
        RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, (RoboTextLigatureKind)index),
                       (int)RoboTextFeatureDefault);
    }
    RoboCheck(RoboTextLigaturePolicyIsValid(&policy));
    RoboCheckEqInt((int)RoboTextLigaturePolicyWrite(&policy, line, sizeof(line), &length), (int)RoboOk);
    RoboCheckStrEq(line, "(nothing stated)");
    RoboCheckEqInt((int)length, (int)strlen("(nothing stated)"));

    /* The default: the three that make text readable on, the two that decorate it off. */
    RoboTextLigaturePolicyDefault(&policy);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureStandard), (int)RoboTextFeatureOn);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureContextual), (int)RoboTextFeatureOn);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureRequired), (int)RoboTextFeatureOn);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureDiscretionary),
                   (int)RoboTextFeatureOff);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureHistorical),
                   (int)RoboTextFeatureOff);
    RoboCheckEqInt((int)RoboTextLigaturePolicyWrite(&policy, line, sizeof(line), &length), (int)RoboOk);
    RoboCheckStrEq(line, "liga=on clig=on rlig=on dlig=off hlig=off");

    /* Setting one kind leaves the others where they were, and a state that is not one of the three is
     * refused rather than stored. */
    RoboTextLigaturePolicyDefault(&other);
    RoboCheckEqInt((int)RoboTextLigaturePolicySet(&other, RoboTextLigatureDiscretionary, RoboTextFeatureOn),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&other, RoboTextLigatureDiscretionary),
                   (int)RoboTextFeatureOn);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&other, RoboTextLigatureStandard), (int)RoboTextFeatureOn);
    RoboCheck(!RoboTextLigaturePolicyEqual(&policy, &other));
    RoboCheckEqInt((int)RoboTextLigaturePolicySet(&other, RoboTextLigatureDiscretionary, RoboTextFeatureOff),
                   (int)RoboOk);
    RoboCheck(RoboTextLigaturePolicyEqual(&policy, &other));

    RoboCheckEqInt((int)RoboTextLigaturePolicySet(&other, RoboTextLigatureKindCount, RoboTextFeatureOn),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextLigaturePolicySet(&other, RoboTextLigatureStandard,
                                                  (RoboTextFeatureState)7),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextLigaturePolicySet(NULL, RoboTextLigatureStandard, RoboTextFeatureOn),
                   (int)RoboInvalidArgument);

    /* A policy holding a state that is not one of the three is not valid, which is what
     * `RoboTextShapeOptionsIsValid` asks. */
    RoboCheck(RoboTextLigaturePolicyIsValid(&policy));
    policy.kinds[RoboTextLigatureHistorical] = (RoboTextFeatureState)9;
    RoboCheck(!RoboTextLigaturePolicyIsValid(&policy));
    RoboCheck(!RoboTextLigaturePolicyIsValid(NULL));
    RoboCheck(!RoboTextLigaturePolicyEqual(&policy, NULL));
    RoboCheck(RoboTextLigaturePolicyEqual(NULL, NULL));

    /* Unknown kinds answer `Default` rather than reading past the array. */
    RoboTextLigaturePolicyInit(&policy);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureKindCount),
                   (int)RoboTextFeatureDefault);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(NULL, RoboTextLigatureStandard), (int)RoboTextFeatureDefault);

    /* A dump that does not fit is refused rather than truncated: a shorter line reads as a different
     * policy. */
    RoboCheckEqInt((int)RoboTextLigaturePolicyWrite(&other, line, 8u, &length), (int)RoboNotSupported);
    RoboCheckEqInt((int)RoboTextLigaturePolicyWrite(NULL, line, sizeof(line), &length),
                   (int)RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* Variants: the stylesheet's vocabulary                               */
/* ------------------------------------------------------------------ */

static void TestLigatureVariants(void)
{
    RoboTextLigaturePolicy policy;
    RoboTextLigaturePolicy composed;

    /* `normal` states nothing, and applying it after something else keeps it: a stylesheet that says
     * `normal` is saying "whatever the default is", not "reset". */
    RoboTextLigaturePolicyDefault(&policy);
    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantNormal, &policy);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureStandard), (int)RoboTextFeatureOn);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureDiscretionary),
                   (int)RoboTextFeatureOff);

    /* `none` turns the optional kinds off **and leaves `rlig` as it was**: required ligatures are not
     * optional typography, and a declaration that switched them off would produce wrong letters in Arabic
     * rather than a plainer `fi`. */
    RoboTextLigaturePolicyDefault(&policy);
    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantNone, &policy);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureStandard), (int)RoboTextFeatureOff);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureContextual), (int)RoboTextFeatureOff);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureDiscretionary),
                   (int)RoboTextFeatureOff);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureHistorical),
                   (int)RoboTextFeatureOff);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureRequired), (int)RoboTextFeatureOn);

    /* And from `Init` — where nothing was stated — `none` says nothing about `rlig` either, so the font's
     * own record still decides. */
    RoboTextLigaturePolicyInit(&policy);
    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantNone, &policy);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureRequired),
                   (int)RoboTextFeatureDefault);

    /* Each single-kind variant states its own kind and no other. */
    RoboTextLigaturePolicyInit(&policy);
    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantCommon, &policy);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureStandard), (int)RoboTextFeatureOn);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureContextual),
                   (int)RoboTextFeatureDefault);

    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantNoCommon, &policy);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureStandard), (int)RoboTextFeatureOff);

    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantContextual, &policy);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureContextual), (int)RoboTextFeatureOn);
    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantNoContextual, &policy);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureContextual),
                   (int)RoboTextFeatureOff);

    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantDiscretionary, &policy);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureDiscretionary),
                   (int)RoboTextFeatureOn);
    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantHistorical, &policy);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&policy, RoboTextLigatureHistorical),
                   (int)RoboTextFeatureOn);

    /* Two declarations compose, which is what makes the vocabulary usable a declaration at a time:
     * `font-variant-ligatures: none discretionary-ligatures`. */
    RoboTextLigaturePolicyInit(&composed);
    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantNone, &composed);
    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantDiscretionary, &composed);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&composed, RoboTextLigatureStandard),
                   (int)RoboTextFeatureOff);
    RoboCheckEqInt((int)RoboTextLigaturePolicyState(&composed, RoboTextLigatureDiscretionary),
                   (int)RoboTextFeatureOn);

    /* An unknown variant writes nothing. */
    {
        RoboTextLigaturePolicy empty;

        RoboTextLigaturePolicyInit(&empty);
        RoboTextLigaturePolicyInit(&policy);
        RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantCount, &policy);
        RoboCheck(RoboTextLigaturePolicyEqual(&policy, &empty));
    }
    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantNormal, NULL); /* No crash. */
}

/* ------------------------------------------------------------------ */
/* Policy -> feature set                                               */
/* ------------------------------------------------------------------ */

static void TestLigatureFeatureSet(void)
{
    RoboTextLigaturePolicy policy;
    RoboTextFeatureSet set;
    RoboTextFeatureSet explicitSet;
    const RoboTextFeatureSetting *setting;

    /* `Apply` writes every stated kind, which is five entries for the default policy. */
    RoboTextFeatureSetInit(&set);
    RoboTextLigaturePolicyDefault(&policy);
    RoboCheckEqInt((int)RoboTextLigaturePolicyApply(&policy, &set), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFeatureSetCount(&set), 5);
    setting = RoboTextFeatureSetFind(&set, "liga");
    RoboCheck(setting != NULL);
    RoboCheckEqInt((int)setting->state, (int)RoboTextFeatureOn);
    setting = RoboTextFeatureSetFind(&set, "rlig");
    RoboCheck(setting != NULL);
    RoboCheckEqInt((int)setting->state, (int)RoboTextFeatureOn);
    setting = RoboTextFeatureSetFind(&set, "dlig");
    RoboCheck(setting != NULL);
    RoboCheckEqInt((int)setting->state, (int)RoboTextFeatureOff);
    RoboCheck(RoboTextFeatureSetFind(&set, "kern") == NULL); /* A policy mentions five tags and no more. */

    /* A policy that states nothing writes nothing. */
    RoboTextFeatureSetInit(&set);
    RoboTextLigaturePolicyInit(&policy);
    RoboCheckEqInt((int)RoboTextLigaturePolicyApply(&policy, &set), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFeatureSetCount(&set), 0);

    /* `Apply` is the layer that owns the whole answer: it overwrites. */
    RoboTextFeatureSetInit(&explicitSet);
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&explicitSet, "liga", RoboTextFeatureOff, 0u), (int)RoboOk);
    RoboTextLigaturePolicyDefault(&policy);
    RoboCheckEqInt((int)RoboTextLigaturePolicyApply(&policy, &explicitSet), (int)RoboOk);
    setting = RoboTextFeatureSetFind(&explicitSet, "liga");
    RoboCheckEqInt((int)setting->state, (int)RoboTextFeatureOn);

    /* `ApplyIfUnset` is the layer that supplies a default: the caller's own decision about `liga` stands
     * while the other four kinds are filled in. This is the rule the run set relies on. */
    RoboTextFeatureSetInit(&explicitSet);
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&explicitSet, "liga", RoboTextFeatureOff, 0u), (int)RoboOk);
    RoboTextLigaturePolicyApplyIfUnset(&policy, &explicitSet);
    RoboCheckEqInt((int)RoboTextFeatureSetCount(&explicitSet), 5);
    setting = RoboTextFeatureSetFind(&explicitSet, "liga");
    RoboCheckEqInt((int)setting->state, (int)RoboTextFeatureOff); /* The specific decision wins. */
    setting = RoboTextFeatureSetFind(&explicitSet, "clig");
    RoboCheckEqInt((int)setting->state, (int)RoboTextFeatureOn);
    setting = RoboTextFeatureSetFind(&explicitSet, "dlig");
    RoboCheckEqInt((int)setting->state, (int)RoboTextFeatureOff);

    /* A set that is already full of other tags takes what fits and reports nothing: a policy is a default,
     * and a caller that named sixteen features has said more than this layer has to say. */
    RoboTextFeatureSetInit(&set);
    for (size_t index = 0u; index < RoboTextMaxFeatures; index++) {
        char tag[5];
        static const char kBase[] = "zz00";

        memcpy(tag, kBase, sizeof(kBase));
        tag[2] = (char)('a' + (int)(index % 26));
        tag[3] = (char)('a' + (int)(index / 26));
        RoboCheckEqInt((int)RoboTextFeatureSetSet(&set, tag, RoboTextFeatureOn, 0u), (int)RoboOk);
    }
    RoboTextLigaturePolicyDefault(&policy);
    RoboTextLigaturePolicyApplyIfUnset(&policy, &set);
    RoboCheckEqInt((int)RoboTextFeatureSetCount(&set), (int)RoboTextMaxFeatures);
    RoboCheck(RoboTextFeatureSetFind(&set, "liga") == NULL);

    /* NULL arguments are refused rather than followed. */
    RoboCheckEqInt((int)RoboTextLigaturePolicyApply(NULL, &set), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextLigaturePolicyApply(&policy, NULL), (int)RoboInvalidArgument);
    RoboTextLigaturePolicyApplyIfUnset(NULL, &set);
    RoboTextLigaturePolicyApplyIfUnset(&policy, NULL);
}

/* ------------------------------------------------------------------ */
/* Options and the run set: the wiring                                 */
/* ------------------------------------------------------------------ */

static void TestLigatureOptions(void)
{
    RoboTextShapeOptions options;
    RoboTextLigaturePolicy expected;
    RoboTextFeatureSet unrelated;

    /* `Init` installs the default policy, so a caller that never mentions ligatures gets the module's
     * answer rather than an empty one. */
    RoboTextShapeOptionsInit(&options);
    RoboTextLigaturePolicyDefault(&expected);
    RoboCheck(RoboTextLigaturePolicyEqual(&options.ligatures, &expected));
    RoboCheck(RoboTextShapeOptionsIsValid(&options));
    RoboCheckEqInt((int)RoboTextFeatureSetCount(&options.features), 0); /* The policy is separate data. */

    /* A caller's explicit setting lives beside the policy rather than inside it. */
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&options, "dlig", RoboTextFeatureOn, 0u), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextFeatureSetCount(&options.features), 1);
    RoboCheck(RoboTextShapeOptionsIsValid(&options));

    /* An options record carrying a policy state that is not a feature state is not valid, which is how a
     * caller learns it built one. */
    options.ligatures.kinds[RoboTextLigatureHistorical] = (RoboTextFeatureState)11;
    RoboCheck(!RoboTextShapeOptionsIsValid(&options));
    RoboTextLigaturePolicyDefault(&options.ligatures);
    RoboCheck(RoboTextShapeOptionsIsValid(&options));

    /* The five tags are the ligature layer's; `RoboTextFeatureSet` stays a general feature set. */
    RoboTextFeatureSetInit(&unrelated);
    RoboCheckEqInt((int)RoboTextFeatureSetSet(&unrelated, "kern", RoboTextFeatureOn, 0u), (int)RoboOk);
    RoboCheck(!RoboTextLigatureKindForTag("kern", NULL));
}

static void TestLigatureRunSet(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeOptions options;
    RoboTextShapeOptions none;
    RoboTextRunSet *set = NULL;
    RoboTextRunSet *offSet = NULL;
    const RoboTextRun *run;
    const RoboTextFeatureSetting *setting;
    const char *text = "office";
    RoboTextView view = View(text);

    /* Default options: the run's resolved features carry all five ligature tags, because nothing else
     * would make a `liga` decision reachable from the pipeline. */
    RoboTextShapeOptionsInit(&options);
    RoboCheckEqInt((int)RoboTextRunSetCreate(view, &fixture.font, &options, &set, NULL), (int)RoboOk);
    RoboCheckEqInt((int)RoboTextRunSetTextRunCount(set), 1);
    run = RoboTextRunSetTextRun(set, 0u);
    RoboCheck(run != NULL);
    setting = RoboTextFeatureSetFind(&run->features, "liga");
    RoboCheck(setting != NULL);
    RoboCheckEqInt((int)setting->state, (int)RoboTextFeatureOn);
    setting = RoboTextFeatureSetFind(&run->features, "rlig");
    RoboCheck(setting != NULL);
    RoboCheckEqInt((int)setting->state, (int)RoboTextFeatureOn);
    setting = RoboTextFeatureSetFind(&run->features, "dlig");
    RoboCheck(setting != NULL);
    RoboCheckEqInt((int)setting->state, (int)RoboTextFeatureOff);
    RoboCheckEqInt((int)RoboTextFeatureSetCount(&run->features), 5);

    /* A caller's `liga=off` outranks the default policy, and the other four tags still arrive. */
    RoboTextShapeOptionsInit(&options);
    RoboCheckEqInt((int)RoboTextShapeOptionsSetFeature(&options, "liga", RoboTextFeatureOff, 0u),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTextRunSetCreate(view, &fixture.font, &options, &offSet, NULL), (int)RoboOk);
    run = RoboTextRunSetTextRun(offSet, 0u);
    setting = RoboTextFeatureSetFind(&run->features, "liga");
    RoboCheck(setting != NULL);
    RoboCheckEqInt((int)setting->state, (int)RoboTextFeatureOff);
    setting = RoboTextFeatureSetFind(&run->features, "clig");
    RoboCheck(setting != NULL);
    RoboCheckEqInt((int)setting->state, (int)RoboTextFeatureOn);

    /* And a policy of `none` turns the optional kinds off in the run, `rlig` excepted. */
    RoboTextShapeOptionsInit(&none);
    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantNone, &none.ligatures);
    RoboTextRunSetDestroy(offSet);
    offSet = NULL;
    RoboCheckEqInt((int)RoboTextRunSetCreate(view, &fixture.font, &none, &offSet, NULL), (int)RoboOk);
    run = RoboTextRunSetTextRun(offSet, 0u);
    setting = RoboTextFeatureSetFind(&run->features, "liga");
    RoboCheckEqInt((int)setting->state, (int)RoboTextFeatureOff);
    setting = RoboTextFeatureSetFind(&run->features, "dlig");
    RoboCheckEqInt((int)setting->state, (int)RoboTextFeatureOff);
    setting = RoboTextFeatureSetFind(&run->features, "rlig");
    RoboCheckEqInt((int)setting->state, (int)RoboTextFeatureOn); /* Required shaping survives. */

    RoboTextRunSetDestroy(offSet);
    RoboTextRunSetDestroy(set);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* The shipped pack: a real discretionary ligature                     */
/* ------------------------------------------------------------------ */

static void TestLigatureShippedPack(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeOptions options;
    RoboTextLigatureReport plainReport;
    RoboTextLigatureReport ligatedReport;
    RoboTextLigatureReport aloneReport;
    const RoboTextGlyphRun *plainRun;
    const RoboTextGlyphRun *ligatedRun;
    const RoboTextGlyph *ligatureGlyph;

    /* The default policy: `dlig` is off, so `!?` is the two glyphs the cmap produced. The shipped pack has
     * no `liga`, which is why this suite's standard-ligature evidence is 12.10's fixture instead. */
    RoboTextShapeOptionsInit(&options);
    {
        RoboTextShapeResult *result = ShapeWith(&fixture, &options, "!?");

        plainRun = OnlyRun(result);
        RoboCheckEqInt((int)RoboTextGlyphRunCount(plainRun), 2);
        RoboCheckEqInt((int)RoboTextGlyphRunLigatures(plainRun), 0);
        RoboCheckEqInt((int)RoboTextGlyphRunSubstituted(plainRun), 0);
        RoboCheckEqInt((int)RoboTextLigatureReportForRun(plainRun, &plainReport), (int)RoboOk);
        RoboCheckEqInt((int)plainReport.ligatureGlyphs, 0);
        RoboCheckEqInt((int)plainReport.absorbedScalars, 0);
        RoboCheckEqInt((int)plainReport.maxComponents, 0);

        /* No ligature glyph exists to ask about, and the door says so rather than naming a glyph that is
         * not one. */
        {
            RoboTextGlyphIndex index = 7u;

            RoboCheckEqInt((int)RoboTextGlyphRunLigatureAt(plainRun, 0u, &index, NULL), (int)RoboNotFound);
            RoboCheckEqInt((int)index, 0);
        }
        RoboTextShapeResultDestroy(result);
    }

    /* `font-variant-ligatures: discretionary-ligatures`: the policy states `dlig`, the tag reaches the
     * plan, and Inter's own lookup turns `!?` into U+203D. Two scalars in, one glyph out. */
    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantDiscretionary, &options.ligatures);
    {
        RoboTextShapeResult *result = ShapeWith(&fixture, &options, "!?");
        RoboTextShapeResult *alone;
        RoboTextGlyphIndex ligatureIndex = 0u;
        RoboTextRange source = RoboTextRangeMake(0u, 0u);

        ligatedRun = OnlyRun(result);
        RoboCheckEqInt((int)RoboTextGlyphRunCount(ligatedRun), 1);
        RoboCheckEqInt((int)RoboTextGlyphRunScalarCount(ligatedRun), 2);
        RoboCheckEqInt((int)RoboTextGlyphRunLigatures(ligatedRun), 1);
        RoboCheckEqInt((int)RoboTextGlyphRunSubstituted(ligatedRun), 1);
        RoboCheckEqInt((int)RoboTextGlyphRunGenerationMode(ligatedRun), (int)RoboTextGlyphGenerationGsub);

        RoboCheckEqInt((int)RoboTextGlyphRunLigatureAt(ligatedRun, 0u, &ligatureIndex, NULL), (int)RoboOk);
        RoboCheckEqInt((int)ligatureIndex, 0);
        RoboCheckEqInt((int)RoboTextGlyphRunLigatureAt(ligatedRun, 1u, &ligatureIndex, NULL), (int)RoboNotFound);

        ligatureGlyph = RoboTextGlyphRunGlyph(ligatedRun, 0u);
        RoboCheck(ligatureGlyph != NULL);
        RoboCheck(ligatureGlyph->substituted);
        /* Both scalars, one glyph: the range a selection needs, and the component count that tells a
         * ligature apart from a decomposition. */
        RoboCheckEqInt((int)ligatureGlyph->ligatureComponents, 2);
        RoboCheckEqInt((int)ligatureGlyph->source.start, 0);
        RoboCheckEqInt((int)ligatureGlyph->source.end, 2);
        RoboCheckEqInt((int)RoboTextGlyphRunSourceOf(ligatedRun, 0u, &source, NULL), (int)RoboOk);
        RoboCheckEqInt((int)source.start, 0);
        RoboCheckEqInt((int)source.end, 2);

        /* The report: one glyph absorbed two scalars, and the run has one glyph for two scalars. */
        RoboCheckEqInt((int)RoboTextLigatureReportForRun(ligatedRun, &ligatedReport), (int)RoboOk);
        RoboCheckEqInt((int)ligatedReport.ligatureGlyphs, 1);
        RoboCheckEqInt((int)ligatedReport.absorbedScalars, 2);
        RoboCheckEqInt((int)ligatedReport.maxComponents, 2);
        RoboCheckEqInt((int)ligatedReport.glyphs, 1);
        RoboCheckEqInt((int)ligatedReport.scalars, 2);
        RoboCheckEqInt((int)ligatedReport.substitutedGlyphs, 1);

        /* The ligature's advance is the **substitute's** own `hmtx` entry: measuring the interrobang on its
         * own gives the same width, and a run whose width came from summing `!` and `?` would differ. */
        alone = ShapeWith(&fixture, &options, TextInterrobang);
        {
            const RoboTextGlyphRun *aloneRun = OnlyRun(alone);
            const RoboTextGlyph *aloneGlyph = RoboTextGlyphRunGlyph(aloneRun, 0u);

            RoboCheckEqInt((int)RoboTextGlyphRunCount(aloneRun), 1);
            RoboCheck(aloneGlyph != NULL);
            RoboCheckEqInt((int)aloneGlyph->glyphId, (int)ligatureGlyph->glyphId);
            RoboCheckEqInt((int)(aloneGlyph->advanceX * 64.0f), (int)(ligatureGlyph->advanceX * 64.0f));
            RoboCheckEqInt((int)(RoboTextShapeResultMeasurement(alone)->width * 64.0f),
                           (int)(RoboTextShapeResultMeasurement(result)->width * 64.0f));
            RoboCheckEqInt((int)RoboTextLigatureReportForRun(aloneRun, &aloneReport), (int)RoboOk);
            RoboCheckEqInt((int)aloneReport.ligatureGlyphs, 0); /* It is a substitute, not a ligature. */
        }
        /* The width the text measures to is the ligature's, not the sum of the two glyphs it replaced: the
         * reader-visible half of the whole claim, and the reason measurement follows the shaped glyph run
         * rather than the character count. */
        RoboCheckEqInt((int)(RoboTextShapeResultMeasurement(result)->width * 64.0f),
                       (int)(RoboTextShapeResultMeasurement(alone)->width * 64.0f));
        RoboTextShapeResultDestroy(alone);
        RoboTextShapeResultDestroy(result);
    }

    /* Discretionary ligatures are the policy's business and `none` is the caller's answer: with the
     * optional kinds off the same text is two glyphs again. */
    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantNone, &options.ligatures);
    {
        RoboTextShapeResult *result = ShapeWith(&fixture, &options, "!?");

        plainRun = OnlyRun(result);
        RoboCheckEqInt((int)RoboTextGlyphRunCount(plainRun), 2);
        RoboCheckEqInt((int)RoboTextGlyphRunLigatures(plainRun), 0);
        RoboTextShapeResultDestroy(result);
    }

    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Source mapping across a ligature                                    */
/* ------------------------------------------------------------------ */

static void TestLigatureProvenance(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeOptions options;
    RoboTextShapeResult *result;
    const RoboTextGlyphRun *run;
    RoboTextGlyphIndex first = 0u;
    RoboTextGlyphIndex count = 0u;

    RoboTextShapeOptionsInit(&options);
    RoboTextLigaturePolicyFromVariant(RoboTextLigatureVariantDiscretionary, &options.ligatures);
    result = ShapeWith(&fixture, &options, "x!?y");
    run = OnlyRun(result);
    RoboCheckEqInt((int)RoboTextGlyphRunCount(run), 3); /* `x`, the interrobang, `y`. */
    RoboCheckEqInt((int)RoboTextGlyphRunScalarCount(run), 4);

    /* Every scalar a glyph stands for, and the glyph each scalar belongs to. Two scalars, one glyph: this
     * is the mapping a caret will be built on, and it is why the ligature cannot simply vanish into a
     * glyph id. */
    {
        const RoboTextGlyph *ligature = RoboTextGlyphRunGlyph(run, 1u);

        RoboCheck(ligature != NULL);
        RoboCheckEqInt((int)ligature->ligatureComponents, 2);
        RoboCheckEqInt((int)ligature->source.start, 1);
        RoboCheckEqInt((int)ligature->source.end, 3);
    }
    RoboCheckEqInt((int)RoboTextGlyphRunGlyphsForScalar(run, 1u, &first, &count, NULL), (int)RoboOk);
    RoboCheckEqInt((int)first, 1);
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt((int)RoboTextGlyphRunGlyphsForScalar(run, 2u, &first, &count, NULL), (int)RoboOk);
    RoboCheckEqInt((int)first, 1);
    RoboCheckEqInt((int)count, 1);
    RoboCheckEqInt((int)RoboTextGlyphRunGlyphForScalarIndex(run, 2u, &first, NULL), (int)RoboOk);
    RoboCheckEqInt((int)first, 1);
    /* The scalars outside the ligature are still one glyph each. */
    RoboCheckEqInt((int)RoboTextGlyphRunGlyphForScalarIndex(run, 0u, &first, NULL), (int)RoboOk);
    RoboCheckEqInt((int)first, 0);
    RoboCheckEqInt((int)RoboTextGlyphRunGlyphForScalarIndex(run, 3u, &first, NULL), (int)RoboOk);
    RoboCheckEqInt((int)first, 2);

    /* The per-run counters agree with the per-glyph facts, and `multiScalarSources` is exactly the ligature
     * count — a thing the glyph stats could not tell before 12.12. */
    {
        RoboTextGlyphStats stats;

        RoboCheckEqInt((int)RoboTextGlyphRunGetStats(run, &stats), (int)RoboOk);
        RoboCheckEqInt((int)stats.glyphs, 3);
        RoboCheckEqInt((int)stats.ligatureGlyphs, 1);
        RoboCheckEqInt((int)stats.ligatureScalars, 2);
        RoboCheckEqInt((int)stats.multiScalarSources, 1);
    }

    /* A glyph dump names the component count, so a diff of two dumps can tell a ligature from a
     * decomposition. */
    {
        char line[256];
        size_t length = 0u;

        RoboCheckEqInt((int)RoboTextGlyphWrite(RoboTextGlyphRunGlyph(run, 1u), line, sizeof(line), &length),
                       (int)RoboOk);
        RoboCheck(strstr(line, "from=2") != NULL);
    }

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* The report's own doors                                              */
/* ------------------------------------------------------------------ */

static void TestLigatureReport(void)
{
    TextFixture fixture = TextFixtureOpen();
    RoboTextShapeOptions options;
    RoboTextShapeResult *result;
    const RoboTextGlyphRun *run;
    RoboTextLigatureReport report;
    char line[192];
    size_t length = 0u;

    RoboTextShapeOptionsInit(&options);
    result = ShapeWith(&fixture, &options, "abc");
    run = OnlyRun(result);

    /* Plain text: the report exists, is filled, and says nothing happened. */
    RoboCheckEqInt((int)RoboTextLigatureReportForRun(run, &report), (int)RoboOk);
    RoboCheckEqInt((int)report.ligatureGlyphs, 0);
    RoboCheckEqInt((int)report.glyphs, 3);
    RoboCheckEqInt((int)report.scalars, 3);
    RoboCheckEqInt((int)RoboTextLigatureReportWrite(&report, line, sizeof(line), &length), (int)RoboOk);
    RoboCheckStrEq(line, "ligatures=0 absorbed=0 max=0 glyphs=3 scalars=3 substituted=0");

    /* A dump that does not fit is refused rather than truncated. */
    RoboCheckEqInt((int)RoboTextLigatureReportWrite(&report, line, 12u, &length), (int)RoboNotSupported);
    RoboCheckEqInt((int)RoboTextLigatureReportWrite(NULL, line, sizeof(line), &length),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextLigatureReportForRun(NULL, &report), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextLigatureReportForRun(run, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTextGlyphRunLigatureAt(NULL, 0u, &(RoboTextGlyphIndex){0}, NULL),
                   (int)RoboInvalidArgument);

    RoboTextShapeResultDestroy(result);
    TextFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */

void TestRegisterTextLigature(void)
{
    TestRegister("text.ligature.vocabulary", TestLigatureVocabulary);
    TestRegister("text.ligature.policy", TestLigaturePolicy);
    TestRegister("text.ligature.variants", TestLigatureVariants);
    TestRegister("text.ligature.feature_set", TestLigatureFeatureSet);
    TestRegister("text.ligature.options", TestLigatureOptions);
    TestRegister("text.ligature.run_set", TestLigatureRunSet);
    TestRegister("text.ligature.shipped_pack", TestLigatureShippedPack);
    TestRegister("text.ligature.provenance", TestLigatureProvenance);
    TestRegister("text.ligature.report", TestLigatureReport);
}
