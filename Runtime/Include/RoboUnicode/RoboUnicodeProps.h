/*
 * RoboUnicodeProps — Unicode Character Database property queries (Goals 3.4, 3.12).
 *
 * Backed by generated tables from Unicode 16.0.0 UCD (see Tools/ucd/).
 * Encoding APIs remain in RoboUnicode.h; property enums/tables in RoboUcdData.h.
 *
 * Script vs Script_Extensions are distinct (UAX #24). Common/Inherited are
 * UCD values — script-run heuristics are separate (see script_runs).
 */

#ifndef ROBO_UNICODE_PROPS_H
#define ROBO_UNICODE_PROPS_H

#include "RoboBase/RoboError.h"
#include "RoboUnicode/RoboUcdData.h"
#include "RoboUnicode/RoboUnicode.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Same pin as encoding + UCD tables. */
#define RoboUnicodeUcdVersionString RoboUcdVersionString

UnicodeGeneralCategory UnicodeCategory(Uchar32 cp);
UnicodeScript MakeUnicodeScript(Uchar32 cp);
unsigned UnicodeCombiningClass(Uchar32 cp);
UnicodeEastAsianWidth MakeUnicodeEastAsianWidth(Uchar32 cp);

int UnicodeIsWhitespace(Uchar32 cp);
int UnicodeIsAlphabetic(Uchar32 cp);
int UnicodeIsUppercase(Uchar32 cp);
int UnicodeIsLowercase(Uchar32 cp);
int UnicodeIsDefaultIgnorable(Uchar32 cp);

int UnicodeIsEmoji(Uchar32 cp);
int UnicodeIsEmojiPresentation(Uchar32 cp);
int UnicodeIsEmojiModifier(Uchar32 cp);
int UnicodeIsEmojiModifierBase(Uchar32 cp);
int UnicodeIsEmojiComponent(Uchar32 cp);
int UnicodeIsExtendedPictographic(Uchar32 cp);
int UnicodeIsRegionalIndicator(Uchar32 cp);
int UnicodeIsVariationSelector(Uchar32 cp);

const char *UnicodeCategoryName(UnicodeGeneralCategory gc);
const char *UnicodeScriptName(UnicodeScript sc);
/** ISO 15924 four-letter code (Latn, Arab, Zyyy, Zinh, Zzzz, …). */
const char *UnicodeScriptShortName(UnicodeScript sc);
/** Long or short name / Qaai → script; UNKNOWN if unrecognized. */
UnicodeScript UnicodeScriptFromName(const char *name);

/**
 * Script_Extensions (UAX #24). Always returns >= 1 for in-range code points.
 * If no explicit SCX, writes the single Script property value.
 * Writes min(count, capacity) into @p out when out != NULL.
 */
size_t UnicodeScriptExtensions(Uchar32 cp,
                                      UnicodeScript *out,
                                      size_t capacity);

/** 1 if SCX (or Script when no SCX) contains @p script. */
int UnicodeHasScript(Uchar32 cp, UnicodeScript script);

/**
 * Minimal script-run segmentation for future shaping (heuristic, not UCD).
 * See RoboUnicodeProps.c for the Common/Inherited continuation policy.
 */
typedef struct UnicodeScriptRun {
    size_t start; /* inclusive code-point index */
    size_t end;   /* exclusive */
    UnicodeScript script;
} UnicodeScriptRun;

Result UnicodeScriptRuns(const Uchar32 *cps,
                                       size_t len,
                                       UnicodeScriptRun *out,
                                       size_t capacity,
                                       size_t *outCount);

const char *UnicodeUcdVersionString(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_PROPS_H */
