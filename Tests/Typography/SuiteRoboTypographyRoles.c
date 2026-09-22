/*
 * RoboTypography's vocabulary and scale (Phase 12.3) — `typography.roles.*`, `typography.scale.*`,
 * `typography.vocabulary.*` and `typography.conformance.*`.
 *
 * This suite owns no font: every case here is a pure function of bytes, so it runs against the module
 * alone and a failure means the *vocabulary* is wrong rather than a face. The face-touching cases are
 * one file over (`SuiteRoboTypographyResolver.c`), which is the split the module itself has between
 * "what a role means" and "which face answers it".
 *
 * The scale cases are written as *properties over the whole table* rather than as twenty-one
 * assertions about twenty-one numbers: one entry per role, in role order, every entry a settable type
 * setting, and the UI roles inside the band the shell's geometry was drawn for. A test that pinned
 * each number individually would fail on every deliberate restyle, which is exactly the change the
 * scale exists to make easy.
 */

#include "RoboTypographyTestSupport.h"

/* ------------------------------------------------------------------ */
/* Roles                                                               */
/* ------------------------------------------------------------------ */

static void TestRoleVocabulary(void)
{
    size_t index;
    RoboTypographyRole parsed = RoboTypographyRoleNone;

    /* `None` is not a role: it is the value of a cleared record. */
    RoboCheck(!RoboTypographyRoleIsValid(RoboTypographyRoleNone));
    RoboCheck(RoboTypographyRoleName(RoboTypographyRoleNone) == NULL);

    /* Every defined role round-trips through its own name, and the names are distinct. */
    for (index = 0u; index < RoboTypographyRoleCount; index++) {
        RoboTypographyRole role = (RoboTypographyRole)(index + 1u);
        const char *name = RoboTypographyRoleName(role);
        size_t other;

        RoboCheck(name != NULL);
        RoboCheck(RoboTypographyRoleIsValid(role));
        RoboCheckEqInt((int)RoboTypographyRoleFromName(name, 0u, &parsed), (int)RoboOk);
        RoboCheckEqInt((int)parsed, (int)role);
        for (other = index + 1u; other < RoboTypographyRoleCount; other++) {
            RoboCheck(RoboTypographyRoleName((RoboTypographyRole)(other + 1u)) != NULL);
            RoboCheck(strcmp(name, RoboTypographyRoleName((RoboTypographyRole)(other + 1u))) != 0);
        }
    }

    /* Parsing is case-insensitive and ignores the separators a human writes between words. */
    RoboCheckEqInt((int)RoboTypographyRoleFromName("window-title", 0u, &parsed), (int)RoboOk);
    RoboCheckEqInt((int)parsed, (int)RoboTypographyRoleWindowTitle);
    RoboCheckEqInt((int)RoboTypographyRoleFromName("DOCUMENT_HEADING2", 0u, &parsed), (int)RoboOk);
    RoboCheckEqInt((int)parsed, (int)RoboTypographyRoleDocumentHeading2);
    RoboCheckEqInt((int)RoboTypographyRoleFromName("window title", 0u, &parsed), (int)RoboOk);
    RoboCheckEqInt((int)RoboTypographyRoleFromName("nope", 0u, &parsed), (int)RoboNotFound);
    RoboCheckEqInt((int)RoboTypographyRoleFromName("", 0u, &parsed), (int)RoboNotFound);
    RoboCheckEqInt((int)RoboTypographyRoleFromName(NULL, 0u, &parsed), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTypographyRoleFromName("WindowTitle", 3u, &parsed), (int)RoboNotFound);

    /* The four groups partition the vocabulary: every role is in exactly one of them. */
    for (index = 0u; index < RoboTypographyRoleCount; index++) {
        RoboTypographyRole role = (RoboTypographyRole)(index + 1u);
        int desktop = RoboTypographyRoleIsDesktop(role);
        int chrome = RoboTypographyRoleIsChrome(role);
        int document = RoboTypographyRoleIsDocument(role);
        int system = RoboTypographyRoleIsSystem(role);

        RoboCheckEqInt(desktop + chrome + document + system, 1);
    }
    /* And the grouping is the enum's own order, so an insertion in the middle of the enum without a
     * matching insertion in a predicate is caught here rather than in the pixels. */
    RoboCheckEqInt((int)RoboTypographyRoleDesktopTitle, 1);
    RoboCheck(RoboTypographyRoleIsDesktop(RoboTypographyRoleDesktopTitle));
    RoboCheck(RoboTypographyRoleIsDesktop(RoboTypographyRoleWindowSubtitle));
    RoboCheck(RoboTypographyRoleIsChrome(RoboTypographyRoleChromeLabel));
    RoboCheck(RoboTypographyRoleIsChrome(RoboTypographyRoleChromeStatus));
    RoboCheck(RoboTypographyRoleIsDocument(RoboTypographyRoleDocumentBody));
    RoboCheck(RoboTypographyRoleIsDocument(RoboTypographyRoleDocumentCode));
    RoboCheck(RoboTypographyRoleIsSystem(RoboTypographyRoleSystemLabel));
    RoboCheck(RoboTypographyRoleIsSystem(RoboTypographyRoleSystemMonospace));
    RoboCheck(!RoboTypographyRoleIsChrome(RoboTypographyRoleWindowTitle));
    RoboCheck(!RoboTypographyRoleIsDesktop(RoboTypographyRoleChromeLabel));
}

/* ------------------------------------------------------------------ */
/* Optical size and weight                                             */
/* ------------------------------------------------------------------ */

static void TestOpticalAndWeight(void)
{
    RoboTypographyOpticalSize optical = RoboTypographyOpticalSmall;
    FontWeight weight = 0u;

    RoboCheckEqInt((int)RoboTypographyOpticalSizeCount, 5);
    RoboCheck(!RoboTypographyOpticalSizeIsValid((RoboTypographyOpticalSize)5));
    RoboCheck(RoboTypographyOpticalSizeIsValid(RoboTypographyOpticalSmall));
    RoboCheck(RoboTypographyOpticalSizeIsValid(RoboTypographyOpticalLargeDisplay));
    RoboCheckStrEq(RoboTypographyOpticalSizeName(RoboTypographyOpticalSmall), "small");
    RoboCheckStrEq(RoboTypographyOpticalSizeName(RoboTypographyOpticalLargeDisplay),
                   "large-display");
    RoboCheckStrEq(RoboTypographyOpticalSizeName((RoboTypographyOpticalSize)9), "unknown");
    RoboCheckEqInt(
        (int)RoboTypographyOpticalSizeFromName("LARGE_DISPLAY", 0u, &optical), (int)RoboOk);
    RoboCheckEqInt((int)optical, (int)RoboTypographyOpticalLargeDisplay);
    RoboCheckEqInt((int)RoboTypographyOpticalSizeFromName("huge", 0u, &optical),
                   (int)RoboNotFound);
    RoboCheckEqInt((int)RoboTypographyOpticalSizeFromName(NULL, 0u, &optical),
                   (int)RoboInvalidArgument);

    /* The four weights, by name and by number. */
    RoboCheckEqInt((int)RoboTypographyWeightCount, 4);
    RoboCheckStrEq(RoboTypographyWeightName(RoboTypographyWeightRegular), "regular");
    RoboCheckStrEq(RoboTypographyWeightName(RoboTypographyWeightSemiBold), "semibold");
    RoboCheck(RoboTypographyWeightName(RoboTypographyWeightBold + 1u) == NULL);
    RoboCheckEqInt((int)RoboTypographyWeightFromName("BOLD", 0u, &weight), (int)RoboOk);
    RoboCheckEqInt((int)weight, (int)RoboTypographyWeightBold);
    RoboCheckEqInt((int)RoboTypographyWeightFromName("normal", 0u, &weight), (int)RoboNotFound);
    RoboCheckEqInt((int)RoboTypographyWeightFromName(NULL, 0u, &weight), (int)RoboInvalidArgument);

    /*
     * The step-down rule, including the tie it documents: an exact distance tie goes to the *lighter*
     * weight. 450 is 50 from 400 and 50 from 500, and the answer is 400; 500 is exact; 550 is 50 from
     * both 500 and 600 and answers 500. A weight outside the range clamps to the nearer end, and an
     * invalid weight is answered with the regular step rather than with an undefined value.
     */
    RoboCheckEqInt((int)RoboTypographyNearestWeight(100u), (int)RoboTypographyWeightRegular);
    RoboCheckEqInt((int)RoboTypographyNearestWeight(400u), (int)RoboTypographyWeightRegular);
    RoboCheckEqInt((int)RoboTypographyNearestWeight(450u), (int)RoboTypographyWeightRegular);
    RoboCheckEqInt((int)RoboTypographyNearestWeight(500u), (int)RoboTypographyWeightMedium);
    RoboCheckEqInt((int)RoboTypographyNearestWeight(550u), (int)RoboTypographyWeightMedium);
    RoboCheckEqInt((int)RoboTypographyNearestWeight(600u), (int)RoboTypographyWeightSemiBold);
    RoboCheckEqInt((int)RoboTypographyNearestWeight(700u), (int)RoboTypographyWeightBold);
    RoboCheckEqInt((int)RoboTypographyNearestWeight(900u), (int)RoboTypographyWeightBold);
    /* The heaviest *valid* CSS weight clamps to the heaviest step... */
    RoboCheckEqInt((int)RoboTypographyNearestWeight(1000u), (int)RoboTypographyWeightBold);
    /* ...and a value that is not a CSS weight at all (0, or past 1000) is answered with the regular
     * step rather than with an undefined one, because there is nothing to round. */
    RoboCheckEqInt((int)RoboTypographyNearestWeight(0u), (int)RoboTypographyWeightRegular);
    RoboCheckEqInt((int)RoboTypographyNearestWeight(10000u), (int)RoboTypographyWeightRegular);
}

/* ------------------------------------------------------------------ */
/* The style record                                                    */
/* ------------------------------------------------------------------ */

static void TestStyleRecord(void)
{
    RoboTypographyStyle style;
    RoboTypographyStyle other;
    char text[128];
    size_t length = 0u;

    RoboTypographyStyleInit(&style);
    RoboCheckEqInt((int)style.role, (int)RoboTypographyRoleNone);
    RoboCheck(style.face == NULL);
    /* An unset record is not a usable style: there is no role and no size. */
    RoboCheck(!RoboTypographyStyleIsValid(&style));
    RoboCheck(!RoboTypographyStyleIsValid(NULL));

    style.role = RoboTypographyRoleWindowTitle;
    style.fontRole = RoboFontSystemRoleUIText;
    style.weight = RoboTypographyWeightMedium;
    style.sizePx = 13.0f;
    style.lineHeightPx = 16.0f;
    style.opticalSize = RoboTypographyOpticalUI;
    RoboCheck(RoboTypographyStyleIsValid(&style));

    /* A face-less style is still valid: a missing family is a state to report, not a style to refuse. */
    other = style;
    RoboCheck(RoboTypographyStyleEqual(&style, &other));
    other.sizePx = 0.0f;
    RoboCheck(!RoboTypographyStyleIsValid(&other));
    other = style;
    other.lineHeightPx = 0.0f;
    RoboCheck(!RoboTypographyStyleIsValid(&other));
    other = style;
    other.opticalSize = (RoboTypographyOpticalSize)9;
    RoboCheck(!RoboTypographyStyleIsValid(&other));
    other = style;
    other.role = RoboTypographyRoleNone;
    RoboCheck(!RoboTypographyStyleIsValid(&other));

    /* Equality is field for field, including the face pointer. */
    other = style;
    other.face = (FontFace *)0x1; /* never dereferenced: this is a pointer comparison */
    RoboCheck(!RoboTypographyStyleEqual(&style, &other));
    RoboCheck(RoboTypographyStyleEqual(NULL, NULL));
    RoboCheck(!RoboTypographyStyleEqual(&style, NULL));

    /* The metrics door without a face: the size *is* the line extent, which is what a measurer reads
     * for an unknown face -- and never zero, which would collapse a line box. */
    RoboCheckEqInt((int)RoboTypographyStyleApplyFontMetrics(&style), (int)RoboOk);
    RoboCheck(style.ascentPx == 13.0f);
    RoboCheck(style.descentPx == 0.0f);
    RoboCheckEqInt((int)RoboTypographyStyleApplyFontMetrics(NULL), (int)RoboInvalidArgument);

    /* The dump line names the role, the weight, the size, the leading and the optical class. */
    RoboCheckEqInt((int)RoboTypographyStyleWrite(&style, text, sizeof(text), &length), (int)RoboOk);
    RoboCheckStrEq(text, "WindowTitle weight=medium size=13.0 line=16.0 optical=ui ascent=13.0 face=0");
    RoboCheck(length == strlen(text));
    /* A buffer too small is refused and told what it needed, rather than truncated. */
    {
        char tiny[8];
        size_t needed = 0u;

        RoboCheckEqInt((int)RoboTypographyStyleWrite(&style, tiny, sizeof(tiny), &needed),
                       (int)RoboNotSupported);
        RoboCheck(needed > 0u);
        RoboCheck(needed > sizeof(tiny) - 1u);
    }
    RoboCheckEqInt((int)RoboTypographyStyleWrite(NULL, text, sizeof(text), NULL),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTypographyStyleWrite(&style, NULL, 0u, NULL), (int)RoboInvalidArgument);
}

/* ------------------------------------------------------------------ */
/* The scale                                                           */
/* ------------------------------------------------------------------ */

static void TestScaleTable(void)
{
    RoboTypographyScaleEntry scale[RoboTypographyScaleCount];
    size_t count = 0u;
    size_t index;

    RoboCheckEqInt((int)RoboTypographyScaleCount, (int)RoboTypographyRoleCount);
    RoboCheckEqInt(
        (int)RoboTypographyDefaultScale(scale, RoboTypographyScaleCount, &count), (int)RoboOk);
    RoboCheckEqInt((int)count, (int)RoboTypographyScaleCount);

    /*
     * One entry per role, in role order, each a settable setting. The ordering is what lets the
     * resolver's per-role cache index by `role - 1` at all, so it is a contract rather than a
     * stylistic preference.
     */
    for (index = 0u; index < count; index++) {
        RoboCheckEqInt((int)scale[index].role, (int)(index + 1u));
        RoboCheck(RoboTypographyScaleEntryIsValid(&scale[index]));
        RoboCheck(RoboTypographyRoleIsValid(scale[index].role));
        RoboCheck(RoboTypographyOpticalSizeIsValid(scale[index].opticalSize));
        RoboCheck(RoboTypographyWeightName(scale[index].weight) != NULL);
        RoboCheck(RoboTypographyRoleIsChrome(scale[index].role) ||
                  RoboTypographyRoleIsDesktop(scale[index].role) ||
                  RoboTypographyRoleIsDocument(scale[index].role) ||
                  RoboTypographyRoleIsSystem(scale[index].role));
    }

    /* A partial scale is refused rather than truncated: a role left out would look like a restyle. */
    RoboCheckEqInt(
        (int)RoboTypographyDefaultScale(scale, RoboTypographyScaleCount - 1u, NULL),
        (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTypographyDefaultScale(NULL, 4u, NULL), (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTypographyDefaultScale(scale, 0u, NULL), (int)RoboInvalidArgument);

    /* Every role is reachable through the single-role door, and it agrees with the table. */
    for (index = 0u; index < RoboTypographyRoleCount; index++) {
        RoboTypographyRole role = (RoboTypographyRole)(index + 1u);
        RoboTypographyScaleEntry entry;

        RoboCheckEqInt((int)RoboTypographyScaleForRole(role, &entry), (int)RoboOk);
        RoboCheck(RoboTypographyScaleEntryIsValid(&entry));
        RoboCheckEqInt((int)entry.role, (int)role);
        RoboCheckEqInt((int)entry.sizePx, (int)scale[index].sizePx);
        RoboCheckEqInt((int)entry.weight, (int)scale[index].weight);
        RoboCheckEqInt((int)entry.fontRole, (int)scale[index].fontRole);
    }
    RoboCheckEqInt((int)RoboTypographyScaleForRole(RoboTypographyRoleNone, &scale[0]),
                   (int)RoboInvalidArgument);
    RoboCheckEqInt((int)RoboTypographyScaleForRole(RoboTypographyRoleWindowTitle, NULL),
                   (int)RoboInvalidArgument);

    /* The lookup takes a *caller's* scale, which is what makes a restyle a dataset change. */
    {
        RoboTypographyScaleEntry custom[RoboTypographyScaleCount];
        RoboTypographyScaleEntry one;

        RoboCheckEqInt(
            (int)RoboTypographyDefaultScale(custom, RoboTypographyScaleCount, NULL), (int)RoboOk);
        custom[RoboTypographyRoleWindowTitle - 1u].sizePx = 20.0f;
        custom[RoboTypographyRoleWindowTitle - 1u].lineHeightPx = 26.0f;
        RoboCheck(RoboTypographyScaleFindEntry(custom, RoboTypographyScaleCount,
                                               RoboTypographyRoleWindowTitle) != NULL);
        one = *RoboTypographyScaleFindEntry(custom, RoboTypographyScaleCount,
                                            RoboTypographyRoleWindowTitle);
        RoboCheckEqInt((int)one.sizePx, 20);
        RoboCheck(RoboTypographyScaleFindEntry(custom, 0u, RoboTypographyRoleWindowTitle) == NULL);
        RoboCheck(RoboTypographyScaleFindEntry(NULL, 4u, RoboTypographyRoleWindowTitle) == NULL);
        RoboCheck(RoboTypographyScaleFindEntry(custom, RoboTypographyScaleCount,
                                               RoboTypographyRoleNone) == NULL);
    }

    /*
     * The *shape* of the scale, as properties rather than as numbers: the document headings are the
     * display sizes and are strictly larger as the heading level falls; every UI role fits the bands
     * the shell's own geometry fixed (a 30 px panel and a 24 px title bar); and no role's leading is
     * below its size (which `ScaleEntryIsValid` also refuses, but a scale whose sizes shifted by a
     * later restyle should be caught here rather than in the pixels).
     */
    {
        const RoboTypographyScaleEntry *h1 =
            RoboTypographyScaleFindEntry(scale, count, RoboTypographyRoleDocumentHeading1);
        const RoboTypographyScaleEntry *h2 =
            RoboTypographyScaleFindEntry(scale, count, RoboTypographyRoleDocumentHeading2);
        const RoboTypographyScaleEntry *h3 =
            RoboTypographyScaleFindEntry(scale, count, RoboTypographyRoleDocumentHeading3);
        const RoboTypographyScaleEntry *body =
            RoboTypographyScaleFindEntry(scale, count, RoboTypographyRoleDocumentBody);

        RoboCheck(h1 && h2 && h3 && body);
        if (h1 && h2 && h3 && body) {
            RoboCheck(h1->sizePx > h2->sizePx);
            RoboCheck(h2->sizePx > h3->sizePx);
            RoboCheck(h3->sizePx > body->sizePx);
            RoboCheck(h1->opticalSize == RoboTypographyOpticalLargeDisplay);
            RoboCheck(body->opticalSize == RoboTypographyOpticalBody);
        }
        for (index = 0u; index < count; index++) {
            if (RoboTypographyRoleIsChrome(scale[index].role) ||
                RoboTypographyRoleIsDesktop(scale[index].role) ||
                RoboTypographyRoleIsSystem(scale[index].role)) {
                RoboCheck(scale[index].sizePx >= 11.0f);
                RoboCheck(scale[index].sizePx <= 13.0f);
            }
            RoboCheck(scale[index].lineHeightPx >= scale[index].sizePx);
        }
    }
}

/** A scale entry is refused when it is not a setting this build could draw. */
static void TestScaleEntryValidation(void)
{
    RoboTypographyScaleEntry entry;

    RoboCheckEqInt((int)RoboTypographyScaleForRole(RoboTypographyRoleDocumentBody, &entry),
                   (int)RoboOk);
    RoboCheck(RoboTypographyScaleEntryIsValid(&entry));
    RoboCheck(!RoboTypographyScaleEntryIsValid(NULL));

    {
        RoboTypographyScaleEntry bad = entry;

        bad.role = RoboTypographyRoleNone;
        RoboCheck(!RoboTypographyScaleEntryIsValid(&bad));
        bad = entry;
        bad.fontRole = RoboFontSystemRoleNone;
        RoboCheck(!RoboTypographyScaleEntryIsValid(&bad));
        bad = entry;
        bad.sizePx = 0.0f;
        RoboCheck(!RoboTypographyScaleEntryIsValid(&bad));
        bad = entry;
        bad.lineHeightPx = 0.0f;
        RoboCheck(!RoboTypographyScaleEntryIsValid(&bad));
        /* Leading below the size overlaps ascenders with descenders: the one thing an entry owns. */
        bad = entry;
        bad.lineHeightPx = bad.sizePx - 1.0f;
        RoboCheck(!RoboTypographyScaleEntryIsValid(&bad));
        bad = entry;
        bad.weight = 123u;
        RoboCheck(!RoboTypographyScaleEntryIsValid(&bad));
        bad = entry;
        bad.opticalSize = (RoboTypographyOpticalSize)11;
        RoboCheck(!RoboTypographyScaleEntryIsValid(&bad));
    }
}

/* ------------------------------------------------------------------ */
/* Capabilities                                                        */
/* ------------------------------------------------------------------ */

static void TestCapabilities(void)
{
    TypographyCapabilities caps;
    int32_t major = 0;
    int32_t minor = 0;
    int32_t patch = 0;

    RoboTypographyCapabilitiesInit(&caps);
    RoboCheck(!caps.roles && !caps.scale);

    RoboCheckEqInt((int)RoboTypographyGetCapabilities(&caps), (int)RoboOk);
    RoboCheckEqInt((int)RoboTypographyGetCapabilities(NULL), (int)RoboInvalidArgument);

    /* What this build does. */
    RoboCheckEqInt(caps.roles, 1);
    RoboCheckEqInt(caps.scale, 1);
    RoboCheckEqInt(caps.opticalSizeClasses, 1);
    RoboCheckEqInt(caps.weightSelection, 1);
    RoboCheckEqInt(caps.fallback, 1);
    RoboCheckEqInt(caps.cache, 1);
    RoboCheckEqInt(caps.documentDefaults, 1);
    RoboCheckEqInt(caps.desktopBinding, 1);

    /* And what it deliberately does not: each a named later goal, asserted so the claim stays a
     * fact rather than a wish a later change quietly flipped. */
    RoboCheckEqInt(caps.variableAxisApplication, 0);
    RoboCheckEqInt(caps.letterSpacing, 0);
    RoboCheckEqInt(caps.shaping, 0);

    RoboCheckEqInt((int)RoboTypographyGetVersion(&major, &minor, &patch), (int)RoboOk);
    RoboCheckEqInt((int)major, RoboTypographyVersionMajor);
    RoboCheckEqInt((int)minor, RoboTypographyVersionMinor);
    RoboCheckEqInt((int)patch, RoboTypographyVersionPatch);
    RoboCheckStrEq(RoboTypographyVersionString(), "1.0.0");
    RoboCheckEqInt((int)RoboTypographyGetVersion(NULL, &minor, &patch), (int)RoboInvalidArgument);

    /* The diagnostic vocabulary: five domains, the fifth hole being `unknown`. */
    RoboCheckEqInt((int)RoboTypographyDomainCount, 5);
    RoboCheck(RoboTypographyDomainIsKnown(RoboTypographyDomainRole));
    RoboCheck(RoboTypographyDomainIsKnown(RoboTypographyDomainDocument));
    RoboCheck(!RoboTypographyDomainIsKnown(RoboTypographyDomainNone));
    RoboCheck(!RoboTypographyDomainIsKnown((TypographyDiagnosticDomain)5));
    RoboCheckStrEq(RoboTypographyDomainName(RoboTypographyDomainScale), "scale");
    RoboCheckStrEq(RoboTypographyDomainName(RoboTypographyDomainNone), "unknown");
}

/* ------------------------------------------------------------------ */
/* Fuzz                                                                */
/* ------------------------------------------------------------------ */

static void TestFuzz(void)
{
    uint8_t junk[256];
    size_t index;

    for (index = 0u; index < sizeof(junk); index++) {
        junk[index] = (uint8_t)(index * 31u + 7u);
    }
    /* The parsers must survive arbitrary bytes: the point is not that anything resolves, it is that
     * nothing reads past the end, divides by zero, or fails to advance. */
    RoboCheckEqInt((int)RoboTypographyFuzzParse(junk, sizeof(junk)), (int)RoboOk);
    RoboCheckEqInt((int)RoboTypographyFuzzParse(NULL, 0u), (int)RoboOk);
    {
        static const uint8_t slashes[] = {'/', '-', '_', ' ', 'd', 'i', 's', 'p', 'l', 'a', 'y', 0};
        RoboCheckEqInt((int)RoboTypographyFuzzParse(slashes, sizeof(slashes)), (int)RoboOk);
    }
}

void TestRegisterTypographyRoles(void)
{
    TestRegister("typography.roles.vocabulary", TestRoleVocabulary);
    TestRegister("typography.vocabulary.optical_weight", TestOpticalAndWeight);
    TestRegister("typography.roles.style_record", TestStyleRecord);
    TestRegister("typography.scale.table", TestScaleTable);
    TestRegister("typography.scale.entry_validation", TestScaleEntryValidation);
    TestRegister("typography.conformance.capabilities", TestCapabilities);
    TestRegister("typography.roles.fuzz", TestFuzz);
}
