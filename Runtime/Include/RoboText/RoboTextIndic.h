/*
 * Indic shaping in RoboText (Phase 12.19 §16–§22) — syllables, a base, reordering, stages.
 *
 * ## The model
 *
 * An Indic syllable is not a character and not a word: it is a **consonant cluster with its marks**, written
 * in an order that is *not* the order it is drawn in. `क` `्` `ष` (ka, virama, ssa) is one syllable whose two
 * consonants become one half-form plus a base; a pre-base vowel sign is written *after* its consonant and
 * drawn *before* it; a `ra` + virama that starts the syllable's shaping sequence is a **reph** that is drawn
 * above the base it followed. So shaping Devanagari is three things: find the syllables, find each syllable's
 * **base**, and move the elements whose written order is not their shaping order — before the font's features
 * run (so they see the sequence the font expects) and after it (so the glyphs the features produced end up in
 * visual order).
 *
 * ## Where the script's own rules live
 *
 * In **this module**, as data and narrowly scoped rules, which is what the phase's §22 asks for and what the
 * Arabic side does differently on purpose: Arabic's one input (a joining type) is a Unicode property, so it
 * lives in RoboUnicode; an Indic script's positional categories (`Indic_Positional_Category` in the UCD) are
 * **not** in this tree's tables, so the script rules are stated here, once per script, in the small tables at
 * the top of `RoboTextIndic.c`. They are data: `RoboTextIndicScriptRules` is a row per script, and a script
 * with no row is refused rather than shaped with Devanagari's rules.
 *
 * ## What is implemented, and what is not
 *
 * Implemented and proved by `text.indic.*`: syllable clusters; base finding; the reph (detection, the
 * initial move that lets `rphf` match it, and the final move that places it after the base); pre-base vowel
 * signs; virama-terminated (half-form) consonants; per-position masks for `rphf`, `half`, `blwf`, `pstf`,
 * `pref` and `cjct`; and the three stages (common, substitution, presentation) with the reorder steps between
 * them.
 *
 * Not implemented, and named so a reader is not misled: the *other* scripts' reorder rules differ in where a
 * reph goes and which matras move (their rows carry `hasReph` and the same rule set is applied, which is
 * right for Devanagari and unproved for the rest); `blwf`'s interaction with `rkrf`/`vatu` for below-base
 * forms that need their own reordering; the `haln`/`cjct` "base becomes half form with the last consonant"
 * refinement; and the UCD's positional categories as the data source. Each of those is a *change to a rule
 * table*, not a new mechanism, which is the property this phase was for.
 */
#ifndef ROBO_TEXT_INDIC_H
#define ROBO_TEXT_INDIC_H

#include "RoboBase/RoboError.h"
#include "RoboText/RoboTextComplex.h"
#include "RoboText/RoboTextDiagnostics.h"
#include "RoboText/RoboTextGsub.h"
#include "RoboText/RoboTextTypes.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The mask bits the Indic substitution stage uses, one per position class a feature applies at. */
typedef enum RoboTextIndicMask {
    /** The syllable's reph: where `rphf` matches. */
    RoboTextIndicMaskReph = 1u << 0,
    /** A consonant that halves: where `half` matches. */
    RoboTextIndicMaskHalf = 1u << 1,
    /** The base and what follows it: where `blwf` matches. */
    RoboTextIndicMaskBelow = 1u << 2,
    /** A consonant after the base: where `pstf` matches. */
    RoboTextIndicMaskPost = 1u << 3,
    /** The first consonant of a multi-consonant syllable: where `pref` matches. */
    RoboTextIndicMaskPre = 1u << 4,
    /**
     * A consonant of a multi-consonant syllable: where `cjct` matches.
     *
     * Every consonant of such a syllable carries it, not only those after the first, because a conjunct
     * lookup begins at the sequence's **leftmost** consonant and that is the position whose mask the engine
     * reads. A half form made by `half` keeps its first consonant's source, so the `cjct` that finishes the
     * conjunct is asked at a position this bit has to answer for.
     */
    RoboTextIndicMaskConjunct = 1u << 5,
    /** A consonant, anywhere in its syllable, for the features a font may match at any consonant. */
    RoboTextIndicMaskConsonant = 1u << 6
} RoboTextIndicMask;

/**
 * One script's Indic rules: the scalars that mean something and the reorder rule that applies.
 *
 * A row of the module's table, borrowed, and `NULL` for a script this build has no rules for — which is a
 * **refusal** rather than a fallback, because shaping Kannada with Devanagari's virama would be worse than
 * shaping it plain.
 */
typedef struct RoboTextIndicScriptRules {
    RoboTextScript script;
    /** The virama (the vowel-killer that joins two consonants). */
    RoboTextCodePoint virama;
    /** The nukta (the dot that makes a new letter), or 0 when the script has none. */
    RoboTextCodePoint nukta;
    /** The scalars written *after* their consonant and drawn *before* it, ascending. 0-terminated. */
    const RoboTextCodePoint *preBase;
    size_t preBaseCount;
    /** The scalars drawn below their consonant. 0-terminated. */
    const RoboTextCodePoint *belowBase;
    size_t belowBaseCount;
    /** The scalars drawn after their consonant. 0-terminated. */
    const RoboTextCodePoint *postBase;
    size_t postBaseCount;
    /** The scalars drawn above their consonant. 0-terminated. */
    const RoboTextCodePoint *aboveBase;
    size_t aboveBaseCount;
} RoboTextIndicScriptRules;

/** The rules for @p script, or NULL when this build has none (see the header's last section). */
const RoboTextIndicScriptRules *RoboTextIndicRulesForScript(RoboTextScript script);

/** Non-zero when @p cp is a vowel sign or a mark — the scalars that attach to a syllable's consonants. */
int RoboTextIndicIsAttachment(RoboTextCodePoint cp);

/** Non-zero when @p cp is the script's virama. */
int RoboTextIndicIsVirama(RoboTextScript script, RoboTextCodePoint cp);

/** Non-zero when @p cp is a **consonant**: a letter that carries a virama and can be a syllable's base. */
int RoboTextIndicIsConsonant(RoboTextScript script, RoboTextCodePoint cp);

/* ------------------------------------------------------------------ */
/* Clusters                                                            */
/* ------------------------------------------------------------------ */

/**
 * The syllables of @p range, one cluster each.
 *
 * A cluster's `baseIndex` is the syllable's base **as an offset from the cluster's start**, and its flags say
 * what the reorder steps and the masks need: a reph, a pre-base element, a below-base element, a virama, a
 * nukta. A scalar the rules do not claim (a space, a Latin letter, a digit) is its own simple cluster, so a
 * mixed-script range clusters correctly without a branch on the script at the call site.
 *
 * A syllable whose last scalar is the virama is marked **unsafe to break**: the half form it produces belongs
 * to whatever follows.
 */
Result RoboTextIndicClusters(RoboTextView view,
                             RoboTextRange range,
                             RoboTextComplexCluster *clusters,
                             size_t capacity,
                             size_t *outCount,
                             RoboTextDiag *diag);

/**
 * The position masks the substitution stage runs with: one `uint32_t` per scalar of @p range.
 *
 * The masks are computed over the **original** scalar indices, and the engine looks a mask up by the source
 * scalar a glyph came from — so a glyph carries the position class of the character it was made from, and the
 * initial reorder (which moves glyphs) does not move a class between characters. That is the property that
 * makes "`half` applies to the first consonant and not to the base" survive the reorder.
 */
Result RoboTextIndicMasksBuild(RoboTextView view,
                               RoboTextRange range,
                               uint32_t *masks,
                               size_t capacity,
                               size_t *outCount,
                               RoboTextDiag *diag);

/* ------------------------------------------------------------------ */
/* Reordering                                                          */
/* ------------------------------------------------------------------ */

/**
 * **Initial** reordering: written order → shaping order.
 *
 * Two moves, in this order, per syllable: the reph (`ra` + virama) moves to the syllable's start, so that
 * `rphf` — a feature whose lookup expects the pair at the beginning of the syllable — can match it there; and
 * a pre-base vowel sign moves to just after the reph, so the consonant features see consonants adjacent to
 * their viramas rather than a vowel sign in the middle of a conjunct.
 *
 * The moves are made on the **shaping buffer** and by *glyph*, never on the text: every glyph keeps its
 * `source`, so a caret, a selection and a copy still read the characters in the order the author wrote them
 * (12.19 §7). The text's logical order is the only order this module never changes.
 *
 * @return RoboOk; RoboInvalidArgument for a NULL buffer; RoboOutOfMemory when the buffer could not grow.
 */
Result RoboTextIndicReorderInitial(RoboTextShapingBuffer *buffer,
                                   RoboTextView view,
                                   RoboTextRange range,
                                   RoboTextDiag *diag);

/**
 * **Final** reordering: shaping order → visual order — the reph, and only the reph.
 *
 * By now `rphf` has turned the `ra` + virama into a reph glyph, and it must be drawn above the syllable's
 * base rather than before it, so the glyph whose source covers the reph's `ra` moves to immediately after the
 * glyph whose source covers the base. (Devanagari's rule is "after the main consonant"; the scripts whose
 * reph belongs after a subjoined consonant, or after a post-base matra, need their own rows here and are
 * named in the header as not implemented.)
 */
Result RoboTextIndicReorderFinal(RoboTextShapingBuffer *buffer,
                                 RoboTextView view,
                                 RoboTextRange range,
                                 RoboTextDiag *diag);

/* ------------------------------------------------------------------ */
/* The stages                                                          */
/* ------------------------------------------------------------------ */

/**
 * The Indic strategy's stages: common features, the substitution stage with its masked features, and the
 * presentation features.
 *
 * The **initial reorder runs before the first stage** (a stage field, `reorderBefore`), so the moves happen
 * while the buffer is still one glyph per scalar — which is what lets a move be expressed in scalars. The
 * **final reorder runs after the substitution stage**, because until `rphf` has run there is no reph glyph to
 * place. The presentation stage (`pres`, `abvs`, `blws`, `psts`, `haln`, `calt`) then runs on glyphs that are
 * already in visual order, which is where those features belong: they are about the *shapes* the earlier
 * stages decided, not about the sequence.
 */
Result RoboTextIndicStages(const struct RoboTextComplexPlan *plan,
                           RoboTextComplexStage *stages,
                           size_t capacity,
                           size_t *outCount,
                           RoboTextDiag *diag);

/** One line per cluster (`"[0,3) syllable base=1 virama reph pre"`). Complete-or-absent. */
Result RoboTextIndicClustersWrite(const RoboTextComplexCluster *clusters,
                                  size_t count,
                                  char *buffer,
                                  size_t capacity,
                                  size_t *outLength);

#ifdef __cplusplus
}
#endif

#endif /* ROBO_TEXT_INDIC_H */
