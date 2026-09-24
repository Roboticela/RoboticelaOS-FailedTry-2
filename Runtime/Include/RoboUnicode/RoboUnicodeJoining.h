/*
 * `Joining_Type` — the Unicode property Arabic (and Syriac, N'Ko, Mandaic and Mongolian) shaping is built
 * on (UAX #44 / `ArabicShaping.txt`, Unicode 16.0.0).
 *
 * ## Why it is here and not in the shaper
 *
 * A joining type is a **Unicode property**, in the same sense `Script` and `Bidi_Class` are: the UCD owns
 * it, it is not derivable from a font and not a property of any particular language, and a text layer that
 * kept its own copy would be a second Unicode database to keep in step with the first. That is the rule
 * `RoboBidiMirror.h` states for `Bidi_Mirroring_Glyph`, and it applies here for exactly the same reason:
 * Phase 12.19's Arabic shaper must not hard-code a code-point range (12.19 §9), so the range lives in
 * RoboUnicode and the shaper *asks*.
 *
 * ## What is in the table, and what is not
 *
 * The table is the **Arabic script family**: `Arabic` (U+0600..U+06FF), `Arabic Supplement`
 * (U+0750..U+077F) and `Arabic Extended-A` (U+08A0..U+08FF), plus the two joining controls every script
 * obeys (U+200C `ZWNJ`, type `U`, and U+200D `ZWJ`, type `C`) and U+0640 `TATWEEL` (`C`). It is
 * transcribed from Unicode 16.0.0's `ArabicShaping.txt` rather than generated, because this tree no longer
 * carries the UCD source files the other tables were generated from
 * (the generated files under `Runtime/Source/RoboUnicode/data/` are checked in, the generator is not);
 * `RoboJoiningVersionString`
 * states the version the transcription is against.
 *
 * Everything the table does not name answers by the UCD's own **default rule**: `Mn`, `Me` and `Cf` are
 * `Transparent`, and every other scalar is `NonJoining`. That default is what makes the marks of a script
 * whose block is not listed harmless rather than wrong, and it is why the blocks above only need to state
 * their **letters**: a mark in them is transparent by category.
 *
 * The consequence is named rather than hidden: an `Arabic Extended-B` letter, a Syriac `Dalath`, a
 * Mongolian `A` and an N'Ko letter all answer `NonJoining` here, so a shaper driven by this table renders
 * them in their isolated forms. That is a **scope** limitation of this table, not of the algorithm, and
 * `RoboTextCapabilities::complexScripts` names it. A later change adds those blocks by extending this
 * table and nothing else.
 */
#ifndef ROBO_UNICODE_JOINING_H
#define ROBO_UNICODE_JOINING_H

#include "RoboUnicode/RoboUnicode.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A scalar's `Joining_Type`.
 *
 * The six values UAX #44 defines, in the order the data file states them. The names are the enumeration
 * values' own (`R`, `L`, `D`, `C`, `T`, `U`) spelled out, because `D` alone is not a name a reader can
 * check and `RoboUJtDualJoining` is.
 */
typedef enum UnicodeJoiningType {
    /** `U` — the scalar neither joins nor is joined to; the default for everything not listed. */
    RoboUJtNonJoining = 0,
    /** `C` — join-causing: joins the letters on both sides of it (`TATWEEL`, `ZWJ`). */
    RoboUJtJoinCausing = 1,
    /** `R` — right-joining: joins to the letter *before* it only (`ALEF`, `DAL`, `REH`, `WAW`). */
    RoboUJtRightJoining = 2,
    /** `L` — left-joining: joins to the letter *after* it only. Rare, and absent from the Arabic block. */
    RoboUJtLeftJoining = 3,
    /** `D` — dual-joining: joins on both sides (`BEH`, `TEH`, `LAM`, …). */
    RoboUJtDualJoining = 4,
    /** `T` — transparent: invisible to joining (`Mn`, `Me`, `Cf`, and the marks the data names). */
    RoboUJtTransparent = 5
} UnicodeJoiningType;

/**
 * @p cp's joining type.
 *
 * Total: every scalar answers, including an unassigned one and one outside the BMP (`NonJoining` for a
 * scalar that no block and no category claims).
 */
UnicodeJoiningType UnicodeJoiningTypeOf(Uchar32 cp);

/** `"U"`, `"C"`, `"R"`, `"L"`, `"D"` or `"T"` — the data file's own letter. Never NULL. */
const char *UnicodeJoiningTypeShortName(UnicodeJoiningType type);

/** `"NonJoining"`, `"JoinCausing"`, … — the enumerator's name, for a diagnostic. Never NULL. */
const char *UnicodeJoiningTypeName(UnicodeJoiningType type);

/**
 * Non-zero when @p type can take part in joining at all: `R`, `L`, `D` or `C`.
 *
 * The question "is this a letter that joins" asked once, here, rather than by each of the two callers that
 * need it (the resolver's left and right walks) with their own three-way test.
 */
int UnicodeJoiningTypeJoins(UnicodeJoiningType type);

/**
 * Non-zero when @p type is **right-joining**: a letter that joins to its predecessor and not to its
 * successor.
 *
 * Named separately because it is the one class whose *asymmetry* is the whole of the Arabic rule: an `R`
 * letter takes an initial form from the letter before it and then **breaks** the connection, which is why
 * `ALEF` is written connected on one side and why the letter after it starts a new form.
 */
int UnicodeJoiningTypeJoinsRight(UnicodeJoiningType type);

/** Non-zero when @p type is **left-joining** (`L`); see @ref UnicodeJoiningTypeJoinsRight. */
int UnicodeJoiningTypeJoinsLeft(UnicodeJoiningType type);

/** The Unicode version this table is transcribed from (`"16.0.0"`). Never NULL. */
const char *RoboJoiningVersionString(void);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_UNICODE_JOINING_H */
