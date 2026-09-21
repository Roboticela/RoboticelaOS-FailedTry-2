/*
 * RoboUnicodeProps — thin public wrappers over generated UCD tables.
 * Goal 3.12: Script / Script_Extensions / script-run foundation.
 */

#include "RoboUnicode/RoboUnicodeProps.h"

#include "RoboBase/RoboError.h"

UnicodeGeneralCategory UnicodeCategory(Uchar32 cp)
{
    return UcdGeneralCategory(cp);
}

UnicodeScript MakeUnicodeScript(Uchar32 cp)
{
    return UcdScript(cp);
}

unsigned UnicodeCombiningClass(Uchar32 cp)
{
    return UcdCombiningClass(cp);
}

UnicodeEastAsianWidth MakeUnicodeEastAsianWidth(Uchar32 cp)
{
    return UcdEastAsianWidth(cp);
}

int UnicodeIsWhitespace(Uchar32 cp)
{
    return UcdIsWhiteSpace(cp);
}

int UnicodeIsAlphabetic(Uchar32 cp)
{
    return UcdIsAlphabetic(cp);
}

int UnicodeIsUppercase(Uchar32 cp)
{
    return UcdIsUppercase(cp);
}

int UnicodeIsLowercase(Uchar32 cp)
{
    return UcdIsLowercase(cp);
}

int UnicodeIsDefaultIgnorable(Uchar32 cp)
{
    return UcdIsDefaultIgnorableCodePoint(cp);
}

int UnicodeIsEmoji(Uchar32 cp)
{
    return UcdIsEmoji(cp);
}

int UnicodeIsEmojiPresentation(Uchar32 cp)
{
    return UcdIsEmojiPresentation(cp);
}

int UnicodeIsEmojiModifier(Uchar32 cp)
{
    return UcdIsEmojiModifier(cp);
}

int UnicodeIsEmojiModifierBase(Uchar32 cp)
{
    return UcdIsEmojiModifierBase(cp);
}

int UnicodeIsEmojiComponent(Uchar32 cp)
{
    return UcdIsEmojiComponent(cp);
}

int UnicodeIsExtendedPictographic(Uchar32 cp)
{
    return UcdIsExtendedPictographic(cp);
}

int UnicodeIsRegionalIndicator(Uchar32 cp)
{
    return UcdIsRegionalIndicator(cp);
}

int UnicodeIsVariationSelector(Uchar32 cp)
{
    return UcdIsVariationSelector(cp);
}

const char *UnicodeCategoryName(UnicodeGeneralCategory gc)
{
    return UcdGeneralCategoryName(gc);
}

const char *UnicodeScriptName(UnicodeScript sc)
{
    return UcdScriptName(sc);
}

const char *UnicodeScriptShortName(UnicodeScript sc)
{
    return UcdScriptShortName(sc);
}

UnicodeScript UnicodeScriptFromName(const char *name)
{
    return UcdScriptFromName(name);
}

size_t UnicodeScriptExtensions(Uchar32 cp,
                                      UnicodeScript *out,
                                      size_t capacity)
{
    return UcdScriptExtensions(cp, out, capacity);
}

int UnicodeHasScript(Uchar32 cp, UnicodeScript script)
{
    return UcdHasScript(cp, script);
}

const char *UnicodeUcdVersionString(void)
{
    return UcdVersionString();
}

/*
 * Script-run foundation (shaping-prep heuristic — NOT UCD property truth).
 *
 * Policy:
 *   - Concrete scripts (not Common/Inherited/Unknown) start or continue a run
 *     of the same concrete script.
 *   - Common and Inherited continue the previous concrete script when one
 *     exists; otherwise they form/continue a Common run.
 *   - Unknown breaks to its own run.
 *
 * Callers that need raw property values must use UnicodeScript /
 * script_extensions instead.
 */
Result UnicodeScriptRuns(const Uchar32 *cps,
                                       size_t len,
                                       UnicodeScriptRun *out,
                                       size_t capacity,
                                       size_t *outCount)
{
    size_t i;
    size_t nruns = 0;
    UnicodeScript cur = RoboUScriptUnknown;
    size_t runStart = 0;
    int haveRun = 0;

    if (outCount) {
        *outCount = 0;
    }
    if ((!cps && len > 0u) || (capacity > 0u && !out) || !outCount) {
        return RoboInvalidArgument;
    }
    if (len == 0u) {
        return RoboOk;
    }

    for (i = 0; i < len; i++) {
        UnicodeScript sc = MakeUnicodeScript(cps[i]);
        UnicodeScript resolved;

        if (sc == RoboUScriptUnknown) {
            resolved = RoboUScriptUnknown;
        } else if (sc == RoboUScriptCommon || sc == RoboUScriptInherited) {
            if (haveRun && cur != RoboUScriptUnknown &&
                cur != RoboUScriptCommon) {
                resolved = cur;
            } else {
                resolved = RoboUScriptCommon;
            }
        } else {
            resolved = sc;
        }

        if (!haveRun) {
            cur = resolved;
            runStart = i;
            haveRun = 1;
            continue;
        }
        if (resolved == cur) {
            continue;
        }

        /* Close previous run */
        if (nruns < capacity) {
            out[nruns].start = runStart;
            out[nruns].end = i;
            out[nruns].script = cur;
        }
        nruns++;
        cur = resolved;
        runStart = i;
    }

    if (haveRun) {
        if (nruns < capacity) {
            out[nruns].start = runStart;
            out[nruns].end = len;
            out[nruns].script = cur;
        }
        nruns++;
    }

    *outCount = nruns;
    /* If nruns > capacity, out holds only the first `capacity` runs;
     * caller must enlarge and retry. */
    return RoboOk;
}
