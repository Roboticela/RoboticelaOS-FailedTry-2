/*
 * RoboCSS image suite (Phase 18.16) — css.image.*
 *
 * Four closed sets over a family whose central fact is that **an `<image>` is not a
 * file**, the shared closed-vocabulary computation body two of them are read through,
 * and the registry rows all of it is registered by.
 *
 * Three things this suite is really about, and each is a fact no single accessor call
 * could state.
 *
 * First, the image function vocabulary. `url()` names a resource, `linear-gradient()`
 * computes pixels, `image-set()` chooses between alternatives and `cross-fade()`
 * composes them. The classifications are columns rather than a tree: a repeating
 * gradient is *also* a gradient and is on an axis, a non-gradient is on no axis, and
 * "does the value state an intrinsic size" is a separate question from which kind it
 * is — `image-set()` inherits the chosen alternative's size rather than stating one.
 *
 * Second, the classifications that are *not* each other's negation. `image-rendering`'s
 * `auto` is in neither the smoothing group nor the pixelated group, so a consumer that
 * read "not smoothing" as "pixelated" would pin a choice `auto` deliberately leaves
 * open; `object-fit`'s `none` preserves the aspect ratio *without* scaling, so
 * "preserves the ratio" is not "scales". And the unknown answers are the initial's
 * behaviour for the two sets whose initial is the safe direction — `auto` for
 * rendering, `from-image` for orientation — because discarding a rotated photo or
 * pinning an algorithm is exactly what a build that cannot read a value must not do.
 *
 * Third, the split by *grammar*. Two properties have a genuinely closed set and take a
 * typed shape, so `image-rendering: banana` and `object-fit: middle` are refused with a
 * sentence. `image-orientation` is `from-image | none | <angle>`, `image-resolution` is
 * a `||` of two parts and `object-position` is the position pair the background module
 * owns, so those declarations are CARRIED and the keyword pair inside the orientation
 * is read through RoboCSSImage.h.
 *
 * Nothing here opens a file, resolves a URL, samples a colour stop, interpolates a
 * gradient, scales a pixel or measures a box. A value at this phase is a value with
 * classifications in it, and every expectation below is about the value, what it
 * classifies as, or where it came from.
 */

#include "RoboCssTestSupport.h"
#include "RoboCSS/RoboCSSImage.h"
#include "RoboCSS/RoboCSSVersion.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* image-rendering                                                     */
/* ------------------------------------------------------------------ */

static void TestImageRenderingVocabulary(void)
{
    static const char *const keywords[] = {
        "auto", "smooth", "high-quality", "crisp-edges", "pixelated"
    };
    static const struct {
        int automatic;
        int smoothing;
        int pixelated;
        int showsPixelGrid;
    } expected[] = {
        {1, 0, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 1, 1}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssImageRenderingCount, 5);
    RoboCheckEqInt((int)CssImageRenderingCount(), 5);
    for (index = 0u; index < 5u; index++) {
        CssImageRendering rendering = (CssImageRendering)index;

        RoboCheckEqInt(CssImageRenderingIsKnown(rendering), 1);
        RoboCheckStrEq(CssImageRenderingName(rendering), keywords[index]);
        RoboCheckEqInt(CssImageRenderingFromText(keywords[index]), (int)rendering);
        RoboCheck(CssImageRenderingSliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssImageRenderingIsAutomatic(rendering),
                          expected[index].automatic);
        RoboCheckEqInt(CssImageRenderingIsSmoothing(rendering),
                          expected[index].smoothing);
        RoboCheckEqInt(CssImageRenderingIsPixelated(rendering),
                          expected[index].pixelated);
        RoboCheckEqInt(CssImageRenderingShowsPixelGrid(rendering),
                          expected[index].showsPixelGrid);
    }
    RoboCheckEqInt(CssImageRenderingIsKnown((CssImageRendering)99), 0);
    RoboCheckStrEq(CssImageRenderingName((CssImageRendering)99), "");
    RoboCheckEqInt(CssImageRenderingFromText("PIXELATED"),
                      (int)RoboCssImageRenderingPixelated);
    RoboCheckEqInt(CssImageRenderingFromText(NULL),
                      (int)RoboCssImageRenderingAuto);
    RoboCheckEqInt(CssImageRenderingFromText("banana"),
                      (int)RoboCssImageRenderingAuto);
    RoboCheckEqInt(CssImageRenderingSliceIsKnown(CssSliceFromText("banana")), 0);
    RoboCheckEqInt(CssImageRenderingSliceIsKnown(CssSliceEmpty()), 0);
    /*
     * `auto` is in neither group, and that is the whole reason the groups are separate:
     * a consumer that read "not smoothing" as "pixelated" would pin the algorithm `auto`
     * leaves to the user agent.
     */
    RoboCheckEqInt(CssImageRenderingIsAutomatic(RoboCssImageRenderingAuto), 1);
    RoboCheckEqInt(CssImageRenderingIsSmoothing(RoboCssImageRenderingAuto), 0);
    RoboCheckEqInt(CssImageRenderingIsPixelated(RoboCssImageRenderingAuto), 0);
    RoboCheckEqInt(CssImageRenderingShowsPixelGrid(RoboCssImageRenderingAuto), 0);
    /* `crisp-edges` promises "do not blur"; `pixelated` names the algorithm. */
    RoboCheckEqInt(CssImageRenderingIsPixelated(RoboCssImageRenderingCrispEdges), 1);
    RoboCheckEqInt(CssImageRenderingShowsPixelGrid(RoboCssImageRenderingCrispEdges), 0);
    RoboCheckEqInt(CssImageRenderingShowsPixelGrid(RoboCssImageRenderingPixelated), 1);
    /* An unknown value answers the initial's behaviour: the user agent decides. */
    RoboCheckEqInt(CssImageRenderingIsAutomatic((CssImageRendering)99), 1);
    RoboCheckEqInt(CssImageRenderingIsSmoothing((CssImageRendering)99), 0);
    RoboCheckEqInt(CssImageRenderingIsPixelated((CssImageRendering)99), 0);

    {
        char buffer[RoboCssImageRenderingTextMax];
        size_t length = 0u;
        char tiny[4];

        RoboCheckEqInt(CssImageRenderingWrite(RoboCssImageRenderingHighQuality,
                                              buffer, sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "high-quality");
        RoboCheckEqInt((int)length, 12);
        /* A buffer too small reports the length it needed rather than truncating. */
        RoboCheckEqInt(CssImageRenderingWrite(RoboCssImageRenderingHighQuality,
                                              tiny, sizeof(tiny), &length, NULL),
                          RoboNotSupported);
        RoboCheckEqInt((int)length, 12);
        RoboCheckEqInt(CssImageRenderingWrite((CssImageRendering)99, buffer,
                                              sizeof(buffer), &length, NULL),
                          RoboInvalidArgument);
    }
}

/* ------------------------------------------------------------------ */
/* object-fit                                                          */
/* ------------------------------------------------------------------ */

static void TestObjectFitVocabulary(void)
{
    static const char *const keywords[] = {
        "fill", "contain", "cover", "none", "scale-down"
    };
    static const struct {
        int preservesRatio;
        int scales;
        int clips;
        int scalesDownOnly;
    } expected[] = {
        {0, 1, 0, 0}, {1, 1, 0, 0}, {1, 1, 1, 0}, {1, 0, 1, 0}, {1, 1, 0, 1}
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssObjectFitCount, 5);
    RoboCheckEqInt((int)CssObjectFitCount(), 5);
    for (index = 0u; index < 5u; index++) {
        CssObjectFit fit = (CssObjectFit)index;

        RoboCheckEqInt(CssObjectFitIsKnown(fit), 1);
        RoboCheckStrEq(CssObjectFitName(fit), keywords[index]);
        RoboCheckEqInt(CssObjectFitFromText(keywords[index]), (int)fit);
        RoboCheck(CssObjectFitSliceIsKnown(CssSliceFromText(keywords[index])) == 1);
        RoboCheckEqInt(CssObjectFitPreservesAspectRatio(fit),
                          expected[index].preservesRatio);
        RoboCheckEqInt(CssObjectFitScales(fit), expected[index].scales);
        RoboCheckEqInt(CssObjectFitClips(fit), expected[index].clips);
        RoboCheckEqInt(CssObjectFitScalesDownOnly(fit),
                          expected[index].scalesDownOnly);
    }
    RoboCheckEqInt(CssObjectFitIsKnown((CssObjectFit)99), 0);
    RoboCheckStrEq(CssObjectFitName((CssObjectFit)99), "");
    RoboCheckEqInt(CssObjectFitFromText("SCALE-DOWN"),
                      (int)RoboCssObjectFitScaleDown);
    RoboCheckEqInt(CssObjectFitFromText(NULL), (int)RoboCssObjectFitFill);
    RoboCheckEqInt(CssObjectFitFromText("banana"), (int)RoboCssObjectFitFill);
    RoboCheckEqInt(CssObjectFitSliceIsKnown(CssSliceFromText("banana")), 0);
    /*
     * `none` is the row that proves "preserves the ratio" is not "scales": it shows the
     * source's ratio by not scaling at all, and it clips rather than fitting.
     */
    RoboCheckEqInt(CssObjectFitPreservesAspectRatio(RoboCssObjectFitNone), 1);
    RoboCheckEqInt(CssObjectFitScales(RoboCssObjectFitNone), 0);
    RoboCheckEqInt(CssObjectFitClips(RoboCssObjectFitNone), 1);
    /* `fill` is the only value that distorts, and it never clips. */
    RoboCheckEqInt(CssObjectFitPreservesAspectRatio(RoboCssObjectFitFill), 0);
    RoboCheckEqInt(CssObjectFitScales(RoboCssObjectFitFill), 1);
    RoboCheckEqInt(CssObjectFitClips(RoboCssObjectFitFill), 0);
    /* `contain` and `cover` preserve the ratio and differ only in which side fills. */
    RoboCheckEqInt(CssObjectFitPreservesAspectRatio(RoboCssObjectFitContain), 1);
    RoboCheckEqInt(CssObjectFitPreservesAspectRatio(RoboCssObjectFitCover), 1);
    RoboCheckEqInt(CssObjectFitClips(RoboCssObjectFitContain), 0);
    RoboCheckEqInt(CssObjectFitClips(RoboCssObjectFitCover), 1);
    /* `scale-down` is the one value that compares the intrinsic size with the box. */
    RoboCheckEqInt(CssObjectFitScalesDownOnly(RoboCssObjectFitScaleDown), 1);
    RoboCheckEqInt(CssObjectFitScalesDownOnly(RoboCssObjectFitContain), 0);
    /* An unknown value claims nothing; the initial (`fill`) scales. */
    RoboCheckEqInt(CssObjectFitPreservesAspectRatio((CssObjectFit)99), 0);
    RoboCheckEqInt(CssObjectFitScales((CssObjectFit)99), 1);
    RoboCheckEqInt(CssObjectFitScalesDownOnly((CssObjectFit)99), 0);

    {
        char buffer[RoboCssObjectFitTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssObjectFitWrite(RoboCssObjectFitScaleDown, buffer,
                                         sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "scale-down");
        RoboCheckEqInt((int)length, 10);
    }
}

/* ------------------------------------------------------------------ */
/* image-orientation                                                   */
/* ------------------------------------------------------------------ */

static void TestImageOrientationVocabulary(void)
{
    static const char *const keywords[] = { "from-image", "none" };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssImageOrientationCount, 2);
    RoboCheckEqInt((int)CssImageOrientationCount(), 2);
    for (index = 0u; index < 2u; index++) {
        CssImageOrientation orientation = (CssImageOrientation)index;

        RoboCheckEqInt(CssImageOrientationIsKnown(orientation), 1);
        RoboCheckStrEq(CssImageOrientationName(orientation), keywords[index]);
        RoboCheckEqInt(CssImageOrientationFromText(keywords[index]), (int)index);
        RoboCheck(CssImageOrientationSliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
    }
    RoboCheckEqInt(CssImageOrientationIsKnown((CssImageOrientation)99), 0);
    RoboCheckStrEq(CssImageOrientationName((CssImageOrientation)99), "");
    RoboCheckEqInt(CssImageOrientationFromText("FROM-IMAGE"),
                      (int)RoboCssImageOrientationFromImage);
    RoboCheckEqInt(CssImageOrientationFromText(NULL),
                      (int)RoboCssImageOrientationFromImage);
    RoboCheckEqInt(CssImageOrientationFromText("banana"),
                      (int)RoboCssImageOrientationFromImage);
    RoboCheckEqInt(CssImageOrientationSliceIsKnown(CssSliceFromText("banana")), 0);
    /*
     * `from-image` reads the *file*: it is the initial, and an unknown value answers its
     * behaviour so that a build that cannot read the value does not silently discard an
     * author's rotated photo.
     */
    RoboCheckEqInt(CssImageOrientationIsFromImage(
                       RoboCssImageOrientationFromImage), 1);
    RoboCheckEqInt(CssImageOrientationIsNone(
                       RoboCssImageOrientationFromImage), 0);
    RoboCheckEqInt(CssImageOrientationIsFromImage(RoboCssImageOrientationNone), 0);
    RoboCheckEqInt(CssImageOrientationIsNone(RoboCssImageOrientationNone), 1);
    RoboCheckEqInt(CssImageOrientationIsFromImage((CssImageOrientation)99), 1);
    RoboCheckEqInt(CssImageOrientationIsNone((CssImageOrientation)99), 0);

    {
        char buffer[RoboCssImageOrientationTextMax];
        size_t length = 0u;

        RoboCheckEqInt(CssImageOrientationWrite(RoboCssImageOrientationFromImage,
                                                buffer, sizeof(buffer), &length,
                                                NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "from-image");
        RoboCheckEqInt((int)length, 10);
    }
}

/* ------------------------------------------------------------------ */
/* The image function vocabulary                                       */
/* ------------------------------------------------------------------ */

static void TestImageFunctionVocabulary(void)
{
    static const char *const keywords[] = {
        "url", "src", "image", "image-set", "cross-fade", "element", "paint",
        "linear-gradient", "radial-gradient", "conic-gradient",
        "repeating-linear-gradient", "repeating-radial-gradient",
        "repeating-conic-gradient"
    };
    size_t index;

    CssFixture();
    RoboCheckEqInt((int)RoboCssImageFunctionCount, 13);
    RoboCheckEqInt((int)CssImageFunctionCount(), 13);
    for (index = 0u; index < 13u; index++) {
        CssImageFunction function = (CssImageFunction)index;

        RoboCheckEqInt(CssImageFunctionIsKnown(function), 1);
        RoboCheckStrEq(CssImageFunctionName(function), keywords[index]);
        RoboCheckEqInt(CssImageFunctionFromText(keywords[index]), (int)index);
        RoboCheck(CssImageFunctionSliceIsKnown(
                      CssSliceFromText(keywords[index])) == 1);
    }
    RoboCheckEqInt(CssImageFunctionIsKnown((CssImageFunction)99), 0);
    RoboCheckStrEq(CssImageFunctionName((CssImageFunction)99), "");
    RoboCheckEqInt(CssImageFunctionFromText("RADIAL-GRADIENT"),
                      (int)RoboCssImageFunctionRadialGradient);
    RoboCheckEqInt(CssImageFunctionFromText(NULL), (int)RoboCssImageFunctionUrl);
    RoboCheckEqInt(CssImageFunctionFromText("banana"),
                      (int)RoboCssImageFunctionUrl);
    RoboCheckEqInt(CssImageFunctionSliceIsKnown(CssSliceFromText("banana")), 0);

    /*
     * The classifications, which is what a consumer switches on. A gradient is six
     * spellings, and a repeating one is *also* a gradient and is on an axis.
     */
    RoboCheckEqInt(CssImageFunctionIsGradient(RoboCssImageFunctionUrl), 0);
    RoboCheckEqInt(CssImageFunctionIsGradient(RoboCssImageFunctionLinearGradient), 1);
    RoboCheckEqInt(CssImageFunctionIsGradient(RoboCssImageFunctionRadialGradient), 1);
    RoboCheckEqInt(CssImageFunctionIsGradient(RoboCssImageFunctionConicGradient), 1);
    RoboCheckEqInt(CssImageFunctionIsGradient(
                       RoboCssImageFunctionRepeatingConicGradient), 1);
    /* Repeating is a second column, not a second kind: the repeating rows are gradients. */
    RoboCheckEqInt(CssImageFunctionIsRepeating(
                       RoboCssImageFunctionRepeatingLinearGradient), 1);
    RoboCheckEqInt(CssImageFunctionIsRepeating(RoboCssImageFunctionLinearGradient), 0);
    /* Each axis predicate is true for its own two spellings and no others. */
    RoboCheckEqInt(CssImageFunctionIsLinear(RoboCssImageFunctionLinearGradient), 1);
    RoboCheckEqInt(CssImageFunctionIsLinear(
                       RoboCssImageFunctionRepeatingLinearGradient), 1);
    RoboCheckEqInt(CssImageFunctionIsLinear(RoboCssImageFunctionRadialGradient), 0);
    RoboCheckEqInt(CssImageFunctionIsRadial(RoboCssImageFunctionRadialGradient), 1);
    RoboCheckEqInt(CssImageFunctionIsRadial(
                       RoboCssImageFunctionRepeatingRadialGradient), 1);
    RoboCheckEqInt(CssImageFunctionIsConic(RoboCssImageFunctionConicGradient), 1);
    RoboCheckEqInt(CssImageFunctionIsConic(
                       RoboCssImageFunctionRepeatingConicGradient), 1);
    /* "not linear" is *not* "radial": a conic gradient samples by angle, not distance. */
    RoboCheckEqInt(CssImageFunctionIsLinear(RoboCssImageFunctionConicGradient), 0);
    RoboCheckEqInt(CssImageFunctionIsRadial(RoboCssImageFunctionConicGradient), 0);
    /* A non-gradient is on no axis at all. */
    RoboCheckEqInt(CssImageFunctionIsLinear(RoboCssImageFunctionUrl), 0);
    RoboCheckEqInt(CssImageFunctionIsRadial(RoboCssImageFunctionUrl), 0);
    RoboCheckEqInt(CssImageFunctionIsConic(RoboCssImageFunctionUrl), 0);

    /*
     * The predicate that makes an "auto" size mean two different things: a resource and
     * an element state their own size, a gradient is whatever the box says, and an
     * `image-set()` inherits its chosen alternative's rather than stating one.
     */
    RoboCheckEqInt(CssImageFunctionHasIntrinsicSize(RoboCssImageFunctionUrl), 1);
    RoboCheckEqInt(CssImageFunctionHasIntrinsicSize(RoboCssImageFunctionSrc), 1);
    RoboCheckEqInt(CssImageFunctionHasIntrinsicSize(RoboCssImageFunctionImage), 1);
    RoboCheckEqInt(CssImageFunctionHasIntrinsicSize(RoboCssImageFunctionElement), 1);
    RoboCheckEqInt(CssImageFunctionHasIntrinsicSize(
                       RoboCssImageFunctionLinearGradient), 0);
    RoboCheckEqInt(CssImageFunctionHasIntrinsicSize(
                       RoboCssImageFunctionImageSet), 0);
    RoboCheckEqInt(CssImageFunctionHasIntrinsicSize(
                       RoboCssImageFunctionCrossFade), 0);
    RoboCheckEqInt(CssImageFunctionHasIntrinsicSize(
                       RoboCssImageFunctionPaint), 0);
    /* `url` and `src` are the two spellings a loader has to fetch. */
    RoboCheckEqInt(CssImageFunctionIsResourceReference(RoboCssImageFunctionUrl), 1);
    RoboCheckEqInt(CssImageFunctionIsResourceReference(RoboCssImageFunctionSrc), 1);
    RoboCheckEqInt(CssImageFunctionIsResourceReference(
                       RoboCssImageFunctionLinearGradient), 0);
    /* An unknown function claims nothing. */
    RoboCheckEqInt(CssImageFunctionIsGradient((CssImageFunction)99), 0);
    RoboCheckEqInt(CssImageFunctionHasIntrinsicSize((CssImageFunction)99), 0);
    RoboCheckEqInt(CssImageFunctionIsResourceReference((CssImageFunction)99), 0);

    {
        char buffer[RoboCssImageFunctionTextMax];
        size_t length = 0u;
        char tiny[8];

        RoboCheckEqInt(CssImageFunctionWrite(
                           RoboCssImageFunctionRepeatingLinearGradient, buffer,
                           sizeof(buffer), &length, NULL),
                          RoboOk);
        RoboCheckStrEq(buffer, "repeating-linear-gradient");
        RoboCheckEqInt((int)length, 25);
        /* The longest keyword is the capacity the writer promises. */
        RoboCheckEqInt(CssImageFunctionWrite(
                           RoboCssImageFunctionRepeatingLinearGradient, tiny,
                           sizeof(tiny), &length, NULL),
                          RoboNotSupported);
        RoboCheckEqInt((int)length, 25);
    }
}

/* ------------------------------------------------------------------ */
/* Computation                                                         */
/* ------------------------------------------------------------------ */

static void TestImageComputedShapes(void)
{
    CssFixture();

    /* The two typed rows read through the shared closed-vocabulary body, so a value
     * this build can read is a KEYWORD carrying the canonical spelling. */
    CssComputeEquals(RoboCssPropertyComputationImageRendering, "o: pixelated",
                       "pixelated");
    CssComputeEquals(RoboCssPropertyComputationImageRendering, "o: PIXELATED",
                       "pixelated");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationImageRendering,
                                        "o: auto"),
                      "keyword");
    CssComputeEquals(RoboCssPropertyComputationObjectFit, "o: scale-down",
                       "scale-down");
    CssComputeEquals(RoboCssPropertyComputationObjectFit, "o: COVER", "cover");

    /* A reference is deferred -- legal CSS whose value is decided where the reference
     * is -- and it is carried rather than refused. */
    {
        uint32_t flags = 0u;

        CssComputeEquals(RoboCssPropertyComputationObjectFit, "o: var(--fit)",
                           "var(--fit)");
        CssComputeText(RoboCssPropertyComputationObjectFit, "o: var(--fit)",
                         NULL, 0u, &flags);
        RoboCheckEqInt(flags, 1u);
    }

    /* The refusals, which is what the closed set buys. */
    CssComputeEquals(RoboCssPropertyComputationImageRendering, "o: banana",
                       "not an image-rendering value");
    CssComputeEquals(RoboCssPropertyComputationImageRendering, "o: 3px",
                       "not an identifier");
    CssComputeEquals(RoboCssPropertyComputationImageRendering, "o: crisp edges",
                       "not a single image-rendering keyword");
    CssComputeEquals(RoboCssPropertyComputationObjectFit, "o: middle",
                       "not an object-fit value");
    CssComputeEquals(RoboCssPropertyComputationObjectFit, "o: banana",
                       "not an object-fit value");

    /* The carried half: an orientation angle, a resolution and a position pair all
     * survive intact, including the word nothing recognises. */
    CssComputeEquals(RoboCssPropertyComputationPreserve, "o: 90deg", "90deg");
    CssComputeEquals(RoboCssPropertyComputationPreserve, "o: from-image flip",
                       "from-image flip");
    CssComputeEquals(RoboCssPropertyComputationPreserve, "o: 2dppx", "2dppx");
    CssComputeEquals(RoboCssPropertyComputationPreserve, "o: 25% 75%", "25% 75%");
    CssComputeEquals(RoboCssPropertyComputationPreserve, "o: banana", "banana");
    RoboCheckStrEq(CssComputeKind(RoboCssPropertyComputationPreserve,
                                        "o: banana"),
                      "preserved");
}

/* ------------------------------------------------------------------ */
/* Registration                                                        */
/* ------------------------------------------------------------------ */

static void TestImageRegistry(void)
{
    /*
     * Three inherited rows and two non-inherited ones, and the split is the family's
     * home stated as a list: how an image is rendered, how a file's own orientation is
     * read and its resolution are settings of the element that holds the image, so a
     * child inherits them, while fitting the content to a box is the box's own business.
     */
    static const struct {
        const char *name;
        const char *shape;
        int inherited;
    } expected[] = {
        {"image-orientation", "from-image", 1},
        {"image-rendering", "auto", 1},
        {"image-resolution", "1dppx", 1},
        {"object-fit", "fill", 0},
        {"object-position", "50%,_,50%", 0}
    };
    static const struct {
        const char *name;
        CssPropertyComputation computation;
    } shapes[] = {
        {"image-rendering", RoboCssPropertyComputationImageRendering},
        {"object-fit", RoboCssPropertyComputationObjectFit},
        /* The carried half stays PRESERVED, including the rows whose grammar is
         * wider than any closed set. */
        {"image-orientation", RoboCssPropertyComputationPreserve},
        {"image-resolution", RoboCssPropertyComputationPreserve},
        {"object-position", RoboCssPropertyComputationPreserve}
    };
    CssPropertyRegistry *registry = NULL;
    size_t index;

    CssFixture();
    RoboCheckEqInt(CssPropertyRegistryCreateStandard(&registry, NULL), RoboOk);
    for (index = 0u; index < sizeof(expected) / sizeof(expected[0]); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, expected[index].name);
        char shape[64];

        RoboCheck(definition != NULL);
        if (!definition) {
            continue;
        }
        RoboCheckEqInt(CssPropertyDefinitionInherited(definition),
                          expected[index].inherited);
        CssShape(CssPropertyDefinitionInitialValue(definition), shape,
                  sizeof(shape));
        RoboCheckStrEq(shape, expected[index].shape);
    }

    /* The shape is read from the definition rather than inferred from the name, so a
     * row that had drifted onto another family's computation shows here. */
    for (index = 0u; index < sizeof(shapes) / sizeof(shapes[0]); index++) {
        const CssPropertyDefinition *definition =
            CssPropertyRegistryFind(registry, shapes[index].name);

        RoboCheck(definition != NULL);
        if (definition) {
            RoboCheckEqInt(definition->computation,
                              shapes[index].computation);
        }
    }

    CssPropertyRegistryDestroy(registry);
}

static void TestImageCapabilities(void)
{
    CssCapabilities caps;

    CssFixture();

    RoboCheckEqInt(CssGetCapabilities(&caps), RoboOk);
    RoboCheckEqInt(caps.images, 1);
    RoboCheckEqInt((int)RoboCssPropertyComputationCount, 74);
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          RoboCssPropertyComputationImageRendering),
                      1);
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          RoboCssPropertyComputationObjectFit),
                      1);
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationImageRendering),
                      "image-rendering");
    RoboCheckStrEq(CssPropertyComputationName(
                          RoboCssPropertyComputationObjectFit),
                      "object-fit");
    RoboCheckEqInt(CssPropertyComputationIsKnown(
                          (CssPropertyComputation)
                              RoboCssPropertyComputationCount),
                      0);
    RoboCheckStrEq(CssDiagnosticDomainName(RoboCssDomainImage), "image");
    RoboCheckEqInt(CssDiagnosticDomainIsKnown(RoboCssDomainImage), 1);
    RoboCheckEqInt(RoboCssDomainCount, 37);
}

/** The same declaration computes to the same value twice, by structural equality. */
static void TestImageDeterminism(void)
{
    CssDeclarationList list = RoboCssCleared;
    CssError error = RoboCssCleared;
    CssComputedValue first = RoboCssCleared;
    CssComputedValue second = RoboCssCleared;
    const CssDeclaration *declaration;

    CssFixture();
    RoboCheckEqInt(CssParseDeclarations("o: crisp-edges", &list, &error),
                      RoboOk);
    declaration = CssDeclarationListAt(&list, 0u);
    RoboCheck(declaration != NULL);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationImageRendering,
                          CssDeclarationValue(declaration), &first, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueCompute(
                          RoboCssPropertyComputationImageRendering,
                          CssDeclarationValue(declaration), &second, NULL),
                      RoboOk);
    RoboCheckEqInt(CssComputedValueEquals(&first, &second), 1);

    {
        CssDeclarationList otherList = RoboCssCleared;
        CssError otherError = RoboCssCleared;
        CssComputedValue other = RoboCssCleared;

        RoboCheckEqInt(CssParseDeclarations("o: pixelated", &otherList,
                                                 &otherError), RoboOk);
        declaration = CssDeclarationListAt(&otherList, 0u);
        RoboCheckEqInt(CssComputedValueCompute(
                              RoboCssPropertyComputationImageRendering,
                              CssDeclarationValue(declaration), &other, NULL),
                          RoboOk);
        /* `crisp-edges` and `pixelated` are different values, not two spellings of one
         * -- the distinction the fourth rendering column exists to make visible. */
        RoboCheckEqInt(CssComputedValueEquals(&first, &other), 0);
        CssComputedValueDispose(&other);
        CssDeclarationListDispose(&otherList);
    }

    CssComputedValueDispose(&first);
    CssComputedValueDispose(&second);
    CssDeclarationListDispose(&list);
}

/* ------------------------------------------------------------------ */
/* Suite registration                                                  */
/* ------------------------------------------------------------------ */

void TestRegisterCssImage(void);

void TestRegisterCssImage(void)
{
    TestRegister("css.image.rendering_vocabulary", TestImageRenderingVocabulary);
    TestRegister("css.image.object_fit_vocabulary", TestObjectFitVocabulary);
    TestRegister("css.image.orientation_vocabulary",
                 TestImageOrientationVocabulary);
    TestRegister("css.image.function_vocabulary", TestImageFunctionVocabulary);
    TestRegister("css.image.computed", TestImageComputedShapes);
    TestRegister("css.image.registry", TestImageRegistry);
    TestRegister("css.image.capabilities", TestImageCapabilities);
    TestRegister("css.image.determinism", TestImageDeterminism);
}
