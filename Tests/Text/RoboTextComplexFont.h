/*
 * The complex-script fixture (Phase 12.19) — one built face, both scripts, and the reasons each rule exists.
 *
 * ## Why one face and not two
 *
 * Arabic and Devanagari need *different* features, but a face's ScriptList decides which of them a run
 * reaches: 12.19's plan resolver asks the font for `arab` or for `deva`, so one fixture carrying both is the
 * shape a real multilingual font has, and a mixed-script case (`text.arabic.bidi_mixed`, `text.indic.
 * mixed_latin`) needs exactly that. The tables are built by `RoboTextConformanceFont.h`'s writer, extended
 * in 12.19 with the two rule kinds the complex scripts need — **single substitutions** (a contextual form is
 * one glyph for one glyph) and the **attachments** (cursive, mark-to-base, mark-to-mark, mark-to-ligature).
 *
 * ## What the fixture states, and what each rule is for
 *
 * Arabic, over `BEH MEEM LAM ALEF`:
 *
 *   `isol`/`init`/`medi`/`fina`   four single substitutions per letter, one glyph id per (letter, form), so a
 *                                case can assert the *form* by id and a wrong mask shows as a wrong id rather
 *                                than as the same id twice;
 *   `rlig`                        `lam` + `alef` -> one glyph: the ligature every Arabic font has, and the
 *                                case that proves a many-to-one substitution keeps both scalars in one source
 *                                range (`text.arabic.lam_alef_ligature`);
 *   `mark`                        a mark-to-base lookup for `fatha` on `beh`, with anchors that are *not* the
 *                                origin, so a case that reads a placement can tell "attached" from "left where
 *                                its advance put it";
 *   `mkmk`                        `shadda` on `fatha`: the second mark attaches to the first, which is where
 *                                "two diacritics stack" comes from;
 *   `curs`                        entry and exit anchors for `beh` and `meem`, so the cursive chain has a
 *                                distance to move the pen by — a number GPOS does not carry, which is why the
 *                                engine is given the font context.
 *
 * Devanagari, over `KA SSA RA MA` and the virama:
 *
 *   `rphf`   `ra` + virama -> the reph glyph. The engine's initial reorder exists to put that pair first;
 *   `half`   `ka` + virama -> the half form;
 *   `cjct`   half-ka + `ssa` -> the conjunct, so `text.indic.conjunct` can assert one glyph covering three
 *            scalars.
 *
 * The **virama and every matra advance nothing** in `hmtx`, which is what makes an Indic width a statement
 * about the consonant rather than about its vowel sign. The two product glyphs carry distinct advances, so a
 * defect that left a conjunct unjoined shows as two advances instead of one.
 *
 * ## Not in the fixture, and named rather than implied
 *
 * No `blwf`/`pstf`/`pref` rules: 12.19 implements their **masks** (a below-base consonant carries
 * `RoboTextIndicMaskBelow` and so on) and this fixture states no below-base or post-base form to prove them
 * against. No `locl`, no `calt`, no `nukt`/`akhn`: each is a lookup the engine already runs, and a fixture
 * that stated them would be proving the same reader twice. `Documentation/Runtime/Text/ComplexScripts.md`
 * §"what the fixture does not state" repeats this list beside the engine's own limits.
 */
#ifndef ROBO_TEXT_COMPLEX_FONT_H
#define ROBO_TEXT_COMPLEX_FONT_H

#include "RoboTextConformanceFont.h"

#include "RoboText/RoboTextArabic.h"
#include "RoboText/RoboTextComplex.h"
#include "RoboText/RoboTextIndic.h"

/* ------------------------------------------------------------------ */
/* The glyph ids                                                       */
/* ------------------------------------------------------------------ */

/*
 * Explicit `cmap` runs rather than the canonical list: the complex scripts need ids a case can compute from
 * a code point, and using the shared canonical mapping would have moved every European id to make room. Each
 * run maps `first..last` onto consecutive ids starting at the number below, so a helper is one subtraction.
 *
 *   1..26     `A`..`Z`           (mixed Latin cases)
 *   40..65    `a`..`z`
 *   70        space
 *   80..89    `0`..`9`
 *   100..133  the Arabic letters `U+0627`..`U+0648`
 *   140..143  the Arabic marks `U+064E`..`U+0651`
 *   160..196  the Devanagari letters `U+0915`..`U+0939`
 *   200..215  the Devanagari signs `U+093E`..`U+094D`
 *   240..256  the products: forms, ligatures, reph, half form and conjunct
 */

/** The glyph id of the uppercase Latin letter @p cp. */
#define CplxUpperGlyph(cp) ((uint16_t)(1u + ((cp) - 0x0041u)))

/** The glyph id of the lowercase Latin letter @p cp. */
#define CplxLowerGlyph(cp) ((uint16_t)(40u + ((cp) - 0x0061u)))

/** The glyph id of the Arabic letter @p cp. */
#define CplxArabicGlyph(cp) ((uint16_t)(100u + ((cp) - 0x0627u)))

/** The glyph id of the Arabic mark @p cp. */
#define CplxArabicMarkGlyph(cp) ((uint16_t)(140u + ((cp) - 0x064Eu)))

/** The glyph id of the Devanagari letter @p cp. */
#define CplxDevanagariGlyph(cp) ((uint16_t)(160u + ((cp) - 0x0915u)))

/** The glyph id of the Devanagari sign @p cp. */
#define CplxDevanagariSignGlyph(cp) ((uint16_t)(200u + ((cp) - 0x093Eu)))

#define CplxSpaceGlyph 70u
#define CplxDigitOneGlyph 81u /* `1`, the digit the Devanagari length cases use. */

/* The Latin letters the mixed cases write, so no suite spells a subtraction of its own. */
#define CplxUpperA CplxUpperGlyph(0x0041u)
#define CplxUpperB CplxUpperGlyph(0x0042u)
#define CplxLowerX CplxLowerGlyph(0x0078u)
#define CplxLowerY CplxLowerGlyph(0x0079u)

/* The Arabic letters: BEH (dual-joining), MEEM (dual-joining), LAM (dual-joining), ALEF (right-joining). */
#define CplxBeh CplxArabicGlyph(0x0628u)
#define CplxMeem CplxArabicGlyph(0x0645u)
#define CplxLam CplxArabicGlyph(0x0644u)
#define CplxAlef CplxArabicGlyph(0x0627u)
#define CplxFatha CplxArabicMarkGlyph(0x064Eu)
#define CplxShadda CplxArabicMarkGlyph(0x0651u)

/* The Devanagari letters and signs the Indic cases use. */
#define CplxKa CplxDevanagariGlyph(0x0915u)
#define CplxSsa CplxDevanagariGlyph(0x0937u)
#define CplxRa CplxDevanagariGlyph(0x0930u)
#define CplxMa CplxDevanagariGlyph(0x092Eu)
#define CplxVirama CplxDevanagariSignGlyph(0x094Du)
#define CplxIMatra CplxDevanagariSignGlyph(0x093Fu) /* the pre-base vowel sign `i` */
#define CplxAaMatra CplxDevanagariSignGlyph(0x093Eu) /* the post-base vowel sign `aa` */

/* The products. The first eight are one per (letter, form) that a case asserts, then the ligature. */
#define CplxIsolBeh 240u
#define CplxInitBeh 241u
#define CplxMediBeh 242u
#define CplxFinaBeh 243u
#define CplxInitMeem 244u
#define CplxFinaMeem 245u
#define CplxInitLam 246u
#define CplxFinaLam 247u
#define CplxLamAlef 248u
#define CplxFinaAlef 249u
#define CplxReph 250u
#define CplxHalfKa 251u
#define CplxKsa 252u
#define CplxGlyphCount 260u

/* ------------------------------------------------------------------ */
/* The spec                                                            */
/* ------------------------------------------------------------------ */

/**
 * One rule: @p glyph becomes @p substitute under @p tag.
 *
 * A macro rather than an inline function because the spec is filled with a list of these, and a list reads as
 * a list when each row is one line.
 */
#define CplxSubst(spec, tagText, glyphId, substituteId)                                            \
    do {                                                                                           \
        ConfSubstRule *rule = &(spec)->subst[(spec)->substCount++];                                 \
        memcpy(rule->tag, tagText, 5u);                                                             \
        rule->glyph = (uint16_t)(glyphId);                                                          \
        rule->substitute = (uint16_t)(substituteId);                                                \
    } while (0)

/** One ligature: @p a and @p b become @p result under @p tagText. */
#define CplxLigature(spec, tagText, a, b, resultId)                                                 \
    do {                                                                                           \
        ConfLigatureRule *rule = &(spec)->ligatures[(spec)->ligatureCount++];                        \
        memcpy(rule->tag, tagText, 5u);                                                             \
        rule->components[0] = (uint16_t)(a);                                                        \
        rule->components[1] = (uint16_t)(b);                                                        \
        rule->componentCount = 2u;                                                                  \
        rule->result = (uint16_t)(resultId);                                                        \
    } while (0)

/**
 * One mark attachment: @p markId's anchor joins @p anchorId's, at the two coordinates, under @p tagText.
 *
 * `markY` is *above* `markX` in the argument list the way the format states the anchors' two numbers, and
 * every rule in this fixture writes them in font units.
 */
#define CplxMark(spec, tagText, markId, anchorId, cls, markXUnits, markYUnits, anchorXUnits,          \
                 anchorYUnits)                                                                      \
    do {                                                                                           \
        ConfMarkRule *rule = &(spec)->markBase[(spec)->markBaseCount++];                             \
        memcpy(rule->tag, tagText, 5u);                                                             \
        rule->mark = (uint16_t)(markId);                                                            \
        rule->anchorGlyph = (uint16_t)(anchorId);                                                   \
        rule->markClass = (uint16_t)(cls);                                                          \
        rule->markX = (int16_t)(markXUnits);                                                        \
        rule->markY = (int16_t)(markYUnits);                                                        \
        rule->anchorX = (int16_t)(anchorXUnits);                                                    \
        rule->anchorY = (int16_t)(anchorYUnits);                                                    \
    } while (0)

/** One mark-to-**mark** attachment (`mkmk`): @p markId stacks on @p secondId. */
#define CplxMarkMark(spec, tagText, markId, secondId, cls, markXUnits, markYUnits, anchorXUnits,      \
                     anchorYUnits)                                                                  \
    do {                                                                                           \
        ConfMarkRule *rule = &(spec)->markMark[(spec)->markMarkCount++];                             \
        memcpy(rule->tag, tagText, 5u);                                                             \
        rule->mark = (uint16_t)(markId);                                                            \
        rule->anchorGlyph = (uint16_t)(secondId);                                                   \
        rule->markClass = (uint16_t)(cls);                                                          \
        rule->markX = (int16_t)(markXUnits);                                                        \
        rule->markY = (int16_t)(markYUnits);                                                        \
        rule->anchorX = (int16_t)(anchorXUnits);                                                    \
        rule->anchorY = (int16_t)(anchorYUnits);                                                    \
    } while (0)

/** One cursive glyph: its entry and exit anchors, under @p tagText. */
#define CplxCursive(spec, tagText, glyphId, entryXUnits, entryYUnits, exitXUnits, exitYUnits)         \
    do {                                                                                           \
        ConfCursiveRule *rule = &(spec)->cursive[(spec)->cursiveCount++];                            \
        memcpy(rule->tag, tagText, 5u);                                                             \
        rule->glyph = (uint16_t)(glyphId);                                                          \
        rule->entryX = (int16_t)(entryXUnits);                                                      \
        rule->entryY = (int16_t)(entryYUnits);                                                      \
        rule->exitX = (int16_t)(exitXUnits);                                                        \
        rule->exitY = (int16_t)(exitYUnits);                                                        \
    } while (0)

/** The anchor coordinates and advances a case would otherwise restate, in font units. */
#define CplxBaseLetterAdvance 500u
#define CplxFormAdvance 520u
#define CplxLamAlefAdvance 900u
#define CplxHalfKaAdvance 250u
#define CplxKsaAdvance 700u

/** `fatha` on `beh`: the mark's own anchor and the base's, neither of them the origin. */
#define CplxFathaMarkXUnits 0
#define CplxFathaMarkYUnits 60
#define CplxFathaBaseXUnits 250
#define CplxFathaBaseYUnits 400

/** `shadda` on `fatha`: the second mark's anchor is the first mark's, higher up. */
#define CplxShaddaMarkXUnits 0
#define CplxShaddaMarkYUnits 70
#define CplxShaddaOnFathaXUnits 0
#define CplxShaddaOnFathaYUnits 150

/** Cursive anchors: the entry is the letter's join on one side and the exit on the other. */
#define CplxBehEntryXUnits 30
#define CplxBehExitXUnits 470
#define CplxMeemEntryXUnits 25
#define CplxMeemExitXUnits 475

/**
 * Fill @p spec with the complex fixture: Latin for the mixed cases, Arabic's four forms and its other
 * features, and Devanagari's reph, half form and conjunct.
 */
static inline void CplxSpecComplex(ConfFontSpec *spec)
{
    uint16_t id;

    ConfSpecInit(spec, CplxGlyphCount);

    /* ---- The `cmap`: explicit runs, one per block, ids consecutive inside each ---- */
    spec->canonicalCmap = 0;
    spec->cmapCount = 0u;
    spec->cmap[spec->cmapCount].first = 0x0020u;
    spec->cmap[spec->cmapCount].last = 0x0020u;
    spec->cmap[spec->cmapCount].glyph = (uint16_t)CplxSpaceGlyph;
    spec->cmapCount++;
    spec->cmap[spec->cmapCount].first = 0x0041u;
    spec->cmap[spec->cmapCount].last = 0x005Au;
    spec->cmap[spec->cmapCount].glyph = 1u;
    spec->cmapCount++;
    spec->cmap[spec->cmapCount].first = 0x0061u;
    spec->cmap[spec->cmapCount].last = 0x007Au;
    spec->cmap[spec->cmapCount].glyph = 40u;
    spec->cmapCount++;
    spec->cmap[spec->cmapCount].first = 0x0030u;
    spec->cmap[spec->cmapCount].last = 0x0039u;
    spec->cmap[spec->cmapCount].glyph = 80u;
    spec->cmapCount++;
    spec->cmap[spec->cmapCount].first = 0x0627u;
    spec->cmap[spec->cmapCount].last = 0x0648u;
    spec->cmap[spec->cmapCount].glyph = 100u;
    spec->cmapCount++;
    spec->cmap[spec->cmapCount].first = 0x064Eu;
    spec->cmap[spec->cmapCount].last = 0x0651u;
    spec->cmap[spec->cmapCount].glyph = 140u;
    spec->cmapCount++;
    spec->cmap[spec->cmapCount].first = 0x0915u;
    spec->cmap[spec->cmapCount].last = 0x0939u;
    spec->cmap[spec->cmapCount].glyph = 160u;
    spec->cmapCount++;
    spec->cmap[spec->cmapCount].first = 0x093Eu;
    spec->cmap[spec->cmapCount].last = 0x094Du;
    spec->cmap[spec->cmapCount].glyph = 200u;
    spec->cmapCount++;

    /* ---- Advances: a base letter is one width, a mark is none, and a product is its own ---- */
    for (id = 100u; id <= 133u; id++) {
        spec->advances[id] = (uint16_t)CplxBaseLetterAdvance;
    }
    for (id = 140u; id <= 143u; id++) {
        spec->advances[id] = 0u; /* A mark advances nothing: it is placed, not advanced. */
        spec->marks[spec->markCount++] = id;
    }
    for (id = 160u; id <= 196u; id++) {
        spec->advances[id] = (uint16_t)CplxBaseLetterAdvance;
    }
    for (id = 200u; id <= 215u; id++) {
        /*
         * A vowel sign and the virama advance nothing: an Indic width is the consonant's. They are
         * combining marks (`U+093E`..`U+094D` are `Mc`/`Mn`), so GDEF classifies them as class 3 — which
         * is what a lookup that ignores marks reads.
         */
        spec->advances[id] = 0u;
        spec->marks[spec->markCount++] = id;
    }
    spec->advances[CplxIsolBeh] = (uint16_t)CplxFormAdvance;
    spec->advances[CplxInitBeh] = (uint16_t)CplxFormAdvance;
    spec->advances[CplxMediBeh] = (uint16_t)CplxFormAdvance;
    spec->advances[CplxFinaBeh] = (uint16_t)CplxFormAdvance;
    spec->advances[CplxInitMeem] = (uint16_t)CplxFormAdvance;
    spec->advances[CplxFinaMeem] = (uint16_t)CplxFormAdvance;
    spec->advances[CplxInitLam] = (uint16_t)CplxFormAdvance;
    spec->advances[CplxFinaLam] = (uint16_t)CplxFormAdvance;
    spec->advances[CplxFinaAlef] = (uint16_t)CplxFormAdvance;
    spec->advances[CplxLamAlef] = (uint16_t)CplxLamAlefAdvance;
    spec->advances[CplxReph] = 0u; /* The reph sits above the base and advances nothing. */
    spec->advances[CplxHalfKa] = (uint16_t)CplxHalfKaAdvance;
    spec->advances[CplxKsa] = (uint16_t)CplxKsaAdvance;

    /* ---- Arabic: the four contextual forms, one glyph id per (letter, form) ---- */
    CplxSubst(spec, "isol", CplxBeh, CplxIsolBeh);
    CplxSubst(spec, "init", CplxBeh, CplxInitBeh);
    CplxSubst(spec, "medi", CplxBeh, CplxMediBeh);
    CplxSubst(spec, "fina", CplxBeh, CplxFinaBeh);
    CplxSubst(spec, "init", CplxMeem, CplxInitMeem);
    CplxSubst(spec, "fina", CplxMeem, CplxFinaMeem);
    CplxSubst(spec, "init", CplxLam, CplxInitLam);
    CplxSubst(spec, "fina", CplxLam, CplxFinaLam);
    /* `alef` is right-joining: it takes a final shape and never an initial or medial one. */
    CplxSubst(spec, "fina", CplxAlef, CplxFinaAlef);

    /*
     * `rlig`: lam + alef, the ligature every Arabic face has. Its **input** is the formed pair and not the
     * nominal letters, which is a fact about Arabic shaping rather than about this fixture: `rlig` runs in the
     * stage *after* the contextual forms, so the glyphs it matches are `init` lam and `fina` alef. A rule over
     * the nominal ids would be a rule no font could use, and it would make the ligature look broken the moment
     * the four forms started working.
     */
    CplxLigature(spec, "rlig", CplxInitLam, CplxFinaAlef, CplxLamAlef);

    /* ---- Devanagari: the reph, the half form and the conjunct ---- */
    CplxLigature(spec, "rphf", CplxRa, CplxVirama, CplxReph);
    CplxLigature(spec, "half", CplxKa, CplxVirama, CplxHalfKa);
    CplxLigature(spec, "cjct", CplxHalfKa, CplxSsa, CplxKsa);

    /*
     * ---- The attachments ----
     *
     * The **formed** letters are what a shaped Arabic word actually holds, so every attachment names them as
     * well as the base it came from: an anchor belongs to a glyph, and `init`'s glyph is a glyph. One row per
     * (anchor holder, mark) is what a real face writes, and writing only the unsubstituted id would make mark
     * attachment look broken the moment the four forms work — which is exactly the regression this fixture
     * exists to catch.
     */
    CplxMark(spec, "mark", CplxFatha, CplxBeh, 0u, CplxFathaMarkXUnits, CplxFathaMarkYUnits,
             CplxFathaBaseXUnits, CplxFathaBaseYUnits);
    CplxMark(spec, "mark", CplxFatha, CplxIsolBeh, 0u, CplxFathaMarkXUnits, CplxFathaMarkYUnits,
             CplxFathaBaseXUnits, CplxFathaBaseYUnits);
    CplxMark(spec, "mark", CplxFatha, CplxInitBeh, 0u, CplxFathaMarkXUnits, CplxFathaMarkYUnits,
             CplxFathaBaseXUnits, CplxFathaBaseYUnits);
    CplxMark(spec, "mark", CplxFatha, CplxMediBeh, 0u, CplxFathaMarkXUnits, CplxFathaMarkYUnits,
             CplxFathaBaseXUnits, CplxFathaBaseYUnits);
    CplxMark(spec, "mark", CplxFatha, CplxFinaBeh, 0u, CplxFathaMarkXUnits, CplxFathaMarkYUnits,
             CplxFathaBaseXUnits, CplxFathaBaseYUnits);
    CplxMarkMark(spec, "mkmk", CplxShadda, CplxFatha, 0u, CplxShaddaMarkXUnits, CplxShaddaMarkYUnits,
                 CplxShaddaOnFathaXUnits, CplxShaddaOnFathaYUnits);
    CplxCursive(spec, "curs", CplxBeh, CplxBehEntryXUnits, 0, CplxBehExitXUnits, 0);
    CplxCursive(spec, "curs", CplxInitBeh, CplxBehEntryXUnits, 0, CplxBehExitXUnits, 0);
    CplxCursive(spec, "curs", CplxMediBeh, CplxBehEntryXUnits, 0, CplxBehExitXUnits, 0);
    CplxCursive(spec, "curs", CplxFinaBeh, CplxBehEntryXUnits, 0, CplxBehExitXUnits, 0);
    CplxCursive(spec, "curs", CplxMeem, CplxMeemEntryXUnits, 0, CplxMeemExitXUnits, 0);
}

#endif /* ROBO_TEXT_COMPLEX_FONT_H */
