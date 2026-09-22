/*
 * RoboTypography's resolver (Phase 12.3) — `typography.resolve.*`.
 *
 * Every case that needs a face opens the **shipped** system typeface pack through the fixture, so the
 * chain under test is the one the guest runs: the manifest is parsed, the face is discovered and
 * loaded by the same registry and loader, and the role is resolved by Phase 5's selector. A suite that
 * built a face by hand could pass while the real pack was broken, which is the failure this phase's
 * tests exist to catch.
 *
 * The face-less cases use a *virtual* pack -- a manifest that declares only the UI Text role, with the
 * real face planted beside it -- because "the manifest omits a role" is a state the shipped pack
 * cannot express and a state a caller must survive: a role with no family still has a size, a weight
 * and a leading, and a shell that aborted instead would be a shell that could not report its own
 * missing font.
 */

#include "RoboTypographyTestSupport.h"

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

static void TestCreate(void)
{
    TypographyFixture fixture;
    RoboTypographyResolverConfig config;
    TypographyResolver *resolver = NULL;
    TypographyDiag diag;
    RoboTypographyScaleEntry custom[RoboTypographyScaleCount];

    RoboTypographyDiagReset(&diag);
    RoboCheckEqInt((int)RoboTypographyResolverCreate(NULL, &resolver, NULL), (int)RoboInvalidArgument);
    RoboTypographyResolverConfigInit(&config);
    RoboCheckEqInt((int)RoboTypographyResolverCreate(&config, NULL, NULL), (int)RoboInvalidArgument);
    /* No pack: the resolver has nothing to resolve against, and says so as an argument error rather
     * than creating something that would answer every role with no face. */
    RoboCheckEqInt((int)RoboTypographyResolverCreate(&config, &resolver, &diag),
                   (int)RoboInvalidArgument);
    RoboCheck(resolver == NULL);
    RoboCheckEqInt((int)diag.domain, (int)RoboTypographyDomainResolver);

    fixture = TypographyFixtureOpen(NULL);
    RoboCheck(fixture.resolver != NULL);
    RoboCheck(RoboTypographyResolverScale(fixture.resolver) != NULL);
    RoboCheckEqInt((int)RoboTypographyResolverScaleCount(fixture.resolver),
                   (int)RoboTypographyScaleCount);
    RoboTypographyResolverDestroy(NULL); /* safe */
    TypographyFixtureClose(&fixture);

    /* A caller's scale is taken and validated. A duplicate role or an unsettable entry is refused,
     * because the answer would otherwise depend on which entry the lookup reached first. */
    fixture = TypographyFixtureOpen(NULL);
    RoboCheckEqInt((int)RoboTypographyDefaultScale(custom, RoboTypographyScaleCount, NULL),
                   (int)RoboOk);
    config.pack = fixture.pack;
    config.scale = custom;
    config.scaleCount = RoboTypographyScaleCount;
    RoboTypographyDiagReset(&diag);
    RoboCheckEqInt((int)RoboTypographyResolverCreate(&config, &resolver, &diag), (int)RoboOk);
    RoboCheck(resolver != NULL);
    RoboCheck(RoboTypographyResolverScale(resolver) == custom);
    RoboTypographyResolverDestroy(resolver);

    custom[4].role = custom[0].role; /* a duplicate */
    RoboTypographyDiagReset(&diag);
    RoboCheckEqInt((int)RoboTypographyResolverCreate(&config, &resolver, &diag),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)diag.domain, (int)RoboTypographyDomainScale);
    custom[4].role = (RoboTypographyRole)5;

    custom[6].sizePx = 0.0f; /* not a type setting */
    RoboCheckEqInt((int)RoboTypographyResolverCreate(&config, &resolver, NULL),
                   (int)RoboInvalidArgument);
    TypographyFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Roles against the shipped pack                                      */
/* ------------------------------------------------------------------ */

static void TestResolveRoles(void)
{
    TypographyFixture fixture = TypographyFixtureOpen(NULL);
    RoboTypographyResolverStats stats;
    size_t index;

    if (!fixture.resolver) {
        return;
    }

    /*
     * Every role resolves to a valid style with a face, because the shipped manifest binds all three
     * pack roles to the one face. This is the phase's central claim in one loop: a role in, a loaded
     * system face out.
     */
    for (index = 0u; index < RoboTypographyRoleCount; index++) {
        RoboTypographyRole role = (RoboTypographyRole)(index + 1u);
        RoboTypographyStyle style;

        RoboTypographyStyleInit(&style);
        RoboCheckMsg(RoboTypographyResolve(fixture.resolver, role, &style, NULL) == RoboOk,
                     "a role did not resolve");
        RoboCheck(RoboTypographyStyleIsValid(&style));
        RoboCheckMsg(style.face != NULL, "a role resolved without a face");
        RoboCheckEqInt((int)style.role, (int)role);
        RoboCheck(RoboTypographyWeightName(style.weight) != NULL);
        /* The face's own metrics were scaled once: a positive ascent and a non-negative descent. */
        RoboCheck(style.ascentPx > 0.0f);
        RoboCheck(style.descentPx >= 0.0f);
    }

    /* The two faces the shell's own surfaces are bound to, and the weights their roles declare. */
    {
        RoboTypographyStyle title;
        RoboTypographyStyle window;

        RoboCheckEqInt((int)RoboTypographyResolve(fixture.resolver, RoboTypographyRoleDesktopTitle,
                                                  &title, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)title.fontRole, (int)RoboFontSystemRoleUIDisplay);
        RoboCheckEqInt((int)title.weight, (int)RoboTypographyWeightSemiBold);
        RoboCheck(title.sizePx == 13.0f);
        RoboCheck(title.lineHeightPx == 16.0f);
        RoboCheckEqInt((int)title.opticalSize, (int)RoboTypographyOpticalDisplay);

        RoboCheckEqInt((int)RoboTypographyResolve(fixture.resolver, RoboTypographyRoleWindowTitle,
                                                  &window, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)window.fontRole, (int)RoboFontSystemRoleUIText);
        /* Inter is a variable face and its `wght` axis satisfies the request, so the selector
         * surfaces the *requested* weight rather than the instance's default 400. */
        RoboCheckEqInt((int)window.weight, (int)RoboTypographyWeightMedium);

        /* Both roles are one physical family, selected at two weights: the same face object. */
        RoboCheck(window.face == title.face);
    }

    /* The document roles: heading 1 is the display size and the body is the reading size. */
    {
        RoboTypographyStyle h1;
        RoboTypographyStyle body;

        RoboCheckEqInt((int)RoboTypographyResolve(fixture.resolver, RoboTypographyRoleDocumentHeading1,
                                                  &h1, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTypographyResolve(fixture.resolver, RoboTypographyRoleDocumentBody,
                                                  &body, NULL),
                       (int)RoboOk);
        RoboCheck(h1.sizePx == 32.0f);
        RoboCheck(body.sizePx == 16.0f);
        RoboCheck(h1.sizePx > body.sizePx);
        RoboCheck(h1.face == body.face);
    }

    /* An undefined role and a NULL argument are refused rather than answered. */
    {
        RoboTypographyStyle style;

        RoboCheckEqInt((int)RoboTypographyResolve(fixture.resolver, RoboTypographyRoleNone, &style,
                                                  NULL),
                       (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTypographyResolve(fixture.resolver, RoboTypographyRoleWindowTitle,
                                                  NULL, NULL),
                       (int)RoboInvalidArgument);
        RoboCheckEqInt((int)RoboTypographyResolve(NULL, RoboTypographyRoleWindowTitle, &style, NULL),
                       (int)RoboInvalidArgument);
    }

    RoboCheckEqInt((int)RoboTypographyResolverGetStats(fixture.resolver, &stats), (int)RoboOk);
    /* Twenty-one roles resolved once each; the four re-resolves below the loop are cache hits, which
     * is why `resolved` counts the roles and not the calls. */
    RoboCheckEqInt((int)stats.resolved, (int)RoboTypographyRoleCount);
    RoboCheckEqInt((int)stats.unresolved, 0);
    RoboCheckEqInt((int)stats.cacheHits, 4);

    /* A resolver whose scale omits a role reports RoboNotFound rather than inventing a setting. */
    {
        TypographyFixture narrow;
        RoboTypographyResolverConfig config;
        TypographyResolver *resolver = NULL;
        RoboTypographyScaleEntry one;
        RoboTypographyStyle style;

        narrow = TypographyFixtureOpen(NULL);
        RoboCheckEqInt((int)RoboTypographyScaleForRole(RoboTypographyRoleWindowTitle, &one),
                       (int)RoboOk);
        RoboTypographyResolverConfigInit(&config);
        config.pack = narrow.pack;
        config.scale = &one;
        config.scaleCount = 1u;
        RoboCheckEqInt((int)RoboTypographyResolverCreate(&config, &resolver, NULL), (int)RoboOk);
        RoboCheckEqInt((int)RoboTypographyResolve(resolver, RoboTypographyRoleWindowTitle, &style,
                                                  NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTypographyResolve(resolver, RoboTypographyRoleChromeLabel, &style,
                                                  NULL),
                       (int)RoboNotFound);
        RoboTypographyResolverDestroy(resolver);
        TypographyFixtureClose(&narrow);
    }

    TypographyFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Weights                                                             */
/* ------------------------------------------------------------------ */

static void TestWeights(void)
{
    TypographyFixture fixture = TypographyFixtureOpen(NULL);
    RoboTypographyResolverStats stats;
    RoboTypographyStyle style;

    if (!fixture.resolver) {
        return;
    }

    /* An explicit weight is carried to the selector, and the variable face satisfies it. */
    RoboCheckEqInt((int)RoboTypographyResolveWeight(fixture.resolver, RoboTypographyRoleChromeLabel,
                                                    RoboTypographyWeightBold, RoboFontStyleNormal,
                                                    &style, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)style.weight, (int)RoboTypographyWeightBold);
    RoboCheck(style.face != NULL);

    /* A weight the vocabulary cannot name is *snapped*, deterministically and without any
     * emboldening: 550 is equidistant from 500 and 600, and the documented tie rule answers the
     * lighter step. */
    RoboCheckEqInt((int)RoboTypographyResolveWeight(fixture.resolver, RoboTypographyRoleChromeLabel,
                                                    550u, RoboFontStyleNormal, &style, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)style.weight, (int)RoboTypographyWeightMedium);

    RoboCheckEqInt((int)RoboTypographyResolverGetStats(fixture.resolver, &stats), (int)RoboOk);
    RoboCheckEqInt((int)stats.weightStepDowns, 1);

    /* An out-of-range weight clamps to the nearest end rather than being carried through. */
    RoboCheckEqInt((int)RoboTypographyResolveWeight(fixture.resolver, RoboTypographyRoleChromeLabel,
                                                    900u, RoboFontStyleNormal, &style, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)style.weight, (int)RoboTypographyWeightBold);

    /* Weight 0 means "the scale's own", which is what the plain door does. */
    {
        RoboTypographyStyle implicit;
        RoboTypographyStyle zero;

        RoboCheckEqInt((int)RoboTypographyResolve(fixture.resolver, RoboTypographyRoleChromeButton,
                                                  &implicit, NULL),
                       (int)RoboOk);
        RoboCheckEqInt((int)RoboTypographyResolveWeight(fixture.resolver,
                                                        RoboTypographyRoleChromeButton, 0u,
                                                        RoboFontStyleNormal, &zero, NULL),
                       (int)RoboOk);
        RoboCheck(RoboTypographyStyleEqual(&implicit, &zero));
    }

    TypographyFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Cache                                                               */
/* ------------------------------------------------------------------ */

static void TestCache(void)
{
    TypographyFixture fixture = TypographyFixtureOpen(NULL);
    RoboTypographyResolverStats before;
    RoboTypographyResolverStats after;
    RoboTypographyStyle first;
    RoboTypographyStyle second;

    if (!fixture.resolver) {
        return;
    }

    RoboCheckEqInt((int)RoboTypographyResolve(fixture.resolver, RoboTypographyRoleWindowTitle,
                                              &first, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTypographyResolverGetStats(fixture.resolver, &before), (int)RoboOk);
    RoboCheckEqInt((int)before.cacheMisses, 1);
    RoboCheckEqInt((int)before.cacheHits, 0);

    /* The second resolve is a lookup, not a selection: the same style, byte for byte. */
    RoboCheckEqInt((int)RoboTypographyResolve(fixture.resolver, RoboTypographyRoleWindowTitle,
                                              &second, NULL),
                   (int)RoboOk);
    RoboCheck(RoboTypographyStyleEqual(&first, &second));
    RoboCheckEqInt((int)RoboTypographyResolverGetStats(fixture.resolver, &after), (int)RoboOk);
    RoboCheckEqInt((int)after.cacheHits, 1);
    RoboCheckEqInt((int)after.cacheMisses, 1);
    RoboCheckEqInt((int)after.resolved, (int)before.resolved);

    /* A different key is a miss: an override that returned the cached setting would be a *wrong*
     * answer rather than a slow one. */
    RoboCheckEqInt((int)RoboTypographyResolveWeight(fixture.resolver, RoboTypographyRoleWindowTitle,
                                                    RoboTypographyWeightBold, RoboFontStyleNormal,
                                                    &second, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTypographyResolverGetStats(fixture.resolver, &after), (int)RoboOk);
    RoboCheckEqInt((int)after.cacheMisses, 2);
    RoboCheck(!RoboTypographyStyleEqual(&first, &second));

    /* Clearing forces a re-resolution; the counters are history and are not reset. */
    RoboTypographyResolverCacheClear(fixture.resolver);
    RoboCheckEqInt((int)RoboTypographyResolve(fixture.resolver, RoboTypographyRoleWindowTitle,
                                              &second, NULL),
                   (int)RoboOk);
    RoboCheck(RoboTypographyStyleEqual(&first, &second));
    RoboCheckEqInt((int)RoboTypographyResolverGetStats(fixture.resolver, &after), (int)RoboOk);
    RoboCheckEqInt((int)after.cacheMisses, 3);
    RoboTypographyResolverCacheClear(NULL); /* safe */

    TypographyFixtureClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* Coverage fallback                                                   */
/* ------------------------------------------------------------------ */

static void TestGlyphFallback(void)
{
    TypographyFixture fixture = TypographyFixtureOpen(NULL);
    RoboTypographyStyle style;
    FontFallbackResult fallback;
    uint32_t covered[1];

    if (!fixture.resolver) {
        return;
    }

    /* 'A' is covered by the system face, so the primary *is* the answer -- no scan, no second face. */
    covered[0] = (uint32_t)'A';
    RoboCheckEqInt((int)RoboTypographyResolveGlyphs(fixture.resolver, RoboTypographyRoleDocumentBody,
                                                    covered, 1u, &style, &fallback, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)fallback.found, 1);
    RoboCheckEqInt((int)fallback.usedPrimary, 1);
    RoboCheck(style.face != NULL);
    RoboCheck(style.face == fallback.face);
    FontFallbackResultClear(&fallback);

    /* An unassigned code point: the primary does not cover it and nothing else in the pack does, so
     * the answer is an explicit "no fallback" -- and the requested style is returned unchanged. */
    covered[0] = 0x0378u;
    RoboCheckEqInt((int)RoboTypographyResolveGlyphs(fixture.resolver, RoboTypographyRoleDocumentBody,
                                                    covered, 1u, &style, &fallback, NULL),
                   (int)RoboOk);
    RoboCheckEqInt((int)fallback.found, 0);
    RoboCheckEqInt((int)fallback.usedPrimary, 0);
    RoboCheck(style.face != NULL);
    FontFallbackResultClear(&fallback);

    /* Argument shape. */
    RoboCheckEqInt((int)RoboTypographyResolveGlyphs(fixture.resolver, RoboTypographyRoleDocumentBody,
                                                    covered, 0u, &style, &fallback, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTypographyResolveGlyphs(fixture.resolver, RoboTypographyRoleDocumentBody,
                                                    NULL, 1u, &style, &fallback, NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTypographyResolveGlyphs(fixture.resolver, RoboTypographyRoleDocumentBody,
                                                    covered, 1u, &style, NULL, NULL),
                   (int)RoboInvalidArgument);

    TypographyFixtureClose(&fixture);

    /* With no fallback chain bound, the coverage question is refused *explicitly* rather than
     * answered "the primary covers everything". */
    {
        TypographyFixture noFallback;
        RoboTypographyResolverConfig config;
        TypographyResolver *resolver = NULL;

        noFallback = TypographyFixtureOpen(NULL);
        RoboTypographyResolverConfigInit(&config);
        config.pack = noFallback.pack;
        RoboCheckEqInt((int)RoboTypographyResolverCreate(&config, &resolver, NULL), (int)RoboOk);
        covered[0] = (uint32_t)'A';
        RoboCheckEqInt((int)RoboTypographyResolveGlyphs(resolver, RoboTypographyRoleDocumentBody,
                                                        covered, 1u, &style, &fallback, NULL),
                       (int)RoboNotSupported);
        RoboTypographyResolverDestroy(resolver);
        TypographyFixtureClose(&noFallback);
    }
}

/* ------------------------------------------------------------------ */
/* A role with no family                                               */
/* ------------------------------------------------------------------ */

static void TestMissingFamily(void)
{
    /** Only the UI Text role: `display` and `monospace` have no family at all. */
    static const char manifest[] =
        "system-font-pack 1\n"
        "root /System/Fonts\n"
        "family Inter\n"
        "  file UI/Inter.ttf\n"
        "  role text weight 400\n"
        "  weight 400\n"
        "  style normal\n"
        "end\n";
    TypographyVirtualFixture fixture = TypographyVirtualPack(manifest);

    if (!fixture.resolver) {
        TypographyVirtualClose(&fixture);
        return;
    }

    {
        RoboTypographyStyle text;
        RoboTypographyStyle display;
        RoboTypographyStyle mono;
        RoboTypographyResolverStats stats;

        /* The declared role resolves, with a face. */
        RoboCheckEqInt((int)RoboTypographyResolve(fixture.resolver, RoboTypographyRoleWindowTitle,
                                                  &text, NULL),
                       (int)RoboOk);
        RoboCheck(text.face != NULL);

        /*
         * The two roles the manifest does not bind return RoboOk with a *valid*, face-less style:
         * the numbers a layout needs are all there, and the missing face is a fact the caller reports
         * rather than an error that would stop a frame.
         */
        RoboCheckEqInt((int)RoboTypographyResolve(fixture.resolver, RoboTypographyRoleDesktopTitle,
                                                  &display, NULL),
                       (int)RoboOk);
        RoboCheck(display.face == NULL);
        RoboCheck(RoboTypographyStyleIsValid(&display));
        RoboCheck(display.sizePx == 13.0f);
        RoboCheckEqInt((int)display.weight, (int)RoboTypographyWeightSemiBold);
        /* The face-less metric rule: the size is the line extent, never zero. */
        RoboCheck(display.ascentPx == display.sizePx);
        RoboCheck(display.descentPx == 0.0f);

        RoboCheckEqInt((int)RoboTypographyResolve(fixture.resolver, RoboTypographyRoleDocumentCode,
                                                  &mono, NULL),
                       (int)RoboOk);
        RoboCheck(mono.face == NULL);
        RoboCheck(mono.sizePx == 14.0f);

        RoboCheckEqInt((int)RoboTypographyResolverGetStats(fixture.resolver, &stats), (int)RoboOk);
        RoboCheckEqInt((int)stats.unresolved, 2);
        RoboCheckEqInt((int)stats.resolved, 1);

        /* And the coverage door says the *honest* thing rather than "nothing covers this": there is
         * no primary to fall back from. */
        {
            FontFallbackResult fallback;
            uint32_t codepoints[1];

            codepoints[0] = (uint32_t)'A';
            RoboCheckEqInt((int)RoboTypographyResolveGlyphs(
                               fixture.resolver, RoboTypographyRoleDesktopTitle, codepoints, 1u,
                               &display, &fallback, NULL),
                           (int)RoboNotFound);
        }
    }

    TypographyVirtualClose(&fixture);
}

/* ------------------------------------------------------------------ */
/* The document sheet, through the resolver                            */
/* ------------------------------------------------------------------ */

static void TestResolverDocumentSheet(void)
{
    TypographyFixture fixture = TypographyFixtureOpen(NULL);
    char viaResolver[RoboTypographyDocumentSheetMaxBytes];
    char viaScale[RoboTypographyDocumentSheetMaxBytes];
    size_t resolverLength = 0u;
    size_t scaleLength = 0u;

    if (!fixture.resolver) {
        return;
    }
    /* The resolver's door is the scale's door: one implementation, reached two ways, so a document
     * cannot get a different default from the one the scale states. */
    RoboCheckEqInt((int)RoboTypographyResolverDocumentStyleSheet(
                       fixture.resolver, viaResolver, sizeof(viaResolver), &resolverLength),
                   (int)RoboOk);
    RoboCheckEqInt((int)RoboTypographyBuildDocumentStyleSheet(NULL, 0u, viaScale, sizeof(viaScale),
                                                              &scaleLength),
                   (int)RoboOk);
    RoboCheckStrEq(viaResolver, viaScale);
    RoboCheckEqInt((int)resolverLength, (int)scaleLength);
    RoboCheckEqInt((int)RoboTypographyResolverDocumentStyleSheet(NULL, viaResolver,
                                                                 sizeof(viaResolver), NULL),
                   (int)RoboInvalidArgument);

    TypographyFixtureClose(&fixture);
}

void TestRegisterTypographyResolver(void)
{
    TestRegister("typography.resolve.create", TestCreate);
    TestRegister("typography.resolve.roles", TestResolveRoles);
    TestRegister("typography.resolve.weights", TestWeights);
    TestRegister("typography.resolve.cache", TestCache);
    TestRegister("typography.resolve.glyph_fallback", TestGlyphFallback);
    TestRegister("typography.resolve.missing_family", TestMissingFamily);
    TestRegister("typography.resolve.document_sheet", TestResolverDocumentSheet);
}
